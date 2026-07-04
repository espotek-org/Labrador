#pragma once

#include <cstdarg>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>

// In-app debug console — Qt-app parity (Desktop_Interface's
// actionShow_Debug_Console + q_debugstream.h). Where the Qt app hijacked
// std::cout with a streambuf, this registers as librador's log sink
// (librador_logger_set), so it captures everything the library logs —
// including the AVR debug readout, which usbcallhandler emits through
// LIBRADOR_LOG — while still mirroring every message to stdout/stderr
// exactly like librador's default std_logger, so terminal logging keeps
// working.
//
// Threading: librador logs arrive from the iso polling thread and the
// firmware-flash worker as well as the main thread, so the scrollback is
// mutex-guarded. render() and append() run on the main (UI) thread.
//
// Note: on Android LIBRADOR_LOG bypasses the sink entirely
// (__android_log_print, see librador/logging_internal.h), so there the
// console only shows app-side append() messages.
class DebugConsole
{
  public:
    // Register this console as librador's log sink. Call once at startup,
    // before the polling thread can log; the console must outlive librador
    // logging (in practice: make it a member of App). The default terminal
    // output is preserved — logSink mirrors std_logger itself.
    void install();

    // App-side printf-style message. Line-oriented: the message is one
    // scrollback line and is mirrored to stdout with a trailing newline
    // (librador messages, by contrast, embed their own '\n').
    void append(const char* fmt, ...);

    // Draw the floating console window. `hardware_busy` must be true while
    // the App has gated hardware access (firmware flash / bootloader
    // recovery running on the worker thread): the AVR-debug button issues a
    // USB control transfer and must not race the flasher, so it is disabled
    // for the duration.
    void render(bool* p_open, bool hardware_busy);

  private:
    // Matches librador_logger_p; `userdata` is the DebugConsole instance.
    static void logSink(void* userdata, const int level, const char* format, va_list args);

    // Format, split on '\n', append to the scrollback, enforce the caps.
    // Safe to call from any thread; consumes `args`.
    void push(int level, const char* fmt, va_list args);

    // Whole scrollback joined with '\n' (for the Copy button).
    std::string joinedText();

    // Scrollback bounds: whichever cap is hit first drops the oldest lines.
    static constexpr size_t MAX_LINES = 2000;
    static constexpr size_t MAX_BYTES = 256 * 1024;

    struct Line
    {
        int level; // librador logging.h level (LOG_ERROR / LOG_WARNING / LOG_DEBUG)
        std::string text;
    };

    // Everything below is guarded by m_mutex (logs arrive off-thread).
    std::mutex m_mutex;
    std::deque<Line> m_lines;
    size_t m_bytes = 0;    // sum of m_lines text sizes
    size_t m_dropped = 0;  // lines discarded to stay within the caps
    size_t m_errors = 0;   // LOG_ERROR lines currently held
    size_t m_warnings = 0; // LOG_WARNING lines currently held
};
