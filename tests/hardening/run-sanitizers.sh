#!/bin/sh
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
#
# run-sanitizers.sh <native recipe workdir> [work dir]
#
# Builds tests/ (generate_qmap and the unit tests) with the build host's gcc
# and clang against the native Qt sysroot, and runs them under:
#   ASan + UBSan (gcc, clang)   use after free, double free, overflows, leaks
#                               (LSan), undefined behaviour; clang adds its
#                               integer and nullability checks
#   TSan (gcc, clang)           data races
#   valgrind memcheck           on a plain -O2 build, as the recipe builds
#                               the -native tool with the host compiler:
#                               invalid accesses and definite leaks, in
#                               generate_qmap itself (followed through the
#                               tests' fork/exec) as well as the tests
#
# generate_qmap is instrumented too: the tests run it as a child, so each
# run of it, the failing ones included, is checked.
#
# The native workdir is one whose recipe-sysroot-native has qtbase-native,
# e.g. tmp/work/x86_64-linux/qt-features-webos-native/<ver>. Qt itself is not
# instrumented; lsan.supp, tsan.supp and valgrind.supp are for what turns up
# in it, and are empty so far.
set -u

W=${1:?usage: run-sanitizers.sh <native recipe workdir> [work dir]}
WORK=${2:-$HOME/claude-scratch/qt-features-webos/sanitizers}
GTEST=${GTEST_SOURCE_DIR:-$HOME/claude-scratch/qt-features-webos/googletest}
SRC=$(cd "$(dirname "$0")/../.." && pwd)
HERE=$SRC/tests/hardening
N=$W/recipe-sysroot-native/usr
fail=0
mkdir -p "$WORK/tmp"

export LD_LIBRARY_PATH="$N/lib"
export QT_QPA_PLATFORM=offscreen QT_PLUGIN_PATH="$N/plugins:$N/lib/plugins"
export ASAN_OPTIONS=detect_leaks=1:abort_on_error=1:detect_stack_use_after_return=1:fast_unwind_on_malloc=0:strict_string_checks=1
export LSAN_OPTIONS="suppressions=$HERE/lsan.supp:print_suppressions=0"
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
export TSAN_OPTIONS="halt_on_error=1:second_deadlock_stack=1:suppressions=$HERE/tsan.supp"
export DEBUGINFOD_URLS=
# The tests' temporary files, kept off /tmp.
export TMPDIR="$WORK/tmp"

build() {
	name=$1; cc=$2; cxx=$3; flags=$4
	echo "== build $name"
	cmake -S "$SRC/tests" -B "$WORK/$name" -G Ninja -DCMAKE_PREFIX_PATH="$N" \
		-DGTEST_SOURCE_DIR="$GTEST" -DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx" \
		-DCMAKE_BUILD_TYPE=None -DCMAKE_CXX_FLAGS="$flags" \
		-DCMAKE_EXE_LINKER_FLAGS="$flags" \
		>"$WORK/$name-configure.log" 2>&1 || { echo "FAIL configure $name"; tail -20 "$WORK/$name-configure.log"; fail=1; return 1; }
	ninja -C "$WORK/$name" >"$WORK/$name-build.log" 2>&1 \
		|| { echo "FAIL build $name"; grep -E "error|FAILED" "$WORK/$name-build.log" | head -20; fail=1; return 1; }
}

run() {
	name=$1; shift
	if "$@" >"$WORK/$name-run.log" 2>&1; then
		echo "PASS $name ($(grep -E '^\[  PASSED' "$WORK/$name-run.log"))"
	else
		echo "FAIL $name"
		grep -E "ERROR: |runtime error|WARNING: ThreadSanitizer|SUMMARY|Failure|FAILED|== *[0-9]+ errors" "$WORK/$name-run.log" | head -20
		fail=1
	fi
}

# Unsigned wrap-around is defined behaviour, and libstdc++ and Qt's inline
# hashing rely on it; the signed and conversion checks stay.
for cfg in "asan-gcc gcc g++ -fsanitize=address,undefined" \
           "asan-clang clang clang++ -fsanitize=address,undefined,integer,nullability:-fno-sanitize=unsigned-integer-overflow,unsigned-shift-base,implicit-integer-sign-change" \
           "tsan-gcc gcc g++ -fsanitize=thread" "tsan-clang clang clang++ -fsanitize=thread"; do
	# shellcheck disable=SC2086
	set -- $cfg
	build "$1" "$2" "$3" "-g -O1 -fno-omit-frame-pointer $(echo "$4" | tr ':' ' ')" \
		&& run "$1" "$WORK/$1/qt-features-webos-tests"
done

if command -v valgrind >/dev/null 2>&1; then
	if build valgrind gcc g++ "-g -O2"; then
		# PCRE2's JIT code (QRegularExpression) reads past the end of its
		# subject in 16-byte vector loads, which valgrind cannot tell from a
		# bug; Qt's interpreter has no such reads. Each process logs to its
		# own file, so generate_qmap's stderr is still only its own.
		rm -f "$WORK"/vg-*.log
		run valgrind env QT_ENABLE_REGEXP_JIT=0 valgrind --error-exitcode=99 --trace-children=yes \
			--log-file="$WORK/vg-%p.log" \
			--leak-check=full --errors-for-leak-kinds=definite --show-leak-kinds=definite \
			--track-origins=yes --suppressions="$HERE/valgrind.supp" \
			"$WORK/valgrind/qt-features-webos-tests"
		# The children's exits are the tests' to judge; their errors are not.
		# A generate_qmap run under the tests' RLIMIT_FSIZE cannot finish its
		# log either: that one is read as far as it goes, and listed.
		n=0; partial=0; bad=""
		for f in "$WORK"/vg-*.log; do
			n=$((n + 1))
			if grep -qE "Invalid (read|write|free)|uninitialised|definitely lost: [1-9]|Mismatched|Syscall param|ERROR SUMMARY: [1-9]" "$f"; then
				bad="$bad $f"
			elif ! grep -q "ERROR SUMMARY: 0 errors" "$f"; then
				partial=$((partial + 1))
				echo "     valgrind: $(basename "$f") cut short at $(wc -c <"$f") bytes (RLIMIT_FSIZE test), no error in it"
			fi
		done
		if [ -n "$bad" ]; then
			echo "FAIL valgrind: errors in$bad"
			for f in $bad; do grep -m1 -A12 -E "Invalid|uninitialised|definitely lost: [1-9]|Mismatched|Syscall" "$f"; done
			fail=1
		else
			echo "     valgrind: no errors in $n processes (the tests and each generate_qmap run; $partial log(s) cut short)"
		fi
	fi
else
	echo "== valgrind: not installed, skipped"
fi

exit $fail
