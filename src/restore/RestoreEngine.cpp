// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <restore/RestoreEngine.hpp>

#include <exception>
#include <string>

#include <restore/RestoreError.hpp>

namespace btrfsbackup::restore {

namespace {

void throw_if_cancelled(CancellationToken& cancellation) {
    if (cancellation.cancellation_requested()) {
        throw RestoreError(RestoreErrorCode::Cancelled, "restore was cancelled");
    }
}

} // namespace

RestoreExecutor::RestoreExecutor(IRestoreOperations& operations) : operations_(operations) {
}

RestoreResult RestoreExecutor::execute(
    const RestorePlan& plan,
    CancellationToken& cancellation,
    const RestoreProgressSink& progress
) {
    throw_if_cancelled(cancellation);
    if (operations_.exists(plan.staging) || operations_.exists(plan.previous)) {
        throw RestoreError(RestoreErrorCode::DestinationUnsafe, "restore transaction artifacts already exist");
    }

    bool staging_created = false;
    bool previous_moved = false;
    bool committed = false;
    try {
        if (plan.kind == RestoreKind::Subvolume) {
            operations_.create_subvolume_root(plan.staging);
        } else {
            operations_.prepare_copy_root(plan.source, plan.staging);
        }
        staging_created = true;
        RestoreStatistics statistics = operations_.copy_and_verify(plan.source, plan.staging, cancellation, progress);
        throw_if_cancelled(cancellation);

        if (plan.kind == RestoreKind::Drill) {
            operations_.remove_owned_tree(plan.staging);
            return RestoreResult{plan.transaction_id, statistics, true, false};
        }

        if (operations_.exists(plan.destination)) {
            if (plan.existing_destination != ExistingDestinationPolicy::Replace) {
                throw RestoreError(RestoreErrorCode::DestinationExists, "restore destination appeared before commit");
            }
            operations_.move(plan.destination, plan.previous);
            previous_moved = true;
        }
        operations_.move(plan.staging, plan.destination);
        staging_created = false;
        committed = true;
        if (previous_moved) {
            operations_.remove_owned_tree(plan.previous);
        }
        return RestoreResult{plan.transaction_id, statistics, false, true};
    } catch (...) {
        const std::exception_ptr failure = std::current_exception();
        std::string rollback_error;
        try {
            if (committed && operations_.exists(plan.destination)) {
                operations_.remove_owned_tree(plan.destination);
            }
            if (previous_moved && operations_.exists(plan.previous)) {
                operations_.move(plan.previous, plan.destination);
            }
            if (staging_created && operations_.exists(plan.staging)) {
                operations_.remove_owned_tree(plan.staging);
            }
        } catch (const std::exception& error) {
            rollback_error = error.what();
        }
        if (!rollback_error.empty()) {
            throw RestoreError(RestoreErrorCode::RollbackIncomplete, "restore rollback incomplete: " + rollback_error);
        }
        std::rethrow_exception(failure);
    }
}

} // namespace btrfsbackup::restore
