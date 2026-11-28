#include "autoqa/Report.h"

#include "JsonWriter.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <stdexcept>

namespace autoqa
{
double nowSeconds()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static std::string fmt(double v)
{
    char text[32];
    std::snprintf(text, sizeof text, "%.6g", v);
    return text;
}

bool Measurement::inLimits() const
{
    if (is_text)
        return expected_text.empty() || text == expected_text;
    if (!min && !max)
        return true;
    if (!std::isfinite(value))
        return false;
    return (!min || value >= *min) && (!max || value <= *max);
}

std::string Measurement::describe() const
{
    if (is_text)
        return name + " is " + text + ", expected " + expected_text;
    std::string limits;
    if (min)
        limits += ">= " + fmt(*min);
    if (max)
        limits += (limits.empty() ? "" : " and ") + std::string("<= ") + fmt(*max);
    return name + " = " + fmt(value) + " " + unit + " (limit " + limits +
           (expected ? ", expected " + fmt(*expected) : std::string()) + ")";
}

void TestContext::measure(const std::string& name, const std::string& unit, double value,
                          std::optional<double> expected, std::optional<double> min,
                          std::optional<double> max)
{
    Measurement m;
    m.name = name;
    m.unit = unit;
    m.value = value;
    m.expected = expected;
    m.min = min;
    m.max = max;
    m_result.measurements.push_back(m);
}

void TestContext::measureText(const std::string& name, const std::string& value,
                              const std::string& expected)
{
    Measurement m;
    m.name = name;
    m.is_text = true;
    m.text = value;
    m.expected_text = expected;
    m_result.measurements.push_back(m);
}

void TestContext::note(const std::string& text)
{
    m_notes.push_back(text);
}

void TestContext::artifact(const std::string& relative, const std::string& kind,
                           const std::string& caption)
{
    m_result.artifacts.push_back({relative, kind, caption});
}

std::string TestContext::file(const std::string& subdir, const std::string& name) const
{
    const auto create = [this](const std::string& relative) {
        const std::string dir = m_out_dir + "/" + relative;
        if (!SDL_CreateDirectory(dir.c_str()))
            throw std::runtime_error("cannot create " + dir + ": " + SDL_GetError());
    };
    create(subdir);
    if (m_iteration_dir.empty())
        return subdir + "/" + name;
    const std::string relative = subdir + "/" + m_iteration_dir;
    create(relative);
    return relative + "/" + name;
}

std::string TestContext::path(const std::string& subdir, const std::string& suffix) const
{
    std::string slug = m_result.id;
    for (char& c : slug)
        if (c == '.')
            c = '_';
    return file(subdir, slug + "_" + suffix);
}

void Runner::declareCondition(const std::string& name, const std::string& unmet_reason, bool unmet_fails)
{
    if (!m_declared.emplace(name, Condition{false, unmet_reason}).second)
        throw std::logic_error("condition " + name + " is declared twice");
    m_unmet_fails[name] = unmet_fails;
    m_conditions[name] = m_declared.at(name);
}

void Runner::startIteration(int iteration, int repeat)
{
    if (iteration < 1 || iteration > repeat)
        throw std::logic_error("Runner::startIteration: iteration " + std::to_string(iteration) + " of " +
                               std::to_string(repeat));
    m_iteration = iteration;
    m_iteration_dir = repeat > 1 ? "iter" + std::to_string(iteration) : std::string();
    m_conditions = m_declared;
}

void Runner::setCondition(const std::string& name, bool ok, const std::string& reason)
{
    if (!m_running)
        throw std::logic_error("Runner::setCondition(" + name + ") outside a running test");
    const std::vector<std::string>& provides = m_running->provides;
    if (std::find(provides.begin(), provides.end(), name) == provides.end())
        throw std::logic_error(m_running->id + " sets the condition " + name + ", which its TestSpec does not provide");
    if (!m_declared.count(name))
        throw std::logic_error("condition " + name + " was never declared");
    m_conditions[name] = {ok, reason};
}

TestResult Runner::newResult(const std::string& id, const std::string& group, const std::string& title) const
{
    if (m_iteration < 1)
        throw std::logic_error("a test ran before Runner::startIteration");
    TestResult result;
    result.id = id;
    result.iteration = m_iteration;
    result.group = group;
    result.title = title;
    return result;
}

bool Runner::preconditionsMet(TestResult& result, const std::vector<std::string>& needs)
{
    for (const std::string& need : needs)
    {
        const auto it = m_conditions.find(need);
        if (it == m_conditions.end())
            throw std::logic_error(result.id + " needs the condition " + need + ", which was never declared");
        if (!it->second.ok)
        {
            result.status = m_unmet_fails.at(need) ? "fail" : "skip";
            result.message = it->second.reason;
            return false;
        }
    }
    return true;
}

void Runner::record(TestResult& result, TestContext* ctx)
{
    if (result.status == "pass")
    {
        std::string bad;
        for (const Measurement& m : result.measurements)
            if (!m.inLimits())
                bad += (bad.empty() ? "" : "; ") + m.describe();
        if (!bad.empty())
        {
            result.status = "fail";
            result.message = bad;
        }
    }
    if (ctx)
        for (const std::string& n : ctx->notes())
            result.message += (result.message.empty() ? "" : "; ") + n;
    m_results.push_back(result);
    char line[160];
    std::snprintf(line, sizeof line, "%d/%d %s: %s (%.1f s)", static_cast<int>(m_results.size()),
                  m_planned, result.id.c_str(), result.status.c_str(), result.duration_s);
    std::string text = line;
    if (result.status != "pass" && !result.message.empty())
        text += " - " + result.message.substr(0, 300);
    m_progress(text);
    if (m_on_recorded)
        m_on_recorded();
}

void Runner::run(const TestSpec& spec, const Fn& fn)
{
    TestResult result = newResult(spec.id, spec.group, spec.title);
    if (!preconditionsMet(result, spec.needs))
    {
        record(result, nullptr);
        return;
    }
    TestContext ctx(result, m_out_dir, m_iteration_dir);
    const double t0 = nowSeconds();
    m_running = &spec;
    try
    {
        fn(ctx);
    }
    catch (const TestFailed& e)
    {
        result.status = "fail";
        result.message = e.message;
    }
    catch (const TestSkipped& e)
    {
        result.status = "skip";
        result.message = e.message;
    }
    catch (const std::exception& e)
    {
        // A crashing test is reported as an error, and the run goes on.
        result.status = "error";
        result.message = e.what();
    }
    m_running = nullptr;
    result.duration_s = nowSeconds() - t0;
    record(result, &ctx);
}

void Runner::recordFailure(const std::string& id, const std::string& group, const std::string& title,
                           const std::string& message)
{
    TestResult result = newResult(id, group, title);
    result.status = "fail";
    result.message = message;
    record(result, nullptr);
}

TestContext* Runner::begin(const TestSpec& spec)
{
    if (m_open)
        throw std::logic_error("Runner::begin while " + m_open->id + " is still open");
    if (!spec.provides.empty())
        throw std::logic_error("Runner::begin: " + spec.id + " provides conditions, which only run() can set");
    TestResult result = newResult(spec.id, spec.group, spec.title);
    if (!preconditionsMet(result, spec.needs))
    {
        record(result, nullptr);
        return nullptr;
    }
    m_open = result;
    m_open_ctx.emplace(*m_open, m_out_dir, m_iteration_dir);
    m_open_start = nowSeconds();
    return &*m_open_ctx;
}

void Runner::finish(const std::string& failure, const std::string& skip, const std::string& error)
{
    if (!m_open)
        throw std::logic_error("Runner::finish without an open test");
    TestResult& result = *m_open;
    if (!error.empty())
    {
        result.status = "error";
        result.message = error;
    }
    else if (!failure.empty())
    {
        result.status = "fail";
        result.message = failure;
    }
    else if (!skip.empty())
    {
        result.status = "skip";
        result.message = skip;
    }
    result.duration_s = nowSeconds() - m_open_start;
    record(result, &*m_open_ctx);
    m_open_ctx.reset();
    m_open.reset();
}

static void optionalNumber(JsonWriter& json, const char* key, const std::optional<double>& v)
{
    json.key(key);
    if (v)
        json.value(*v);
    else
        json.null();
}

void writeTestJson(JsonWriter& json, const TestResult& t)
{
    json.beginObject();
    json.field("id", t.id);
    json.field("iteration", t.iteration);
    json.field("group", t.group);
    json.field("title", t.title);
    json.field("status", t.status);
    json.field("duration_s", std::round(t.duration_s * 1000) / 1000);
    json.field("message", t.message);
    json.key("measurements").beginArray();
    for (const Measurement& m : t.measurements)
    {
        json.beginObject();
        json.field("name", m.name);
        json.field("unit", m.unit);
        json.key("value");
        if (m.is_text)
            json.value(m.text);
        else
            json.value(m.value);
        json.key("expected");
        if (m.is_text && !m.expected_text.empty())
            json.value(m.expected_text);
        else if (!m.is_text && m.expected)
            json.value(*m.expected);
        else
            json.null();
        optionalNumber(json, "min", m.min);
        optionalNumber(json, "max", m.max);
        json.endObject();
    }
    json.endArray();
    json.key("artifacts").beginArray();
    for (const Artifact& a : t.artifacts)
    {
        json.beginObject();
        json.field("path", a.path);
        json.field("kind", a.kind);
        json.field("caption", a.caption);
        json.endObject();
    }
    json.endArray();
    json.endObject();
}
} // namespace autoqa
