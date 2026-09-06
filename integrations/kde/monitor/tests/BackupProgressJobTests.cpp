// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BackupProgressJob.hpp"
#include "ManagerApi.hpp"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QPointer>

#include <iostream>

namespace {

using btrfsbackup::kde::monitor::BackupProgressJob;

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "not ok - " << message << '\n';
        ++failures;
    }
}

void test_progress_and_cancellation() {
    int cancellation_requests = 0;
    int descriptions = 0;
    int speed_updates = 0;
    unsigned long last_speed = 0;
    int results = 0;
    BackupProgressJob job(
        QStringLiteral("default"),
        QStringLiteral("run-1"),
        QStringLiteral("Default backup"),
        QStringLiteral("backup"),
        [&](const QString& profile_id, const QString& run_id) {
            if (profile_id == QStringLiteral("default") && run_id == QStringLiteral("run-1")) {
                ++cancellation_requests;
            }
        }
    );
    job.setAutoDelete(false);
    QObject::connect(&job, &KJob::description, [&descriptions]() {
        ++descriptions;
    });
    QObject::connect(&job, &KJob::speed, [&](KJob*, unsigned long speed) {
        ++speed_updates;
        last_speed = speed;
    });
    QObject::connect(&job, &KJob::result, [&results]() {
        ++results;
    });

    job.start();
    job.update(
        42,
        2048,
        true,
        QStringLiteral("Transferring backup data"),
        QStringLiteral("Home"),
        QStringLiteral("Backup disk")
    );
    QCoreApplication::processEvents();

    expect(job.percent() == 42, "job exposes manager progress");
    expect(job.capabilities().testFlag(KJob::Killable), "job is killable when the run can be cancelled");
    expect(descriptions >= 1, "job publishes its Plasma description");
    expect(speed_updates == 1 && last_speed == 2048, "job publishes transfer speed");
    expect(!job.kill(KJob::EmitResult), "cancellation request does not terminate the progress job");
    expect(cancellation_requests == 1, "job cancellation contains profile and run identity");
    expect(job.cancellation_requested(), "job exposes the pending cancellation state");
    expect(!job.capabilities().testFlag(KJob::Killable), "pending cancellation cannot be requested twice");
    expect(results == 0, "requesting cancellation does not publish a terminal result");

    job.update(43, 1024, true, QStringLiteral("Transferring backup data"), {}, {});
    expect(!job.capabilities().testFlag(KJob::Killable), "status refresh does not clear pending cancellation");
    job.cancellation_rejected();
    expect(job.capabilities().testFlag(KJob::Killable), "rejected cancellation can be retried");

    expect(!job.kill(KJob::EmitResult), "retried cancellation remains asynchronous");
    expect(cancellation_requests == 2, "rejected cancellation dispatches again when retried");
    job.finish_cancelled();
    expect(results == 1 && job.error() == KJob::KilledJobError, "manager terminal state finishes the job as cancelled");
}

void test_cancellation_capability_gate() {
    int cancellation_requests = 0;
    BackupProgressJob job(
        QStringLiteral("default"),
        QStringLiteral("run-no-cancel"),
        QStringLiteral("Default backup"),
        QStringLiteral("backup"),
        [&](const QString&, const QString&) {
            ++cancellation_requests;
        }
    );
    job.setAutoDelete(false);
    job.start();
    job.update(10, 0, false, QStringLiteral("Preparing backup"), {}, {});

    expect(!job.capabilities().testFlag(KJob::Killable), "job hides unsupported cancellation");
    expect(!job.kill(KJob::EmitResult), "unsupported cancellation is rejected locally");
    expect(cancellation_requests == 0, "unsupported cancellation is not dispatched");
}

void test_success() {
    int results = 0;
    BackupProgressJob job(
        QStringLiteral("archive"),
        QStringLiteral("run-2"),
        QStringLiteral("Archive"),
        QStringLiteral("backup"),
        [](const QString&, const QString&) {}
    );
    job.setAutoDelete(false);
    QObject::connect(&job, &KJob::result, [&results]() {
        ++results;
    });
    job.start();
    job.update(75, 0, false, QStringLiteral("Finalizing backup"), {}, {});
    job.finish_successfully();

    expect(results == 1 && job.error() == KJob::NoError, "successful run finishes without an error");
    expect(job.percent() == 100, "successful run reaches 100 percent");
}

void test_target_validation_has_distinct_description() {
    QString title;
    BackupProgressJob job(
        QStringLiteral("default"),
        QStringLiteral("validation-1"),
        QStringLiteral("Home"),
        QStringLiteral("target-validation"),
        [](const QString&, const QString&) {}
    );
    job.setAutoDelete(false);
    QObject::connect(
        &job,
        &KJob::description,
        [&](KJob*, const QString& value) { title = value; }
    );
    job.start();
    job.update(0, 0, true, QStringLiteral("Validating backup target"), {}, QStringLiteral("Backup disk"));
    QCoreApplication::processEvents();
    expect(title == QStringLiteral("Checking backup target Home"), "target validation has a distinct progress title");
    job.finish_successfully();
}

void test_stopping_tracking_is_not_a_terminal_result() {
    int results = 0;
    auto* job = new BackupProgressJob(
        QStringLiteral("default"),
        QStringLiteral("run-3"),
        QStringLiteral("Default backup"),
        QStringLiteral("backup"),
        [](const QString&, const QString&) {}
    );
    QPointer<BackupProgressJob> tracked_job(job);
    job->setAutoDelete(false);
    QObject::connect(job, &KJob::result, [&results]() {
        ++results;
    });

    job->start();
    job->stop_tracking();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    expect(results == 0, "stopping local tracking does not publish a terminal result");
    expect(tracked_job.isNull(), "stopped local tracking releases the progress job");
}

void test_shared_manager_protocol() {
    const auto capabilities = btrfsbackup::kde::parse_capabilities(
        QStringLiteral(R"({"apiMajor":1,"publicStatusSchemaVersion":1,"historySchemaVersion":1,"features":["cancel-backup","change-signals"]})")
    );
    expect(
        capabilities.has_value() && capabilities->api_major == 1 &&
            capabilities->features.contains(QLatin1String(btrfsbackup::manager_protocol::feature::cancel_backup)),
        "shared client decodes manager capabilities"
    );

    const auto profiles = btrfsbackup::kde::parse_profiles(
        QStringLiteral(R"([{"profileId":"default","name":"Default","targetName":"Disk","sources":[{"id":"home","name":"Home"}]}])")
    );
    expect(
        profiles.has_value() && profiles->size() == 1 && profiles->front().id == QStringLiteral("default"),
        "shared client decodes profile summaries"
    );

    const auto status = btrfsbackup::kde::parse_status(QStringLiteral(
        R"({"schemaVersion":1,"runId":"run-1","operationKind":"backup","state":"running","phase":"transfer","activity":"transferring","canCancel":true,"errorCode":"","sourceName":"Home","targetName":"Disk","speedBps":2048,"etaSeconds":60,"sourceProgress":50,"overallProgress":25,"progressAccuracy":"estimated","sourceIndex":1,"sourceCount":2,"startedAt":"2026-08-29T15:00:00Z","updatedAt":"2026-08-29T16:00:00Z","lastSuccessAt":"2026-08-25T10:00:00Z","lastAttemptAt":"2026-08-29T16:00:00Z","lastAttemptState":"failed"})"
    ));
    expect(
        status.has_value() && status->run_id == QStringLiteral("run-1") && status->operation_kind == QStringLiteral("backup") &&
            status->speed_bps == 2048 && status->last_attempt_state == QStringLiteral("failed") &&
            btrfsbackup::kde::active_run_state(status->state),
        "shared client decodes active run status"
    );
    expect(
        !btrfsbackup::kde::parse_status(QStringLiteral(R"({"schemaVersion":2})")).has_value(),
        "shared client rejects incompatible status schemas"
    );
    expect(
        !btrfsbackup::kde::parse_status(QStringLiteral(
                                            R"({"schemaVersion":1,"lastSuccessAt":"","lastAttemptAt":""})"
                                        ))
             .has_value(),
        "shared client rejects incomplete backup summary"
    );

    const auto operation = btrfsbackup::kde::parse_operation_result(QStringLiteral(
        R"({"schemaVersion":1,"operation":"cancel-backup","operationId":"op-1","profileId":"default","runId":"run-1","accepted":true})"
    ));
    expect(
        operation.has_value() && operation->accepted && operation->run_id == QStringLiteral("run-1"),
        "shared client decodes accepted operation results"
    );
    expect(
        !btrfsbackup::kde::parse_operation_result(QStringLiteral(
                                                     R"({"schemaVersion":1,"accepted":true})"
                                                 ))
             .has_value(),
        "shared client rejects incomplete operation results"
    );
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    KLocalizedString::setApplicationDomain("plasma_applet_org.btrfsbackup.plasmoid");
    test_progress_and_cancellation();
    test_cancellation_capability_gate();
    test_success();
    test_target_validation_has_distinct_description();
    test_stopping_tracking_is_not_a_terminal_result();
    test_shared_manager_protocol();
    if (failures == 0) {
        std::cout << "ok - Plasma progress job tests\n";
    }
    return failures == 0 ? 0 : 1;
}
