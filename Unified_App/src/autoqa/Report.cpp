#include "autoqa/Report.h"

#include "JsonWriter.h"

#include <SDL3/SDL.h>

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

std::string TestContext::path(const std::string& subdir, const std::string& suffix) const
{
    const std::string dir = m_out_dir + "/" + subdir;
    if (!SDL_CreateDirectory(dir.c_str()))
        throw std::runtime_error("cannot create " + dir + ": " + SDL_GetError());
    std::string slug = m_result.id;
    for (char& c : slug)
        if (c == '.')
            c = '_';
    return subdir + "/" + slug + "_" + suffix;
}

void Runner::setCondition(const std::string& name, bool ok, const std::string& reason)
{
    m_conditions[name] = {ok, reason};
}

bool Runner::preconditionsMet(TestResult& result, const std::vector<std::string>& needs)
{
    for (const std::string& need : needs)
    {
        const Condition& c = m_conditions.at(need); // every condition is declared up front
        if (!c.ok)
        {
            result.status = m_unmet_fails.at(need) ? "fail" : "skip";
            result.message = c.reason;
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

void Runner::run(const std::string& id, const std::string& group, const std::string& title,
                 const std::vector<std::string>& needs, const Fn& fn)
{
    TestResult result;
    result.id = id;
    result.group = group;
    result.title = title;
    if (!preconditionsMet(result, needs))
    {
        record(result, nullptr);
        return;
    }
    TestContext ctx(result, m_out_dir);
    const double t0 = nowSeconds();
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
    result.duration_s = nowSeconds() - t0;
    record(result, &ctx);
}

TestContext* Runner::begin(const std::string& id, const std::string& group, const std::string& title,
                           const std::vector<std::string>& needs)
{
    if (m_open)
        throw std::logic_error("Runner::begin while " + m_open->id + " is still open");
    TestResult result;
    result.id = id;
    result.group = group;
    result.title = title;
    if (!preconditionsMet(result, needs))
    {
        record(result, nullptr);
        return nullptr;
    }
    m_open = result;
    m_open_ctx.emplace(*m_open, m_out_dir);
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
