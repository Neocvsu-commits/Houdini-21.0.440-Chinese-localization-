#pragma once

#include "hook_registry.h"
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QString>

namespace HoudiniChineseHooks {

// Summaries derive from the same catalog as installation diagnostics, so adding
// an entry cannot silently omit it here. These are capability observations, not
// all-or-nothing installation gates: each existing fallback remains available.
inline QJsonArray capabilityReport(const HookRegistry &registry) {
    QJsonObject groups;
    for (const HookSpec &spec : kHookSpecs) {
        const QString key = QString::fromLatin1(spec.feature);
        auto group = groups[key].toObject();
        auto roles = group["roles"].toObject();
        auto role = roles[spec.role].toObject();
        const HookRecord &record = registry.record(spec.id);
        const bool installed = !record.targetChanged && record.modulePresent &&
            (record.inlinePatched || record.virtualPatched || record.importSlots() != 0);
        role["catalog_entries"] = role["catalog_entries"].toInt() + 1;
        role["installed_entries"] = role["installed_entries"].toInt() + (installed ? 1 : 0);
        auto missing = role["unavailable_symbols"].toArray();
        if (!installed) missing.append(spec.symbol);
        role["unavailable_symbols"] = missing;
        roles[spec.role] = role;
        group["roles"] = roles;
        groups[key] = group;
    }
    QJsonArray result;
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        auto group = it.value().toObject();
        group["feature"] = it.key();
        int total = 0, installed = 0;
        const auto roles = group["roles"].toObject();
        QJsonArray unavailableRoles;
        for (auto role = roles.constBegin(); role != roles.constEnd(); ++role) {
            const auto data = role.value().toObject();
            total += data["catalog_entries"].toInt();
            installed += data["installed_entries"].toInt();
            if (!data["installed_entries"].toInt()) unavailableRoles.append(role.key());
        }
        group["state"] = installed == 0 ? "not_installed" :
            installed == total ? "catalog_entries_installed" : "partial";
        group["unavailable_roles"] = unavailableRoles;
        group["catalog_entries"] = total;
        group["installed_entries"] = installed;
        result.append(group);
    }
    return result;
}

// Explicit requests only, on the GUI thread. No original UI strings are saved.
inline QJsonObject hookReport(const HookRegistry &registry) {
    static constexpr const char *names[] = {
#define HC_HOOK_NAME(Id, Module, Symbol, Feature, Role, Strategy, Scenario) #Id,
        HC_HOOK_CATALOG(HC_HOOK_NAME)
#undef HC_HOOK_NAME
    };
    QJsonArray hooks;
    for (const HookSpec &spec : kHookSpecs) {
        const HookRecord &record = registry.record(spec.id);
        QJsonObject item;
        item["id"] = names[static_cast<std::size_t>(spec.id)];
        item["module"] = QString::fromWCharArray(spec.module);
        item["symbol"] = spec.symbol;
        item["feature"] = spec.feature;
        item["role"] = spec.role;
        item["permitted_strategy"] = spec.strategy;
        item["scenario"] = spec.scenario;
        item["state"] = record.state();
        item["inline_patched"] = record.inlinePatched;
        item["virtual_patched"] = record.virtualPatched;
        item["import_registered"] = record.importRegistered;
        item["current_import_slots"] = static_cast<qint64>(record.importSlots());
        item["attempts"] = static_cast<int>(record.attempts);
        item["inline_retry_limit_reached"] = record.retryLimited;
        item["last_error"] = static_cast<qint64>(record.lastError);
        item["last_error_stage"] = record.errorStage;
        QJsonObject detour;
        detour["stage"] = record.detourStage;
        detour["allocation_error"] = static_cast<qint64>(record.detourAllocationError);
        detour["query_error"] = static_cast<qint64>(record.detourQueryError);
        detour["allocation_attempts"] = static_cast<qint64>(record.detourAllocationAttempts);
        detour["query_count"] = static_cast<qint64>(record.detourQueryCount);
        item["detour_diagnostic"] = detour;
        item["diagnostics_incomplete"] = record.incompleteDiagnostics;
        item["observed_calls"] = static_cast<qint64>(record.counters.calls.load(std::memory_order_relaxed));
        item["translated_calls"] = static_cast<qint64>(record.counters.translated.load(std::memory_order_relaxed));
        item["lookup_misses"] = static_cast<qint64>(record.counters.lookupMisses.load(std::memory_order_relaxed));
        item["protected_calls"] = static_cast<qint64>(record.counters.protectedText.load(std::memory_order_relaxed));
        hooks.append(item);
    }
    QJsonObject result;
    result["schema"] = "houdini-chinese-hook-report-v1";
    result["runtime_qt"] = qVersion();
    result["capabilities"] = capabilityReport(registry);
    result["observation_enabled"] = registry.observing();
    result["generation"] = static_cast<qint64>(registry.generation());
    result["coverage_note"] = "Counters are historical observations, not proof of complete UI coverage. "
        "An unexercised scenario may have zero calls. Import fallback covers import callers only. "
        "Installed catalog entries do not prove compatible private ABI or full visual coverage.";
    result["hooks"] = hooks;
    QJsonArray nesting;
    for (const HookSpec &parent : kHookSpecs) {
        for (const HookSpec &child : kHookSpecs) {
            const auto count = registry.nestingCount(parent.id, child.id);
            if (!count) continue;
            QJsonObject edge;
            edge["parent"] = names[static_cast<std::size_t>(parent.id)];
            edge["child"] = names[static_cast<std::size_t>(child.id)];
            edge["calls"] = static_cast<qint64>(count);
            nesting.append(edge);
        }
    }
    result["call_edges"] = nesting;
    result["call_edges_note"] = "Same-thread nested hook observations only; "
        "edges do not prove identical text, redundant hooks or asynchronous coverage.";
    return result;
}

inline bool writeHookReport(const QString &path, const QJsonObject &report) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const QByteArray data = QJsonDocument(report).toJson(QJsonDocument::Indented);
    return file.write(data) == data.size() && file.commit();
}

} // namespace HoudiniChineseHooks
