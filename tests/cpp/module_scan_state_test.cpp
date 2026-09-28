#include "../../source/hooks/hook_lifecycle.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <vector>

using HoudiniChineseHooks::ModuleKey;
using HoudiniChineseHooks::ModuleScanState;

int main() {
    const ModuleKey initial{0x100000, 0x3000, 100};
    const ModuleKey late{0x200000, 0x4000, 200};
    ModuleScanState state;

    // Generation zero still requires a real successful scan.
    state.reconcile(std::vector<ModuleKey>{initial});
    assert(state.needsScan(initial, 0, 0));
    state.recordResult(initial, 0, true, 0);
    assert(!state.needsScan(initial, 0, 0));
    assert(!state.needsScan(initial, 0, 600000));
    assert(state.size() == 1);

    // The process can acquire a new importer long after its first minute.
    state.reconcile(std::vector<ModuleKey>{initial, late});
    assert(state.needsScan(late, 0, 600000));
    state.recordResult(late, 0, true, 600000);
    assert(!state.needsScan(late, 0, 600001));
    assert(state.size() == 2);

    // Registering another Hook schedules already loaded importers again.
    assert(state.needsScan(initial, 1, 600001));
    assert(state.needsScan(late, 1, 600001));
    state.recordResult(initial, 1, false, 600001);
    assert(!state.needsScan(initial, 1, 605000));
    assert(state.needsScan(initial, 1, 605001));
    state.recordResult(initial, 1, true, 605001);
    assert(!state.needsScan(initial, 1, 900000));
    assert(!state.needsScan(initial, 0, 900000));

    // A stale failure cannot erase the successful completion of a generation.
    state.recordResult(initial, 0, false, 605002);
    state.recordResult(initial, 1, false, 605002);
    assert(!state.needsScan(initial, 1, 605002));

    // A newer generation bypasses the previous generation's failure backoff.
    state.recordResult(initial, 2, false, 700000);
    assert(!state.needsScan(initial, 2, 700001));
    assert(state.needsScan(initial, 3, 700001));
    state.recordResult(initial, 3, false, 700001);
    assert(!state.needsScan(initial, 3, 705000));
    assert(state.needsScan(initial, 3, 705001));
    state.recordResult(initial, 3, true, 705001);
    assert(!state.needsScan(initial, 3, 1000000));

    // Repeated failures remain pending, with bounded rather than busy retries.
    ModuleScanState retrying;
    std::uint64_t now = 0;
    for (const std::uint64_t delay :
         {5000ULL, 10000ULL, 20000ULL, 40000ULL, 60000ULL, 60000ULL, 60000ULL}) {
        assert(retrying.needsScan(initial, 0, now));
        retrying.recordResult(initial, 0, false, now);
        assert(!retrying.needsScan(initial, 0, now));
        assert(!retrying.needsScan(initial, 0, now + delay - 1));
        assert(retrying.needsScan(initial, 0, now + delay));
        now += delay;
    }
    retrying.recordResult(initial, 0, true, now);
    assert(!retrying.needsScan(initial, 0, now + 60000));

    // Unload observed by a complete snapshot discards success/backoff alike.
    state.reconcile(std::vector<ModuleKey>{late});
    assert(state.size() == 1);
    state.reconcile(std::vector<ModuleKey>{initial, late});
    assert(state.needsScan(initial, 3, 1000001));
    assert(!state.needsScan(late, 0, 1000001));
    state.recordResult(initial, 3, false, 1000001);
    state.reconcile(std::vector<ModuleKey>{late});
    state.reconcile(std::vector<ModuleKey>{initial, late});
    assert(state.needsScan(initial, 3, 1000002));

    // Different size or timestamp at the same base constitutes a new identity.
    state.recordResult(initial, 3, true, 1000002);
    const ModuleKey newSize{initial.base, initial.size + 1, initial.timestamp};
    const ModuleKey newTimestamp{initial.base, initial.size, initial.timestamp + 1};
    assert(state.needsScan(newSize, 3, 1000002));
    assert(state.needsScan(newTimestamp, 3, 1000002));
    const std::vector<ModuleKey> replacement{newTimestamp, late};
    state.reconcile(replacement.begin(), replacement.end());
    assert(state.size() == 1);
    state.recordResult(newTimestamp, 3, true, 1000002);
    assert(!state.needsScan(newTimestamp, 3, 1000003));

    // Identical identity retained in snapshots cannot reveal an unseen reload.
    state.reconcile(replacement);
    assert(!state.needsScan(newTimestamp, 3, 1000004));
    state.reconcile(std::vector<ModuleKey>{});
    assert(state.size() == 0);
    assert(state.needsScan(newTimestamp, 3, 1000005));

    // An unreadable image must neither clear its saved state nor block a new,
    // readable importer from being scanned in the same successful enumeration.
    ModuleScanState partial;
    partial.recordResult(initial, 2, true, 0);
    partial.recordResult(late, 2, false, 0);
    const ModuleKey newest{0x300000, 0x5000, 300};
    std::unordered_set<std::uintptr_t> present{initial.base, late.base, newest.base};
    partial.reconcile(std::vector<ModuleKey>{newest}, present);
    assert(partial.size() == 2);
    assert(!partial.needsScan(initial, 2, 100));
    assert(!partial.needsScan(late, 2, 100));
    assert(partial.needsScan(late, 2, 5000));
    assert(partial.needsScan(newest, 2, 100));
    partial.recordResult(newest, 2, true, 100);
    assert(!partial.needsScan(newest, 2, 101));
    // Recovering metadata confirms a new identity and invalidates old success.
    partial.reconcile(std::vector<ModuleKey>{newTimestamp, newest}, present);
    assert(partial.size() == 2);
    assert(partial.needsScan(newTimestamp, 2, 101));
    assert(!partial.needsScan(late, 2, 101));
    partial.recordResult(newTimestamp, 2, true, 101);
    present.erase(late.base);
    partial.reconcile(std::vector<ModuleKey>{newTimestamp, newest}, present);
    assert(partial.size() == 2);
    assert(partial.needsScan(late, 2, 101));
    partial.reconcile(std::vector<ModuleKey>{}, {});
    assert(partial.size() == 0);

    // Saturation avoids a wrapped deadline turning into an immediate retry.
    const std::uint64_t maximum =
        (std::numeric_limits<std::uint64_t>::max)();
    state.recordResult(initial, 0, false, maximum - 1);
    assert(!state.needsScan(initial, 0, maximum - 1));
    assert(state.needsScan(initial, 0, maximum));

    std::puts("Module scan state tests passed.");
}
