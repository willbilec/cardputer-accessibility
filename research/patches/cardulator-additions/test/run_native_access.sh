#!/usr/bin/env bash
# Run from the cardulator checkout, under Linux/WSL. No firmware/hardware I/O.
set -eu
trial_build=$(mktemp -d /tmp/cardulator-access.XXXXXX)
for source in units.c parse.tab.c strfunc.c; do
  gcc -std=gnu11 -w -Dmain=gnu_units_main -Ilib/gnu-units -c "lib/gnu-units/$source" -o "$trial_build/$source.o"
done
gcc -std=gnu11 -w -Ilib/gnu-units -c src/units_bridge.c -o "$trial_build/bridge.o"
gcc -std=gnu11 -w -c src/embedded_units_dat.c -o "$trial_build/data.o"
gcc -DUNITY_INCLUDE_DOUBLE -I../Unity/src -c ../Unity/src/unity.c -o "$trial_build/unity.o"
g++ -std=c++20 -O2 -DUNIT_TEST=1 -DUNITY_INCLUDE_DOUBLE -I../Unity/src -Isrc -Ilib/tinyexpr-plusplus -Ilib/gnu-units \
 test/test_main.cpp lib/tinyexpr-plusplus/tinyexpr.cpp "$trial_build/units.c.o" "$trial_build/parse.tab.c.o" "$trial_build/strfunc.c.o" "$trial_build/bridge.o" "$trial_build/data.o" "$trial_build/unity.o" -o "$trial_build/math-suite"
math_status=0
"$trial_build/math-suite" || math_status=$?
g++ -std=c++20 -Wall -Wextra -Werror -fmax-errors=3 -fsanitize=address,undefined test/test_accessibility.cpp -o "$trial_build/accessibility"
"$trial_build/accessibility"
g++ -std=c++20 -O2 -Isrc -Ilib/gnu-units test/test_compact_units.cpp "$trial_build/units.c.o" "$trial_build/parse.tab.c.o" "$trial_build/strfunc.c.o" "$trial_build/bridge.o" "$trial_build/data.o" -o "$trial_build/compact-units"
"$trial_build/compact-units"
"$trial_build/compact-units" original
exit "$math_status"
