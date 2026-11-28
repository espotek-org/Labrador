#pragma once

// --auto-qa's test bookkeeping and report.json (schema_version 3, see
// docs/auto-qa.md): tests with measurements and artifacts, screenshots with an
// `expect` caption, and the runner that executes tests and records outcomes.

#include "autoqa/Plan.h"

#include <functional>
#include <stdexcept>
#include <map>
#include <optional>
#include <string>
#include <vector>

class JsonWriter;

namespace autoqa
{
struct Measurement
{
    std::string name;
    std::string unit;
    bool is_text = false;
    double value = 0;
    std::string text; // when is_text
    std::optional<double> expected;
    std::string expected_text; // when is_text and an expectation is set
    std::optional<double> min;
    std::optional<double> max;

    bool inLimits() const;
    std::string describe() const;
};

struct Artifact
{
    std::string path; // relative to the output directory
    std::string kind; // png | csv | json | log
    std::string caption;
};

struct TestResult
{
    std::string id;
    int iteration = 0; // 1-based; set by the Runner from its current iteration
    std::string group;
    std::string title;
    std::string status = "pass"; // pass | fail | skip | error
    double duration_s = 0;
    std::string message;
    std::vector<Measurement> measurements;
    std::vector<Artifact> artifacts;
};

struct Screenshot
{
    std::string id;
    std::string path;
    std::string caption;
    std::string expect;
};

// Thrown by TestContext::fail/skip to end a test early.
struct TestFailed
{
    std::string message;
};
struct TestSkipped
{
    std::string message;
};

// What a test sees: record measurements and artifacts, end early, write files.
class TestContext
{
  public:
    // `iteration_dir` is "" in a run without repeats, else "iter<k>": every file of the
    // test goes one directory level below its top directory, so iterations keep theirs.
    TestContext(TestResult& result, const std::string& out_dir, const std::string& iteration_dir)
        : m_result(result), m_out_dir(out_dir), m_iteration_dir(iteration_dir)
    {
    }

    // A number with optional expectation and limits; outside the limits fails the test.
    void measure(const std::string& name, const std::string& unit, double value,
                 std::optional<double> expected = std::nullopt, std::optional<double> min = std::nullopt,
                 std::optional<double> max = std::nullopt);
    // Text that must equal `expected` (when given) for the test to pass.
    void measureText(const std::string& name, const std::string& value,
                     const std::string& expected = std::string());
    // Context that goes in the message whatever the outcome.
    void note(const std::string& text);
    // `relative` is under the output directory.
    void artifact(const std::string& relative, const std::string& kind, const std::string& caption);
    // A fresh output file path for this test, e.g. path("plots", "ch1.png"): returns
    // the path relative to the output directory (directories are created).
    std::string path(const std::string& subdir, const std::string& suffix) const;
    // The path of `name` in `subdir` (and the iteration's directory below it), relative
    // to the output directory, with the directories created.
    std::string file(const std::string& subdir, const std::string& name) const;
    std::string absolute(const std::string& relative) const { return m_out_dir + "/" + relative; }

    [[noreturn]] void fail(const std::string& message) { throw TestFailed{message}; }
    [[noreturn]] void skip(const std::string& message) { throw TestSkipped{message}; }

    TestResult& result() { return m_result; }
    const std::vector<std::string>& notes() const { return m_notes; }

  private:
    TestResult& m_result;
    std::string m_out_dir;
    std::string m_iteration_dir;
    std::vector<std::string> m_notes;
};

struct Condition
{
    bool ok = false;
    std::string reason;
};

// Runs tests and records their outcomes. A test whose precondition (`needs`) is
// unmet fails or is skipped without running, as the condition says.
class Runner
{
  public:
    using Fn = std::function<void(TestContext&)>;
    using Progress = std::function<void(const std::string&)>;

    Runner(std::string out_dir, Progress progress) : m_out_dir(std::move(out_dir)), m_progress(std::move(progress)) {}

    // A condition starts unmet, for the reason given, at the start of every iteration.
    // Tests that need it while it is unmet fail (unmet_fails) or are skipped.
    void declareCondition(const std::string& name, const std::string& unmet_reason, bool unmet_fails);
    // Starts iteration `iteration` (1-based) of `repeat`: every declared condition goes
    // back to its initial unmet state, and with repeat above 1 the tests' files go to
    // iter<k>/ below their top directories.
    void startIteration(int iteration, int repeat);
    int iteration() const { return m_iteration; }
    // Only the running test may set a condition, and only one its TestSpec provides:
    // the plan adds a test's prerequisites from those declarations, so a test that set
    // a condition it does not declare would make a selection run without its provider.
    void setCondition(const std::string& name, bool ok, const std::string& reason = std::string());

    // Runs one test now and records it.
    void run(const TestSpec& spec, const Fn& fn);
    // Records a failed result for something that is not a planned test (a phase whose
    // worker thread stopped early: hw.suite, firmware.suite).
    void recordFailure(const std::string& id, const std::string& group, const std::string& title,
                       const std::string& message);

    // For tests that span several frames (screenshots): begin() returns the
    // context (or nullptr when a precondition was unmet and the result is
    // already recorded); finish() closes it, `error` holding an exception message.
    // These tests may not provide conditions.
    TestContext* begin(const TestSpec& spec);
    void finish(const std::string& failure, const std::string& skip, const std::string& error);

    // The context begin() opened; throws if none is open.
    TestContext* openContext()
    {
        if (!m_open_ctx)
            throw std::logic_error("Runner::openContext with no open test");
        return &*m_open_ctx;
    }

    const std::vector<TestResult>& results() const { return m_results; }
    void setPlanned(int n) { m_planned = n; }
    // Called after every recorded result (the report is rewritten so a crash keeps it).
    void setOnRecorded(std::function<void()> fn) { m_on_recorded = std::move(fn); }

  private:
    bool preconditionsMet(TestResult& result, const std::vector<std::string>& needs);
    void record(TestResult& result, TestContext* ctx);

    TestResult newResult(const std::string& id, const std::string& group, const std::string& title) const;

    std::string m_out_dir;
    Progress m_progress;
    std::map<std::string, Condition> m_declared; // each condition's initial (unmet) state
    std::map<std::string, Condition> m_conditions;
    std::map<std::string, bool> m_unmet_fails;
    int m_iteration = 0;
    std::string m_iteration_dir;
    const TestSpec* m_running = nullptr; // the test run() is executing
    std::vector<TestResult> m_results;
    int m_planned = 0;
    std::function<void()> m_on_recorded;
    // The multi-frame test in progress.
    std::optional<TestResult> m_open;
    std::optional<TestContext> m_open_ctx;
    double m_open_start = 0;
};

double nowSeconds(); // monotonic

void writeTestJson(JsonWriter& json, const TestResult& t);
} // namespace autoqa
