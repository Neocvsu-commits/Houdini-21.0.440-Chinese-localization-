#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>

namespace HoudiniChineseNativeRtti {

// MSVC x64 stores image-relative RTTI records immediately before each
// virtual-function table. Houdini's private UI types are not present in the
// public HDK, so this is the only generic way to ask whether an arbitrary
// UI_Feel/UI_Look instance derives from a known exported base class.
//
// These functions intentionally do not catch access violations. Production
// callers probe Houdini-owned objects inside an SEH guard; keeping the parser
// platform-neutral also lets the record walking be covered by a normal test.
struct CompleteObjectLocator64 final {
    std::uint32_t signature;
    std::uint32_t offset;
    std::uint32_t constructorDisplacementOffset;
    std::uint32_t typeDescriptorRva;
    std::uint32_t classDescriptorRva;
    std::uint32_t selfRva;
};

struct ClassHierarchyDescriptor64 final {
    std::uint32_t signature;
    std::uint32_t attributes;
    std::uint32_t baseClassCount;
    std::uint32_t baseClassArrayRva;
};

inline bool typeNameEquals(const char *actual,
                           std::string_view expected) noexcept {
    // The pointer comes from private MSVC RTTI. Never scan past the expected
    // name with strlen(): a stale but still-readable address could otherwise
    // walk an arbitrarily large region before the caller's SEH guard fires.
    return actual && !expected.empty() &&
           std::memcmp(actual, expected.data(), expected.size()) == 0 &&
           actual[expected.size()] == '\0';
}

inline const CompleteObjectLocator64 *locator(const void *object) noexcept {
    if (!object || sizeof(void *) != 8)
        return nullptr;
    const auto *vtable = *reinterpret_cast<void *const *const *>(object);
    return vtable
               ? reinterpret_cast<const CompleteObjectLocator64 *>(vtable[-1])
               : nullptr;
}

inline std::uintptr_t
imageBase(const CompleteObjectLocator64 *objectLocator) noexcept {
    if (!objectLocator || objectLocator->signature != 1 ||
        objectLocator->selfRva == 0) {
        return 0;
    }
    return reinterpret_cast<std::uintptr_t>(objectLocator) -
           objectLocator->selfRva;
}

inline const char *typeDescriptorName(std::uintptr_t moduleBase,
                                      std::uint32_t descriptorRva) noexcept {
    if (!moduleBase || !descriptorRva)
        return nullptr;
    // Two pointers precede TypeDescriptor::name on 64-bit MSVC.
    return reinterpret_cast<const char *>(moduleBase + descriptorRva +
                                          2 * sizeof(void *));
}

inline const char *dynamicTypeName(const void *object) noexcept {
    const CompleteObjectLocator64 *objectLocator = locator(object);
    const std::uintptr_t moduleBase = imageBase(objectLocator);
    return moduleBase ? typeDescriptorName(moduleBase,
                                           objectLocator->typeDescriptorRva)
                      : nullptr;
}

inline bool hasBase(const void *object, std::string_view decoratedTypeName,
                    std::uint32_t maxBaseClasses = 256) noexcept {
    const CompleteObjectLocator64 *objectLocator = locator(object);
    const std::uintptr_t moduleBase = imageBase(objectLocator);
    if (!moduleBase || objectLocator->classDescriptorRva == 0)
        return false;

    const auto *hierarchy =
        reinterpret_cast<const ClassHierarchyDescriptor64 *>(
            moduleBase + objectLocator->classDescriptorRva);
    if (hierarchy->baseClassCount == 0 ||
        hierarchy->baseClassCount > maxBaseClasses ||
        hierarchy->baseClassArrayRva == 0) {
        return false;
    }

    const auto *baseClassRvas = reinterpret_cast<const std::uint32_t *>(
        moduleBase + hierarchy->baseClassArrayRva);
    for (std::uint32_t index = 0; index < hierarchy->baseClassCount; ++index) {
        const std::uint32_t baseClassRva = baseClassRvas[index];
        if (!baseClassRva)
            continue;
        // The first field of _RTTIBaseClassDescriptor is the image-relative
        // TypeDescriptor pointer on MSVC x64.
        const std::uint32_t typeRva =
            *reinterpret_cast<const std::uint32_t *>(moduleBase + baseClassRva);
        if (typeNameEquals(typeDescriptorName(moduleBase, typeRva),
                           decoratedTypeName)) {
            return true;
        }
    }
    return false;
}

// Only non-virtual, unambiguous bases are accepted. Do not guess an offset for
// a future host version with a different hierarchy.
inline void *nonVirtualBase(void *object, std::string_view name) noexcept {
    const auto *col = locator(object);
    const auto base = imageBase(col);
    // Construction vtables require an additional vtordisp adjustment. These
    // probes are for live controls, so reject a partially constructed object
    // instead of manufacturing a base pointer with an incomplete adjustment.
    if (!base || !col->classDescriptorRva ||
        col->constructorDisplacementOffset != 0)
        return nullptr;
    const auto *hierarchy = reinterpret_cast<const ClassHierarchyDescriptor64 *>(
        base + col->classDescriptorRva);
    if (!hierarchy->baseClassArrayRva || hierarchy->baseClassCount > 256)
        return nullptr;
    const auto *array = reinterpret_cast<const std::uint32_t *>(
        base + hierarchy->baseClassArrayRva);
    void *result = nullptr;
    for (std::uint32_t i = 0; i < hierarchy->baseClassCount; ++i) {
        if (!array[i])
            continue;
        const auto *record = reinterpret_cast<const std::int32_t *>(base + array[i]);
        if (!typeNameEquals(typeDescriptorName(base, static_cast<std::uint32_t>(record[0])), name))
            continue;
        constexpr std::int32_t kAmbiguousBase = 0x2;
        if (record[3] != -1 || (record[5] & kAmbiguousBase) != 0 || result)
            return nullptr;
        result = static_cast<char *>(object) - col->offset + record[2];
    }
    return result;
}

} // namespace HoudiniChineseNativeRtti
