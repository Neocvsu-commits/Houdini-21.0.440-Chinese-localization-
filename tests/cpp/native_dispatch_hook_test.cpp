#include "../../source/hooks/native_dispatch_hook.h"
#include <cassert>
#include <cstdio>

struct Label {
    virtual int paint(int value) { return value + 1; }
    virtual int measure(int value) { return value + 2; }
};
using Paint = int (*)(Label *, int);
Paint original = nullptr;
int calls = 0;
int translatedPaint(Label *self, int value) {
    ++calls;
    return original(self, value) + 100;
}
__declspec(noinline) int dispatch(Label *label, int value) {
    return label->paint(value);
}
DWORD protectionOf(void *address) {
    MEMORY_BASIC_INFORMATION info{};
    assert(VirtualQuery(address, &info, sizeof(info)) == sizeof(info));
    return info.Protect;
}

struct DeniedRestoreProtection {
    inline static unsigned calls = 0;
    static BOOL change(void *address, SIZE_T size, DWORD protection, DWORD *previous) {
        if (++calls == 2) {
            SetLastError(ERROR_ACCESS_DENIED);
            return FALSE;
        }
        return VirtualProtect(address, size, protection, previous);
    }
};

int main() {
    // No Detours/trampoline exists: exercise the actual compiled virtual call.
    Label label;
    auto **table = *reinterpret_cast<void ***>(&label);
    const auto target = reinterpret_cast<Paint>(table[0]);
    const DWORD protection = protectionOf(table);
    std::size_t slots = 0;
    assert(dispatch(&label, 5) == 6);
    DWORD restoreError = ERROR_GEN_FAILURE;
    assert(HoudiniChineseDispatch::patchVtable(
        table, target, translatedPaint, original, slots, &restoreError));
    assert(restoreError == ERROR_SUCCESS);
    assert(dispatch(&label, 5) == 106 && calls == 1);
    assert(original(&label, 5) == 6);
    assert(protectionOf(table) == protection);
    assert(HoudiniChineseDispatch::patchVtable(table, target, translatedPaint, original, slots));
    assert(slots == 1);
    assert(dispatch(&label, 5) == 106 && calls == 2);

    // Do not patch a neighboring class after the RTTI/data boundary.
    void *adjacent[] = {table[1], &slots, reinterpret_cast<void *>(target)};
    Paint missingOriginal = nullptr;
    assert(!HoudiniChineseDispatch::patchVtable(adjacent, target, translatedPaint, missingOriginal, slots));
    assert(missingOriginal == nullptr && slots == 1);
    assert(adjacent[2] == reinterpret_cast<void *>(target));
    assert(!HoudiniChineseDispatch::patchVtable(nullptr, target, translatedPaint, missingOriginal, slots));

    // Failed probing must not replace the original entry used by an already
    // installed hook or attempt an unaligned atomic pointer exchange.
    Paint differentOriginal = reinterpret_cast<Paint>(table[1]);
    const Paint savedOriginal = differentOriginal;
    assert(!HoudiniChineseDispatch::patchVtable(
        table, target, translatedPaint, differentOriginal, slots));
    assert(differentOriginal == savedOriginal);
    auto **misaligned = reinterpret_cast<void **>(
        reinterpret_cast<unsigned char *>(table) + 1);
    assert(!HoudiniChineseDispatch::patchVtable(
        misaligned, target, translatedPaint, missingOriginal, slots));
    assert(slots == 1);

    DWORD oldProtection = 0;
    assert(VirtualProtect(table, sizeof(void *), PAGE_READWRITE, &oldProtection));
    InterlockedExchangePointer(reinterpret_cast<PVOID volatile *>(table), reinterpret_cast<void *>(target));
    DWORD ignored = 0;
    assert(VirtualProtect(table, sizeof(void *), oldProtection, &ignored));
    assert(dispatch(&label, 5) == 6 && calls == 2);

    // A protection-restore error cannot undo a live CAS or clear the original
    // function that its replacement calls. Exercise actual virtual dispatch
    // while the controlled failure leaves the fixture's page writable.
    original = nullptr;
    restoreError = ERROR_SUCCESS;
    assert(HoudiniChineseDispatch::detail::patchVtableWithProtection<DeniedRestoreProtection>(
        table, target, translatedPaint, original, slots, &restoreError));
    assert(restoreError == ERROR_ACCESS_DENIED);
    assert(original == target && slots == 2);
    assert(protectionOf(table) == PAGE_READWRITE);
    assert(dispatch(&label, 5) == 106 && calls == 3);
    assert(original(&label, 5) == 6);
    // The test owns this deliberate fault and restores the real protection.
    assert(VirtualProtect(table, sizeof(void *), protection, &ignored));
    assert(HoudiniChineseDispatch::patchVtable(
        table, target, translatedPaint, original, slots, &restoreError));
    assert(restoreError == ERROR_SUCCESS && original == target && slots == 2);
    assert(VirtualProtect(table, sizeof(void *), PAGE_READWRITE, &oldProtection));
    InterlockedExchangePointer(reinterpret_cast<PVOID volatile *>(table), reinterpret_cast<void *>(target));
    assert(VirtualProtect(table, sizeof(void *), oldProtection, &ignored));
    assert(dispatch(&label, 5) == 6 && calls == 3);
    std::puts("Native dispatch fallback tests passed.");
}
