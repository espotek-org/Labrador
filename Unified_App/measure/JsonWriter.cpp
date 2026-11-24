#include "JsonWriter.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>

void JsonWriter::newline()
{
    m_out << '\n';
    for (size_t i = 0; i < m_stack.size(); i++)
        m_out << "  ";
}

void JsonWriter::beforeValue()
{
    if (m_done)
        throw std::logic_error("JsonWriter: value after the document ended");
    if (m_stack.empty())
        return; // the top-level value
    Level& level = m_stack.back();
    if (level.scope == Scope::Object)
    {
        if (!m_have_key)
            throw std::logic_error("JsonWriter: object value without a key");
        m_have_key = false;
        return; // key() already wrote the separator and indentation
    }
    if (!level.empty)
        m_out << ',';
    level.empty = false;
    newline();
}

JsonWriter& JsonWriter::key(const std::string& name)
{
    if (m_stack.empty() || m_stack.back().scope != Scope::Object || m_have_key)
        throw std::logic_error("JsonWriter: key '" + name + "' outside an object");
    Level& level = m_stack.back();
    if (!level.empty)
        m_out << ',';
    level.empty = false;
    newline();
    writeString(name);
    m_out << ": ";
    m_have_key = true;
    return *this;
}

JsonWriter& JsonWriter::beginObject()
{
    beforeValue();
    m_out << '{';
    m_stack.push_back({Scope::Object});
    return *this;
}

JsonWriter& JsonWriter::endObject()
{
    if (m_stack.empty() || m_stack.back().scope != Scope::Object || m_have_key)
        throw std::logic_error("JsonWriter: unbalanced endObject");
    const bool empty = m_stack.back().empty;
    m_stack.pop_back();
    if (!empty)
        newline();
    m_out << '}';
    if (m_stack.empty())
        m_done = true;
    return *this;
}

JsonWriter& JsonWriter::beginArray()
{
    beforeValue();
    m_out << '[';
    m_stack.push_back({Scope::Array});
    return *this;
}

JsonWriter& JsonWriter::endArray()
{
    if (m_stack.empty() || m_stack.back().scope != Scope::Array)
        throw std::logic_error("JsonWriter: unbalanced endArray");
    const bool empty = m_stack.back().empty;
    m_stack.pop_back();
    if (!empty)
        newline();
    m_out << ']';
    if (m_stack.empty())
        m_done = true;
    return *this;
}

void JsonWriter::writeString(const std::string& text)
{
    m_out << '"';
    for (const char c : text)
    {
        switch (c)
        {
        case '"': m_out << "\\\""; break;
        case '\\': m_out << "\\\\"; break;
        case '\n': m_out << "\\n"; break;
        case '\r': m_out << "\\r"; break;
        case '\t': m_out << "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                char escaped[8];
                std::snprintf(escaped, sizeof escaped, "\\u%04x", c);
                m_out << escaped;
            }
            else
                m_out << c;
        }
    }
    m_out << '"';
}

JsonWriter& JsonWriter::value(const std::string& text)
{
    beforeValue();
    writeString(text);
    if (m_stack.empty())
        m_done = true;
    return *this;
}

JsonWriter& JsonWriter::value(double number)
{
    beforeValue();
    if (std::isfinite(number))
    {
        char text[32];
        std::snprintf(text, sizeof text, "%.10g", number);
        m_out << text;
    }
    else
        m_out << "null";
    if (m_stack.empty())
        m_done = true;
    return *this;
}

JsonWriter& JsonWriter::value(int64_t number)
{
    beforeValue();
    m_out << number;
    if (m_stack.empty())
        m_done = true;
    return *this;
}

JsonWriter& JsonWriter::value(uint64_t number)
{
    beforeValue();
    m_out << number;
    if (m_stack.empty())
        m_done = true;
    return *this;
}

JsonWriter& JsonWriter::value(bool flag)
{
    beforeValue();
    m_out << (flag ? "true" : "false");
    if (m_stack.empty())
        m_done = true;
    return *this;
}

JsonWriter& JsonWriter::null()
{
    beforeValue();
    m_out << "null";
    if (m_stack.empty())
        m_done = true;
    return *this;
}

void JsonWriter::finish()
{
    if (!m_done || !m_stack.empty())
        throw std::logic_error("JsonWriter: document not complete");
    m_out << '\n';
    m_out.flush();
}
