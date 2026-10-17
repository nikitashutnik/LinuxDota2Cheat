#pragma once

#include <cstdint>
#include <cstddef>
#include <elf.h> // you will need this to check flags for PF_R/PF_W/PF_X

namespace Memory
{
    unsigned int GetProtectionFlags( uintptr_t address );
    const char *GetModuleName( uintptr_t address );
    // Copies n bytes from src without faulting on unmapped memory
    bool SafeRead( const void* src, void* dst, size_t n );
}