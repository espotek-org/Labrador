#include "autoqa/Plan.h"

#include "autoqa/AutoQa.h"
#include "autoqa/HwTests.h"

#include "JsonWriter.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>

namespace autoqa
{
namespace
{
// Every test's minimum level is standard unless its id is in one of these lists, so moving
// a test between levels means moving its id from one list to another. An id here that
// names no test is an error, which keeps the lists in step with renames.
//
// HARD CONSTRAINT (project owner): every waveform test has a version at exactly 831 Hz,
// and the whole 831 Hz battery is part of standard. No *_831hz test may be listed under
// THOROUGH_TESTS; levelTables() refuses one. QUICK_TESTS draws its waveform tests from
// the 831 Hz versions.
const std::vector<std::string> QUICK_TESTS = {
    "hw.board.present",
    "hw.wiring.sg1",
    "hw.wiring.sg2",
    "hw.noise.ch1",
    "hw.noise.ch2",
    "hw.siggen.ch1.sine_831hz",
    "hw.siggen.ch1.square_831hz",
    "hw.siggen.ch1.triangle_831hz",
    "hw.siggen.ch1.sawtooth_831hz",
    "hw.siggen.ch2.sine_831hz",
    "hw.siggen.ch2.square_831hz",
    "hw.siggen.ch2.triangle_831hz",
    "hw.siggen.ch2.sawtooth_831hz",
    "hw.siggen.dc_levels",
    "hw.gain.x0_5_831hz",
    "hw.gain.x4_831hz",
    "hw.gain.x64_831hz",
    "hw.trigger.ch1_rising_831hz",
    "hw.trigger.ch2_rising_square_831hz",
    "hw.transport.bulk.measure",
    "hw.transport.bulk.measure_831hz",
    "hw.transport.iso1.measure",
    "hw.transport.iso1.measure_831hz",
    "hw.transport.iso6.measure",
    "hw.transport.iso6.measure_831hz",
    "gui.desktop_1280x720.classic-dark.scope.text100.board",
    "gui.compact_800x480.classic-dark.text100.board",
    "gui.mobile_480x720.classic-dark.text100.board",
    "gui.desktop_1280x720.classic-dark.scope.text100.noboard",
};
// No test is thorough-only yet; thorough only lengthens the soaks.
const std::vector<std::string> THOROUGH_TESTS = {};

constexpr size_t MAX_SELECTION_ITEMS = 20;
constexpr size_t MAX_ITEM_LENGTH = 80;
constexpr int MAX_REPEAT = 200;

bool endsWith(const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Every test of the suite in its fixed order (hardware phase, screenshot tour, firmware
// round trip), with the parameters of a run at `level`, and each test's minimum level.
struct Catalogue
{
    std::vector<TestSpec> tests;
    std::vector<Level> levels;
};

Catalogue catalogue(Level level)
{
    Catalogue c;
    for (std::vector<TestSpec> part : {hardwareTestSpecs(level), screenshotTestSpecs(), firmwareTestSpecs(level)})
        c.tests.insert(c.tests.end(), part.begin(), part.end());
    std::map<std::string, size_t> index;
    for (size_t i = 0; i < c.tests.size(); i++)
        if (!index.emplace(c.tests[i].id, i).second)
            throw std::logic_error("the suite has two tests with the id " + c.tests[i].id);
    c.levels.assign(c.tests.size(), Level::Standard);
    const auto assign = [&](const std::vector<std::string>& ids, Level l) {
        for (const std::string& id : ids)
        {
            const auto it = index.find(id);
            if (it == index.end())
                throw std::logic_error(std::string("the ") + levelName(l) + " level lists " + id +
                                       ", which is not a test of the suite");
            if (l == Level::Thorough && endsWith(id, "_831hz"))
                throw std::logic_error(id + " is listed as thorough-only, but the 831 Hz battery belongs to standard");
            c.levels[it->second] = l;
        }
    };
    assign(QUICK_TESTS, Level::Quick);
    assign(THOROUGH_TESTS, Level::Thorough);
    return c;
}

bool validItemChar(char ch, bool first)
{
    if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '*')
        return true;
    return !first && (ch == '.' || ch == '-');
}

// `*` matches any run of characters, dots included; everything else matches itself.
bool globMatch(const std::string& pattern, const std::string& text)
{
    size_t p = 0, t = 0, star = std::string::npos, resume = 0;
    while (t < text.size())
    {
        if (p < pattern.size() && pattern[p] == '*')
        {
            star = p++;
            resume = t;
        }
        else if (p < pattern.size() && pattern[p] == text[t])
        {
            p++;
            t++;
        }
        else if (star != std::string::npos)
        {
            p = star + 1;
            t = ++resume;
        }
        else
            return false;
    }
    while (p < pattern.size() && pattern[p] == '*')
        p++;
    return p == pattern.size();
}

// An item without * selects the test with that id and every test whose id starts with
// "<item>."; an item with * is a glob over the whole id.
bool itemMatches(const std::string& item, const std::string& id)
{
    if (item.find('*') != std::string::npos)
        return globMatch(item, id);
    return id == item || id.rfind(item + ".", 0) == 0;
}
} // namespace

const char* levelName(Level level)
{
    switch (level)
    {
    case Level::Quick: return "quick";
    case Level::Standard: return "standard";
    case Level::Thorough: return "thorough";
    }
    throw std::logic_error("levelName: not a level");
}

Level parseLevel(const std::string& text)
{
    for (const Level l : {Level::Quick, Level::Standard, Level::Thorough})
        if (text == levelName(l))
            return l;
    throw UsageError("unknown --auto-qa-level '" + text + "' (quick, standard or thorough)");
}

std::vector<std::string> parseSelection(const std::string& text)
{
    std::vector<std::string> items;
    size_t start = 0;
    for (;;)
    {
        const size_t comma = text.find(',', start);
        items.push_back(text.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    if (items.size() > MAX_SELECTION_ITEMS)
        throw UsageError("--auto-qa-tests '" + text + "' has " + std::to_string(items.size()) + " items, at most " +
                         std::to_string(MAX_SELECTION_ITEMS) + " are allowed");
    for (const std::string& item : items)
    {
        bool ok = !item.empty() && item.size() <= MAX_ITEM_LENGTH;
        for (size_t i = 0; ok && i < item.size(); i++)
            ok = validItemChar(item[i], i == 0);
        if (!ok)
            throw UsageError("--auto-qa-tests item '" + item +
                             "' is not a test id, a dotted prefix or a glob (up to 80 lower-case letters, digits, "
                             "_ . - and *, not starting with . or -)");
    }
    return items;
}

int parseRepeat(const std::string& text)
{
    // Digits only: no sign, no spaces, and short enough not to overflow.
    const bool digits = !text.empty() && text.size() <= 4 &&
                        std::all_of(text.begin(), text.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
    const int n = digits ? std::stoi(text) : 0;
    if (n < 1 || n > MAX_REPEAT)
        throw UsageError("--auto-qa-repeat '" + text + "' is not a whole number from 1 to " + std::to_string(MAX_REPEAT));
    return n;
}

Plan makePlan(const Options& options)
{
    const Catalogue all = catalogue(options.level);
    const size_t n = all.tests.size();
    std::vector<bool> candidate(n), selected(n), included(n);
    for (size_t i = 0; i < n; i++)
        candidate[i] = all.levels[i] <= options.level;

    if (!options.selection)
        selected = candidate;
    else
        for (const std::string& item : *options.selection)
        {
            bool any = false;
            for (size_t i = 0; i < n; i++)
                if (candidate[i] && itemMatches(item, all.tests[i].id))
                    selected[i] = any = true;
            if (!any)
                throw UsageError("--auto-qa-tests item '" + item + "' matches no test at level " +
                                 levelName(options.level));
        }

    // The prerequisite closure: every candidate that establishes a condition an included
    // test needs is included too, until nothing more is added.
    std::map<std::string, std::vector<size_t>> providers;
    for (size_t i = 0; i < n; i++)
        if (candidate[i])
            for (const std::string& cond : all.tests[i].provides)
                providers[cond].push_back(i);
    included = selected;
    for (bool grew = true; grew;)
    {
        grew = false;
        for (size_t i = 0; i < n; i++)
        {
            if (!included[i])
                continue;
            for (const std::string& cond : all.tests[i].needs)
            {
                const auto it = providers.find(cond);
                if (it == providers.end())
                    throw std::logic_error(all.tests[i].id + " needs the condition " + cond +
                                           ", which no test at level " + levelName(options.level) + " establishes");
                for (const size_t p : it->second)
                    if (!included[p])
                        included[p] = grew = true;
            }
        }
    }

    Plan plan;
    plan.options = options;
    for (int k = 1; k <= options.repeat; k++)
        for (size_t i = 0; i < n; i++)
            if (included[i])
                plan.entries.push_back({all.tests[i], all.levels[i], k, !selected[i]});
    return plan;
}

void writePlanJson(JsonWriter& json, const Plan& plan)
{
    json.field("level", levelName(plan.options.level));
    json.key("selection");
    if (plan.options.selection)
    {
        json.beginArray();
        for (const std::string& item : *plan.options.selection)
            json.value(item);
        json.endArray();
    }
    else
        json.null();
    json.field("repeat", plan.options.repeat);
    json.key("planned").beginArray();
    for (const PlannedTest& p : plan.entries)
    {
        json.beginObject();
        json.field("id", p.test.id);
        json.field("group", p.test.group);
        json.field("title", p.test.title);
        json.field("level", levelName(p.level));
        json.field("iteration", p.iteration);
        json.field("prerequisite", p.prerequisite);
        json.endObject();
    }
    json.endArray();
}
} // namespace autoqa
