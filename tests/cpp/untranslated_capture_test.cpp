#include "../../source/capture/untranslated_capture.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTemporaryDir>

#include <cassert>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

namespace {
std::atomic_bool failNextAllocation{false};
}

// Exercise the otherwise rare failure when the lock-free capture arena drains
// into std::map. Fail one C++ allocation, leaving Qt's file save usable.
void *operator new(std::size_t size) {
    if (failNextAllocation.exchange(false, std::memory_order_relaxed))
        throw std::bad_alloc();
    if (void *memory = std::malloc(size ? size : 1))
        return memory;
    throw std::bad_alloc();
}
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    assert(directory.isValid());

    HoudiniChineseCapture::initialize(directory.path(), &application);
    assert(!HoudiniChineseCapture::isEnabled());
    assert(!HoudiniChineseCapture::hasAllocatedStorage());
    assert(HoudiniChineseCapture::beginOneShot());
    assert(HoudiniChineseCapture::isEnabled());
    assert(HoudiniChineseCapture::hasAllocatedStorage());
    HoudiniChineseCapture::submit(QStringLiteral("Untranslated Control"));
    HoudiniChineseCapture::submit(QStringLiteral("Untranslated Control"));
    const HoudiniChineseCapture::OneShotResult first =
        HoudiniChineseCapture::endOneShot();
    assert(first.ready);
    assert(first.saved);
    assert(first.added == 1);
    assert(first.total == 1);
    assert(first.dropped == 0);
    assert(!HoudiniChineseCapture::isEnabled());
    assert(QFile::exists(first.outputPath));
    assert(!HoudiniChineseCapture::hasAllocatedStorage());

    // Submissions between explicit scans are ignored.
    HoudiniChineseCapture::submit(QStringLiteral("Background Miss"));
    assert(HoudiniChineseCapture::beginOneShot());
    HoudiniChineseCapture::submit(QStringLiteral("Untranslated Control"));
    HoudiniChineseCapture::submit(QStringLiteral("Second Control"));
    const HoudiniChineseCapture::OneShotResult second =
        HoudiniChineseCapture::endOneShot();
    assert(second.saved);
    assert(second.added == 1);
    assert(second.total == 2);
    assert(!HoudiniChineseCapture::isEnabled());
    assert(!HoudiniChineseCapture::hasAllocatedStorage());

    QFile file(second.outputPath);
    assert(file.open(QIODevice::ReadOnly));
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    assert(document.isObject());
    const QJsonObject translations =
        document.object().value(QStringLiteral("translations")).toObject();
    assert(translations.size() == 2);
    assert(translations.contains(QStringLiteral("Untranslated Control")));
    assert(translations.contains(QStringLiteral("Second Control")));
    assert(!translations.contains(QStringLiteral("Background Miss")));
    file.close();

    // Several render threads can report different strings concurrently.
    assert(HoudiniChineseCapture::beginOneShot());
    constexpr int workerCount = 12;
    constexpr int valuesPerWorker = 3000;
    std::atomic_bool start{false};
    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    for (int worker = 0; worker < workerCount; ++worker) {
        workers.emplace_back([worker, &start] {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            for (int value = 0; value < valuesPerWorker; ++value) {
                HoudiniChineseCapture::submit(
                    QStringLiteral("Worker %1 untranslated item %2")
                        .arg(worker)
                        .arg(value));
            }
        });
    }
    start.store(true, std::memory_order_release);
    for (std::thread &worker : workers)
        worker.join();
    const HoudiniChineseCapture::OneShotResult concurrent =
        HoudiniChineseCapture::endOneShot();
    assert(concurrent.ready);
    assert(concurrent.saved);
    assert(concurrent.dropped == 0);
    assert(concurrent.added ==
           static_cast<size_t>(workerCount * valuesPerWorker));
    assert(!HoudiniChineseCapture::hasAllocatedStorage());

    // An admitted writer must keep storage alive after the GUI closes the
    // gate. Writers arriving after closure must not enter that storage.
    assert(HoudiniChineseCapture::beginOneShot());
    std::atomic_bool admitted{false};
    std::thread heldWriter([&admitted] {
        HoudiniChineseCapture::ActiveSubmitter writer;
        assert(writer);
        admitted.store(true, std::memory_order_release);
        while (HoudiniChineseCapture::isEnabled())
            std::this_thread::yield();
        assert(HoudiniChineseCapture::hasAllocatedStorage());
        HoudiniChineseCapture::ActiveSubmitter lateWriter;
        assert(!lateWriter);
    });
    while (!admitted.load(std::memory_order_acquire))
        std::this_thread::yield();
    const auto held = HoudiniChineseCapture::endOneShot();
    heldWriter.join();
    assert(held.saved);
    assert(!HoudiniChineseCapture::hasAllocatedStorage());

    // End several scans while hook threads are actively submitting. Joining
    // before endOneShot() would miss the storage-release race entirely.
    for (int pass = 0; pass < 6; ++pass) {
        assert(HoudiniChineseCapture::beginOneShot());
        std::atomic_int submitted{0};
        std::atomic_bool stop{false};
        workers.clear();
        for (int worker = 0; worker < workerCount; ++worker) {
            workers.emplace_back([worker, pass, &submitted, &stop] {
                const QString text = QStringLiteral("Shutdown pass %1 worker %2")
                                         .arg(pass).arg(worker);
                HoudiniChineseCapture::submit(text);
                submitted.fetch_add(1, std::memory_order_release);
                while (!stop.load(std::memory_order_acquire)) {
                    HoudiniChineseCapture::submit(text);
                    std::this_thread::yield();
                }
            });
        }
        while (submitted.load(std::memory_order_acquire) != workerCount)
            std::this_thread::yield();
        const auto shutdown = HoudiniChineseCapture::endOneShot();
        stop.store(true, std::memory_order_release);
        for (auto &worker : workers)
            worker.join();
        assert(shutdown.saved);
        assert(shutdown.added == workerCount);
        assert(!HoudiniChineseCapture::hasAllocatedStorage());
    }

    // A failed file commit retains captured records for the next explicit
    // retry rather than reporting success and freeing the only remaining copy.
    assert(HoudiniChineseCapture::beginOneShot());
    const QString savedOutput = HoudiniChineseCapture::g_outputPath;
    HoudiniChineseCapture::g_outputPath = directory.path();
    HoudiniChineseCapture::submit(QStringLiteral("Preserve after failed save"));
    const auto failedSave = HoudiniChineseCapture::endOneShot();
    assert(!failedSave.saved);
    assert(HoudiniChineseCapture::hasAllocatedStorage());
    HoudiniChineseCapture::g_outputPath = savedOutput;
    assert(HoudiniChineseCapture::beginOneShot());
    const auto retry = HoudiniChineseCapture::endOneShot();
    assert(retry.saved);
    assert(retry.total == failedSave.total);
    assert(!HoudiniChineseCapture::hasAllocatedStorage());

    // A failed map allocation leaves a ready arena slot. Even with no pending
    // disk write, endOneShot() must report failure and retain that slot.
    assert(HoudiniChineseCapture::beginOneShot());
    HoudiniChineseCapture::submit(
        QStringLiteral("Preserve untranslated text when the map allocation fails"));
    failNextAllocation.store(true, std::memory_order_relaxed);
    const auto allocationFailure = HoudiniChineseCapture::endOneShot();
    assert(!failNextAllocation.load(std::memory_order_relaxed));
    assert(!allocationFailure.saved);
    assert(HoudiniChineseCapture::hasAllocatedStorage());
    assert(HoudiniChineseCapture::beginOneShot());
    const auto allocationRetry = HoudiniChineseCapture::endOneShot();
    assert(allocationRetry.saved);
    assert(allocationRetry.total == allocationFailure.total + 1);
    assert(!HoudiniChineseCapture::hasAllocatedStorage());

    std::cout << "untranslated_capture_test: OK\n";
    return 0;
}
