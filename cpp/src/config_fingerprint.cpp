#include <btrfsbackup/config_fingerprint.hpp>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/process.hpp>

namespace fs = std::filesystem;

namespace {

std::string arg_value(const std::vector<std::string>& args, std::size_t& index, const std::string& option) {
    if (index + 1 >= args.size()) {
        throw btrfsbackup::ValidationError(option + " requires a value");
    }
    return args[++index];
}

std::string read_file_bytes(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw btrfsbackup::ValidationError("cannot read " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

void append_record(std::string& data, const std::string& key, const std::string& value) {
    data.append(key);
    data.push_back('=');
    data.append(value);
    data.push_back('\0');
}

std::string sha256_bytes(const std::string& data) {
    fs::path pattern = fs::temp_directory_path() / "btrfs-backup-fingerprint.XXXXXX";
    std::string pattern_string = pattern.string();
    std::vector<char> writable(pattern_string.begin(), pattern_string.end());
    writable.push_back('\0');
    int fd = mkstemp(writable.data());
    if (fd < 0) {
        throw btrfsbackup::ValidationError("cannot create temporary fingerprint file");
    }

    fs::path temporary(writable.data());
    try {
        const char* ptr = data.data();
        std::size_t remaining = data.size();
        while (remaining > 0) {
            ssize_t written = write(fd, ptr, remaining);
            if (written < 0) {
                throw btrfsbackup::ValidationError("cannot write " + temporary.string());
            }
            ptr += written;
            remaining -= static_cast<std::size_t>(written);
        }
        close(fd);
        fd = -1;

        std::string output = btrfsbackup::run_capture({"sha256sum", temporary.string()});
        std::error_code ec;
        fs::remove(temporary, ec);
        std::istringstream stream(output);
        std::string digest;
        stream >> digest;
        if (digest.size() != 64) {
            throw btrfsbackup::ValidationError("sha256sum returned an invalid digest");
        }
        return digest;
    } catch (...) {
        if (fd >= 0) {
            close(fd);
        }
        std::error_code ec;
        fs::remove(temporary, ec);
        throw;
    }
}

} // namespace

namespace btrfsbackup {

std::string compute_config_fingerprint(
    const std::string& version,
    const fs::path& config_file,
    const std::vector<fs::path>& source_files
) {
    std::string data;
    append_record(data, "version", version);
    append_record(data, "main", config_file.filename().string());
    data.append(read_file_bytes(config_file));
    data.push_back('\0');

    for (const fs::path& source_file : source_files) {
        append_record(data, "source", source_file.filename().string());
        data.append(read_file_bytes(source_file));
        data.push_back('\0');
    }

    return sha256_bytes(data);
}

void command_config_fingerprint(const std::vector<std::string>& args, std::ostream& output) {
    std::string version;
    fs::path config_file;
    std::vector<fs::path> source_files;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--version") {
            version = arg_value(args, i, arg);
        } else if (arg == "--config") {
            config_file = arg_value(args, i, arg);
        } else if (arg == "--source") {
            source_files.emplace_back(arg_value(args, i, arg));
        } else {
            throw ValidationError("unknown config-fingerprint option: " + arg);
        }
    }

    if (version.empty()) {
        throw ValidationError("config-fingerprint requires --version");
    }
    if (config_file.empty()) {
        throw ValidationError("config-fingerprint requires --config");
    }

    output << compute_config_fingerprint(version, config_file, source_files) << '\n';
}

} // namespace btrfsbackup
