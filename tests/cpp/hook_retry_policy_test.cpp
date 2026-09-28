#include "../../source/hooks/hook_lifecycle.h"
#include <cassert>
#include <cstdio>

int main() {
    HoudiniChineseHooks::RetryPolicy policy;
    int unavailableTarget = 0;
    int lateTarget = 0;
    assert(!policy.begin(nullptr, 0));
    unsigned attempted = 0;
    for (unsigned now = 0; now <= 60000; now += 2000)
        if (policy.begin(&unavailableTarget, now))
            ++attempted;
    assert(attempted == 3);
    assert(!policy.begin(&unavailableTarget, 1000000));
    assert(!policy.beginLate(nullptr, 1000000));
    assert(!policy.beginLate(&lateTarget, 1000000));
    assert(policy.beginLate(&unavailableTarget, 1000000));
    assert(policy.attempts(&unavailableTarget) == 4);
    assert(!policy.beginLate(&unavailableTarget, 2000000));
    assert(!policy.begin(&unavailableTarget, 2000000));
    // A DLL appearing later has its own budget; another target's failure does
    // not disable unrelated hooks. Duplicate calls in one scan do no work.
    assert(policy.begin(&lateTarget, 59000));
    assert(!policy.begin(&lateTarget, 59000));
    assert(!policy.begin(&lateTarget, 68999));
    assert(policy.begin(&lateTarget, 69000));
    assert(policy.attempts(&lateTarget) == 2);
    assert(!policy.beginLate(&lateTarget, 78999));
    assert(policy.beginLate(&lateTarget, 79000));
    assert(!policy.beginLate(&lateTarget, 90000));
    std::puts("Hook retry policy tests passed.");
}
