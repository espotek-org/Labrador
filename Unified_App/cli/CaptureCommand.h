#pragma once

#include "Command.h"

// `labrador-cli capture`: arm an edge trigger on a scope channel and save the
// full-rate samples around it as CSV.
class CaptureCommand : public Command
{
  public:
    std::string name() const override;
    std::string summary() const override;
    void printHelp(std::ostream& out, const std::string& program) const override;
    int run(const std::vector<std::string>& args) override;
};
