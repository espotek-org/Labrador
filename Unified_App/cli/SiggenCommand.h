#pragma once

#include "Command.h"

// `labrador-cli siggen`: set the two signal generators (and the PSU rail that
// powers their output stage).
class SiggenCommand : public Command
{
  public:
    std::string name() const override;
    std::string summary() const override;
    void printHelp(std::ostream& out, const std::string& program) const override;
    int run(const std::vector<std::string>& args) override;
};
