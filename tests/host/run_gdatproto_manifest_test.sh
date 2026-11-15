#!/bin/sh
# Host-only tests of the production Phase 9 manifest parser. Builds and data
# stay in a newly-created /tmp directory; this script does not read fixtures.
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_dir=$(mktemp -d /tmp/gdatproto-manifest-test.XXXXXX)
case "$test_dir" in
    /tmp/gdatproto-manifest-test.*) ;;
    *) echo "refusing unexpected temporary path: $test_dir" >&2; exit 2 ;;
esac

# Removes the temporary directory, then exits with the status the script was
# already exiting with (a failed compile or test). dash otherwise replaces the
# exit status with the trap's own last status (0 after an echo), so the
# original status is captured first and returned explicitly. A cleanup
# problem turns a passing run into a failure but never hides a failure.
cleanup() {
    status=$?
    trap - EXIT HUP INT TERM
    case "$test_dir" in
        /tmp/gdatproto-manifest-test.*) rm -rf -- "$test_dir" || true ;;
        *) echo "refusing to clean unexpected path: $test_dir" >&2
           if [ "$status" -eq 0 ]; then status=2; fi ;;
    esac
    if [ -e "$test_dir" ]; then
        echo "temporary directory cleanup failed: $test_dir" >&2
        if [ "$status" -eq 0 ]; then status=1; fi
    else
        echo "cleaned temporary test directory: $test_dir"
    fi
    if [ "$status" -ne 0 ]; then
        echo "manifest tests FAILED (exit status $status)" >&2
    fi
    exit "$status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

cd "$repo_root"
cc=${CC:-gcc}
sources="tests/host/test_gdatproto_manifest.c gdatproto/src/gp_manifest.c gdatproto/src/gp_common.c"

# AddressSanitizer and UndefinedBehaviorSanitizer are required (confirmed
# available in the project's WSL environment). A failed sanitizer build
# fails the script with the compiler's exit status; it is never treated as
# "sanitizers unavailable", and the regular build does not run after it.
"$cc" -std=c99 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer -Igdatproto/src -Iresproto/src $sources \
    -o "$test_dir/test_sanitized" || {
    status=$?
    echo "sanitizer build FAILED (exit status $status); AddressSanitizer and UndefinedBehaviorSanitizer are required" >&2
    exit "$status"
}
echo "sanitizer build: AddressSanitizer + UndefinedBehaviorSanitizer"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
    "$test_dir/test_sanitized"

"$cc" -std=c99 -Wall -Wextra -Werror -g -Igdatproto/src -Iresproto/src \
    $sources -o "$test_dir/test_regular"
echo "regular host build"
"$test_dir/test_regular"
