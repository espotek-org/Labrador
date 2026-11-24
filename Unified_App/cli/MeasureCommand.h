#pragma once

#include "Command.h"

// `labrador-cli measure`: optionally drive the signal generators, then record a
// window of scope samples and report each channel's levels and frequency.
class MeasureCommand : public Command
{
  public:
    std::string name() const override;
    std::string summary() const override;
    void printHelp(std::ostream& out, const std::string& program) const override;
    int run(const std::vector<std::string>& args) override;
};
