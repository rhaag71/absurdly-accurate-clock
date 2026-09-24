#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
test_dir=$(mktemp -d /tmp/clock-host-tests.XXXXXX)
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
c++ -std=c++11 -Wall -Wextra -Werror -I include \
    src/nmea_rmc.cpp src/clock_state.cpp src/clock_display.cpp \
    test/host/timebase_test.cpp -o "$test_dir/timebase"
"$test_dir/timebase"
c++ -std=c++11 -Wall -Wextra -Werror -I test/host/stubs -I include \
    src/pd2200.cpp src/clock_display.cpp src/clock_vfd.cpp test/host/vfd_test.cpp -o "$test_dir/vfd"
"$test_dir/vfd"
