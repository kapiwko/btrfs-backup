// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

#include <state/document/RunStatusDocumentCodec.hpp>

namespace btrfsbackup::state {
namespace document {

inline constexpr std::size_t maximum_run_document_size = 1024 * 1024;

struct PrivateRunHistoryDocument {
    PrivateRunHistoryV2 history;
    std::string content;
    std::filesystem::path source;
};

class LatestRunHistoryDocumentReader {
  public:
    [[nodiscard]] std::optional<PrivateRunHistoryDocument> read(
        const std::filesystem::path& history_directory,
        std::size_t maximum_size = maximum_run_document_size
    ) const;
};

} // namespace document
} // namespace btrfsbackup::state
