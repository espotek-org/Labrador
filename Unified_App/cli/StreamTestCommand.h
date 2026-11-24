#pragma once

#include "Command.h"

// `labrador-cli stream-test`: stream the scope for a while and check that the USB
// link delivers every frame intact and on time, and optionally that a looped-back
// periodic signal arrives without gaps or repeats.
class StreamTestCommand : public Command
{
  public:
    std::string name() const override;
    std::string summary() const override;
    void printHelp(std::ostream& out, const std::string& program) const override;
    int run(const std::vector<std::string>& args) override;
};
