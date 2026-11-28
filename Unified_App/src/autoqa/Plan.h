#pragma once

// What a `--auto-qa` run sets out to do (docs/auto-qa.md, "Choosing what runs"): the
// suite's tests in their fixed order, filtered by level and selection, closed over the
// tests that establish the conditions the chosen ones need, and repeated. Computing a
// plan needs no window, no board and no output directory, so `--auto-qa-list` can print
// it and a usage error can exit before anything is created.

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

class JsonWriter;

namespace autoqa
{
// How thorough a run is. A run at level L holds every test whose minimum level is at or
// below L; a level can also set a test's parameters (the soaks are longer at thorough).
enum class Level
{
    Quick,
    Standard,
    Thorough
};
const char* levelName(Level level);

// A test as the plan sees it, before anything runs.
struct TestSpec
{
    std::string id;
    std::string group; // hw | gui | firmware
    std::string title;
    std::vector<std::string> needs;    // conditions that must hold before it runs
    std::vector<std::string> provides; // conditions it establishes (or finds unmet)
};

struct PlannedTest
{
    TestSpec test;
    Level level;       // the lowest level whose runs include this test
    int iteration;     // 1-based
    bool prerequisite; // added only because a selected test needs it
};

// A bad command line: main() prints the message on one line and exits 2.
struct UsageError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

struct Options
{
    Level level = Level::Standard;
    std::optional<std::vector<std::string>> selection; // nullopt: every test of the level
    int repeat = 1;
};

Level parseLevel(const std::string& text);
// The items of a --auto-qa-tests value, checked against the selection grammar.
std::vector<std::string> parseSelection(const std::string& text);
int parseRepeat(const std::string& text);

struct Plan
{
    Options options;
    std::vector<PlannedTest> entries; // every test execution, in execution order
};

// Throws UsageError when a selection item matches no test of the level.
Plan makePlan(const Options& options);

// The plan's fields (level, selection, repeat, planned) into the object being written.
void writePlanJson(JsonWriter& json, const Plan& plan);
} // namespace autoqa
