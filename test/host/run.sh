#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
test_dir=$(mktemp -d /tmp/clock-host-tests.XXXXXX)
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
c++ -std=c++11 -Wall -Wextra -Werror -I include \
    src/nmea_rmc.cpp src/nmea_gga.cpp src/clock_state.cpp src/display_time.cpp src/clock_display.cpp \
    test/host/timebase_test.cpp -o "$test_dir/timebase"
"$test_dir/timebase"
c++ -std=c++11 -Wall -Wextra -Werror -I test/host/stubs -I include \
    src/pd2200.cpp src/clock_state.cpp src/display_time.cpp src/clock_display.cpp src/clock_vfd.cpp test/host/vfd_test.cpp -o "$test_dir/vfd"
"$test_dir/vfd"
c++ -std=c++11 -Wall -Wextra -Werror -I test/host/stubs -I include \
    src/pd2200.cpp src/clock_state.cpp src/display_time.cpp src/clock_display.cpp src/clock_vfd.cpp \
    test/host/hh_test.cpp -o "$test_dir/hh"
"$test_dir/hh"
c++ -std=c++11 -Wall -Wextra -Werror -I test/host/stubs -I include \
    src/clock_state.cpp src/display_time.cpp src/zone_button.cpp src/clock_display.cpp \
    src/pd2200.cpp src/clock_vfd.cpp test/host/timezone_test.cpp -o "$test_dir/timezone"
"$test_dir/timezone"
c++ -std=c++11 -Wall -Wextra -Werror -I test/host/stubs -I include \
    src/clock_state.cpp src/display_time.cpp src/clock_display.cpp src/pd2200.cpp \
    src/clock_vfd.cpp test/host/hh_zero_test.cpp -o "$test_dir/hh_zero"
"$test_dir/hh_zero"
