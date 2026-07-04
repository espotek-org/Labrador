#include "settings.h"

#include "platform/paths.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

// Save at most this often while dirty (see Settings::saveIfDirty).
static constexpr double SAVE_THROTTLE_S = 1.0;

const std::string& Settings::filePath()
{
    if (m_path.empty())
        m_path = getPrefPath() + "settings.ini";
    return m_path;
}

void Settings::load()
{
    std::ifstream file(filePath());
    if (!file.is_open())
        return; // first launch — no settings saved yet, that's fine

    std::string line;
    int lineno = 0;
    while (std::getline(file, line))
    {
        lineno++;
        if (!line.empty() && line.back() == '\r') // tolerate CRLF from hand edits
            line.pop_back();
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0)
        {
            std::cerr << "Settings: skipping corrupt line " << lineno << " of "
                      << m_path << ": \"" << line << "\"" << std::endl;
            continue;
        }
        m_values[line.substr(0, eq)] = line.substr(eq + 1);
    }
}

void Settings::save()
{
    if (!m_dirty)
        return;

    // Write to a temp file and rename over the original so a mid-write kill
    // (Android process death) can't leave a truncated settings.ini behind.
    const std::string tmp = filePath() + ".tmp";
    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            std::cerr << "Settings: FAILED to open " << tmp << " for writing"
                      << std::endl;
            return; // still dirty — the next save retries
        }
        for (const auto& kv : m_values)
            file << kv.first << '=' << kv.second << '\n';
        file.close();
        if (file.fail())
        {
            std::cerr << "Settings: FAILED to write " << tmp << std::endl;
            return;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, m_path, ec);
    if (ec)
    {
        std::cerr << "Settings: FAILED to rename " << tmp << " to " << m_path << ": "
                  << ec.message() << std::endl;
        return;
    }
    m_dirty = false;
}

void Settings::saveIfDirty(double now_seconds)
{
    if (!m_dirty)
        return;
    if (now_seconds - m_last_save_time < SAVE_THROTTLE_S)
        return;
    m_last_save_time = now_seconds;
    save();
}

std::string Settings::getString(const std::string& key, const std::string& fallback) const
{
    const auto it = m_values.find(key);
    return it != m_values.end() ? it->second : fallback;
}

bool Settings::getBool(const std::string& key, bool fallback) const
{
    const auto it = m_values.find(key);
    if (it == m_values.end())
        return fallback;
    if (it->second == "1" || it->second == "true")
        return true;
    if (it->second == "0" || it->second == "false")
        return false;
    std::cerr << "Settings: ignoring non-boolean value for \"" << key << "\": \""
              << it->second << "\"" << std::endl;
    return fallback;
}

double Settings::getDouble(const std::string& key, double fallback) const
{
    const auto it = m_values.find(key);
    if (it == m_values.end())
        return fallback;
    char* end = nullptr;
    const double value = std::strtod(it->second.c_str(), &end);
    if (end == it->second.c_str() || *end != '\0')
    {
        std::cerr << "Settings: ignoring non-numeric value for \"" << key << "\": \""
                  << it->second << "\"" << std::endl;
        return fallback;
    }
    return value;
}

void Settings::set(const std::string& key, const std::string& value)
{
    const auto it = m_values.find(key);
    if (it != m_values.end() && it->second == value)
        return;
    m_values[key] = value;
    m_dirty = true;
}

void Settings::set(const std::string& key, bool value)
{
    set(key, std::string(value ? "1" : "0"));
}

void Settings::set(const std::string& key, double value)
{
    char buf[32];
    snprintf(buf, sizeof buf, "%g", value);
    set(key, std::string(buf));
}
