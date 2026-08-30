// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <string_view>
#include <type_traits>

#include <state/persistence/PersistentDocumentOperations.hpp>
#include <state/persistence/JsonFileBackupRunCheckpointStore.hpp>

namespace {

class WriterOnly final : public btrfsbackup::state::IAtomicDocumentWriter {
  public:
    void ensure_directory(
        const std::filesystem::path&,
        std::filesystem::perms
    ) override {
    }

    void write_atomically(
        const std::filesystem::path&,
        std::string_view,
        std::filesystem::perms
    ) override {
    }
};

static_assert(!std::is_base_of_v<btrfsbackup::state::IDurableDocumentRemover, WriterOnly>);
static_assert(std::is_constructible_v<
              btrfsbackup::state::JsonFileBackupRunCheckpointStore,
              WriterOnly&,
              std::filesystem::path>);

} // namespace

int main() {
    return 0;
}
