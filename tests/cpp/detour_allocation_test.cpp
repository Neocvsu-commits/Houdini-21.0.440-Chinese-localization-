#define _CRT_STDIO_ARBITRARY_WIDE_SPECIFIERS 1
#include <windows.h>
#include <cassert>
#include <cstdio>
#include "../../source/detours/detours.cpp"

int main() {
    SYSTEM_INFO info{}; GetSystemInfo(&info);
    assert(DETOUR_REGION_SIZE >= info.dwPageSize);
    // A short, aligned free span must work from either search direction.
    // An unaligned tail of an existing reservation must never be accepted.
    auto base = static_cast<PBYTE>(VirtualAlloc(nullptr, info.dwAllocationGranularity * 3,
        MEM_RESERVE, PAGE_NOACCESS));
    assert(base);
    assert(VirtualFree(base, 0, MEM_RELEASE));
    auto lo = base + info.dwAllocationGranularity;
    auto hi = lo + info.dwPageSize * 2;
    auto region = detour_alloc_region_from_lo(lo, hi);
    assert(region == lo);
    assert(detour_alloc_region_from_lo(lo + info.dwPageSize, hi) == nullptr);
    assert(VirtualFree(region, 0, MEM_RELEASE));
    region = detour_alloc_region_from_hi(lo - 1, hi);
    assert(region == lo);
    assert(VirtualFree(region, 0, MEM_RELEASE));
    // Exercise many regions, release-list header lookup and reuse.
    assert(DetourTransactionBegin() == NO_ERROR);
    PDETOUR_TRAMPOLINE entries[160]{};
    for (auto &entry : entries) {
        entry = detour_alloc_trampoline(reinterpret_cast<PBYTE>(&main));
        assert(entry);
        const auto header = reinterpret_cast<PDETOUR_REGION>(
            reinterpret_cast<ULONG_PTR>(entry) & ~(ULONG_PTR)(DETOUR_REGION_SIZE - 1));
        assert(header->dwSignature == DETOUR_REGION_SIGNATURE);
        assert(reinterpret_cast<ULONG_PTR>(header) % info.dwAllocationGranularity == 0);
    }
    for (auto entry : entries) detour_free_trampoline(entry);
    auto reused = detour_alloc_trampoline(reinterpret_cast<PBYTE>(&main));
    assert(reused);
    detour_free_trampoline(reused);
    assert(DetourTransactionAbort() == NO_ERROR);
    std::puts("Trampoline allocation: aligned short spans, rejected reservation tails, multi-region release/reuse passed");
}
