#include "paths.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

namespace vpl::hardware::paths {
namespace {

std::string trim(std::string value)
{
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

}  // namespace

bool exists(const std::string &path)
{
    struct stat state {};
    return !path.empty() && stat(path.c_str(), &state) == 0;
}

bool executable(const std::string &path)
{
    return !path.empty() && access(path.c_str(), X_OK) == 0;
}

std::string read(const std::string &path)
{
    std::ifstream stream(path);
    if (!stream)
        return {};
    std::ostringstream contents;
    contents << stream.rdbuf();
    return trim(contents.str());
}

bool write(const std::string &path, const std::string &value)
{
    std::ofstream stream(path);
    if (!stream)
        return false;
    stream << value << '\n';
    return stream.good();
}

bool write_atomically(const std::string &path, const std::string &value)
{
    const std::size_t separator = path.rfind('/');
    if (separator == std::string::npos)
        return false;

    const std::string directory = path.substr(0, separator);
    const std::string temporary = directory + "/." + path.substr(separator + 1) + ".tmp";
    const int descriptor = open(temporary.c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0644);
    if (descriptor < 0)
        return false;

    const char *cursor = value.data();
    std::size_t remaining = value.size();
    while (remaining > 0) {
        const ssize_t written = ::write(descriptor, cursor, remaining);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            close(descriptor);
            unlink(temporary.c_str());
            return false;
        }
        cursor += written;
        remaining -= static_cast<std::size_t>(written);
    }
    if (fsync(descriptor) != 0 || close(descriptor) != 0) {
        unlink(temporary.c_str());
        return false;
    }
    if (rename(temporary.c_str(), path.c_str()) != 0) {
        unlink(temporary.c_str());
        return false;
    }
    return true;
}

std::optional<long> read_long(const std::string &path)
{
    const std::string value = read(path);
    if (value.empty())
        return std::nullopt;
    char *end = nullptr;
    errno = 0;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    return errno == 0 && end && *end == '\0' ? std::optional<long>(parsed) : std::nullopt;
}

std::vector<std::string> children(const std::string &path)
{
    std::vector<std::string> values;
    DIR *directory = opendir(path.c_str());
    if (!directory)
        return values;
    while (dirent *entry = readdir(directory)) {
        if (entry->d_name[0] != '.')
            values.emplace_back(entry->d_name);
    }
    closedir(directory);
    return values;
}

int run(const std::vector<std::string> &arguments)
{
    if (arguments.empty())
        return 127;
    pid_t child = fork();
    if (child < 0)
        return 127;
    if (child == 0) {
        std::vector<char *> argv;
        argv.reserve(arguments.size() + 1);
        for (const std::string &argument : arguments)
            argv.push_back(const_cast<char *>(argument.c_str()));
        argv.push_back(nullptr);
        execv(argv[0], argv.data());
        _exit(127);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 127;
}

int run_quietly(const std::vector<std::string> &arguments)
{
    if (arguments.empty())
        return 127;
    pid_t child = fork();
    if (child < 0)
        return 127;
    if (child == 0) {
        const int null_device = open("/dev/null", O_WRONLY | O_CLOEXEC);
        if (null_device >= 0) {
            dup2(null_device, STDOUT_FILENO);
            dup2(null_device, STDERR_FILENO);
            if (null_device > STDERR_FILENO)
                close(null_device);
        }
        std::vector<char *> argv;
        argv.reserve(arguments.size() + 1);
        for (const std::string &argument : arguments)
            argv.push_back(const_cast<char *>(argument.c_str()));
        argv.push_back(nullptr);
        execv(argv[0], argv.data());
        _exit(127);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 127;
}

std::string run_capture(const std::vector<std::string> &arguments, int *exit_status)
{
    if (exit_status != nullptr)
        *exit_status = 127;
    if (arguments.empty())
        return {};
    int pipe_fds[2] {};
    if (pipe(pipe_fds) != 0)
        return {};
    const pid_t child = fork();
    if (child < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return {};
    }
    if (child == 0) {
        close(pipe_fds[0]);
        (void)dup2(pipe_fds[1], STDOUT_FILENO);
        (void)dup2(pipe_fds[1], STDERR_FILENO);
        if (pipe_fds[1] > STDERR_FILENO)
            close(pipe_fds[1]);
        std::vector<char *> argv;
        argv.reserve(arguments.size() + 1);
        for (const std::string &argument : arguments)
            argv.push_back(const_cast<char *>(argument.c_str()));
        argv.push_back(nullptr);
        execv(argv[0], argv.data());
        _exit(127);
    }

    close(pipe_fds[1]);
    constexpr std::size_t maximum_output = 4096;
    std::string output;
    std::array<char, 512> buffer {};
    for (;;) {
        const ssize_t count = ::read(pipe_fds[0], buffer.data(), buffer.size());
        if (count == 0)
            break;
        if (count < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        const std::size_t remaining = maximum_output - output.size();
        output.append(buffer.data(), std::min(remaining, static_cast<std::size_t>(count)));
    }
    close(pipe_fds[0]);
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    if (exit_status != nullptr)
        *exit_status = WIFEXITED(status) ? WEXITSTATUS(status) : 127;
    return output;
}

bool service_active(const std::string &unit)
{
    // Buildroot's systemd tools may live under /bin rather than /usr/bin.
    // Resolve the installed location instead of reporting an active service
    // as absent merely because the distribution did not create a usrmerge
    // compatibility path.
    const char *systemctl = executable("/bin/systemctl") ? "/bin/systemctl" : "/usr/bin/systemctl";
    return run_quietly({systemctl, "is-active", "--quiet", unit}) == 0;
}

}  // namespace vpl::hardware::paths
