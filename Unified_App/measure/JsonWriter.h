#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

// Streaming JSON writer for the commands' machine-readable output: objects and
// arrays nest with begin/end calls, keys and values are written in call order,
// two-space indented. Misuse (a value without a key inside an object, an
// unbalanced end) is a programming error and throws std::logic_error.
class JsonWriter
{
  public:
    explicit JsonWriter(std::ostream& out) : m_out(out) {}

    JsonWriter& beginObject();
    JsonWriter& endObject();
    JsonWriter& beginArray();
    JsonWriter& endArray();

    // Inside an object, every value (or nested object/array) follows a key().
    JsonWriter& key(const std::string& name);

    JsonWriter& value(const std::string& text);
    JsonWriter& value(const char* text) { return value(std::string(text)); }
    JsonWriter& value(double number); // non-finite numbers are written as null
    JsonWriter& value(int number) { return value(static_cast<int64_t>(number)); }
    JsonWriter& value(int64_t number);
    JsonWriter& value(uint64_t number);
    JsonWriter& value(bool flag);
    JsonWriter& null();

    // Shorthand for key(name).value(v).
    template <typename T> JsonWriter& field(const std::string& name, const T& v)
    {
        key(name);
        return value(v);
    }

    // Writes the closing newline; throws std::logic_error if anything is still open.
    void finish();

  private:
    enum class Scope
    {
        Object,
        Array
    };
    struct Level
    {
        Scope scope;
        bool empty = true;
    };

    void beforeValue();
    void newline();
    void writeString(const std::string& text);

    std::ostream& m_out;
    std::vector<Level> m_stack;
    bool m_have_key = false;
    bool m_done = false;
};
