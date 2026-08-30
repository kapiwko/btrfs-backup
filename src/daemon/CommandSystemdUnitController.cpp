// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/CommandSystemdUnitController.hpp>

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <vector>

namespace btrfsbackup::daemon {

namespace {

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool contains(const std::string& text, const char* fragment) {
    return text.find(fragment) != std::string::npos;
}

SystemdJobFailure classify_failure(const btrfsbackup::backup::CommandResult& result) {
    if (result.cancelled)
        return SystemdJobFailure::Cancelled;
    if (result.timed_out)
        return SystemdJobFailure::TimedOut;

    const std::string output = lowercase(result.output);
    if (contains(output, "job canceled") || contains(output, "job cancelled"))
        return SystemdJobFailure::Cancelled;
    if (contains(output, "timed out"))
        return SystemdJobFailure::TimedOut;
    if (contains(output, "could not be found") || contains(output, "unit not found") ||
        contains(output, "no such unit") || contains(output, "unit is not loaded"))
        return SystemdJobFailure::UnitNotFound;
    if (contains(output, "already running") || contains(output, "already in progress"))
        return SystemdJobFailure::JobAlreadyRunning;
    if (contains(output, "conflicting job") || contains(output, "transaction is destructive"))
        return SystemdJobFailure::JobConflict;
    if (contains(output, "access denied") || contains(output, "authentication is required") ||
        contains(output, "failed to connect to bus") || contains(output, "operation refused"))
        return SystemdJobFailure::ManagerRejected;
    return SystemdJobFailure::UnitFailed;
}

std::optional<int> unit_exit_status(
    btrfsbackup::backup::ICommandRunner& commands,
    const std::string& unit
) {
    const auto status = commands.run({"systemctl", "show", "--property=ExecMainStatus", "--value", unit});
    if (status.exit_code != 0)
        return std::nullopt;
    try {
        std::size_t parsed = 0;
        const int value = std::stoi(status.output, &parsed);
        if (parsed == 0)
            return std::nullopt;
        return value;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

SystemdJobError job_error(
    btrfsbackup::backup::ICommandRunner& commands,
    const btrfsbackup::backup::CommandResult& result,
    const std::string& unit,
    bool inspect_exit_status
) {
    std::optional<int> exit_status;
    if (inspect_exit_status) {
        exit_status = unit_exit_status(commands, unit);
        if (!exit_status)
            exit_status = result.exit_code;
    }
    return {
        .failure = classify_failure(result),
        .detail = result.output,
        .unit_exit_status = exit_status,
    };
}

btrfsbackup::backup::ControlledCommandOptions options(std::chrono::milliseconds timeout) {
    btrfsbackup::backup::ControlledCommandOptions result;
    result.timeout = timeout;
    result.environment.emplace("LC_ALL", "C");
    return result;
}

std::vector<std::string> transient_command(const TransientUnitRequest& request) {
    std::vector<std::string> command{
        "systemd-run",
        "--quiet",
        request.wait ? "--wait" : "--no-block",
        "--collect",
        "--unit=" + request.unit,
    };
    for (const auto& property : request.properties)
        command.push_back("--property=" + property);
    for (const auto& environment : request.environment)
        command.push_back("--setenv=" + environment);
    command.insert(command.end(), request.command.begin(), request.command.end());
    return command;
}

} // namespace

CommandSystemdUnitController::CommandSystemdUnitController(
    btrfsbackup::backup::ICommandRunner& commands
) : commands_(commands) {
}

StartJobResult CommandSystemdUnitController::start_unit(const StartUnitRequest& request) {
    const auto result = commands_.run_controlled(
        {"systemctl", "start", request.unit},
        options(request.timeout)
    );
    if (result.exit_code == 0 && !result.cancelled && !result.timed_out)
        return {};
    return {.error = job_error(commands_, result, request.unit, true)};
}

StopJobResult CommandSystemdUnitController::stop_unit(const StopUnitRequest& request) {
    const auto result = commands_.run_controlled(
        {"systemctl", "stop", request.unit},
        options(request.timeout)
    );
    if (result.exit_code == 0 && !result.cancelled && !result.timed_out)
        return {};
    return {.error = job_error(commands_, result, request.unit, false)};
}

TransientJobResult CommandSystemdUnitController::start_transient_unit(
    const TransientUnitRequest& request
) {
    const auto result = commands_.run_controlled(transient_command(request), options(request.timeout));
    if (result.exit_code == 0 && !result.cancelled && !result.timed_out)
        return {};
    return {.error = job_error(commands_, result, request.unit, request.wait)};
}

} // namespace btrfsbackup::daemon
