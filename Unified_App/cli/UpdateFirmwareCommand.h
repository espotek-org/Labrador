#pragma once

#include "Command.h"

// `labrador-cli update-firmware`: bring the board to the firmware this build of
// librador streams from (reflashing from the bundled hex, or one given with --hex).
class UpdateFirmwareCommand : public Command
{
  public:
    std::string name() const override;
    std::string summary() const override;
    void printHelp(std::ostream& out, const std::string& program) const override;
    int run(const std::vector<std::string>& args) override;
};
