// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string_view>

namespace btrfsbackup::state {

class IAtomicDocumentWriter {
  public:
    virtual ~IAtomicDocumentWriter() = default;

    virtual void ensure_directory(
        const std::filesystem::path& path,
        std::filesystem::perms permissions
    ) = 0;
    virtual void write_atomically(
        const std::filesystem::path& path,
        std::string_view data,
        std::filesystem::perms permissions
    ) = 0;
};

class IDurableDocumentRemover {
  public:
    virtual ~IDurableDocumentRemover() = default;

    virtual void remove_durably(const std::filesystem::path& path) = 0;
};

class IPersistentDocumentOperations : public IAtomicDocumentWriter,
                                      public IDurableDocumentRemover {
  public:
    ~IPersistentDocumentOperations() override = default;
};

} // namespace btrfsbackup::state
