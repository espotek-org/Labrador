#pragma once

#include "autoqa/Plan.h"
#include "autoqa/Report.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace autoqa
{
// The hardware loopback tests, and separately the firmware round trip, run on
// the auto-QA worker thread while the app shows a status screen and leaves the
// board alone. The round trip runs last of all, after the screenshot tour: it is
// the step most likely to break the USB link (re-enumeration through the DFU
// bootloader), so a failure there costs only its own result.
// Wiring assumed and verified by hw.wiring.*: SG1 drives scope input CH1, SG2
// drives CH2. Every test sets the PSU, both generators and the scope itself, so
// none depends on what an earlier one left behind.

// The hardware tests (group hw) and the firmware round trip (group firmware) in the
// suite's order, with the parameters of a run at `level`. Needs no board.
std::vector<TestSpec> hardwareTestSpecs(Level level);
std::vector<TestSpec> firmwareTestSpecs(Level level);

// Runs planned hardware and firmware tests. One object serves the whole run.
class HardwareTests
{
  public:
    // Declares the hardware tests' conditions on `runner`.
    HardwareTests(Runner& runner, std::string out_dir, Level level, std::function<void(const std::string&)> status);
    ~HardwareTests();

    // Drops the device session and what was learnt about the board, so the next
    // iteration starts where the first one did.
    void startIteration();
    // Runs these planned tests (all hw, or all firmware) in order on the calling thread.
    // librador must be shut down (librador_exit) first: the tests open their own
    // sessions, one per transport, and close them again.
    void run(const std::vector<PlannedTest>& planned);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace autoqa
