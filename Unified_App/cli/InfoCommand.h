#pragma once

#include "Command.h"

// `labrador-cli info`: report the attached board's state, firmware, USB
// transport and stored calibration without changing anything.
class InfoCommand : public Command
{
  public:
    std::string name() const override;
    std::string summary() const override;
    void printHelp(std::ostream& out, const std::string& program) const override;
    int run(const std::vector<std::string>& args) override;
};
