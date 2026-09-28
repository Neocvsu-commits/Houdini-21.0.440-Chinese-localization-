#include "../../source/hooks/hook_registry.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

using namespace HoudiniChineseHooks;

namespace {
constexpr HookId outerId = HookId::hookedCVText;
constexpr HookId innerId = HookId::hookedRVRender;

void expectState(const HookRegistry &registry, HookId id, const char *expected) {
    assert(std::strcmp(registry.record(id).state(), expected) == 0);
}

void renderNested(HookRegistry &registry) {
    HookObservation outer(registry, outerId);
    noteTranslationOutcome(TranslationOutcome::Translated);
    {
        HookObservation inner(registry, innerId);
        noteTranslationOutcome(TranslationOutcome::LookupMiss);
    }
    noteTranslationOutcome(TranslationOutcome::Protected);
}

void installationStates() {
    HookRegistry registry;
    int rawExport = 0;
    int virtualExport = 0;
    const auto target = reinterpret_cast<std::uintptr_t>(&rawExport);
    const std::uintptr_t module = 0x100000;
    const std::uintptr_t importerA = 0x200000;
    const std::uintptr_t importerB = 0x300000;

    expectState(registry, outerId, "waiting_for_module");
    registry.resolve(outerId, 0, 0);
    assert(registry.generation() == 0);
    expectState(registry, outerId, "waiting_for_module");
    registry.resolve(outerId, module, 0);
    assert(registry.generation() == 0);
    expectState(registry, outerId, "symbol_missing");

    registry.resolve(outerId, module, target);
    const std::uint64_t resolvedGeneration = registry.generation();
    assert(resolvedGeneration > 0);
    expectState(registry, outerId, "resolved_not_patched");
    registry.resolve(outerId, module, target);
    assert(registry.generation() == resolvedGeneration);

    registry.beginInline(&rawExport);
    registry.inlineResult(&rawExport, false, 8, "detour_attach");
    assert(std::strcmp(registry.record(outerId).errorStage, "detour_attach") == 0);
    expectState(registry, outerId, "installation_failed");
    registry.registerImport(&rawExport);
    const std::uint64_t registeredGeneration = registry.generation();
    assert(registeredGeneration > resolvedGeneration);
    expectState(registry, outerId, "import_registered_not_patched");
    registry.registerImport(&rawExport);
    registry.importResult(&rawExport, importerA, 0, 0);
    assert(registry.generation() == registeredGeneration);
    expectState(registry, outerId, "import_registered_not_patched");

    // Counts represent replacement slots currently present, not new writes.
    registry.importResult(&rawExport, importerA, 2, 0);
    expectState(registry, outerId, "patched_not_observed");
    registry.importResult(&rawExport, importerA, 2, 0);
    assert(registry.record(outerId).importSlots() == 2);
    registry.importResult(&rawExport, importerB, 3, 0);
    assert(registry.record(outerId).importSlots() == 5);
    assert(registry.generation() == registeredGeneration);

    registry.forgetAbsentImporters(
        [=](std::uintptr_t importer) { return importer == importerB; });
    assert(registry.record(outerId).importSlots() == 3);
    expectState(registry, outerId, "patched_not_observed");
    registry.forgetAbsentImporters([](std::uintptr_t) { return false; });
    assert(registry.record(outerId).importSlots() == 0);
    expectState(registry, outerId, "import_registered_not_patched");

    // Either successful implementation patch is meaningful without an importer.
    registry.retryLimited(&rawExport);
    assert(registry.record(outerId).retryLimited);
    registry.inlineResult(&rawExport, true, 0);
    assert(!registry.record(outerId).retryLimited);
    assert(std::strcmp(registry.record(outerId).errorStage, "") == 0);
    expectState(registry, outerId, "patched_not_observed");
    registry.resolve(innerId, module,
                     reinterpret_cast<std::uintptr_t>(&virtualExport));
    registry.virtualResult(&virtualExport, true);
    expectState(registry, innerId, "patched_not_observed");

    registry.enableObservation(true);
    renderNested(registry);
    expectState(registry, outerId, "observed");
    expectState(registry, innerId, "observed");

    // Historical hits must not hide a now-missing or changed implementation.
    registry.resolve(outerId, 0, 0);
    expectState(registry, outerId, "waiting_for_module");
    registry.resolve(outerId, module,
                     reinterpret_cast<std::uintptr_t>(&virtualExport));
    expectState(registry, outerId, "target_changed_requires_restart");
    assert(registry.record(outerId).target == target);
}

void observationAttribution() {
    HookRegistry registry;
    renderNested(registry);
    noteTranslationOutcome(TranslationOutcome::Translated);
    assert(registry.record(outerId).counters.calls == 0);
    assert(registry.record(outerId).counters.translated == 0);
    assert(registry.record(innerId).counters.calls == 0);
    assert(registry.record(innerId).counters.lookupMisses == 0);

    registry.enableObservation(true);
    renderNested(registry);
    const HookCounters &outer = registry.record(outerId).counters;
    const HookCounters &inner = registry.record(innerId).counters;
    assert(outer.calls == 1 && outer.translated == 1 && outer.protectedText == 1);
    assert(outer.lookupMisses == 0);
    assert(inner.calls == 1 && inner.lookupMisses == 1);
    assert(inner.translated == 0 && inner.protectedText == 0);

    // Unwinding an exception must restore the attribution of the caller.
    {
        HookObservation outerScope(registry, outerId);
        try {
            HookObservation innerScope(registry, innerId);
            throw 1;
        } catch (int) {
            noteTranslationOutcome(TranslationOutcome::Translated);
        }
    }
    assert(outer.calls == 2 && outer.translated == 2);
    assert(inner.calls == 2 && inner.translated == 0);
    noteTranslationOutcome(TranslationOutcome::Translated);
    assert(outer.translated == 2 && inner.translated == 0);

    registry.enableObservation(false);
    renderNested(registry);
    assert(outer.calls == 2 && outer.translated == 2 && outer.protectedText == 1);
    assert(inner.calls == 2 && inner.lookupMisses == 1);
    assert(registry.nestingCount(outerId, innerId) == 2);

    // A disabled nested scope must not attribute its outcome to an enabled
    // caller. This also exercises a diagnostic stop during an existing call.
    registry.enableObservation(true);
    {
        HookObservation outerScope(registry, outerId);
        registry.enableObservation(false);
        {
            HookObservation disabledInner(registry, innerId);
            noteTranslationOutcome(TranslationOutcome::LookupMiss);
        }
        registry.enableObservation(true);
        noteTranslationOutcome(TranslationOutcome::Translated);
    }
    assert(outer.calls == 3 && outer.translated == 3 && outer.lookupMisses == 0);
    assert(inner.calls == 2 && inner.lookupMisses == 1);
    assert(registry.nestingCount(outerId, innerId) == 2);
    assert(activeHookRegistry == nullptr);
}

void parallelObservation() {
    HookRegistry registry;
    registry.enableObservation(true);
    constexpr std::size_t threadCount = 8;
    constexpr std::size_t iterations = 2000;
    std::atomic<std::size_t> ready{0};
    std::atomic<bool> begin{false};
    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    for (std::size_t i = 0; i < threadCount; ++i) {
        workers.emplace_back([&] {
            ready.fetch_add(1);
            while (!begin.load())
                std::this_thread::yield();
            for (std::size_t call = 0; call < iterations; ++call)
                renderNested(registry);
            // A finished worker has no active scope left to receive this.
            noteTranslationOutcome(TranslationOutcome::Translated);
        });
    }
    while (ready.load() != threadCount)
        std::this_thread::yield();
    begin.store(true);
    for (auto &worker : workers)
        worker.join();

    const std::uint64_t expected = threadCount * iterations;
    const HookCounters &outer = registry.record(outerId).counters;
    const HookCounters &inner = registry.record(innerId).counters;
    assert(outer.calls == expected && outer.translated == expected);
    assert(outer.protectedText == expected && outer.lookupMisses == 0);
    assert(inner.calls == expected && inner.lookupMisses == expected);
    assert(inner.translated == 0 && inner.protectedText == 0);
    assert(registry.nestingCount(outerId, innerId) == expected);
}
} // namespace

int main() {
    installationStates();
    observationAttribution();
    parallelObservation();
    std::puts("Hook registry tests passed.");
}
