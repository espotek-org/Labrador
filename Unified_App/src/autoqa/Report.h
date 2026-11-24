#pragma once

// --auto-qa's test bookkeeping and report.json (schema_version 1, see
// docs/auto-qa.md): tests with measurements and artifacts, screenshots with an
// `expect` caption, and the runner that executes tests and records outcomes.

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
    TestContext(TestResult& result, const std::string& out_dir) : m_result(result), m_out_dir(out_dir) {}

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
    std::string absolute(const std::string& relative) const { return m_out_dir + "/" + relative; }

    [[noreturn]] void fail(const std::string& message) { throw TestFailed{message}; }
    [[noreturn]] void skip(const std::string& message) { throw TestSkipped{message}; }

    TestResult& result() { return m_result; }
    const std::vector<std::string>& notes() const { return m_notes; }

  private:
    TestResult& m_result;
    std::string m_out_dir;
    std::vector<std::string> m_notes;
};

struct Condition
{
    bool ok = false;
    std::string reason;
};

// Runs tests in registration order and records their outcomes. A test whose
// precondition (`needs`) is unmet fails or is skipped without running, as the
// condition says.
class Runner
{
  public:
    using Fn = std::function<void(TestContext&)>;
    using Progress = std::function<void(const std::string&)>;

    Runner(std::string out_dir, Progress progress) : m_out_dir(std::move(out_dir)), m_progress(std::move(progress)) {}

    void setCondition(const std::string& name, bool ok, const std::string& reason = std::string());
    // Tests whose `needs` were not met: fail (true) or skip (false).
    void setUnmetFails(const std::string& name, bool fails) { m_unmet_fails[name] = fails; }

    // Runs one test now and records it.
    void run(const std::string& id, const std::string& group, const std::string& title,
             const std::vector<std::string>& needs, const Fn& fn);

    // For tests that span several frames (screenshots): begin() returns the
    // context (or nullptr when a precondition was unmet and the result is
    // already recorded); finish() closes it, `error` holding an exception message.
    TestContext* begin(const std::string& id, const std::string& group, const std::string& title,
                       const std::vector<std::string>& needs);
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

    std::string m_out_dir;
    Progress m_progress;
    std::map<std::string, Condition> m_conditions;
    std::map<std::string, bool> m_unmet_fails;
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
