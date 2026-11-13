#!/usr/bin/env python3
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
#
# build-all.py [--work DIR] --native WD --armv7 WD --aarch64 WD [--corei7 WD]
#
# The warning gate: generate_qmap and the tests (which include every header
# in the tree, qweboskeyextension.h among them) compiled at -O2 -Wall -Wextra
# for each architecture, with each compiler:
#
#   x86-64   gcc    the build host's gcc against the native Qt sysroot: the
#                   build generate_qmap really gets, as a -native tool
#            gcc    the image's x86_64-webos-linux-g++ (--corei7), on its
#                   sysroot; no corei7 recipe has Qt, so Qt's headers come
#                   from the native sysroot (same Qt, same architecture)
#            clang  the build host's clang++ against the native Qt sysroot
#   armv7    gcc    the image's arm-webos-linux-gnueabi-g++, its own flags
#            clang  clang++ --target=arm-webos-linux-gnueabi, same sysroot
#   aarch64  gcc    the image's aarch64-webos-linux-g++, its own flags
#            clang  clang++ --target=aarch64-webos-linux, same sysroot
#
# Each WD is a Yocto recipe workdir (tmp/work/<arch>/<recipe>/<ver>):
#   --native   one whose recipe-sysroot-native has qtbase-native, e.g.
#              x86_64-linux/qt-features-webos-native/<ver>
#   --armv7    an armv7 recipe with Qt (QtGui, QtInputSupport) in its
#   --aarch64  recipe-sysroot and a temp/run.do_compile to take the image's
#              compile flags from, e.g. luneos-components, luna-surfacemanager
#   --corei7   any corei7-64 recipe, for the x86-64 image toolchain
#
# The compile commands are the host build's (CMake, tests/CMakeLists.txt),
# with the compiler, the target flags and the sysroot swapped per build;
# googletest's headers come from --gtest. A warning in this tree fails the
# gate; one from a header in a sysroot or googletest is listed, not ours to
# fix. armv7 is in the list because it is the 32-bit target.

import argparse, collections, glob, json, os, re, shlex, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--work", default=os.path.expanduser("~/claude-scratch/qt-features-webos/build-all"))
ap.add_argument("--gtest", default=os.path.expanduser("~/claude-scratch/qt-features-webos/googletest"))
ap.add_argument("--native", required=True)
ap.add_argument("--armv7", required=True)
ap.add_argument("--aarch64", required=True)
ap.add_argument("--corei7")
opts = ap.parse_args()

SRC = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
WARN = re.compile(r"^(?P<file>[^\s:]+):(?P<line>\d+):(?:\d+:)? warning: (?P<msg>.*)$", re.M)
EXTRA = ["-O2", "-Wall", "-Wextra"]
# Flags only gcc knows.
GCC_ONLY = ("-fcanon-prefix-map", "-mthumb-interwork", "-fno-tree-vrp", "-fmacro-prefix-map",
            "-fdebug-prefix-map", "-ffile-prefix-map", "-mbranch-protection")


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def classify(text):
    ours, theirs = collections.Counter(), collections.Counter()
    for m in WARN.finditer(text):
        f = os.path.realpath(m["file"]) if os.path.exists(m["file"]) else m["file"]
        mine = f.startswith(SRC + "/")
        key = "%s:%s: %s" % (f.replace(SRC + "/", ""), m["line"], m["msg"])
        (ours if mine else theirs)[key] += 1
    return ours, theirs


def image_cxx(wd):
    """The image's g++ and target flags, from the recipe's run.do_compile."""
    text = open(os.path.join(wd, "temp", "run.do_compile")).read()
    cxx = shlex.split(re.search(r'^export CXX="([^"]*)"', text, re.M).group(1))
    cxx = [a for a in cxx if a != "ccache" and not a.startswith("--sysroot")]
    gxx = glob.glob(os.path.join(wd, "recipe-sysroot-native/usr/bin/*", cxx[0]))[0]
    return gxx, cxx[1:]


def triple_of(gxx):
    return os.path.basename(gxx)[: -len("-g++")]


def clang_layout(sysroot, triple):
    # clang does not take the "webos" vendor for a gcc layout it knows, so
    # it is shown where the sysroot's gcc and libstdc++ headers are.
    gccdir = sorted(glob.glob(os.path.join(sysroot, "usr/lib", triple, "*")))[-1]
    cxxinc = os.path.join(sysroot, "usr/include/c++", os.path.basename(gccdir))
    return ["--gcc-install-dir=" + gccdir, "-isystem", cxxinc, "-isystem", os.path.join(cxxinc, triple)]


native = os.path.join(os.path.abspath(opts.native), "recipe-sysroot-native", "usr")
host_bd = os.path.join(opts.work, "host")
cfg = run(["cmake", "-S", os.path.join(SRC, "tests"), "-B", host_bd, "-G", "Ninja",
           "-DCMAKE_PREFIX_PATH=" + native, "-DGTEST_SOURCE_DIR=" + opts.gtest,
           "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "-DCMAKE_BUILD_TYPE=Release",
           "-DCMAKE_CXX_FLAGS_RELEASE=" + " ".join(EXTRA)])
if cfg.returncode:
    sys.exit("configure failed:\n" + cfg.stdout[-3000:] + cfg.stderr[-3000:])
db = [e for e in json.load(open(os.path.join(host_bd, "compile_commands.json")))
      if os.path.realpath(e["file"]).startswith(SRC + "/")]
if not db:
    sys.exit("no compile commands for this tree")


def base_args(e):
    """The host command without compiler, output and the build's own -O/-W."""
    args = shlex.split(e["command"])[1:]
    out, skip = [], False
    for a in args:
        if skip:
            skip = False
            continue
        if a in ("-o", "-MF", "-MT", "-MQ"):
            skip = True
            continue
        if a.startswith(("-M", "-O")) or a in EXTRA:
            continue
        out.append(a)
    return out


def swap_sysroot(args, sysroot):
    new = os.path.join(sysroot, "usr")
    args = [a.replace(native, new) for a in args]
    # <sysroot>/usr/include is on the default path already; named again with
    # -isystem it moves ahead of libstdc++'s directory and breaks the
    # #include_next <stdlib.h> in <cstdlib>.
    inc, out, i = os.path.join(new, "include"), [], 0
    while i < len(args):
        if args[i] in ("-isystem", "-I") and i + 1 < len(args) and args[i + 1] == inc:
            i += 2
            continue
        if args[i] in ("-isystem" + inc, "-I" + inc):
            i += 1
            continue
        out.append(args[i])
        i += 1
    return out


builds = []   # (arch, compiler, argv prefix, arg transform)
for arch, key in (("armv7", "armv7"), ("aarch64", "aarch64")):
    wd = os.path.abspath(getattr(opts, key))
    sysroot = os.path.join(wd, "recipe-sysroot")
    gxx, tflags = image_cxx(wd)
    triple = triple_of(gxx)
    builds.append((arch, "gcc " + run([gxx, "-dumpfullversion"]).stdout.strip(),
                   [gxx] + tflags + ["--sysroot=" + sysroot],
                   lambda a, s=sysroot: swap_sysroot(a, s)))
    builds.append((arch, "clang",
                   ["clang++", "--target=" + triple, "--sysroot=" + sysroot]
                   + [f for f in tflags if not f.startswith(GCC_ONLY)]
                   + clang_layout(sysroot, triple),
                   lambda a, s=sysroot: swap_sysroot(a, s)))

builds.append(("x86-64", "gcc " + run(["g++", "-dumpfullversion"]).stdout.strip() + " (host, native Qt)",
               ["g++"], lambda a: a))
builds.append(("x86-64", "clang (host, native Qt)", ["clang++"], lambda a: a))
if opts.corei7:
    wd = os.path.abspath(opts.corei7)
    sysroot = os.path.join(wd, "recipe-sysroot")
    gxx = glob.glob(os.path.join(wd, "recipe-sysroot-native/usr/bin/*/x86_64-*-g++"))[0]
    # Qt's headers stay the native sysroot's; everything else is corei7's.
    builds.append(("x86-64", "gcc " + run([gxx, "-dumpfullversion"]).stdout.strip() + " (image, corei7)",
                   [gxx, "-march=corei7", "-mtune=corei7", "-fstack-protector-strong",
                    "-D_FORTIFY_SOURCE=2", "-Wformat", "-Wformat-security", "--sysroot=" + sysroot],
                   lambda a: a))

fail = False
results = []
for arch, cc, prefix, transform in builds:
    out, ok, n = "", True, 0
    for e in db:
        args = prefix + EXTRA + transform(base_args(e))
        if "clang" in cc:
            args = [a for a in args if not a.startswith(GCC_ONLY)] + [
                "-Wno-unknown-warning-option", "-Wno-unused-command-line-argument"]
        args += ["-o", "/dev/null"]
        r = run(args, cwd=e["directory"])
        out += r.stderr
        n += 1
        if r.returncode:
            ok = False
            print("FAIL %s %s %s\n%s" % (arch, cc, e["file"], r.stderr[-3000:]))
    ours, theirs = classify(out)
    results.append((arch, cc, ok, n, ours, theirs))

print()
for arch, cc, ok, n, ours, theirs in results:
    state = "ok" if ok else "BUILD FAILED"
    print("%-8s %-40s %s, %d files, our warnings: %d, other headers: %d"
          % (arch, cc, state, n, len(ours), len(theirs)))
    for k in sorted(ours):
        print("    " + k)
    for k in sorted(theirs):
        print("    (header) " + k)
    if not ok or ours:
        fail = True
sys.exit(1 if fail else 0)
