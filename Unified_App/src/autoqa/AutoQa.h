#pragma once

// `labrador --auto-qa <out_dir>`: the whole QA in one run of the release binary
// (docs/auto-qa.md). Hardware loopback tests and the firmware round trip run on a
// worker thread while the window shows a status screen; then a screenshot tour
// drives the app's own layout, theme, side-panel page and text-size setters with
// the board connected and its generators looped back, and again with USB off;
// then report.json is written and the app exits, 0 only if every test passed.

#include "autoqa/Report.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class App;

namespace autoqa
{
struct Shot;

class AutoQa
{
  public:
    // Creates `out_dir` and starts progress.log in it straight away (the QA server
    // watches for it). Throws std::runtime_error if either cannot be created.
    // Also fixes the environment the app reads (testing hooks off, generators on).
    AutoQa(std::string out_dir, std::string exe_path);
    ~AutoQa();

    // The settings.ini the app uses for the whole run instead of the user's.
    std::string settingsPath() const;

    // One flushed, timestamped line in progress.log (and stdout). Thread safe.
    void log(const std::string& line);

    // Called once per frame from App::Update. While hardwarePhase() is true the
    // app must leave the board alone and only draw statusText().
    void frame(App& app);
    bool hardwarePhase() const { return m_phase == Phase::Start || m_phase == Phase::Hardware; }
    std::string statusText() const;

  private:
    enum class Phase
    {
        Start,
        Hardware,
        Shots,
        Done
    };
    enum class ShotStep
    {
        Setup,
        Settle,
        Capture
    };

    void startHardware(App& app);
    void finishHardware(App& app);
    void shotFrame(App& app);
    void evaluateShot(App& app, TestContext& ctx, const Shot& shot, int w, int h,
                      const std::vector<unsigned char>& rgb);
    void finish(App& app);
    void writeReport();

    std::string m_out;
    std::string m_exe;
    std::string m_started_at;
    double m_t0;
    std::mutex m_mutex; // progress.log and the status text
    std::FILE* m_progress = nullptr;
    std::string m_status;
    Runner m_runner;
    std::vector<Screenshot> m_screenshots;

    Phase m_phase = Phase::Start;
    std::thread m_worker;
    std::atomic<bool> m_worker_done{false};
    std::string m_worker_error;

    std::vector<Shot> m_shots;
    size_t m_shot = 0;
    ShotStep m_step = ShotStep::Setup;
    double m_step_start = 0;
    int m_step_frames = 0;
    int m_last_w = 0, m_last_h = 0, m_stable_frames = 0;
    bool m_usb_disabled = false;
    bool m_board_seen_streaming = false;
};
} // namespace autoqa
