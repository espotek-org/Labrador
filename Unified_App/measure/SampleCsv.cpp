#include "SampleCsv.h"

#include <stdexcept>

#include <cstdio>
#include <ostream>

void writeSamplesCsv(std::ostream& out, const std::vector<double>& ch1,
                     const std::vector<double>& ch2, double sample_rate_hz, int64_t zero_index)
{
    const bool two_channels = !ch2.empty();
    out << (two_channels ? "t,CH1,CH2\n" : "t,CH1\n");

    char line[96];
    for (size_t i = 0; i < ch1.size(); i++)
    {
        const double t = (static_cast<double>(i) - static_cast<double>(zero_index)) / sample_rate_hz;
        const int length =
            two_channels ? std::snprintf(line, sizeof line, "%.7f,%.4f,%.4f\n", t, ch1[i], ch2[i])
                         : std::snprintf(line, sizeof line, "%.7f,%.4f\n", t, ch1[i]);
        out.write(line, length);
    }
    out.flush();
    if (!out)
        throw std::runtime_error("writing the CSV failed");
}
