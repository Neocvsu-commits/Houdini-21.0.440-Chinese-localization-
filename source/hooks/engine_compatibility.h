#pragma once

// The loader and engine share a plain-data ABI. No Qt/HDK types cross it.
#include <cstdint>
#include <string>

namespace HoudiniChineseEngine {
inline constexpr std::uint32_t kMagic = 0x48434331;
inline constexpr std::uint32_t kProtocol = 1;
inline constexpr unsigned kIdentityResource = 101;

struct Identity final {
    std::uint32_t magic = kMagic;
    std::uint32_t protocol = kProtocol;
    std::uint32_t machine = 0x8664; // Windows AMD64
    std::uint32_t hdk = 0;
    std::uint32_t qtMajor = 0;
    std::uint32_t qtMinor = 0;
    std::uint32_t qtPatch = 0;
    std::uint32_t houdiniMajor = 0;
    std::uint32_t houdiniMinor = 0;
    std::uint32_t houdiniBuild = 0; // Informational; not the HDK API identity.
    std::uint32_t houdiniPatch = 0; // HDK API identity excludes stub versions.
};
static_assert(sizeof(Identity) == 44);

inline bool matches(const Identity &engine, const Identity &host) noexcept {
    return engine.magic == kMagic && engine.protocol == kProtocol &&
        host.magic == kMagic && host.protocol == kProtocol &&
        engine.machine == 0x8664 && host.machine == 0x8664 &&
        engine.hdk != 0 && engine.hdk == host.hdk &&
        (engine.qtMajor == 5 || engine.qtMajor == 6) &&
        engine.qtMajor == host.qtMajor && engine.qtMinor == host.qtMinor &&
        engine.qtPatch == host.qtPatch &&
        engine.houdiniMajor == host.houdiniMajor &&
        engine.houdiniMinor == host.houdiniMinor &&
        // Stub builds require an exact host build: their ABI is not encoded
        // in HDK_API_VERSION. Normal daily builds can share an engine key.
        engine.houdiniPatch == host.houdiniPatch &&
        (!engine.houdiniPatch || engine.houdiniBuild == host.houdiniBuild);
}

inline std::wstring fileName(const Identity &) {
    // Each release package owns one engine; validate its embedded ABI resource.
    return L"HoudiniChineseEngine.dll";
}

enum class Status : std::uint32_t {
    Waiting, Starting, Loaded, UnsupportedHost, HostTimeout,
    MixedQtRuntime, MissingHdkApi, EngineMissing, IdentityMismatch,
    LoadFailed, StartFailed, InternalError, AlreadyLoaded
};

struct LoaderStatus final {
    std::uint32_t size = sizeof(LoaderStatus);
    std::uint32_t protocol = kProtocol;
    Status status = Status::Waiting;
    std::uint32_t error = 0;
    Identity host;
};
static_assert(sizeof(LoaderStatus) == 60);
} // namespace HoudiniChineseEngine
