#include "display_time.hpp"
#include "clock_state.hpp"

namespace presentation {
DisplayZone nextZone(DisplayZone zone) {
    return static_cast<DisplayZone>((static_cast<unsigned>(zone) + 1) % 5);
}
const char* zoneName(DisplayZone zone) {
    const char* names[] = {"UTC", "Eastern", "Central", "Mountain", "Pacific"};
    return names[static_cast<unsigned>(zone)];
}
namespace {
int64_t transition(unsigned year, unsigned month, unsigned sunday,
                   int preceding_offset) {
    nmea::Utc date;
    date.year = year; date.month = month; date.day = 1;
    const int64_t first = clock_model::toUnix(date);
    // Unix day 0 was Thursday; Sunday is weekday 0.
    const unsigned weekday = static_cast<unsigned>((first / 86400 + 4) % 7);
    const unsigned day = 1 + (7 - weekday) % 7 + 7 * (sunday - 1);
    // Local 02:00, interpreted with the offset BEFORE the transition.
    return first + (day - 1) * 86400LL + (2 - preceding_offset) * 3600LL;
}
}
DisplayTime convertUtcForDisplay(const nmea::Utc& utc, DisplayZone zone) {
    if (zone == DisplayZone::utc) return {utc, "UTC", 0, false};
    const unsigned index = static_cast<unsigned>(zone) - 1;
    const int standard = -5 - static_cast<int>(index);
    const int64_t epoch = clock_model::toUnix(utc);
    const bool daylight = epoch >= transition(utc.year, 3, 2, standard) &&
                          epoch < transition(utc.year, 11, 1, standard + 1);
    const char* standard_labels[] = {"EST", "CST", "MST", "PST"};
    const char* daylight_labels[] = {"EDT", "CDT", "MDT", "PDT"};
    const int offset = standard + (daylight ? 1 : 0);
    return {clock_model::fromUnix(epoch + offset * 3600LL),
            daylight ? daylight_labels[index] : standard_labels[index], offset, daylight};
}
}
