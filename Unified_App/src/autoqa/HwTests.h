#pragma once

#include "autoqa/Report.h"

#include <functional>
#include <string>

namespace autoqa
{
// The hardware loopback tests and the firmware round trip, run on the auto-QA
// worker thread while the app shows a status screen and leaves the board alone.
// Wiring assumed and verified by hw.wiring.*: SG1 drives scope input CH1, SG2
// drives CH2. Every test sets the PSU, both generators and the scope itself, so
// none depends on what an earlier one left behind.
//
// librador must be shut down (librador_exit) before this runs: the tests open
// their own sessions, one per transport, and close them again.
void runHardwareTests(Runner& runner, const std::string& out_dir,
                      const std::function<void(const std::string&)>& status);

// Number of tests runHardwareTests records (for "n/m" progress).
int hardwareTestCount();
} // namespace autoqa
