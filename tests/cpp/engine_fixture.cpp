#include <windows.h>
static bool attached = false;
extern "C" __declspec(dllexport) BOOL WINAPI FixtureAttached() { return attached; }
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) attached = true;
    return TRUE;
}
