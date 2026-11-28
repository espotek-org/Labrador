#include "autoqa/Environment.h"

#include <cstring>
#include <stdexcept>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#define AUTOQA_X86 1
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

namespace autoqa
{
#ifdef AUTOQA_X86
namespace
{
void cpuid(unsigned leaf, unsigned out[4])
{
#if defined(_MSC_VER)
    int regs[4];
    __cpuid(regs, static_cast<int>(leaf));
    for (int i = 0; i < 4; i++)
        out[i] = static_cast<unsigned>(regs[i]);
#else
    // Not __get_cpuid: it refuses leaves above the basic maximum, and the hypervisor's
    // leaves start at 0x40000000.
    __cpuid(leaf, out[0], out[1], out[2], out[3]);
#endif
}
} // namespace
#endif

Machine detectMachine()
{
    Machine m;
#ifdef AUTOQA_X86
    unsigned regs[4];
    cpuid(1, regs);
    if (regs[2] & (1u << 31)) // ECX bit 31: a hypervisor is present
    {
        m.virtual_machine = true;
        cpuid(0x40000000, regs);
        char vendor[13];
        std::memcpy(vendor, &regs[1], 4); // EBX, ECX, EDX
        std::memcpy(vendor + 4, &regs[2], 4);
        std::memcpy(vendor + 8, &regs[3], 4);
        vendor[12] = '\0';
        m.hypervisor = vendor;
    }
#endif
#if defined(__APPLE__)
    int present = 0;
    size_t size = sizeof present;
    if (sysctlbyname("kern.hv_vmm_present", &present, &size, nullptr, 0) != 0)
        throw std::runtime_error("sysctl kern.hv_vmm_present failed");
    if (present)
        m.virtual_machine = true;
#endif
    return m;
}
} // namespace autoqa
