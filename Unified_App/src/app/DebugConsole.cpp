#include "DebugConsole.h"

#include "imgui.h"
#include "librador.h" // librador_logger_set, librador_avr_debug, librador_is_connected; pulls logging.h (LOG_* levels)

#include <cstdio>
#include <cstring>

void DebugConsole::install()
{
    // Route librador's logging through this console. This replaces the
    // library's default std_logger, so logSink reproduces its
    // stdout/stderr behaviour itself — the console captures logs, it does
    // not silence them.
    librador_logger_set(this, &DebugConsole::logSink);
}

void DebugConsole::logSink(void* userdata, const int level, const char* format, va_list args)
{
    // Mirror to the terminal first, exactly like librador's default
    // std_logger (librador.cpp): errors to stderr, everything else to
    // stdout. vfprintf consumes a va_list, so copy it for the capture pass.
    va_list mirror_args;
    va_copy(mirror_args, args);
    vfprintf((level > LOG_ERROR) ? stdout : stderr, format, mirror_args);
    va_end(mirror_args);

    static_cast<DebugConsole*>(userdata)->push(level, format, args);
}

void DebugConsole::append(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    // append() is line-oriented, so the stdout mirror adds the newline that
    // librador format strings carry themselves.
    va_list mirror_args;
    va_copy(mirror_args, args);
    vfprintf(stdout, fmt, mirror_args);
    fputc('\n', stdout);
    va_end(mirror_args);

    push(LOG_DEBUG, fmt, args);
    va_end(args);
}

void DebugConsole::push(int level, const char* fmt, va_list args)
{
    // Fixed-size format buffer: debug messages are short, and anything
    // longer is truncated rather than heap-formatted (vsnprintf always
    // NUL-terminates). This also caps any single line well below MAX_BYTES.
    char buf[2048];
    vsnprintf(buf, sizeof(buf), fmt, args);

    std::lock_guard<std::mutex> lock(m_mutex);

    // librador messages carry their own newlines — one message can hold
    // several lines (the AVR debug header block) or none (some warnings) —
    // so split them here to keep the scrollback strictly one line per entry.
    const char* p = buf;
    while (*p != '\0')
    {
        const char* nl = strchr(p, '\n');
        const size_t len = nl ? (size_t)(nl - p) : strlen(p);
        m_lines.push_back({ level, std::string(p, len) });
        m_bytes += len;
        if (level == LOG_ERROR)
            m_errors++;
        else if (level == LOG_WARNING)
            m_warnings++;
        if (!nl)
            break;
        p = nl + 1;
    }

    // Bounded memory: drop the oldest lines once either cap is exceeded.
    while (m_lines.size() > MAX_LINES || m_bytes > MAX_BYTES)
    {
        const Line& oldest = m_lines.front();
        m_bytes -= oldest.text.size();
        if (oldest.level == LOG_ERROR)
            m_errors--;
        else if (oldest.level == LOG_WARNING)
            m_warnings--;
        m_lines.pop_front();
        m_dropped++;
    }
}

std::string DebugConsole::joinedText()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string out;
    out.reserve(m_bytes + m_lines.size());
    for (const Line& line : m_lines)
    {
        out += line.text;
        out += '\n';
    }
    return out;
}

void DebugConsole::render(bool* p_open, bool hardware_busy)
{
    ImGui::SetNextWindowSize(ImVec2(560.0f, 320.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Debug Console", p_open))
    {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Clear"))
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lines.clear();
        m_bytes = 0;
        m_dropped = 0;
        m_errors = 0;
        m_warnings = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy"))
        ImGui::SetClipboardText(joinedText().c_str());
    ImGui::SameLine();

    // AVR debug (Qt parity: the Desktop_Interface console button ->
    // isodriver avrDebug): asks the board for its unified_debug block; the
    // readout arrives through the log sink like any other librador log.
    // CAVEAT: the App gates hardware access while a firmware flash /
    // bootloader recovery runs on the worker thread — issuing a control
    // transfer then would race the flasher — so the App passes
    // hardware_busy and the button is disabled for the duration (and
    // whenever no device is connected).
    ImGui::BeginDisabled(hardware_busy || !librador_is_connected());
    if (ImGui::Button("AVR debug"))
        librador_avr_debug();
    ImGui::EndDisabled();

    // Line-count / level summary (counters are maintained on push/drop, so
    // this is just a read under the lock).
    size_t lines, errors, warnings, dropped;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        lines = m_lines.size();
        errors = m_errors;
        warnings = m_warnings;
        dropped = m_dropped;
    }
    ImGui::SameLine();
    if (dropped > 0)
        ImGui::Text("%zu lines (%zu err, %zu warn, %zu dropped)", lines, errors, warnings, dropped);
    else
        ImGui::Text("%zu lines (%zu err, %zu warn)", lines, errors, warnings);

    // Scrollback: dark console-style child, same look as
    // LogicDecodeControl::renderConsole.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(20, 20, 25, 255));
    if (ImGui::BeginChild("##debug_scrollback", ImVec2(0, 0), ImGuiChildFlags_Borders,
            ImGuiWindowFlags_HorizontalScrollbar))
    {
        // Tight spacing for a terminal feel; the clipper keeps a full
        // 2000-line backlog cheap. The lock is held while drawing — sink
        // calls are short, so contention is negligible.
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 1.0f));
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ImGuiListClipper clipper;
            clipper.Begin((int)m_lines.size());
            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
                {
                    const Line& line = m_lines[(size_t)i];
                    const bool colored = (line.level == LOG_ERROR || line.level == LOG_WARNING);
                    if (line.level == LOG_ERROR)
                        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 96, 96, 255));
                    else if (line.level == LOG_WARNING)
                        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 208, 96, 255));
                    ImGui::TextUnformatted(line.text.c_str(), line.text.c_str() + line.text.size());
                    if (colored)
                        ImGui::PopStyleColor();
                }
            }
            clipper.End();
        }
        ImGui::PopStyleVar();

        // Stick to the newest output unless the user has scrolled away
        // (same pattern as LogicDecodeControl::renderConsole).
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::End();
}
