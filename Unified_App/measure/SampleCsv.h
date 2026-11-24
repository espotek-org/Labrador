#pragma once

#include <cstdint>
#include <iosfwd>
#include <vector>

// Writes samples as CSV with the header "t,CH1,CH2" ("t,CH1" when `ch2` is empty):
// t in seconds, 0 at sample `zero_index`; the channels in volts. Throws
// std::runtime_error if the stream fails.
void writeSamplesCsv(std::ostream& out, const std::vector<double>& ch1,
                     const std::vector<double>& ch2, double sample_rate_hz, int64_t zero_index);
