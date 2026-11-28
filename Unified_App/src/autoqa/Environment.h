#pragma once

// What kind of machine --auto-qa is running on. A hypervisor services a passed-through USB
// device in software, and can leave it unserviced for a fraction of a second while the host
// is busy; real hardware polls on the controller's own schedule. The stream soaks judge the
// two differently (HwTests.cpp, transportSoak), and the report says which rule applied.

#include <string>

namespace autoqa
{
struct Machine
{
    bool virtual_machine = false;
    // The hypervisor's CPUID vendor string ("VMwareVMware", "KVMKVMKVM", "Microsoft Hv",
    // ...) where the processor has one; empty on real hardware and where it can't be read.
    std::string hypervisor;
};

// x86 and x86-64: the hypervisor-present bit of CPUID leaf 1, and leaf 0x40000000's vendor
// string. macOS on any processor: kern.hv_vmm_present. Elsewhere (ARM Linux) nothing says,
// and the machine counts as real hardware, the stricter rule.
Machine detectMachine();
} // namespace autoqa
