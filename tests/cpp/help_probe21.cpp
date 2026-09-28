// Test-only DLL for calling the documented H21 help API in an isolated GUI.
#define _USE_MATH_DEFINES
#include <cmath>
#include <UT/UT_HelpManager.h>
#include <UT/UT_String.h>
#include <windows.h>
extern "C" __declspec(dllexport) int WINAPI
readTooltip21(const char *url, wchar_t *buffer, int capacity) {
    auto *manager = UTgetHelpManager();
    if (!manager) return 0;
    UT_String text;
    if (!manager->getTooltip(url, text)) return 0;
    return MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, buffer, capacity);
}
