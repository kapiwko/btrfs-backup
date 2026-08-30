// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BackupProgressJob.hpp"
#include "ManagerApi.hpp"

#include <KLocalizedString>

#include <QCoreApplication>

#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "not ok - " << message << '\n';
        ++failures;
    }
}

void test_progress_and_cancellation() {
    bool cancellation_requested = false;
    int descriptions = 0;
    int speed_updates = 0;
    unsigned long last_speed = 0;
    int results = 0;
    BackupProgressJob job(
        QStringLiteral("default"),
        QStringLiteral("run-1"),
        QStringLiteral("Default backup"),
        [&](const QString& profile_id, const QString& run_id) {
            cancellation_requested = profile_id == QStringLiteral("default") &&
                run_id == QStringLiteral("run-1");
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
    expect(job.kill(KJob::EmitResult), "killable job accepts cancellation");
    expect(cancellation_requested, "job cancellation contains profile and run identity");
    expect(results == 1 && job.error() == KJob::KilledJobError, "cancelled job finishes as cancelled");
}

void test_success() {
    int results = 0;
    BackupProgressJob job(
        QStringLiteral("archive"),
        QStringLiteral("run-2"),
        QStringLiteral("Archive"),
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

void test_shared_manager_protocol() {
    const auto capabilities = btrfsbackup::kde::parse_capabilities(
        QStringLiteral(R"({"apiMajor":1,"publicStatusSchemaVersion":3,"features":["cancel-backup","change-signals"]})")
    );
    expect(
        capabilities.has_value() && capabilities->api_major == 1 &&
            capabilities->features.contains(QStringLiteral("cancel-backup")),
        "shared client decodes manager capabilities"
    );

    const auto profiles = btrfsbackup::kde::parse_profiles(
        QStringLiteral(R"([{"profileId":"default","name":"Default","targetName":"Disk"}])")
    );
    expect(
        profiles.has_value() && profiles->size() == 1 && profiles->front().id == QStringLiteral("default"),
        "shared client decodes profile summaries"
    );

    const auto status = btrfsbackup::kde::parse_status(QStringLiteral(
        R"({"schemaVersion":3,"runId":"run-1","state":"running","phase":"transfer","activity":"transferring","canCancel":true,"errorCode":"","sourceName":"Home","targetName":"Disk","speedBps":2048,"etaSeconds":60,"sourceProgress":50,"overallProgress":25,"progressAccuracy":"estimated"})"
    ));
    expect(
        status.has_value() && status->run_id == QStringLiteral("run-1") &&
            status->speed_bps == 2048 && btrfsbackup::kde::active_run_state(status->state),
        "shared client decodes active run status"
    );
    expect(
        !btrfsbackup::kde::parse_status(QStringLiteral(R"({"schemaVersion":2})")).has_value(),
        "shared client rejects incompatible status schemas"
    );
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    KLocalizedString::setApplicationDomain("plasma_applet_org.btrfsbackup.plasmoid");
    test_progress_and_cancellation();
    test_success();
    test_shared_manager_protocol();
    if (failures == 0) {
        std::cout << "ok - Plasma progress job tests\n";
    }
    return failures == 0 ? 0 : 1;
}
