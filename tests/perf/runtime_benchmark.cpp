#include "../../source/hooks/hook_registry.h"
#include "../../source/hooks/hook_lifecycle.h"
#include "../../source/translation/display_text.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
using namespace HoudiniChineseHooks;
constexpr std::size_t kSamples = 5;
constexpr std::size_t kHotIterations = 1000000;
constexpr std::size_t kCleanupIterations = 128;
constexpr std::size_t kModules = 1000;
constexpr std::size_t kImportersPerHook = 32;
volatile std::uint64_t resultSink = 0;

template <typename Operation>
double medianNs(std::size_t iterations, Operation operation) {
    std::array<double, kSamples> samples{};
    for (auto &sample : samples) {
        const auto start = Clock::now();
        const std::uint64_t result = operation(iterations);
        const auto end = Clock::now();
        // Every timed loop contributes an externally observable result. Keep
        // the volatile write outside the timed region, especially for the
        // default-off observation path whose cost is only a few instructions.
        resultSink = resultSink + result;
        sample = std::chrono::duration<double, std::nano>(end - start).count() /
                 static_cast<double>(iterations);
    }
    std::sort(samples.begin(), samples.end());
    return samples[kSamples / 2];
}

double cacheNs(bool hit) {
    HoudiniChineseLookupCache::DirectMapped<> cache;
    const QString source = hit ? QStringLiteral("Material Properties")
                               : QStringLiteral("Unmapped Properties");
    const QString translation = hit ? QStringLiteral("材质属性") : QString{};
    std::uint64_t resolutions = 0;
    const auto resolver = [&] { ++resolutions; return translation; };
    cache.lookup(source, resolver); // Warm-up is deliberately outside timing.
    const double result = medianNs(kHotIterations, [&](std::size_t count) {
        std::uint64_t checksum = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const QString text = cache.lookup(source, resolver);
            checksum += static_cast<std::uint64_t>(text.size()) +
                        static_cast<std::uint64_t>(text.isEmpty());
        }
        return checksum;
    });
    if (resolutions != 1)
        throw std::runtime_error("The warmed cache unexpectedly called its resolver");
    return result;
}

double observationNs(bool enabled) {
    HookRegistry registry;
    registry.enableObservation(enabled);
    const double result = medianNs(kHotIterations, [&](std::size_t count) {
        std::uint64_t checksum = 0;
        for (std::size_t i = 0; i < count; ++i) {
            HookObservation observation(registry, HookId::hookedDrawPoint);
            checksum += activeHookCounters ? 1U : 0U;
        }
        return checksum;
    });
    const auto calls = registry.record(HookId::hookedDrawPoint).counters.calls.load();
    if (calls != (enabled ? kSamples * kHotIterations : 0) || activeHookCounters)
        throw std::runtime_error("Observation did not preserve its counter/TLS contract");
    return result;
}

std::vector<ModuleKey> moduleSnapshot() {
    std::vector<ModuleKey> modules;
    modules.reserve(kModules);
    for (std::size_t i = 0; i < kModules; ++i)
        modules.push_back({0x10000000U + i * 0x10000U, 0x10000U, 1});
    return modules;
}

template <bool UseHashSet>
double cleanupNs(const std::vector<ModuleKey> &modules) {
    HookRegistry registry;
    for (std::size_t hook = 0; hook < kHookCount; ++hook)
        for (std::size_t slot = 0; slot < kImportersPerHook; ++slot) {
            // Spread each hook's importers across the identical live snapshot.
            const auto module = (hook * 37 + slot * 19) % modules.size();
            registry.record(static_cast<HookId>(hook)).importerSlots.emplace(
                modules[module].base, 1);
        }
    return medianNs(kCleanupIterations, [&](std::size_t count) {
        std::uint64_t checksum = 0;
        for (std::size_t pass = 0; pass < count; ++pass) {
            if constexpr (UseHashSet) {
                // Include allocation, construction and destruction on each
                // pass, exactly as in patchNewImportModules().
                std::unordered_set<std::uintptr_t> presentBases;
                presentBases.reserve(modules.size());
                for (const auto &module : modules)
                    presentBases.insert(module.base);
                registry.forgetAbsentImporters([&](std::uintptr_t base) {
                    return presentBases.find(base) != presentBases.end();
                });
            } else {
                registry.forgetAbsentImporters([&](std::uintptr_t base) {
                    return std::any_of(modules.begin(), modules.end(),
                        [base](const ModuleKey &module) { return module.base == base; });
                });
            }
            for (std::size_t hook = 0; hook < kHookCount; ++hook)
                checksum += registry.record(static_cast<HookId>(hook)).importerSlots.size();
        }
        if (checksum != count * kHookCount * kImportersPerHook)
            throw std::runtime_error("A cleanup pass incorrectly removed a live importer");
        return checksum;
    });
}
} // namespace

int main() {
    try {
        const double hit = cacheNs(true);
        const double miss = cacheNs(false);
        const double off = observationNs(false);
        const double on = observationNs(true);
        const auto modules = moduleSnapshot();
        const double linear = cleanupNs<false>(modules);
        const double hashed = cleanupNs<true>(modules);
        std::cout << std::fixed << std::setprecision(3)
            << "{\n  \"scope\": \"single-thread synthetic microbenchmark; not Houdini total CPU\",\n"
            << "  \"compiler\": \"MSVC /O2 /MD, Qt " << qVersion() << "\",\n"
            << "  \"samples\": " << kSamples << ",\n"
            << "  \"statistic\": \"median\",\n"
            << "  \"hot_iterations_per_sample\": " << kHotIterations << ",\n"
            << "  \"cache_hot_hit_ns\": " << hit << ",\n"
            << "  \"cache_hot_cached_miss_ns\": " << miss << ",\n"
            << "  \"observation_disabled_ns\": " << off << ",\n"
            << "  \"observation_enabled_ns\": " << on << ",\n"
            << "  \"cleanup_modules\": " << kModules << ",\n"
            << "  \"cleanup_hooks\": " << kHookCount << ",\n"
            << "  \"cleanup_importers_per_hook\": " << kImportersPerHook << ",\n"
            << "  \"cleanup_all_importers_present\": true,\n"
            << "  \"cleanup_passes_per_sample\": " << kCleanupIterations << ",\n"
            << "  \"cleanup_old_any_of_us\": " << linear / 1000.0 << ",\n"
            << "  \"cleanup_hash_set_including_build_us\": " << hashed / 1000.0 << ",\n"
            << "  \"checksum\": " << resultSink << "\n}\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
