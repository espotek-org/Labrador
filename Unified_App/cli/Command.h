#pragma once

#include <algorithm>
#include <iosfwd>
#include <stdexcept>
#include <string>
#include <vector>

// Exit statuses shared by every command: EXIT_SUCCESS (0) plus these two.
constexpr int EXIT_RUNTIME_ERROR = 1; // the command line was fine but the work failed
constexpr int EXIT_USAGE_ERROR = 2;   // the command line itself was wrong

// Malformed command line. main() prints it with a pointer to --help.
class UsageError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

// A well-formed command that failed at runtime (no board, capture timed out, ...).
class CommandError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

// One subcommand of labrador-cli. To add one: implement this interface, then list
// it in main.cpp and cli/CMakeLists.txt.
class Command
{
  public:
    virtual ~Command() = default;

    // Word typed after the program name, e.g. "capture".
    virtual std::string name() const = 0;

    // One line for the top-level command list.
    virtual std::string summary() const = 0;

    virtual void printHelp(std::ostream& out, const std::string& program) const = 0;

    // `args` excludes the program and command names. Throws UsageError or
    // CommandError; returns the exit status otherwise.
    virtual int run(const std::vector<std::string>& args) = 0;
};

inline bool wantsHelp(const std::vector<std::string>& args)
{
    return std::any_of(args.begin(), args.end(), [](const std::string& arg)
                       { return arg == "-h" || arg == "--help"; });
}
