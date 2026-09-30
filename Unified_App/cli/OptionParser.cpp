#include "OptionParser.h"

#include "Command.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <ostream>

void OptionParser::addOption(std::string name, std::string metavar,
                             std::string description, ValueHandler handler)
{
    m_specs.push_back({std::move(name), std::move(metavar), std::move(description),
                       std::move(handler), nullptr});
}

void OptionParser::addFlag(std::string name, std::string description,
                           FlagHandler handler)
{
    m_specs.push_back(
        {std::move(name), "", std::move(description), nullptr, std::move(handler)});
}

const OptionParser::Spec* OptionParser::find(const std::string& name) const
{
    const auto it = std::find_if(m_specs.begin(), m_specs.end(),
                                 [&](const Spec& spec) { return spec.name == name; });
    return it == m_specs.end() ? nullptr : &*it;
}

void OptionParser::parse(const std::vector<std::string>& args) const
{
    for (size_t i = 0; i < args.size(); i++)
    {
        const std::string& arg = args[i];
        if (arg.rfind("--", 0) != 0)
            throw UsageError("unexpected argument '" + arg + "'");

        const size_t eq = arg.find('=');
        const std::string name = arg.substr(0, eq);
        const Spec* spec = find(name);
        if (!spec)
            throw UsageError("unknown option '" + name + "'");

        if (spec->onFlag)
        {
            if (eq != std::string::npos)
                throw UsageError("option '" + name + "' does not take a value");
            spec->onFlag();
            continue;
        }

        if (eq != std::string::npos)
            spec->onValue(arg.substr(eq + 1));
        else if (i + 1 < args.size())
            spec->onValue(args[++i]);
        else
            throw UsageError("option '" + name + "' needs a value");
    }
}

void OptionParser::printOptions(std::ostream& out) const
{
    std::vector<std::string> heads;
    for (const Spec& spec : m_specs)
        heads.push_back(spec.metavar.empty() ? spec.name
                                             : spec.name + " " + spec.metavar);
    heads.push_back("-h, --help");

    size_t width = 0;
    for (const std::string& head : heads)
        width = std::max(width, head.size());

    for (size_t i = 0; i < m_specs.size(); i++)
        out << "  " << std::left << std::setw(static_cast<int>(width)) << heads[i]
            << "  " << m_specs[i].description << '\n';
    out << "  " << std::left << std::setw(static_cast<int>(width)) << heads.back()
        << "  Show this help\n";
}

double OptionParser::toDouble(const std::string& option, const std::string& text)
{
    char* end = nullptr;
    errno = 0;
    const double value = std::strtod(text.c_str(), &end);
    if (text.empty() || end != text.c_str() + text.size() || errno == ERANGE ||
        !std::isfinite(value))
        throw UsageError("option '" + option + "' expects a number, got '" + text + "'");
    return value;
}
