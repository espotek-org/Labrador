#include "CaptureCommand.h"
#include "Command.h"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
const std::string PROGRAM = "labrador-cli";

using CommandList = std::vector<std::unique_ptr<Command>>;

void printUsage(std::ostream& out, const CommandList& commands)
{
    out << PROGRAM << " - command-line tools for the EspoTek Labrador board\n\n"
        << "Usage:\n"
        << "  " << PROGRAM << " <command> [options]\n"
        << "  " << PROGRAM << " help [command]\n\n"
        << "Commands:\n";
    for (const auto& command : commands)
        out << "  " << std::left << std::setw(10) << command->name()
            << command->summary() << '\n';
    out << "\nRun '" << PROGRAM << " <command> --help' for a command's options.\n"
        << "Only one program can use the board at a time: close the Labrador app first.\n"
        << "Exit status: 0 success, " << EXIT_RUNTIME_ERROR
        << " the work failed (no board, timeout, ...), " << EXIT_USAGE_ERROR
        << " bad command line.\n";
}

Command* findCommand(const CommandList& commands, const std::string& name)
{
    for (const auto& command : commands)
        if (command->name() == name)
            return command.get();
    return nullptr;
}
} // namespace

int main(int argc, char** argv)
{
    CommandList commands;
    commands.push_back(std::make_unique<CaptureCommand>());

    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty())
    {
        printUsage(std::cerr, commands);
        return EXIT_USAGE_ERROR;
    }

    const std::string& first = args[0];
    if (first == "-h" || first == "--help")
    {
        printUsage(std::cout, commands);
        return EXIT_SUCCESS;
    }

    if (first == "help")
    {
        if (args.size() == 1)
        {
            printUsage(std::cout, commands);
            return EXIT_SUCCESS;
        }
        const Command* target = findCommand(commands, args[1]);
        if (!target)
        {
            std::cerr << PROGRAM << ": unknown command '" << args[1] << "'\n";
            return EXIT_USAGE_ERROR;
        }
        target->printHelp(std::cout, PROGRAM);
        return EXIT_SUCCESS;
    }

    Command* command = findCommand(commands, first);
    if (!command)
    {
        std::cerr << PROGRAM << ": unknown command '" << first << "'\n"
                  << "Try '" << PROGRAM << " --help'.\n";
        return EXIT_USAGE_ERROR;
    }

    const std::vector<std::string> commandArgs(args.begin() + 1, args.end());
    if (wantsHelp(commandArgs))
    {
        command->printHelp(std::cout, PROGRAM);
        return EXIT_SUCCESS;
    }

    try
    {
        return command->run(commandArgs);
    }
    catch (const UsageError& e)
    {
        std::cerr << PROGRAM << " " << first << ": " << e.what() << "\n"
                  << "Try '" << PROGRAM << " " << first << " --help'.\n";
        return EXIT_USAGE_ERROR;
    }
    catch (const std::exception& e)
    {
        std::cerr << PROGRAM << " " << first << ": error: " << e.what() << '\n';
        return EXIT_RUNTIME_ERROR;
    }
}
