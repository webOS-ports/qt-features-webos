#!/usr/bin/env python3
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
#
# mutation-check.py [--work DIR] <native recipe workdir>
#
# Shows the tests can fail: each mutant below breaks one behaviour they are
# meant to pin, in a copy of the tree, and the unit tests (C++ mutants) or
# prf/run-prf-tests.sh (qmake mutants) have to fail on it, or the build has
# to. The unmutated copy has to pass first, or nothing below means anything.
# Most of them put back a fault this tree had: generate_qmap's unchecked
# QFile, the substring globs in webos-service.prf, webosSet's lost fifth
# default.

import argparse, os, shutil, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--work", default=os.path.expanduser("~/claude-scratch/qt-features-webos/mutants"))
ap.add_argument("--gtest", default=os.path.expanduser("~/claude-scratch/qt-features-webos/googletest"))
ap.add_argument("workdir")
opts = ap.parse_args()
sys.stdout.reconfigure(line_buffering=True)

SRC = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
NATIVE = os.path.join(os.path.abspath(opts.workdir), "recipe-sysroot-native", "usr")
GQ = "tools/generate_qmap/generate_qmap.cpp"
WK = "tools/generate_qmap/webos_keymap_p.h"
DM = "tools/generate_qmap/qevdevkeyboard_defaultmap_p.h"
KH = "include/qweboskeyextension.h"
SVC = "features/webos-service.prf"
VARS = "features/webos-variables.prf"

# name, file, text, replacement, kind ("unit" or "prf"), what should catch it
# (file, text and replacement may be parallel lists)
MUTANTS = [
    # generate_qmap as it was: a buffered QFile whose flush nobody checks.
    ("plain-qfile", [GQ, GQ, GQ],
     ["#include <QSaveFile>", "    QSaveFile f(", "    if (ds.status() != QDataStream::Ok || !f.commit()) {"],
     ["#include <QFile>", "    QFile f(", "    if (ds.status() != QDataStream::Ok) {"],
     "unit", "GenerateQmap.NonRegularTargetFails, .WriteFailure*"),
    ("commit-unchecked", GQ,
     "    if (ds.status() != QDataStream::Ok || !f.commit()) {",
     "    f.commit();\n    if (ds.status() != QDataStream::Ok) {",
     "unit", "GenerateQmap.WriteFailureFailsAndKeepsTheOldFile"),
    ("usage-exits-0", GQ,
     "argc > 0 && argv[0] ? argv[0] : \"generate_qmap\");\n        return 1;",
     "argc > 0 && argv[0] ? argv[0] : \"generate_qmap\");\n        return 0;",
     "unit", "GenerateQmap.WrongArgumentCountFailsCleanly"),
    ("open-failure-exits-0", GQ,
     "filename, qPrintable(f.errorString()));\n        return 1;\n    }\n\n    // .qmap",
     "filename, qPrintable(f.errorString()));\n        return 0;\n    }\n\n    // .qmap",
     "unit", "GenerateQmap.MissingDirectoryFails"),
    ("webos-table-first", GQ,
     "    for (quint32 i = 0; i < default_keymap_size; ++i)\n        ds << WebOSKeymapHandler::s_keymap_default[i];\n"
     "    for (quint32 i = 0; i < webos_keymap_size; ++i)\n        ds << webos_keymap[i];\n",
     "    for (quint32 i = 0; i < webos_keymap_size; ++i)\n        ds << webos_keymap[i];\n"
     "    for (quint32 i = 0; i < default_keymap_size; ++i)\n        ds << WebOSKeymapHandler::s_keymap_default[i];\n",
     "unit", "GenerateQmap.WritesTheDefaultTableThenWebOSThenCompose"),
    ("compose-count-short", GQ,
     "qmap_keymap_size << default_keycompose_size;",
     "qmap_keymap_size << default_keycompose_size - 1;",
     "unit", "GenerateQmap.WritesTheDefaultTableThenWebOSThenCompose"),
    # The keymap.
    ("mediatek-key-lost", WK, "    { 250, 0xffff,", "    { 251, 0xffff,",
     "unit", "KeymapTables.WebOSEntriesAreTheOnesIntended, EvdevHandler.*"),
    ("hangup-read-as-back", WK, "{ 172, 0xffff, Qt::Key_HomePage,", "{ 172, 0xffff, Qt::Key_Back,",
     "unit", "KeymapTables.WebOSQtKeysAreDistinctFromTheDefaultTables"),
    ("entry-shadowed-by-default", WK, "{ 122, 0xffff, Qt::Key_Hangul,", "{ 113, 0xffff, Qt::Key_Hangul,",
     "unit", "KeymapTables.WebOSEntriesAreNotShadowedByTheDefaultTable"),
    ("entry-with-modifier", WK, "{ 231, 0xffff, Qt::Key_Call,     0x00,", "{ 231, 0xffff, Qt::Key_Call,     0x01,",
     "unit", "KeymapTables.WebOSEntriesAreTheOnesIntended, EvdevHandler.*"),
    ("qkeyboardmap-alias-back", WK,
     "// Keymap for WebOS specific",
     "#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)\nnamespace QEvdevKeyboardMap = QKeyboardMap;\n#endif\n\n// Keymap for WebOS specific",
     "unit", "the build (0002)"),
    ("default-table-drift", DM,
     "    {   2, 0x0031, 0x00000031, 0x00, 0x00, 0x0000 },",
     "    {   2, 0x0032, 0x00000032, 0x00, 0x00, 0x0000 },",
     "unit", "KeymapTables.DefaultTableIsQtsOwn"),
    # The key extension header.
    ("new-shared-key-value", KH, "Key_webOS_Exit        = 0x01200001", "Key_webOS_Exit        = 0x01200002",
     "unit", "KeyExtension.NoNewValueIsShared"),
    ("key-value-is-a-qt-key", KH, "Key_webOS_Info        = 0x01200002", "Key_webOS_Info        = 0x01000000",
     "unit", "KeyExtension.NoValueIsAQtKey"),
    ("key-value-implicit", KH, "Key_webOS_Settings    = 0x01200004", "Key_webOS_Settings",
     "unit", "KeyExtension.EveryKeyHasAnExplicitValue"),
    # qmake features.
    ("ls2-role-service-substring", [SVC, SVC],
     ["SYSBUS_GLOB_IN = *.role.json.in", "SYSBUS_GLOB_IN = *.service.in"],
     ["SYSBUS_GLOB_IN = *role*.in", "SYSBUS_GLOB_IN = *service*.in"],
     "prf", "webos-service: ACG files each installed once"),
    ("ls2-perm-substring", SVC, "SYSBUS_GLOB = *.perm.json", "SYSBUS_GLOB = *.perm*.json",
     "prf", "webos-service: type words inside a name"),
    ("ls2-groups-dropped", SVC, "SYSBUS_GLOB = *.groups.json *.group.json", "SYSBUS_GLOB = *.group.json",
     "prf", "webos-service: ACG files each installed once"),
    ("webosset-fifth-default", VARS, "isEmpty(value): value = $$6", "isEmpty(value): value = $$5",
     "prf", "webos-variables: webosSet takes the first non-empty of five defaults"),
    ("upstart-not-substituted", "features/webos.prf",
     "    QMAKE_SUBSTITUTES += $$UPSTART_FILES\n", "",
     "prf", "webos: upstart file substituted"),
]

TREE = os.path.join(opts.work, "tree")
BUILD = os.path.join(opts.work, "build")
ENV = dict(os.environ, LD_LIBRARY_PATH=os.path.join(NATIVE, "lib"), QT_QPA_PLATFORM="offscreen",
           TMPDIR=os.path.join(opts.work, "tmp"))


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, env=ENV, **kw)


def copy_tree():
    if os.path.exists(TREE):
        shutil.rmtree(TREE)
    files = run(["git", "ls-files", "--cached", "--others", "--exclude-standard"], cwd=SRC).stdout.split()
    for f in files:
        dst = os.path.join(TREE, f)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(os.path.join(SRC, f), dst)


def unit():
    b = run(["ninja", "-C", BUILD])
    if b.returncode:
        return False, "build failed"
    r = run([os.path.join(BUILD, "qt-features-webos-tests"), "--gtest_brief=1"])
    # "[  FAILED  ] Suite.Test (3 ms)"
    failed = [l[len("[  FAILED  ]"):].split()[0] for l in r.stdout.splitlines()
              if l.startswith("[  FAILED  ]") and "." in l]
    return r.returncode == 0, ", ".join(sorted(set(failed))) or r.stdout[-300:]


def prf():
    r = run([os.path.join(TREE, "tests/prf/run-prf-tests.sh"), os.path.join(NATIVE, "bin", "qmake6")])
    failed = [l[5:] for l in r.stdout.splitlines() if l.startswith("FAIL ")]
    return r.returncode == 0, "; ".join(failed)


os.makedirs(ENV["TMPDIR"], exist_ok=True)
copy_tree()
cfg = run(["cmake", "-S", os.path.join(TREE, "tests"), "-B", BUILD, "-G", "Ninja",
           "-DCMAKE_PREFIX_PATH=" + NATIVE, "-DGTEST_SOURCE_DIR=" + opts.gtest, "-DCMAKE_BUILD_TYPE=Debug"])
if cfg.returncode:
    sys.exit("configure failed:\n" + cfg.stdout[-2000:] + cfg.stderr[-2000:])

for kind, check in (("unit", unit), ("prf", prf)):
    ok, why = check()
    if not ok:
        sys.exit("the unmutated tree fails its %s tests: %s" % (kind, why))
print("unmutated tree: unit and prf tests pass\n")

survivors = []
for name, files, olds, news, kind, catcher in MUTANTS:
    if isinstance(files, str):
        files, olds, news = [files], [olds], [news]
    saved = {}
    applied = True
    for f, old, new in zip(files, olds, news):
        p = os.path.join(TREE, f)
        text = saved.setdefault(p, open(p).read()) if p not in saved else open(p).read()
        if text.count(old) != 1:
            print("BROKEN %-28s text not found once in %s" % (name, f))
            applied = False
            break
        open(p, "w").write(text.replace(old, new))
    if applied:
        ok, why = (unit if kind == "unit" else prf)()
        if ok:
            survivors.append(name)
            print("SURVIVED %-28s (meant for: %s)" % (name, catcher))
        else:
            print("killed   %-28s by %s" % (name, why[:200]))
    else:
        survivors.append(name)
    for p, text in saved.items():
        open(p, "w").write(text)

print()
print("%d mutants, %d survived%s" % (len(MUTANTS), len(survivors), (": " + ", ".join(survivors)) if survivors else ""))
sys.exit(1 if survivors else 0)
