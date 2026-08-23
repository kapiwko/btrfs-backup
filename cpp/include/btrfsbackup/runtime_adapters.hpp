#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <btrfsbackup/process.hpp>

namespace btrfsbackup {

class ICommandRunner {
public:
    virtual ~ICommandRunner() = default;
    virtual CommandResult run(const std::vector<std::string>& argv) = 0;
};

class SystemCommandRunner final : public ICommandRunner {
public:
    CommandResult run(const std::vector<std::string>& argv) override;
};

std::string capture_command(ICommandRunner& runner, const std::vector<std::string>& argv);

class IFileSystemEffects {
public:
    virtual ~IFileSystemEffects() = default;
    virtual bool exists(const std::filesystem::path& path) = 0;
    virtual bool is_directory(const std::filesystem::path& path) = 0;
    virtual void create_directories(const std::filesystem::path& path) = 0;
    virtual void remove_file(const std::filesystem::path& path) = 0;
    virtual void remove_directory(const std::filesystem::path& path) = 0;
    virtual void remove_tree(const std::filesystem::path& path) = 0;
    virtual void rename_path(const std::filesystem::path& source, const std::filesystem::path& target) = 0;
    virtual std::vector<std::filesystem::path> list_directory(const std::filesystem::path& path) = 0;
};

class StdFileSystemEffects final : public IFileSystemEffects {
public:
    bool exists(const std::filesystem::path& path) override;
    bool is_directory(const std::filesystem::path& path) override;
    void create_directories(const std::filesystem::path& path) override;
    void remove_file(const std::filesystem::path& path) override;
    void remove_directory(const std::filesystem::path& path) override;
    void remove_tree(const std::filesystem::path& path) override;
    void rename_path(const std::filesystem::path& source, const std::filesystem::path& target) override;
    std::vector<std::filesystem::path> list_directory(const std::filesystem::path& path) override;
};

} // namespace btrfsbackup
