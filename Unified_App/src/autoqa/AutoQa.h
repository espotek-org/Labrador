#pragma once

// `labrador --auto-qa <out_dir>`: the whole QA in one run of the release binary
// (docs/auto-qa.md). Hardware loopback tests run on a worker thread while the
// window shows a status screen; then a screenshot tour drives the app's own
// layout, theme, side-panel page and text-size setters with the board connected
// and its generators looped back, and again with USB off; then the firmware round
// trip runs on the worker; then report.json is written and the app exits, 0 only
// if every test passed. A run carries out a Plan: the options choose its tests and
// how many times it repeats them, and each iteration goes through the three phases.

#include "autoqa/Environment.h"
#include "autoqa/Plan.h"
#include "autoqa/Report.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <functional>
#include <thread>
#include <vector>

class App;

namespace autoqa
{
struct Shot;
class HardwareTests;

// The screenshot tour's tests (group gui) in the tour's order. Needs no window.
std::vector<TestSpec> screenshotTestSpecs();

class AutoQa
{
  public:
    // Creates `out_dir`, starts progress.log in it and writes the first report.json,
    // with the whole plan and no results, straight away (the QA server watches for
    // them). Throws std::runtime_error if any of them cannot be created. Also fixes the
    // environment the app reads (testing hooks off, generators on).
    AutoQa(std::string out_dir, std::string exe_path, Plan plan);
    ~AutoQa();

    // The settings.ini the app uses for the whole run instead of the user's.
    std::string settingsPath() const;

    // One flushed, timestamped line in progress.log (and stdout). Thread safe.
    void log(const std::string& line);

    // Called once per frame from App::Update. While hardwarePhase() is true the
    // app must leave the board alone and only draw statusText().
    void frame(App& app);
    bool hardwarePhase() const
    {
        return m_phase == Phase::Start || m_phase == Phase::Hardware || m_phase == Phase::Firmware;
    }
    std::string statusText() const;

  private:
    enum class Phase
    {
        Start,
        Hardware,
        Shots,
        Firmware,
        Done
    };
    enum class ShotStep
    {
        Setup,
        Settle,
        Capture
    };

    // The worker's status line for the window; while a test makes progress (a soak's
    // running counts) it also goes to progress.log at least every HEARTBEAT_S.
    void setStatus(const std::string& status);
    // An iteration runs the three phases over its planned tests; a phase with none is skipped.
    void startIteration(App& app);
    void startHardware(App& app);
    void finishHardware(App& app);
    // The board work on the worker thread, with the app's own librador session set aside.
    void startWorker(std::function<void()> work);
    void restoreAppSession();
    void startShots(App& app);
    void startFirmware(App& app);
    void finishFirmware(App& app);
    void finishIteration(App& app);
    void shotFrame(App& app);
    const Shot& shotFor(const std::string& id) const;
    void evaluateShot(App& app, TestContext& ctx, const Shot& shot, int w, int h,
                      const std::vector<unsigned char>& rgb);
    void finish(App& app);
    // complete is false while the run is still going (the report is rewritten after
    // every result, so a crash or a killed VM leaves everything recorded so far).
    void writeReport(bool complete);

    std::string m_out;
    std::string m_exe;
    Plan m_plan;
    Machine m_machine = detectMachine();
    std::string m_started_at;
    double m_t0;
    mutable std::mutex m_mutex; // progress.log, the status text and the heartbeat's clock
    std::FILE* m_progress = nullptr;
    std::string m_status;
    double m_last_line_t = 0;
    Runner m_runner;
    std::unique_ptr<HardwareTests> m_hw;
    std::vector<Screenshot> m_screenshots;

    Phase m_phase = Phase::Start;
    std::thread m_worker;
    std::atomic<bool> m_worker_done{false};
    std::string m_worker_error;

    // The current iteration (1-based) and its planned tests, phase by phase.
    int m_iteration = 0;
    std::vector<PlannedTest> m_iter_hw, m_iter_shots, m_iter_firmware;

    std::vector<Shot> m_shots; // the whole matrix, looked up by id
    size_t m_shot = 0;         // index into m_iter_shots
    ShotStep m_step = ShotStep::Setup;
    double m_step_start = 0;
    int m_step_frames = 0;
    int m_last_w = 0, m_last_h = 0, m_stable_frames = 0;
    bool m_usb_disabled = false;
    bool m_board_seen_streaming = false;
};
} // namespace autoqa
