#!/usr/bin/env bash
# ============================================================================
# Desktop tests. They compile the engine straight out of src/ with plain g++ and
# never touch the Rack SDK, so a character that will not open, a note that is
# judged wrongly or a sheet cut up in the wrong place shows up here in a second
# instead of after a rebuild and a restart of Rack.
#
#   tests/run_tests.sh            all of them
#   tests/run_tests.sh chart      just tests/test_chart.cpp
# ============================================================================
set -e
cd "$(dirname "$0")/.."

BUILD=build/tests
mkdir -p "$BUILD"

# The fixtures are checked in. This is for a fresh clone, and for when one is
# deleted on purpose to see the generator still works.
if [ ! -f tests/fixtures/mod/characters/dad.json ]; then
    echo "-- Generating test fixtures..."
    python3 tools/make_fixtures.py
fi

CXX="${CXX:-g++}"
# An array, not a string: this project's own folder has spaces in its name, and
# word-splitting a flags string would tear the fixture path in half.
FLAGS=(-std=c++17 -g -O1 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
       -Isrc -Ithird_party -Itests
       "-DFIXTURE_DIR=\"$PWD/tests/fixtures\"")

only="${1:-}"
fails=0
ran=0

for test_src in tests/test_*.cpp; do
    name="$(basename "$test_src" .cpp)"       # test_chart
    short="${name#test_}"                     # chart
    if [ -n "$only" ] && [ "$short" != "$only" ]; then
        continue
    fi

    # Each test names the engine files it needs on its first line, as
    #   // deps: Chart.cpp Character.cpp
    deps="$(sed -n 's|^// deps: ||p' "$test_src" | head -1)"
    srcs=("$test_src")
    for d in $deps; do
        srcs+=("src/$d")
    done

    echo "== $name"
    ran=$((ran + 1))
    "$CXX" "${FLAGS[@]}" -o "$BUILD/$name" "${srcs[@]}"
    if ! "$BUILD/$name"; then
        fails=$((fails + 1))
    fi
done

echo ""
if [ "$ran" = 0 ]; then
    echo "No tests matched '$only'."
    exit 1
fi
if [ "$fails" != 0 ]; then
    echo "FAILED: $fails of $ran test programs"
    exit 1
fi
echo "All $ran test programs passed."
