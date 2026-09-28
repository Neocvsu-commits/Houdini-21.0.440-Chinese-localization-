#pragma once

#include <Windows.h>
#include <Psapi.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace HoudiniChineseImports {

struct PatchResult {
    std::size_t matched = 0;
    std::size_t patched = 0;
    std::size_t alreadyPatched = 0;
    std::size_t failed = 0;
    bool scanned = false;
    DWORD error = 0;

    bool complete() const noexcept { return scanned && failed == 0; }
};

namespace detail {

inline void recordFailure(PatchResult &result, DWORD error) noexcept {
    ++result.failed;
    if (!result.error)
        result.error = error ? error : ERROR_GEN_FAILURE;
}

struct ModuleReference final {
    HMODULE value = nullptr;
    ~ModuleReference() {
        if (value)
            FreeLibrary(value);
    }
};

// An image can contain inaccessible padding. Cache the current readable region
// so walking a normal IAT does not require a system call for every pointer.
class ImageReader final {
public:
    ImageReader(HMODULE module, std::size_t size) noexcept
        : base_(reinterpret_cast<std::uintptr_t>(module)), size_(size) {}

    bool contains(std::size_t offset, std::size_t length) noexcept {
        if (offset > size_ || length > size_ - offset)
            return false;
        auto address = base_ + offset;
        const auto end = address + length;
        if (address < base_ || end < address)
            return false;
        while (address < end) {
            if (address < readableBegin_ || address >= readableEnd_) {
                MEMORY_BASIC_INFORMATION info{};
                if (VirtualQuery(reinterpret_cast<void *>(address), &info,
                                 sizeof(info)) != sizeof(info) ||
                    info.State != MEM_COMMIT ||
                    (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
                    return false;
                const DWORD access = info.Protect & 0xff;
                if (access != PAGE_READONLY && access != PAGE_READWRITE &&
                    access != PAGE_WRITECOPY && access != PAGE_EXECUTE_READ &&
                    access != PAGE_EXECUTE_READWRITE &&
                    access != PAGE_EXECUTE_WRITECOPY)
                    return false;
                readableBegin_ = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
                readableEnd_ = readableBegin_ + info.RegionSize;
                if (readableEnd_ <= address)
                    return false;
            }
            address = end < readableEnd_ ? end : readableEnd_;
        }
        return true;
    }

    // Copy metadata instead of dereferencing it after VirtualQuery: another
    // component can change a page's protection between the check and the read.
    bool copy(std::size_t offset, void *destination, std::size_t length) noexcept {
        if (!destination || !contains(offset, length))
            return false;
        // This runs once per import slot across many modules. Keep the read
        // in-process rather than adding a ReadProcessMemory syscall per slot.
        __try {
            std::memcpy(destination, reinterpret_cast<const void *>(base_ + offset), length);
            return true;
        } __except ((GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
                     GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ||
                     GetExceptionCode() == EXCEPTION_GUARD_PAGE ||
                     GetExceptionCode() == EXCEPTION_DATATYPE_MISALIGNMENT)
                        ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            return false;
        }
    }

private:
    std::uintptr_t base_;
    std::size_t size_;
    std::uintptr_t readableBegin_ = 0;
    std::uintptr_t readableEnd_ = 0;
};

struct PageProtection final {
    static BOOL change(void *address, SIZE_T size, DWORD protection,
                       DWORD *previous) noexcept {
        return VirtualProtect(address, size, protection, previous);
    }
};

// The small policy seam permits deterministic protection-failure tests; normal
// callers always use the Windows implementation through patchModule below.
template <typename Protection>
PatchResult patchModuleWithProtection(HMODULE module, void *original,
                                     void *replacement) {
    PatchResult result;
    if (!module || !original || !replacement || original == replacement) {
        recordFailure(result, ERROR_INVALID_PARAMETER);
        return result;
    }
    ModuleReference held;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            reinterpret_cast<LPCWSTR>(module), &held.value)) {
        recordFailure(result, GetLastError());
        return result;
    }
    // FROM_ADDRESS accepts an interior address; the patch API requires the
    // actual image base so a stale/incorrect handle cannot pass as a module.
    if (held.value != module) {
        recordFailure(result, ERROR_INVALID_HANDLE);
        return result;
    }
    MODULEINFO info{};
    if (!GetModuleInformation(GetCurrentProcess(), held.value, &info,
                              sizeof(info))) {
        recordFailure(result, GetLastError());
        return result;
    }
    if (info.lpBaseOfDll != module || info.SizeOfImage < sizeof(IMAGE_DOS_HEADER)) {
        recordFailure(result, ERROR_BAD_EXE_FORMAT);
        return result;
    }
    auto *base = reinterpret_cast<unsigned char *>(module);
    ImageReader reader(module, info.SizeOfImage);
    const auto malformed = [&result]() {
        recordFailure(result, ERROR_BAD_EXE_FORMAT);
        return result;
    };
    IMAGE_DOS_HEADER dos{};
    if (!reader.copy(0, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < 0)
        return malformed();
    IMAGE_NT_HEADERS nt{};
    if (!reader.copy(static_cast<std::size_t>(dos.e_lfanew), &nt, sizeof(nt)) ||
        nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER) ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC)
        return malformed();
    if (nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT) {
        result.scanned = true;
        return result;
    }
    const IMAGE_DATA_DIRECTORY directory =
        nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress && !directory.Size) {
        result.scanned = true;
        return result;
    }
    if (!directory.VirtualAddress || directory.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR) ||
        !reader.contains(directory.VirtualAddress, directory.Size))
        return malformed();
    const std::size_t descriptorCount = directory.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);
    for (std::size_t descriptorIndex = 0; descriptorIndex < descriptorCount;
         ++descriptorIndex) {
        IMAGE_IMPORT_DESCRIPTOR descriptor{};
        if (!reader.copy(directory.VirtualAddress + descriptorIndex * sizeof(descriptor),
                         &descriptor, sizeof(descriptor)))
            return malformed();
        if (!descriptor.Name) {
            result.scanned = true;
            return result;
        }
        if (!reader.contains(descriptor.Name, 1) || !descriptor.FirstThunk ||
            descriptor.FirstThunk % alignof(void *) != 0)
            return malformed();
        std::size_t offset = descriptor.FirstThunk;
        for (;;) {
            if (!reader.contains(offset, sizeof(IMAGE_THUNK_DATA)))
                return malformed();
            auto *thunk = reinterpret_cast<IMAGE_THUNK_DATA *>(base + offset);
            auto **slot = reinterpret_cast<void **>(&thunk->u1.Function);
            // Snapshot via a guarded read; a preceding VirtualQuery is not a
            // guarantee that a third-party component leaves this page readable.
            void *value = nullptr;
            if (!reader.copy(offset, &value, sizeof(value)))
                return malformed();
            if (!value)
                break;
            if (value == replacement) {
                ++result.matched;
                ++result.alreadyPatched;
            } else if (value == original) {
                ++result.matched;
                DWORD previous = 0;
                if (!Protection::change(slot, sizeof(void *), PAGE_READWRITE, &previous)) {
                    recordFailure(result, GetLastError());
                } else {
                    void *observed = InterlockedCompareExchangePointer(
                        reinterpret_cast<PVOID volatile *>(slot), replacement, original);
                    if (observed == original) {
                        ++result.patched;
                        if (!FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void *)))
                            recordFailure(result, GetLastError());
                    } else if (observed == replacement) {
                        ++result.alreadyPatched;
                    } else {
                        recordFailure(result, ERROR_RETRY);
                    }
                    DWORD ignored = 0;
                    if (!Protection::change(slot, sizeof(void *), previous, &ignored))
                        recordFailure(result, GetLastError());
                }
            }
            offset += sizeof(IMAGE_THUNK_DATA);
        }
    }
    // A truncated descriptor array is not a successfully scanned image.
    return malformed();
}

} // namespace detail

struct ModuleIdentity final {
    std::uintptr_t base = 0;
    std::uint32_t size = 0;
    std::uint32_t timestamp = 0;
};

// A complete module enumeration can contain an image whose headers were
// protected or damaged by a third-party component. Such an image is unknown,
// not an unload notification, and must not prevent other modules being scanned.
inline bool readModuleIdentity(HMODULE module, ModuleIdentity &identity) noexcept {
    identity = {};
    if (!module)
        return false;
    detail::ModuleReference held;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            reinterpret_cast<LPCWSTR>(module), &held.value) ||
        held.value != module)
        return false;
    MODULEINFO info{};
    if (!GetModuleInformation(GetCurrentProcess(), held.value, &info, sizeof(info)) ||
        info.lpBaseOfDll != module || info.SizeOfImage < sizeof(IMAGE_DOS_HEADER))
        return false;
    detail::ImageReader reader(module, info.SizeOfImage);
    IMAGE_DOS_HEADER dos{};
    if (!reader.copy(0, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < 0)
        return false;
    IMAGE_NT_HEADERS nt{};
    if (!reader.copy(static_cast<std::size_t>(dos.e_lfanew), &nt, sizeof(nt)) ||
        nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER) ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC)
        return false;
    identity = {reinterpret_cast<std::uintptr_t>(module), info.SizeOfImage,
                nt.FileHeader.TimeDateStamp};
    return true;
}

inline PatchResult patchModule(HMODULE module, void *original, void *replacement) {
    return detail::patchModuleWithProtection<detail::PageProtection>(
        module, original, replacement);
}

} // namespace HoudiniChineseImports
