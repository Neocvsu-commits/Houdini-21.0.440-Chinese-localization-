#include "../../source/hooks/import_hook.h"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

using Function = int (*)(int);
Function original = nullptr;
std::atomic<int> activeCalls{0};
int replacementCalls = 0;

int translated(int value) {
    activeCalls.fetch_add(1);
    ++replacementCalls;
    const int answer = original(value) + 1000;
    activeCalls.fetch_sub(1);
    return answer;
}

int competingHook(int value) {
    return value + 2000;
}

HMODULE loadFixture(const wchar_t *name) {
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, 32768);
    assert(length && length < 32768);
    const std::wstring executable(path, length);
    const auto separator = executable.find_last_of(L"\\/");
    assert(separator != std::wstring::npos);
    const auto fullPath = executable.substr(0, separator + 1) + name;
    HMODULE module = LoadLibraryExW(fullPath.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    assert(module);
    return module;
}

Function resolve(HMODULE module, const char *name) {
    const FARPROC address = GetProcAddress(module, name);
    assert(address);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(address), "Function pointer size mismatch");
    std::memcpy(&function, &address, sizeof(function));
    return function;
}

DWORD protectionOf(void *address) {
    MEMORY_BASIC_INFORMATION info{};
    assert(VirtualQuery(address, &info, sizeof(info)) == sizeof(info));
    return info.Protect;
}

// Only the page-protection boundary is injectable. Parsing, address matching,
// actual CAS, module lifetime and imported calls use the production code.
struct ControlledProtection {
    enum class Mode { DenyWrite, DenyRestore, CompetingPatch };
    inline static Mode mode = Mode::DenyWrite;
    inline static unsigned calls = 0;
    inline static void *slot = nullptr;
    inline static DWORD initialProtection = 0;

    static void reset(Mode next) {
        mode = next;
        calls = 0;
        slot = nullptr;
        initialProtection = 0;
    }

    static BOOL change(void *address, SIZE_T size, DWORD protection,
                       DWORD *previous) {
        ++calls;
        if (calls == 1) {
            slot = address;
            initialProtection = protectionOf(address);
        }
        if ((mode == Mode::DenyWrite && calls == 1) ||
            (mode == Mode::DenyRestore && calls == 2)) {
            SetLastError(ERROR_ACCESS_DENIED);
            return FALSE;
        }
        const BOOL changed = VirtualProtect(address, size, protection, previous);
        if (changed && mode == Mode::CompetingPatch && calls == 1) {
            InterlockedExchangePointer(reinterpret_cast<PVOID volatile *>(address),
                                       reinterpret_cast<void *>(competingHook));
        }
        return changed;
    }
};

void assertFailure(const HoudiniChineseImports::PatchResult &result) {
    assert(!result.complete() && result.failed > 0 && result.error != 0);
}

template <typename Value>
void assertRejectedImageField(HMODULE module, Value *field, Value invalid,
                              void *source, void *replacement) {
    // Alter only a fixture's metadata and restore it synchronously. No DLL
    // calls, loads, or other workers run while this owned test image is invalid.
    DWORD previous = 0;
    assert(VirtualProtect(field, sizeof(Value), PAGE_READWRITE, &previous));
    const Value saved = *field;
    *field = invalid;
    const auto result = HoudiniChineseImports::patchModule(module, source, replacement);
    *field = saved;
    DWORD ignored = 0;
    assert(VirtualProtect(field, sizeof(Value), previous, &ignored));
    assertFailure(result);
    assert(!result.scanned && result.patched == 0 && result.error == ERROR_BAD_EXE_FORMAT);
}

template <typename Value>
void assertRejectedIdentityField(HMODULE module, Value *field, Value invalid) {
    DWORD previous = 0;
    assert(VirtualProtect(field, sizeof(Value), PAGE_READWRITE, &previous));
    const Value saved = *field;
    *field = invalid;
    HoudiniChineseImports::ModuleIdentity identity{1, 2, 3};
    const bool accepted = HoudiniChineseImports::readModuleIdentity(module, identity);
    *field = saved;
    DWORD ignored = 0;
    assert(VirtualProtect(field, sizeof(Value), previous, &ignored));
    assert(!accepted && identity.base == 0 && identity.size == 0 && identity.timestamp == 0);
}

} // namespace

int main() {
    using HoudiniChineseImports::patchModule;
    using HoudiniChineseImports::detail::patchModuleWithProtection;
    HMODULE target = loadFixture(L"import_hook_target.dll");
    HMODULE consumer = loadFixture(L"import_hook_consumer.dll");
    HMODULE noImports = loadFixture(L"import_hook_no_imports.dll");
    original = resolve(target, "import_hook_target");
    Function consume = resolve(consumer, "import_hook_consume");
    void *source = reinterpret_cast<void *>(original);
    void *replacement = reinterpret_cast<void *>(translated);

    assert(original(5) == 12 && consume(5) == 112);
    assertFailure(patchModule(nullptr, source, replacement));
    assertFailure(patchModule(consumer, nullptr, replacement));
    assertFailure(patchModule(consumer, source, source));
    const auto interior = reinterpret_cast<HMODULE>(
        reinterpret_cast<unsigned char *>(consumer) + 1);
    assertFailure(patchModule(interior, source, replacement));
    void *unowned = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(unowned);
    assertFailure(patchModule(static_cast<HMODULE>(unowned), source, replacement));
    assert(VirtualFree(unowned, 0, MEM_RELEASE));

    const auto emptyImage = patchModule(noImports, source, replacement);
    assert(emptyImage.complete() && emptyImage.matched == 0 && emptyImage.patched == 0);
    const auto absentImport = patchModule(target, source, replacement);
    assert(absentImport.complete() && absentImport.matched == 0 && absentImport.patched == 0);

    auto *emptyBase = reinterpret_cast<unsigned char *>(noImports);
    auto *emptyDos = reinterpret_cast<IMAGE_DOS_HEADER *>(emptyBase);
    auto *emptyNt = reinterpret_cast<IMAGE_NT_HEADERS *>(emptyBase + emptyDos->e_lfanew);

    HoudiniChineseImports::ModuleIdentity identity;
    assert(HoudiniChineseImports::readModuleIdentity(noImports, identity));
    assert(identity.base == reinterpret_cast<std::uintptr_t>(noImports));
    assert(identity.size == emptyNt->OptionalHeader.SizeOfImage);
    assert(identity.timestamp == emptyNt->FileHeader.TimeDateStamp);
    assert(!HoudiniChineseImports::readModuleIdentity(interior, identity));
    assert(identity.base == 0 && identity.size == 0 && identity.timestamp == 0);
    assert(!HoudiniChineseImports::readModuleIdentity(nullptr, identity));
    assertRejectedIdentityField(noImports, &emptyDos->e_lfanew, LONG{0x7fffffff});
    assertRejectedIdentityField(noImports, &emptyDos->e_magic, WORD{0});
    assertRejectedIdentityField(noImports, &emptyNt->Signature, DWORD{0});
    assertRejectedIdentityField(noImports, &emptyNt->OptionalHeader.Magic, WORD{0});

    // Reproduce a protection change after ImageReader cached readability.
    // Reads must fail safely even though contains() has a stale region cache.
    for (DWORD blocked : {DWORD{PAGE_NOACCESS}, DWORD{PAGE_READONLY | PAGE_GUARD}}) {
        HoudiniChineseImports::detail::ImageReader reader(noImports, emptyNt->OptionalHeader.SizeOfImage);
        unsigned char byte = 0;
        assert(reader.copy(0, &byte, 1));
        DWORD previous = 0;
        assert(VirtualProtect(emptyBase, sizeof(IMAGE_DOS_HEADER), blocked, &previous));
        const bool copied = reader.copy(0, &byte, 1);
        DWORD ignored = 0;
        assert(VirtualProtect(emptyBase, sizeof(IMAGE_DOS_HEADER), previous, &ignored));
        assert(!copied);
    }

    // A third-party image with protected headers cannot crash the GUI scanner.
    // Keep this owned, import-free fixture idle and restore each page before
    // assertions or any subsequent loader activity.
    for (DWORD blocked : {DWORD{PAGE_NOACCESS}, DWORD{PAGE_READONLY | PAGE_GUARD}}) {
        DWORD previous = 0;
        assert(VirtualProtect(emptyBase, sizeof(IMAGE_DOS_HEADER), blocked, &previous));
        const bool accepted = HoudiniChineseImports::readModuleIdentity(noImports, identity);
        DWORD ignored = 0;
        assert(VirtualProtect(emptyBase, sizeof(IMAGE_DOS_HEADER), previous, &ignored));
        assert(!accepted && identity.base == 0);
        assert(HoudiniChineseImports::readModuleIdentity(noImports, identity));
    }

    auto *emptyDirectory = &emptyNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    assertRejectedImageField(noImports, emptyDirectory,
        IMAGE_DATA_DIRECTORY{0xfffffff0u, 64u}, source, replacement);
    assertRejectedImageField(noImports, emptyDirectory,
        IMAGE_DATA_DIRECTORY{0u, 64u}, source, replacement);
    auto *consumerBase = reinterpret_cast<unsigned char *>(consumer);
    const auto *consumerDos = reinterpret_cast<const IMAGE_DOS_HEADER *>(consumerBase);
    const auto *consumerNt = reinterpret_cast<const IMAGE_NT_HEADERS *>(consumerBase + consumerDos->e_lfanew);
    auto *firstImport = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(consumerBase +
        consumerNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    assert(firstImport->Name && firstImport->FirstThunk);
    assertRejectedImageField(consumer, &firstImport->FirstThunk,
        firstImport->FirstThunk + DWORD{1}, source, replacement);

    ControlledProtection::reset(ControlledProtection::Mode::DenyWrite);
    const auto denied = patchModuleWithProtection<ControlledProtection>(consumer, source, replacement);
    assertFailure(denied);
    assert(denied.scanned && denied.matched == 1 && denied.patched == 0);
    assert(denied.error == ERROR_ACCESS_DENIED && consume(5) == 112);
    assert(protectionOf(ControlledProtection::slot) == ControlledProtection::initialProtection);

    // A successful CAS and failed protection restore is partial success, never
    // a clean install. Restore this deliberate test fault before continuing.
    ControlledProtection::reset(ControlledProtection::Mode::DenyRestore);
    const auto unrestored = patchModuleWithProtection<ControlledProtection>(consumer, source, replacement);
    assertFailure(unrestored);
    assert(unrestored.scanned && unrestored.patched == 1 && unrestored.failed == 1);
    assert(consume(5) == 1112 && replacementCalls == 1 && original(5) == 12);
    assert(activeCalls.load() == 0);
    DWORD ignored = 0;
    assert(VirtualProtect(ControlledProtection::slot, sizeof(void *),
                          ControlledProtection::initialProtection, &ignored));
    const auto firstRestore = patchModule(consumer, replacement, source);
    assert(firstRestore.complete() && firstRestore.patched == 1 && consume(5) == 112);

    // A third party changing a slot between lookup and CAS must be reported;
    // the production exchange must not overwrite that intervening hook.
    ControlledProtection::reset(ControlledProtection::Mode::CompetingPatch);
    const auto competed = patchModuleWithProtection<ControlledProtection>(consumer, source, replacement);
    assertFailure(competed);
    assert(competed.scanned && competed.patched == 0 && competed.error == ERROR_RETRY);
    assert(consume(5) == 2105 && replacementCalls == 1);
    assert(protectionOf(ControlledProtection::slot) == ControlledProtection::initialProtection);
    assert(activeCalls.load() == 0);
    const auto competitorRestore = patchModule(consumer,
        reinterpret_cast<void *>(competingHook), source);
    assert(competitorRestore.complete() && competitorRestore.patched == 1 && consume(5) == 112);

    const DWORD initialProtection = protectionOf(ControlledProtection::slot);
    const auto installed = patchModule(consumer, source, replacement);
    assert(installed.complete() && installed.matched == 1 && installed.patched == 1);
    assert(installed.alreadyPatched == 0 && installed.error == 0);
    // This is a real call compiled into a distinct DLL. No Detours or inline
    // trampoline exists, and the exported target remains directly callable.
    assert(consume(5) == 1112 && replacementCalls == 2 && original(5) == 12);
    assert(protectionOf(ControlledProtection::slot) == initialProtection);
    const auto repeated = patchModule(consumer, source, replacement);
    assert(repeated.complete() && repeated.matched == 1 && repeated.patched == 0);
    assert(repeated.alreadyPatched == 1 && consume(5) == 1112 && replacementCalls == 3);

    // A module loaded after the first installation starts unpatched. The same
    // pending import route can patch it without reinstalling existing modules.
    assert(!GetModuleHandleW(L"import_hook_late_consumer.dll"));
    HMODULE late = loadFixture(L"import_hook_late_consumer.dll");
    Function consumeLate = resolve(late, "import_hook_consume");
    assert(consumeLate(5) == 212 && replacementCalls == 3);
    const auto lateInstalled = patchModule(late, source, replacement);
    assert(lateInstalled.complete() && lateInstalled.patched == 1);
    assert(consumeLate(5) == 1212 && replacementCalls == 4);
    assert(consume(5) == 1112 && replacementCalls == 5 && original(5) == 12);

    // The fixture is single threaded; every imported call has returned before
    // removing the hooks and unloading code they could otherwise jump into.
    assert(activeCalls.load() == 0);
    const auto restoreConsumer = patchModule(consumer, replacement, source);
    const auto restoreLate = patchModule(late, replacement, source);
    assert(restoreConsumer.complete() && restoreConsumer.patched == 1);
    assert(restoreLate.complete() && restoreLate.patched == 1);
    assert(consume(5) == 112 && consumeLate(5) == 212 && replacementCalls == 5);
    assert(protectionOf(ControlledProtection::slot) == initialProtection);
    assert(FreeLibrary(late));
    assert(FreeLibrary(noImports));
    assert(FreeLibrary(consumer));
    assert(FreeLibrary(target));
    std::puts("Import hook DLL integration tests passed (fallback, late load, repeat, failures, restore).");
}
