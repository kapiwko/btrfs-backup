#include <btrfsbackup/process.hpp>

#include <sys/wait.h>
#include <unistd.h>

#include <vector>

#include <btrfsbackup/errors.hpp>

namespace btrfsbackup {

std::string run_capture(const std::vector<std::string>& argv) {
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        throw ValidationError("cannot create pipe");
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        throw ValidationError("cannot fork");
    }
    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        std::vector<char*> args;
        args.reserve(argv.size() + 1);
        for (const auto& item : argv) {
            args.push_back(const_cast<char*>(item.c_str()));
        }
        args.push_back(nullptr);
        execvp(args[0], args.data());
        _exit(127);
    }
    close(pipefd[1]);
    std::string output;
    char buffer[4096];
    ssize_t n;
    while ((n = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
        output.append(buffer, static_cast<std::size_t>(n));
    }
    close(pipefd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        throw ValidationError("command failed: " + argv.front());
    }
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }
    return output;
}

} // namespace btrfsbackup
