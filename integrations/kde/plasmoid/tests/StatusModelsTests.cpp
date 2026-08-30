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
        "schemaVersion": 3,
        "runId": "run-1",
        "state": "%1",
        "phase": "transferring",
        "activity": "transferring",
        "canCancel": %2,
        "errorCode": "",
        "sourceName": "Home",
        "targetName": "Backup disk",
        "speedBps": 1024,
        "etaSeconds": 20,
        "sourceProgress": 30,
        "overallProgress": 40,
        "progressAccuracy": "estimated"
    })")
        .arg(state, can_cancel ? QStringLiteral("true") : QStringLiteral("false"));
}

void test_run_status_and_terminal_transition() {
    RunStatusModel model;
    int finished = 0;
    QObject::connect(&model, &RunStatusModel::activeRunFinished, [&finished]() { ++finished; });
    model.setCancelSupported(true);

    expect(model.apply(run_payload(QStringLiteral("running"), true)), "running status was rejected");
    expect(model.canCancel(), "cancellable status was not exposed");
    expect(model.overallProgress() == 40 && model.speedBps() == 1024, "run progress was not applied");
    expect(model.speedText() == QStringLiteral("1,0 KiB/s"), "transfer rate was not localized");
    expect(model.apply(run_payload(QStringLiteral("succeeded"), false)), "terminal status was rejected");
    expect(finished == 1, "active-to-terminal transition was not reported exactly once");

    model.setCancelSupported(false);
    expect(!model.canCancel(), "cancel capability was ignored");
}

void test_history_validation_and_reset() {
    BackupHistoryModel model;
    expect(
        model.apply(QStringLiteral(R"([{
            "state": "succeeded",
            "errorCode": "",
            "sourceName": "Home",
            "targetName": "Backup disk",
            "finishedAt": "2026-08-30T12:34:56Z",
            "overallProgress": 100
        }])")),
        "history was rejected"
    );
    expect(model.entries().size() == 1, "history entry was not applied");
    expect(!model.apply(QStringLiteral("[null]")), "invalid history entry was accepted");
    expect(model.entries().size() == 1, "rejected history changed the model");
    model.reset();
    expect(model.entries().isEmpty(), "history reset retained entries");
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
