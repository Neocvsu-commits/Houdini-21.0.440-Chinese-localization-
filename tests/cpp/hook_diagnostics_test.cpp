#include "../../source/hooks/hook_diagnostics.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <cassert>
#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    using namespace HoudiniChineseHooks;
    HookRegistry registry;
    const auto id = HookId::hookedRERenderPutCString;
    registry.resolve(id, 0x1000, 0x1100);
    registry.registerImport(reinterpret_cast<void *>(0x1100));
    auto report = hookReport(registry);
    assert(report["hooks"].toArray().size() == static_cast<qsizetype>(kHookCount));
    const auto get = [id](const QJsonObject &data) {
        return data["hooks"].toArray()[static_cast<qsizetype>(id)].toObject();
    };
    assert(get(report)["state"] == "import_registered_not_patched");
    assert(get(report)["current_import_slots"].toInteger() == 0);
    registry.beginInline(reinterpret_cast<void *>(0x1100));
    registry.forTarget(reinterpret_cast<void *>(0x1100), [](auto &entry) {
        entry.detourStage = "detour_allocate_trampoline";
        entry.detourAllocationError = 487;
        entry.detourAllocationAttempts = 2;
        entry.detourQueryCount = 17;
    });
    registry.inlineResult(reinterpret_cast<void *>(0x1100), false, 8,
                          "detour_allocate_trampoline");
    report = hookReport(registry);
    assert(get(report)["last_error_stage"] == "detour_allocate_trampoline");
    const auto detail = get(report)["detour_diagnostic"].toObject();
    assert(detail["allocation_error"].toInt() == 487);
    assert(detail["query_count"].toInt() == 17);
    registry.beginInline(reinterpret_cast<void *>(0x1100));
    assert(get(hookReport(registry))["detour_diagnostic"].toObject()["allocation_error"].toInt() == 0);
    const auto feature = [](const QJsonObject &data, const QString &name) {
        for (const auto &item : data["capabilities"].toArray())
            if (item.toObject()["feature"] == name) return item.toObject();
        return QJsonObject();
    };
    assert(feature(report, "native-re")["state"] == "not_installed");
    registry.importResult(reinterpret_cast<void *>(0x1100), 0x2000, 2, 0);
    registry.enableObservation(true);
    {
        HookObservation scope(registry, id);
        noteTranslationOutcome(TranslationOutcome::Translated);
    }
    report = hookReport(registry);
    assert(get(report)["state"] == "observed");
    assert(get(report)["current_import_slots"].toInteger() == 2);
    assert(get(report)["translated_calls"].toInteger() == 1);
    assert(get(report)["module"] == "libRE.dll");
    assert(get(report)["role"] == "draw");
    {
        HookObservation parent(registry, id);
        HookObservation child(registry, HookId::hookedRVAddText);
    }
    report = hookReport(registry);
    const auto edges = report["call_edges"].toArray();
    assert(edges.size() == 1);
    assert(edges[0].toObject()["parent"] == "hookedRERenderPutCString");
    assert(edges[0].toObject()["child"] == "hookedRVAddText");
    assert(edges[0].toObject()["calls"].toInteger() == 1);
    const auto native = feature(report, "native-re");
    assert(native["state"] == "partial");
    assert(native["unavailable_roles"].toArray().contains("measure"));
    assert(!native["unavailable_roles"].toArray().contains("draw"));
    int catalogTotal = 0;
    for (const auto &item : report["capabilities"].toArray())
        catalogTotal += item.toObject()["catalog_entries"].toInt();
    assert(catalogTotal == static_cast<int>(kHookCount));
    // A stale patch cannot report availability after an export changes.
    registry.resolve(id, 0x1000, 0x1200);
    assert(feature(hookReport(registry), "native-re")["state"] == "not_installed");
    QTemporaryDir temporary;
    assert(temporary.isValid());
    const auto path = temporary.filePath("report.json");
    assert(writeHookReport(path, report));
    QFile file(path);
    assert(file.open(QIODevice::ReadOnly));
    assert(QJsonDocument::fromJson(file.readAll()).object() == report);
    assert(!writeHookReport(temporary.filePath("missing/report.json"), report));
    assert(!writeHookReport(temporary.path(), report));
    std::puts("hook_diagnostics_test: passed");
}
