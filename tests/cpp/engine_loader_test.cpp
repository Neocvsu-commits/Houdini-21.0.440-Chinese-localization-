// Exercise the real pre-load resource check and Windows loading path. Including
// the loader in an EXE does not call its DllMain or start a background thread.
#include "../../source/loader.cpp"
#include <cassert>
#include <cstdio>

int wmain(int argc, wchar_t **argv) {
    assert(argc == 4);
    Identity host;
    host.hdk = 22000368; host.qtMajor = 6; host.qtMinor = 8; host.qtPatch = 3;
    host.houdiniMajor = 22; host.houdiniBuild = 368;
    assert(fileName(host) == L"HoudiniChineseEngine.dll");
    assert(matches(host, host));
    auto newer = host; newer.houdiniBuild = 369;
    assert(matches(host, newer));
    for (auto field : {&Identity::magic, &Identity::protocol, &Identity::machine,
                       &Identity::hdk, &Identity::qtMajor, &Identity::qtMinor,
                       &Identity::qtPatch, &Identity::houdiniMajor,
                       &Identity::houdiniMinor, &Identity::houdiniPatch}) {
        auto changed = host;
        ++(changed.*field);
        assert(!matches(host, changed));
    }
    auto stub = host; stub.houdiniPatch = 1;
    assert(!matches(host, stub));
    assert(matches(stub, stub));
    newer = stub; ++newer.houdiniBuild;
    assert(!matches(stub, newer));
    assert(fileName(stub) == L"HoudiniChineseEngine.dll");
    auto qt5 = host; qt5.qtMajor = 5; qt5.qtMinor = 15; qt5.qtPatch = 2;
    assert(matches(qt5, qt5) && !matches(qt5, host));

    Identity resource;
    assert(readIdentity(argv[1], resource));
    assert(matches(resource, host));
    assert(!GetModuleHandleW(argv[1])); // Resource inspection executes no code.
    auto wrongQt = host; ++wrongQt.qtPatch;
    assert(!loadEngine(argv[1], wrongQt));
    assert(g_status.status == Status::IdentityMismatch);
    assert(!GetModuleHandleW(argv[1]));
    assert(!loadEngine(argv[2], host)); // A DLL without an identity cannot run.
    assert(g_status.status == Status::IdentityMismatch);
    assert(!GetModuleHandleW(argv[2]));
    assert(!loadEngine(std::wstring(argv[1]) + L".missing", host));
    assert(g_status.status == Status::EngineMissing);
    HMODULE engine = loadEngine(argv[1], host);
    assert(engine);
    const auto attached = entry<BOOL (WINAPI *)()>(engine, "FixtureAttached");
    assert(attached && attached());
    FreeLibrary(engine);
    LoaderStatus output;
    assert(HoudiniChineseGetLoaderStatus(&output, sizeof(output)));
    assert(!HoudiniChineseGetLoaderStatus(&output, 1));
    assert(!HoudiniChineseRequestHookReport(L"unused.json"));
    // Probe a real installed Qt DLL, not a fixture with our assumed exports.
    Module qt;
    qt.handle = LoadLibraryExW(argv[3], nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    assert(qt.handle);
    const auto qtVersion = entry<const char *(__cdecl *)()>(qt.handle, "qVersion");
    assert(qtVersion && qtVersion());
    DWORD parts[4] = {};
    assert(fileVersion(argv[3], parts));
    assert(std::string(qtVersion()) == std::to_string(parts[0]) + "." +
        std::to_string(parts[1]) + "." + std::to_string(parts[2]));
    std::puts("engine_loader_test: passed (real DLL pre-load rejection and loading)");
}
