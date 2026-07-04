#pragma once

#include <map>
#include <string>

// Minimal hand-rolled key=value store persisted as "settings.ini" in the
// per-user pref dir (getPrefPath(), platform/paths.h). No sections, no
// quoting: one "key=value" per line; '#' or ';' starts a comment line.
// Values are stored as strings; the typed getters parse on demand.
//
// Error philosophy (matches the rest of the app): a missing file is the
// normal first-launch case and ignored; a corrupt *line* is skipped with a
// stderr warning; a failed *write* is always reported on stderr — never
// silently swallowed (the store stays dirty so the next save retries).
class Settings
{
  public:
    // Read settings.ini from getPrefPath(). A missing file is expected on
    // first launch and leaves the store empty.
    void load();

    // Write to disk now if any set() changed a value (no-op when clean).
    // Reports failures on stderr.
    void save();

    // Throttled save for calling once per frame: writes at most once per
    // second while dirty, so changes persist promptly even on platforms that
    // never shut down cleanly (Android may never call App::ShutDown).
    void saveIfDirty(double now_seconds);

    // Getters return `fallback` when the key is absent — an absent key is the
    // expected state on first launch, or after an update adds new settings.
    // A present-but-unparsable value warns on stderr and returns `fallback`.
    std::string getString(const std::string& key, const std::string& fallback) const;
    bool getBool(const std::string& key, bool fallback) const;
    double getDouble(const std::string& key, double fallback) const;

    // Setters mark the store dirty only when the stored value actually
    // changes, so they are cheap to call every frame with live UI state.
    void set(const std::string& key, const std::string& value);
    void set(const std::string& key, const char* value) { set(key, std::string(value)); }
    void set(const std::string& key, bool value);
    void set(const std::string& key, double value);

  private:
    const std::string& filePath();

    std::map<std::string, std::string> m_values;
    std::string m_path; // resolved lazily (getPrefPath() creates the dir)
    bool m_dirty = false;
    double m_last_save_time = -1e9;
};
