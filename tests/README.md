qt-features-webos test and hardening harness
============================================

What this tree ships, and what is tested:

- `tools/generate_qmap`: a native tool luna-surfacemanager's recipe runs to
  write `webos-keymap.qmap`, the keymap Qt's evdev keyboard handler loads on
  the device: Qt's default table followed by `webos_keymap[]`.
- `include/qweboskeyextension.h`: the webOS key codes, added to namespace Qt.
- `features/*.prf`: the qmake features components build with (`webos`,
  `webos-variables`, `webos-service`).

The C++ is tested with gtest, the qmake features with fixture projects run
through qmake. Hardening is the same set as the other LuneOS components:
sanitizers and valgrind over the tests, static analysis, a warning gate on
every architecture with both compilers, and a mutation check.

generate_qmap is a `-native` tool, so everything that runs uses the native
Qt sysroot: any recipe workdir whose `recipe-sysroot-native` has
qtbase-native, e.g. `tmp/work/x86_64-linux/qt-features-webos-native/<ver>`.
The build host has no googletest; it is built from source
(`GTEST_SOURCE_DIR`, e.g. a v1.17.0 checkout from `downloads/git2`).

Unit tests (`unit/`)
--------------------

    N=<native workdir>/recipe-sysroot-native/usr
    cmake -S tests -B build -G Ninja -DCMAKE_PREFIX_PATH=$N \
        -DGTEST_SOURCE_DIR=<googletest>
    ninja -C build
    LD_LIBRARY_PATH=$N/lib build/qt-features-webos-tests

| File | What it pins |
|---|---|
| `test_generate_qmap.cpp` | the file written: header, Qt's default table, then `webos_keymap[]`, then the compose table, nothing more; reproducible; permissions from the umask; an existing file replaced whole; a symlink written through. Wrong arguments, a missing directory, a non-regular target and a write failing part-way (RLIMIT_FSIZE, as a full disk) all exit 1, leave no stray file, and leave an existing keymap untouched |
| `test_keymap_tables.cpp` | the copied default table still being Qt's own, row for row (against the Qt headers the tests build with); each `webos_keymap[]` entry, from the kernel's keycode names; none shadowed by a default entry for the same keycode and modifiers (Qt takes the first); each Qt key distinct from the default table and from the others, but for KEY_PROG1 and MediaTek's 250, the same key on different phones |
| `test_evdev_handler.cpp` | the generated file through Qt's own `QEvdevKeyboardHandler`, into an offscreen window: Qt loads it, every webOS key arrives as its Qt key, default keys (letters, Shift, volume) unchanged, unmapped keys still dropped, and a truncated file refused, which leaves a phone without any of the webOS keys |
| `test_key_extension.cpp` | `qweboskeyextension.h` compiling into namespace Qt; every key with an explicit value; no value shared beyond the seven pairs webOS OSE shipped with; no value Qt itself uses for a key |

The tests run generate_qmap as a child process, so its own exits are what
is checked.

qmake features (`prf/`)
-----------------------

    LD_LIBRARY_PATH=$N/lib tests/prf/run-prf-tests.sh $N/bin/qmake6

Each case is a small project, configured with this tree's `features/` first
on QMAKEFEATURES and installed with INSTALL_ROOT; the installed tree is
compared with what it should be: webos-service's ACG files each installed
once into their own directory, with names that contain "service", "api",
"perm" or "group"; the legacy pub/prv layout; the `.in` substitution; the
refusal without WEBOS_SYSBUS_DIR; webosSet's defaults and environment
overrides; webos's upstart files.

Hardening (`hardening/`)
------------------------

- `build-all.py --native WD --armv7 WD --aarch64 WD [--corei7 WD]`: the
  warning gate. generate_qmap and the tests at -O2 -Wall -Wextra, with the
  image's gcc and with clang, for armv7, aarch64 and x86-64, each on its
  own sysroot; for x86-64 also the build host's gcc against the native Qt,
  which is what the recipe builds generate_qmap with. The armv7 and aarch64
  workdirs need Qt in their sysroot and a `temp/run.do_compile` for the
  image's flags (luneos-components, luna-surfacemanager); no corei7 recipe
  has Qt, so that build takes Qt's headers from the native sysroot. A
  warning in this tree fails it.
- `run-sanitizers.sh <native WD>`: the tests, generate_qmap included, under
  ASan+UBSan and TSan with gcc and clang, and valgrind memcheck following
  the tests into generate_qmap, one log per process. Qt is not
  instrumented; `lsan.supp`, `tsan.supp` and `valgrind.supp` are for what
  is found in it (empty so far). valgrind runs with Qt's PCRE2 JIT off,
  whose vector loads read past the end of a string. The run under the
  file size limit cannot finish its own log; it is listed, and read as far
  as it goes.
- `static-analysis.sh <native WD>`: gcc `-fanalyzer` at -O0 and -O2 (at
  its default limits, on generate_qmap and the tests' helpers; it does not
  finish the gtest bodies), the clang analyzer with the alpha checkers, clang-tidy, cppcheck (exhaustive,
  inconclusive), flawfinder and Coccinelle (`cocci/`): a tool's output
  through a plain QFile, and qFatal() in a tool. The rules are checked
  against `cocci/selftest.cpp` so one that matches nothing fails, and both
  flag the old generate_qmap. Findings not in `baseline.txt` fail. sparse is
  not run: it parses C only.
- `mutation-check.py <native WD>`: twenty mutants, each putting back a fault
  this tree had (the unchecked QFile, the substring globs in
  webos-service.prf, webosSet's lost fifth default) or one the tests are
  there to stop (a webOS key lost, shadowed, given a modifier or another
  key's Qt code; the default table drifting from Qt's; the QKeyboardMap
  alias that broke the build on Qt 6.9; a shared or Qt-colliding key value).
  Each has to make the unit tests, the prf tests or the build fail.

There is nothing to run on a device: the keymap goes through the same
QEvdevKeyboardHandler code here as on the phone. What a device adds is the
kernel's keycodes, which are a per-device question for the shell.
