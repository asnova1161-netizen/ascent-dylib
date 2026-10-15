// ===========================================================================
//  ascent-ios / memory.hpp
//  In-process memory access for the injected dylib.
//
//  wonderful reads another process (HD-Player.exe) through ReadProcessMemory
//  and translates "guest" VAs. We live *inside* Standoff 2, so we read our own
//  address space. All reads go through vm_read_overwrite() so a racing unmap
//  returns KERN_INVALID_ADDRESS instead of crashing the game.
// ===========================================================================
#pragma once

#include <mach/mach.h>
#include <mach/vm_map.h>
#include <mach/vm_region.h>
#include <cstdint>
#include <cstring>
#include <vector>

struct MemRegion {
    uintptr_t base;
    size_t    size;
};

// Enumerate writable, non-executable, read-mapped regions: Unity native heap,
// managed GC heap, transform pools. That is where playerpattern blocks live.
inline std::vector<MemRegion> enumWritableRegions() {
    std::vector<MemRegion> out;
    vm_address_t address = 0;

    while (true) {
        vm_size_t size = 0;
        vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t objectName = MACH_PORT_NULL;

        const kern_return_t kr = vm_region_64(mach_task_self(), &address, &size,
                                              VM_REGION_BASIC_INFO_64,
                                              (vm_region_info_t)&info, &count, &objectName);
        if (kr != KERN_SUCCESS || size == 0) break;

        const bool readable = (info.protection & VM_PROT_READ) != 0;
        const bool writable = (info.protection & VM_PROT_WRITE) != 0;
        const bool exec     = (info.protection & VM_PROT_EXECUTE) != 0;
        const bool shared   = (info.shared) != 0;

        if (readable && writable && !exec && !shared && size >= 0x1000)
            out.push_back({ (uintptr_t)address, (size_t)size });

        address += size;
        if (address == 0) break;  // overflow guard
    }
    return out;
}

// Crash-safe read of our own memory.
inline bool safeRead(uintptr_t addr, void* dst, size_t size) {
    if (addr == 0 || size == 0) return false;
    vm_size_t got = 0;
    const kern_return_t kr = vm_read_overwrite(mach_task_self(),
                                               (vm_address_t)addr, (vm_size_t)size,
                                               (vm_address_t)dst, &got);
    return kr == KERN_SUCCESS && got == size;
}

template <typename T>
inline bool safeReadT(uintptr_t addr, T& out) {
    return safeRead(addr, &out, sizeof(T));
}
