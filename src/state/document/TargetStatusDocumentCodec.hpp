// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <core/RuntimeTime.hpp>

namespace btrfsbackup::state {
namespace document {

enum class TargetSpaceState {
    Normal,
    BelowConfiguredMinimum,
};

struct TargetStorageStatusV1 {
    std::uint64_t capacity_bytes = 0;
    std::uint64_t used_bytes = 0;
    std::uint64_t available_bytes = 0;
    int usage_percent = 0;
    RuntimeTimePoint measured_at;
    bool live = false;
    TargetSpaceState space_state = TargetSpaceState::Normal;
};

struct TargetStatusV1 {
    std::string profile_id;
    std::string target_name;
    std::string state;
    bool connected = false;
    bool unlocked = false;
    bool mounted = false;
    bool safe_to_remove = false;
    std::optional<TargetStorageStatusV1> storage;
};

class TargetStatusDocumentCodec {
  public:
    [[nodiscard]] TargetStatusV1 parse(std::string_view document) const;
    [[nodiscard]] std::optional<TargetStatusV1> try_parse(std::string_view document) const noexcept;
    [[nodiscard]] std::string serialize(const TargetStatusV1& status) const;
};

[[nodiscard]] std::string target_space_state_name(TargetSpaceState state);

} // namespace document
} // namespace btrfsbackup::state
