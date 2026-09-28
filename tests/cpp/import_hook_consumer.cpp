extern "C" __declspec(dllimport) int import_hook_target(int value);

#ifndef CONSUMER_BIAS
#define CONSUMER_BIAS 100
#endif

extern "C" __declspec(dllexport) int import_hook_consume(int value) {
    return import_hook_target(value) + CONSUMER_BIAS;
}
