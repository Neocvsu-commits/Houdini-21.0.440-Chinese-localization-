extern "C" __declspec(dllexport) int import_hook_target(int value) {
    return value + 7;
}
