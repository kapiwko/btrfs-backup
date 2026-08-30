// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <chrono>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <backup/model/BackupRunPlan.hpp>
#include <cli/runner/RunnerPresenter.hpp>
#include <config/model/Json.hpp>

#include "support/TestHelpers.hpp"

namespace fs = std::filesystem;

namespace {

namespace backup = btrfsbackup::backup;
namespace runner = btrfsbackup::cli::runner;
namespace config = btrfsbackup::config;

struct ExpectedAction {
    std::string kind;
    fs::path primary_path;
    fs::path secondary_path;
};

void test_plan_encodes_every_action_kind() {
    const btrfsbackup::SourceId source_id{"root"};
    const btrfsbackup::RunId run_id{"run-1"};
    const fs::path source = "/source";
    const fs::path local_directory = "/snapshots/root";
    const fs::path local_snapshot = local_directory / "root-run-1";
    const fs::path remote_directory = "/target/snapshots/root";
    const fs::path incoming_directory = "/target/.incoming/root";
    const fs::path incoming_run_directory = incoming_directory / "run-1";
    const fs::path received_snapshot = incoming_run_directory / "root-run-1";
    const fs::path final_snapshot = remote_directory / "root-run-1";

    backup::PendingRecoveryPlan recovery{
        .marker_path = "/state/default/pending-root",
        .pending_snapshot_path = local_snapshot,
        .effects = {
            backup::DeletePendingLocalSnapshot{local_snapshot},
            backup::ClearPendingMarker{"/state/default/pending-root"},
        },
        .message = "remove orphan",
    };
    config::ProfileHookCommand hook{
        .program = config::HookProgramPath{"/hooks/snapshot"},
        .arguments = {"--source", "root"},
        .timeout = std::chrono::seconds{12},
    };
    backup::RetentionPlan retention{
        .source_id = source_id,
        .keep_count = 3,
    };

    std::vector<backup::BackupRunAction> actions;
    actions.emplace_back(backup::RecoverPendingAction{source_id, recovery});
    actions.emplace_back(backup::CleanupIncomingAction{source_id, incoming_directory});
    actions.emplace_back(backup::RunHookAction{source_id, backup::HookPhase::BeforeSnapshot, hook});
    actions.emplace_back(backup::CreateSnapshotAction{
        source_id,
        source,
        local_directory,
        local_snapshot,
        final_snapshot,
        "/state/default",
        run_id,
    });
    actions.emplace_back(backup::RunHookAction{source_id, backup::HookPhase::AfterSnapshot, hook});
    actions.emplace_back(backup::SendReceiveAction{
        source_id,
        local_snapshot,
        std::nullopt,
        remote_directory,
        incoming_run_directory,
    });
    actions.emplace_back(backup::VerifyReceivedAction{source_id, local_snapshot, received_snapshot});
    actions.emplace_back(backup::CommitReceivedAction{source_id, local_snapshot, received_snapshot, final_snapshot});
    actions.emplace_back(backup::ApplyRemoteRetentionAction{source_id, retention});
    actions.emplace_back(backup::ApplyLocalRetentionAction{source_id, retention});
    actions.emplace_back(backup::CleanupSourceAction{
        source_id,
        received_snapshot,
        incoming_run_directory,
        "/state/default/pending-root.json",
        "/state/default",
    });

    backup::BackupRunPlan plan{
        .profile_id = btrfsbackup::ProfileId{"default"},
        .run_id = run_id,
        .target_mount_point = "/target",
        .sources = {backup::BackupSourceRunPlan{source_id, std::move(actions)}},
    };

    std::ostringstream output;
    const int exit_code = runner::present_runner_plan(plan, output);
    const config::Json document = config::Json::parse(output.str());
    const config::Json& encoded = document.at("sources").at(0).at("actions");
    const std::array expected{
        ExpectedAction{"recover-pending", local_snapshot, {}},
        ExpectedAction{"cleanup-incoming", incoming_directory, {}},
        ExpectedAction{"before-snapshot-hook", {}, {}},
        ExpectedAction{"create-snapshot", local_snapshot, source},
        ExpectedAction{"after-snapshot-hook", {}, {}},
        ExpectedAction{"send-receive", local_snapshot, incoming_run_directory},
        ExpectedAction{"verify-received", received_snapshot, local_snapshot},
        ExpectedAction{"commit-received", received_snapshot, final_snapshot},
        ExpectedAction{"apply-remote-retention", remote_directory, {}},
        ExpectedAction{"apply-local-retention", local_directory, {}},
        ExpectedAction{"cleanup-source", {}, {}},
    };

    test_helpers::expect_eq("exit code", std::to_string(exit_code), "0");
    test_helpers::expect_eq("action count", std::to_string(encoded.size()), std::to_string(expected.size()));
    for (std::size_t index = 0; index < expected.size(); ++index) {
        const std::string prefix = "action " + std::to_string(index);
        test_helpers::expect_eq(prefix + " kind", encoded.at(index).at("kind").get<std::string>(), expected[index].kind);
        test_helpers::expect_eq(prefix + " source", encoded.at(index).at("sourceId").get<std::string>(), "root");
        test_helpers::expect_eq(prefix + " primary", encoded.at(index).at("primaryPath").get<std::string>(), expected[index].primary_path.string());
        test_helpers::expect_eq(prefix + " secondary", encoded.at(index).at("secondaryPath").get<std::string>(), expected[index].secondary_path.string());
    }

    for (const std::size_t index : {std::size_t{2}, std::size_t{4}}) {
        const config::Json& encoded_hook = encoded.at(index).at("hook");
        test_helpers::expect_eq("hook program", encoded_hook.at("program").get<std::string>(), "/hooks/snapshot");
        test_helpers::expect_eq("hook timeout", std::to_string(encoded_hook.at("timeoutSeconds").get<int>()), "12");
    }
}

} // namespace

int main() {
    test_plan_encodes_every_action_kind();
    return test_helpers::finish("runner JSON codec tests");
}
