// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BackupHistoryModel.hpp"
#include "RunStatusModel.hpp"

#include <iostream>

#include <QLocale>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

QString run_payload(const QString& state, bool can_cancel) {
    return QStringLiteral(R"({
        "schemaVersion": 1,
        "runId": "run-1",
        "operationKind": "backup",
        "state": "%1",
        "phase": "transferring",
        "activity": "transferring",
        "canCancel": %2,
        "errorCode": "",
        "sourceName": "Home",
        "targetName": "Backup disk",
        "bytesProcessed": 1048576,
        "bytesTotalEstimated": 4194304,
        "speedBps": 1024,
        "etaSeconds": 20,
        "sourceProgress": 30,
        "overallProgress": 40,
        "progressAccuracy": "estimated",
        "lastSuccessAt": "2026-08-18T18:42:00Z",
        "lastAttemptAt": "2026-08-30T12:34:56Z",
        "lastAttemptState": "failed"
        ,"sourceIndex": 2
        ,"sourceCount": 5
        ,"startedAt": "2026-08-30T12:30:00Z"
        ,"updatedAt": "2026-08-30T12:34:56Z"
    })")
        .arg(state, can_cancel ? QStringLiteral("true") : QStringLiteral("false"));
}

void test_run_status_and_terminal_transition() {
    RunStatusModel model;
    int finished = 0;
    QObject::connect(&model, &RunStatusModel::activeRunFinished, [&finished]() { ++finished; });
    model.setCancelSupported(true);

    expect(model.apply(run_payload(QStringLiteral("running"), true)), "running status was rejected");
    expect(model.operationKind() == QStringLiteral("backup"), "operation kind was not exposed");
    expect(model.canCancel(), "cancellable status was not exposed");
    expect(model.overallProgress() == 40 && model.speedBps() == 1024, "run progress was not applied");
    expect(model.bytesProcessed() == 1048576 && model.bytesTotalEstimated() == 4194304, "transfer byte counts were not applied");
    expect(model.bytesProcessedText() == QStringLiteral("1,0 MiB"), "processed byte count was not localized");
    expect(model.bytesTotalEstimatedText() == QStringLiteral("4,0 MiB"), "estimated byte count was not localized");
    expect(model.speedText() == QStringLiteral("1,0 KiB/s"), "transfer rate was not localized");
    expect(model.lastSuccessAt() == QStringLiteral("2026-08-18T18:42:00Z"), "last success was not applied");
    expect(model.lastAttemptAt() == QStringLiteral("2026-08-30T12:34:56Z"), "last attempt was not applied");
    expect(model.lastAttemptState() == QStringLiteral("failed"), "last attempt state was not applied");
    expect(model.sourceIndex() == 2 && model.sourceCount() == 5, "source position was not applied");
    expect(model.freshnessState() == QStringLiteral("informational"), "backup freshness was not informational");
    expect(model.apply(run_payload(QStringLiteral("succeeded"), false)), "terminal status was rejected");
    expect(model.elapsedSeconds() == 296, "run duration was not derived from public timestamps");
    expect(finished == 1, "active-to-terminal transition was not reported exactly once");

    model.setCancelSupported(false);
    expect(!model.canCancel(), "cancel capability was ignored");
}

void test_history_validation_and_reset() {
    BackupHistoryModel model;
    expect(
        model.apply(QStringLiteral(R"([{
            "schemaVersion": 1,
            "state": "succeeded",
            "errorCode": "",
            "sourceName": "Home",
            "targetName": "Backup disk",
            "startedAt": "2026-08-30T12:30:00Z",
            "finishedAt": "2026-08-30T12:34:56Z",
            "sourceCount": 2,
            "overallProgress": 100,
            "bytesTransferred": 4294967296
        }])")),
        "history was rejected"
    );
    expect(model.entries().size() == 1, "history entry was not applied");
    const QVariantMap history_entry = model.entries().front().toMap();
    expect(
        history_entry.value(QStringLiteral("bytesTransferred")).toLongLong() == 4294967296LL,
        "history byte count was not retained"
    );
    expect(
        !history_entry.value(QStringLiteral("bytesTransferredText")).toString().isEmpty(),
        "history byte count was not formatted"
    );
    expect(
        model.entries().front().toMap().value(QStringLiteral("durationSeconds")).toInt() == 296,
        "history duration was not derived"
    );
    expect(!model.apply(QStringLiteral("[null]")), "invalid history entry was accepted");
    expect(model.entries().size() == 1, "rejected history changed the model");
    model.reset();
    expect(model.entries().isEmpty(), "history reset retained entries");
    model.setPageSize(0);
    expect(model.pageSize() == 1, "history page size was not bounded at the lower limit");
    model.setPageSize(101);
    expect(model.pageSize() == 100, "history page size was not bounded at the upper limit");
}

} // namespace

int main() {
    QLocale::setDefault(QLocale(QLocale::Polish, QLocale::Poland));
    test_run_status_and_terminal_transition();
    test_history_validation_and_reset();
    if (failures == 0) {
        std::cout << "status model tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
