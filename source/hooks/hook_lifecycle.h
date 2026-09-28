#pragma once

// ---------------------------------------------------------------------------
// hook lifecycle
// ---------------------------------------------------------------------------
// Shared lifecycle policy for hooks injected into Houdini.
// 1. Wait for QApplication without touching Qt objects from DllMain.
// 2. Install currently resolvable hooks on the GUI thread.
// 3. Use Qt lifecycle events for objects created later.
// 4. Optionally poll for modules that load late and bypass those events.
namespace HoudiniHookLifecycle {

constexpr int kApplicationWaitAttempts = 300;
constexpr int kApplicationWaitIntervalMs = 100;
constexpr int kDeferredScanIntervalMs = 2000;
constexpr int kDeferredScanWindowMs = 60000;
constexpr int kSteadyScanIntervalMs = 15000;

// Houdini loads libRV and some Qt Quick consumers after the main UI appears.
// Scan promptly during startup, then at a lower rate for the entire session.
// Only new modules, new registrations and failed import scans are repatched.
constexpr bool kEnableDeferredPolling = true;

} // namespace HoudiniHookLifecycle

// ---------------------------------------------------------------------------
// hook retry policy
// ---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace HoudiniChineseHooks {

// Only hook installation touches this policy, on the GUI thread. Missing
// modules/exports never consume an attempt. A failed target gets a bounded
// retry after startup has progressed, rather than suspending all host threads
// again on every module scan for a full minute.
class RetryPolicy final {
  public:
    static constexpr unsigned kMaxAttempts = 3;
    static constexpr std::uint64_t kRetryDelayMs = 10000;

    bool begin(const void *target, std::uint64_t now) {
        if (!target)
            return false;
        auto &entry = attempts_[target];
        if (entry.count >= kMaxAttempts || (entry.count && now < entry.next))
            return false;
        ++entry.count;
        entry.next = now + kRetryDelayMs;
        return true;
    }

    unsigned attempts(const void *target) const {
        const auto found = attempts_.find(target);
        return found == attempts_.end() ? 0 : found->second.count;
    }

    // Explicit startup-complete recovery, never granted by ordinary polling.
    // Only callers that classified a recoverable failure may use this budget.
    bool beginLate(const void *target, std::uint64_t now) {
        const auto found = attempts_.find(target);
        if (found == attempts_.end()) return false;
        auto &entry = found->second;
        if (entry.lateUsed || now < entry.next) return false;
        entry.lateUsed = true;
        ++entry.count;
        entry.next = now + kRetryDelayMs;
        return true;
    }

  private:
    struct Attempt { unsigned count = 0; std::uint64_t next = 0; bool lateUsed = false; };
    std::unordered_map<const void *, Attempt> attempts_;
};

} // namespace HoudiniChineseHooks

// ---------------------------------------------------------------------------
// module scan state
// ---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace HoudiniChineseHooks {

struct ModuleKey final {
    std::uintptr_t base = 0;
    std::uint32_t size = 0;
    std::uint32_t timestamp = 0;

    bool operator==(const ModuleKey &other) const noexcept {
        return base == other.base && size == other.size &&
               timestamp == other.timestamp;
    }
};

struct ModuleKeyHash final {
    std::size_t operator()(const ModuleKey &key) const noexcept {
        std::size_t value = std::hash<std::uintptr_t>{}(key.base);
        combine(value, std::hash<std::uint32_t>{}(key.size));
        combine(value, std::hash<std::uint32_t>{}(key.timestamp));
        return value;
    }

  private:
    static void combine(std::size_t &value, std::size_t part) noexcept {
        value ^= part + std::size_t{0x9e3779b9U} + (value << 6) + (value >> 2);
    }
};

// GUI-thread state only; this class neither examines modules nor installs hooks.
// Generations increase when the registry gains an import-hook target. A failed
// scan remains pending for that generation, without suppressing a newer one.
class ModuleScanState final {
  public:
    static constexpr std::uint64_t kInitialRetryDelayMs = 5000;
    static constexpr std::uint64_t kMaxRetryDelayMs = 60000;

    bool needsScan(const ModuleKey &key, std::uint64_t generation,
                   std::uint64_t now) const {
        const auto found = entries_.find(key);
        if (found == entries_.end())
            return true;
        const Entry &entry = found->second;
        if (entry.hasCompleted && entry.completedGeneration >= generation)
            return false;
        return !entry.hasAttempted || entry.attemptedGeneration != generation ||
               now >= entry.nextAttempt;
    }

    void recordResult(const ModuleKey &key, std::uint64_t generation,
                      bool success, std::uint64_t now) {
        Entry &entry = entries_[key];
        // An old result must not invalidate a successful newer scan.
        if (entry.hasCompleted && entry.completedGeneration >= generation)
            return;
        if (success) {
            entry.hasCompleted = true;
            entry.completedGeneration = generation;
            entry.hasAttempted = false;
            entry.retryDelay = 0;
            entry.nextAttempt = 0;
            return;
        }

        if (!entry.hasAttempted || entry.attemptedGeneration != generation) {
            entry.retryDelay = kInitialRetryDelayMs;
        } else if (entry.retryDelay >= kMaxRetryDelayMs / 2) {
            entry.retryDelay = kMaxRetryDelayMs;
        } else {
            entry.retryDelay *= 2;
        }
        entry.hasAttempted = true;
        entry.attemptedGeneration = generation;
        const std::uint64_t maximum =
            (std::numeric_limits<std::uint64_t>::max)();
        entry.nextAttempt = now > maximum - entry.retryDelay
                                ? maximum
                                : now + entry.retryDelay;
    }

    // Supply a complete, successful module snapshot. An enumeration failure is
    // not an empty snapshot: the caller must preserve state in that case.
    template <typename Iterator>
    void reconcile(Iterator first, Iterator last) {
        const std::unordered_set<ModuleKey, ModuleKeyHash> current(first, last);
        for (auto iterator = entries_.begin(); iterator != entries_.end();) {
            if (current.find(iterator->first) == current.end())
                iterator = entries_.erase(iterator);
            else
                ++iterator;
        }
    }

    void reconcile(const std::vector<ModuleKey> &snapshot) {
        reconcile(snapshot.begin(), snapshot.end());
    }

    // Enumeration succeeded, but individual images may have unreadable
    // metadata. Retain those images' state until their identity is known again;
    // discard only an absent base or a confirmed replacement at the same base.
    void reconcile(const std::vector<ModuleKey> &readableKeys,
                   const std::unordered_set<std::uintptr_t> &enumeratedBases) {
        std::unordered_map<std::uintptr_t, ModuleKey> identities;
        identities.reserve(readableKeys.size());
        for (const ModuleKey &key : readableKeys)
            identities.emplace(key.base, key);
        for (auto iterator = entries_.begin(); iterator != entries_.end();) {
            const ModuleKey &key = iterator->first;
            const auto known = identities.find(key.base);
            if (enumeratedBases.find(key.base) == enumeratedBases.end() ||
                (known != identities.end() && !(known->second == key)))
                iterator = entries_.erase(iterator);
            else
                ++iterator;
        }
    }

    std::size_t size() const noexcept { return entries_.size(); }

  private:
    struct Entry final {
        std::uint64_t completedGeneration = 0;
        std::uint64_t attemptedGeneration = 0;
        std::uint64_t nextAttempt = 0;
        std::uint64_t retryDelay = 0;
        bool hasCompleted = false;
        bool hasAttempted = false;
    };

    // Size/timestamp detect replacement at the same base. Polling cannot detect
    // an unload/reload entirely between snapshots when all three fields match.
    // Observing the module absent in a snapshot does discard its completed state.
    std::unordered_map<ModuleKey, Entry, ModuleKeyHash> entries_;
};

} // namespace HoudiniChineseHooks

// ---------------------------------------------------------------------------
// runtime compatibility
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string_view>

namespace HoudiniChineseCompatibility {

constexpr bool isMatchingHdkApi(std::uint32_t compiled,
                                std::uint32_t runtime) noexcept {
    return compiled != 0 && runtime != 0 && compiled == runtime;
}

constexpr bool isQt6Major(int major) noexcept { return major == 6; }

constexpr bool isMatchingQtRuntime(std::string_view compiled,
                                   std::string_view runtime) noexcept {
    return !compiled.empty() && compiled == runtime;
}

constexpr bool hasCoreQtCapabilities(bool core, bool gui,
                                     bool widgets) noexcept {
    return core && gui && widgets;
}

constexpr bool isCompatibleRuntime(std::uint32_t compiledHdk,
                                   std::uint32_t runtimeHdk, int qtMajor,
                                   std::string_view compiledQt,
                                   std::string_view runtimeQt, bool core,
                                   bool gui, bool widgets) noexcept {
    return isMatchingHdkApi(compiledHdk, runtimeHdk) && isQt6Major(qtMajor) &&
           isMatchingQtRuntime(compiledQt, runtimeQt) &&
           hasCoreQtCapabilities(core, gui, widgets);
}

} // namespace HoudiniChineseCompatibility
