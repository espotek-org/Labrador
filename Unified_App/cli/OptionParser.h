#pragma once

#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

// Declarative `--name value` / `--name=value` / `--flag` parser. Commands register
// their options once; the same registration drives parsing and the help text, so the
// two cannot drift apart. Every problem is reported as a UsageError naming the option.
class OptionParser
{
  public:
    using ValueHandler = std::function<void(const std::string& value)>;
    using FlagHandler = std::function<void()>;

    void addOption(std::string name, std::string metavar, std::string description,
                   ValueHandler handler);
    void addFlag(std::string name, std::string description, FlagHandler handler);

    // Options only; a stray positional argument is an error.
    void parse(const std::vector<std::string>& args) const;

    // Aligned option list, ending with the -h/--help entry that main() handles.
    void printOptions(std::ostream& out) const;

    // Strict number parsing (rejects "", "3x", "nan"); `option` names the culprit.
    static double toDouble(const std::string& option, const std::string& text);

  private:
    struct Spec
    {
        std::string name;
        std::string metavar; // empty for flags
        std::string description;
        ValueHandler onValue;
        FlagHandler onFlag;
    };

    const Spec* find(const std::string& name) const;

    std::vector<Spec> m_specs;
};
