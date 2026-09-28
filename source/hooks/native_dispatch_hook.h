#pragma once

#include <Windows.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace HoudiniChineseDispatch {

inline bool executable(const void *address) {
    MEMORY_BASIC_INFORMATION info{};
    if (!address || VirtualQuery(address, &info, sizeof(info)) != sizeof(info) ||
        info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
        return false;
    const DWORD access = info.Protect & 0xff;
    return access == PAGE_EXECUTE || access == PAGE_EXECUTE_READ ||
           access == PAGE_EXECUTE_READWRITE || access == PAGE_EXECUTE_WRITECOPY;
}

namespace detail {

struct PageProtection final {
    static BOOL change(void *address, SIZE_T size, DWORD protection,
                       DWORD *previous) noexcept {
        return VirtualProtect(address, size, protection, previous);
    }
};

// Exported MSVC vtables contain executable entries followed by padding or the
// next table's RTTI pointer. Never scan through that boundary into another class.
template <typename Protection, typename Function>
bool patchVtableWithProtection(void **table, Function target, Function replacement,
                               Function &original, std::size_t &patchedSlots,
                               DWORD *restoreError) {
    if (restoreError)
        *restoreError = ERROR_SUCCESS;
    if (!table || reinterpret_cast<std::uintptr_t>(table) % alignof(void *) != 0 ||
        (original && original != target) ||
        !executable(reinterpret_cast<void *>(target)) ||
        !executable(reinterpret_cast<void *>(replacement)))
        return false;
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(table, &info, sizeof(info)) != sizeof(info) ||
        info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
        return false;
    const auto end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
    const auto begin = reinterpret_cast<std::uintptr_t>(table);
    const auto count = (std::min)((end - begin) / sizeof(void *), std::size_t{256});
    for (std::size_t index = 0; index < count; ++index) {
        void *entry = table[index];
        if (entry == reinterpret_cast<void *>(replacement))
            return original == target;
        if (!executable(entry))
            break;
        if (entry != reinterpret_cast<void *>(target))
            continue;
        DWORD protection = 0;
        if (!Protection::change(&table[index], sizeof(void *), PAGE_READWRITE, &protection))
            return false;
        // Publish the callable original before another thread can see the hook.
        original = target;
        void *observed = InterlockedCompareExchangePointer(
            reinterpret_cast<PVOID volatile *>(&table[index]),
            reinterpret_cast<void *>(replacement), reinterpret_cast<void *>(target));
        DWORD ignored = 0;
        if (!Protection::change(&table[index], sizeof(void *), protection, &ignored)) {
            const DWORD error = GetLastError();
            if (restoreError)
                *restoreError = error ? error : ERROR_GEN_FAILURE;
        }
        if (observed == reinterpret_cast<void *>(target)) {
            ++patchedSlots;
            return true;
        }
        if (observed == reinterpret_cast<void *>(replacement))
            return true;
        original = nullptr;
        return false;
    }
    return false;
}

} // namespace detail

// The boolean describes dispatch installation. A failed protection restore is
// reported separately: an already published replacement still needs original.
template <typename Function>
bool patchVtable(void **table, Function target, Function replacement,
                 Function &original, std::size_t &patchedSlots,
                 DWORD *restoreError = nullptr) {
    return detail::patchVtableWithProtection<detail::PageProtection>(
        table, target, replacement, original, patchedSlots, restoreError);
}

} // namespace HoudiniChineseDispatch
