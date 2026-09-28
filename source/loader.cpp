// Qt/HDK-independent package entry. All inspection/loading runs outside DllMain.
#include <windows.h>
#include <atomic>
#include <cstring>
#include <string>
#include <vector>
#include "hooks/engine_compatibility.h"

namespace {
using namespace HoudiniChineseEngine;
HMODULE g_loader = nullptr;
std::atomic<HMODULE> g_engine{nullptr};
SRWLOCK g_statusLock = SRWLOCK_INIT;
LoaderStatus g_status;

void publish(Status status, DWORD error = 0, const Identity *host = nullptr) noexcept {
    AcquireSRWLockExclusive(&g_statusLock);
    g_status.status = status;
    g_status.error = error;
    if (host) g_status.host = *host;
    ReleaseSRWLockExclusive(&g_statusLock);
}

struct Module final {
    HMODULE handle = nullptr;
    ~Module() { if (handle) FreeLibrary(handle); }
};

template <typename Function> Function entry(HMODULE module, const char *name) noexcept {
    const FARPROC address = module ? GetProcAddress(module, name) : nullptr;
    Function result = nullptr;
    static_assert(sizeof(result) == sizeof(address));
    std::memcpy(&result, &address, sizeof(result));
    return result;
}

std::wstring modulePath(HMODULE module) {
    std::wstring path(32768, L'\0');
    const DWORD count = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (!count || count >= path.size()) return {};
    path.resize(count);
    return path;
}

bool fileVersion(const std::wstring &path, DWORD (&parts)[4]) {
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (!size || size > 1024 * 1024) return false;
    std::vector<BYTE> bytes(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, bytes.data())) return false;
    VS_FIXEDFILEINFO *info = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(bytes.data(), L"\\", reinterpret_cast<void **>(&info), &length) ||
        length < sizeof(*info) || !info || info->dwSignature != 0xfeef04bd) return false;
    parts[0] = HIWORD(info->dwFileVersionMS);
    parts[1] = LOWORD(info->dwFileVersionMS);
    parts[2] = HIWORD(info->dwFileVersionLS);
    parts[3] = LOWORD(info->dwFileVersionLS);
    return true;
}

// Read an embedded resource as data: this cannot execute an engine or load Qt.
bool readIdentity(const std::wstring &path, Identity &identity) {
    Module image;
    image.handle = LoadLibraryExW(path.c_str(), nullptr,
        LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!image.handle) return false;
    HRSRC resource = FindResourceW(image.handle, MAKEINTRESOURCEW(kIdentityResource), MAKEINTRESOURCEW(10));
    if (!resource || SizeofResource(image.handle, resource) != sizeof(identity)) return false;
    HGLOBAL loaded = LoadResource(image.handle, resource);
    if (!loaded) return false;
    const void *data = LockResource(loaded);
    if (!data) return false;
    std::memcpy(&identity, data, sizeof(identity));
    return true;
}

HMODULE loadEngine(const std::wstring &path, const Identity &host) {
    // Hold the file without write/delete sharing from validation through load.
    // Updating a running plugin is intentionally a next-process operation.
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        publish(Status::EngineMissing, GetLastError(), &host);
        return nullptr;
    }
    struct CloseFile { HANDLE value; ~CloseFile() { CloseHandle(value); } } held{file};
    Identity engine;
    if (!readIdentity(path, engine) || !matches(engine, host)) {
        publish(Status::IdentityMismatch, ERROR_REVISION_MISMATCH, &host);
        return nullptr;
    }
    // Restrict dependency search to the package, host executable and System32.
    HMODULE loaded = LoadLibraryExW(path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
        LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!loaded) publish(Status::LoadFailed, GetLastError(), &host);
    return loaded;
}

DWORD runLoader() {
    const std::wstring hostPath = modulePath(nullptr);
    const auto separator = hostPath.find_last_of(L"\\/");
    const std::wstring name = hostPath.substr(separator == std::wstring::npos ? 0 : separator + 1);
    if (_wcsicmp(name.c_str(), L"houdini.exe") &&
        _wcsicmp(name.c_str(), L"houdinifx.exe") &&
        _wcsicmp(name.c_str(), L"houdinicore.exe")) {
        publish(Status::UnsupportedHost);
        return 0;
    }
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(g_loader), &pinned)) {
        publish(Status::InternalError, GetLastError());
        return 1;
    }
    Module ut, core;
    unsigned qtMajor = 0;
    for (unsigned attempt = 0; attempt < 300; ++attempt) {
        if (!ut.handle) GetModuleHandleExW(0, L"libUT.dll", &ut.handle);
        if (!core.handle) {
            if (GetModuleHandleExW(0, L"Qt6Core.dll", &core.handle)) qtMajor = 6;
            else if (GetModuleHandleExW(0, L"Qt5Core.dll", &core.handle)) qtMajor = 5;
        }
        if (ut.handle && core.handle) break;
        Sleep(100);
    }
    if (!ut.handle || !core.handle) {
        publish(Status::HostTimeout);
        return 1;
    }
    if (GetModuleHandleW(qtMajor == 6 ? L"Qt5Core.dll" : L"Qt6Core.dll")) {
        publish(Status::MixedQtRuntime);
        return 1;
    }
    const auto getHdk = entry<unsigned (__cdecl *)()>(ut.handle, "?UTgetHDKAPIVersion@@YAIXZ");
    if (!getHdk) {
        publish(Status::MissingHdkApi);
        return 1;
    }
    Identity host;
    host.hdk = getHdk();
    DWORD version[4] = {};
    if (!fileVersion(hostPath, version)) {
        publish(Status::UnsupportedHost, GetLastError());
        return 1;
    }
    host.houdiniMajor = version[0]; host.houdiniMinor = version[1];
    // SideFX's Windows version resource is major, minor, stub, build, unlike
    // SYS_VERSION_FULL's major.minor.build notation (22,0,0,368 => 22.0.368).
    host.houdiniBuild = version[3]; host.houdiniPatch = version[2];
    if (!fileVersion(modulePath(core.handle), version) || version[0] != qtMajor) {
        publish(Status::MixedQtRuntime);
        return 1;
    }
    host.qtMajor = version[0]; host.qtMinor = version[1]; host.qtPatch = version[2];
    const auto getQt = entry<const char *(__cdecl *)()>(core.handle, "qVersion");
    const std::string expected = std::to_string(host.qtMajor) + "." +
        std::to_string(host.qtMinor) + "." + std::to_string(host.qtPatch);
    if (!getQt || !getQt() || expected != getQt()) {
        publish(Status::MixedQtRuntime, ERROR_REVISION_MISMATCH, &host);
        return 1;
    }
    const std::wstring hostBin = hostPath.substr(0, separator + 1);
    // Check every static Qt dependency before loading the engine. Late Qt Quick
    // libraries are inspected on disk in the host bin directory, not guessed.
    for (const wchar_t *part : {L"Core", L"Gui", L"Widgets", L"Qml", L"Quick"}) {
        const std::wstring dll = L"Qt" + std::to_wstring(qtMajor) + part + L".dll";
        Module module;
        GetModuleHandleExW(0, dll.c_str(), &module.handle);
        const std::wstring path = module.handle ? modulePath(module.handle) : hostBin + dll;
        if (_wcsicmp(path.c_str(), (hostBin + dll).c_str()) || !fileVersion(path, version) ||
            version[0] != host.qtMajor || version[1] != host.qtMinor || version[2] != host.qtPatch) {
            publish(Status::MixedQtRuntime, ERROR_REVISION_MISMATCH, &host);
            return 1;
        }
    }
    publish(Status::Starting, 0, &host);
    // Refuse duplicate installations in the same process, including copies of
    // this package under a different directory.
    const std::wstring engineName = fileName(host);
    if (GetModuleHandleW(engineName.c_str())) {
        publish(Status::AlreadyLoaded, ERROR_ALREADY_EXISTS, &host);
        return 1;
    }
    const std::wstring ownPath = modulePath(g_loader);
    const std::wstring enginePath = ownPath.substr(0, ownPath.find_last_of(L"\\/") + 1) + L"bin\\" + engineName;
    HMODULE engine = loadEngine(enginePath, host);
    if (!engine) return 1;
    const auto start = entry<BOOL (WINAPI *)()>(engine, "HoudiniChineseStartEngine");
    if (!start || !start()) {
        // Never unload a partially initialized engine that may own callbacks.
        publish(Status::StartFailed, ERROR_DLL_INIT_FAILED, &host);
        return 1;
    }
    g_engine.store(engine, std::memory_order_release);
    publish(Status::Loaded, 0, &host);
    return 0;
}

DWORD WINAPI worker(LPVOID) noexcept {
    try { return runLoader(); }
    catch (...) { publish(Status::InternalError); return 1; }
}
} // namespace

extern "C" __declspec(dllexport) BOOL WINAPI
HoudiniChineseGetLoaderStatus(HoudiniChineseEngine::LoaderStatus *output, DWORD size) noexcept {
    if (!output || size != sizeof(*output)) return FALSE;
    AcquireSRWLockShared(&g_statusLock);
    *output = g_status;
    ReleaseSRWLockShared(&g_statusLock);
    return TRUE;
}

extern "C" __declspec(dllexport) void WINAPI
HoudiniChineseEnableHookDiagnostics(BOOL enabled) noexcept {
    const auto function = entry<void (WINAPI *)(BOOL)>(g_engine.load(std::memory_order_acquire),
                                                     "HoudiniChineseEnableHookDiagnostics");
    if (function) function(enabled);
}

extern "C" __declspec(dllexport) BOOL WINAPI
HoudiniChineseRequestHookReport(const wchar_t *path) noexcept {
    const auto function = entry<BOOL (WINAPI *)(const wchar_t *)>(g_engine.load(std::memory_order_acquire),
                                                               "HoudiniChineseRequestHookReport");
    return function ? function(path) : FALSE;
}

extern "C" __declspec(dllexport) BOOL WINAPI
HoudiniChineseNotifyUIReady() noexcept {
    const auto function = entry<BOOL (WINAPI *)()>(g_engine.load(std::memory_order_acquire),
                                                  "HoudiniChineseNotifyUIReady");
    return function ? function() : FALSE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_loader = instance;
        DisableThreadLibraryCalls(instance);
        HANDLE thread = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread); // No waiting under the loader lock.
        else publish(Status::InternalError, GetLastError());
    }
    return TRUE;
}
