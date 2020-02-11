#include <FPR/debug/assert.h>

#include <llvm/Support/CommandLine.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>

namespace fs = std::filesystem;

namespace option {
llvm::cl::opt<std::string> config{
    "c",
    llvm::cl::desc("Config file location."),
    llvm::cl::init("../config.txt")};
}

struct Config {
    std::set<fs::path> srcs;
    fs::path dest;

    std::set<fs::path> blacklist;
    std::string rsync_flags;
};

const Config init_from_config(const fs::path &config) {
    Config ret;
    fpr::asrt(
        fs::exists(config),
        [&config]() {
            fpr::err() << "Config file does not exist: " << config << "\n";
        });

    std::ifstream ifs{config};
    fpr::asrt(
        ifs.is_open(),
        [&config]() {
            fpr::err() << "Error while opening file: " << config << "\n";
        });

    enum class State {
        NONE,
        DEST,
        DIRS,
        BLACKLIST,
        RSYNC_FLAGS,
    };

    std::string buf;
    State state{State::NONE};
    while (std::getline(ifs, buf)) {
        if (buf == "dest:") {
            state = State::DEST;
            continue;
        } else if (buf == "dirs:") {
            state = State::DIRS;
            continue;
        } else if (buf == "blacklist:") {
            state = State::BLACKLIST;
            continue;
        } else if (buf == "rsync_flags:") {
            state = State::RSYNC_FLAGS;
            continue;
        }

        if (state == State::RSYNC_FLAGS) {
            ret.rsync_flags = buf;
            continue;
        }

        const fs::path path{fs::absolute(fs::path{buf})};
        fpr::asrt(
            fs::exists(path),
            [&path]() { fpr::err() << "Does not exist: " << path << "\n"; });
        switch (state) {
        default:
        case State::NONE:
            fpr::asrt(false, []() { fpr::err() << "Parsing error...\n"; });
            break;
        case State::DEST:
            ret.dest = path;
            break;
        case State::DIRS:
            ret.srcs.insert(path);
            break;
        case State::BLACKLIST:
            ret.blacklist.insert(path);
            break;
        }
    }

    return ret;
}

void format_size(fpr::Logger &os, ulong bytes) {
    os << std::fixed << std::setprecision(2);
    if (bytes < 1000U) {
        os << bytes << " B";
    } else if (bytes < 1000000U) {
        os << (float)bytes / 1000U << " KB";
    } else if (bytes < 1000000000U) {
        os << (float)bytes / 1000000U << " MB";
    } else {
        os << (float)bytes / 1000000000U << " GB";
    }
}

struct BackupInfo {
    std::set<fs::path> items;
    ulong total_filesize{0UL};

    void print();
};

bool check_dir(const fs::path &path, BackupInfo &bi, const std::set<fs::path> &blacklist) {
    auto dir_file_size{0UL};
    std::set<fs::path> send_dirs;
    auto mixed_flag{false};

    if (blacklist.count(path)) {
        // Ignore if blacklisted
        return true;
    }

    for (auto &c : fs::directory_iterator(path)) {
        if (c.is_symlink()) {
            // Ignore symlinks
            continue;
        }

        if (c.is_directory()) {
            // Check if this is a '.git' file
            auto &path{c.path()};
            if (path.stem() == ".git") {
                // fpr::info() << "git: " << path << std::endl;
                return true;
            }
            if (check_dir(path, bi, blacklist)) {
                mixed_flag = true;
            } else {
                send_dirs.insert(path);
            }
        } else {
            auto &f{c.path()};
            auto file_size{fs::file_size(f)};
            fpr::asrt(
                file_size != -1,
                [&f]() {
                    fpr::err() << "Failed to get filesize: " << f << "\n";
                });
            dir_file_size += file_size;
        }
    }
    static const auto large_warning_size{500000000U};
    if (dir_file_size > large_warning_size) {
        auto warn{fpr::warn()};
        format_size(warn, dir_file_size);
        warn << " > ";
        format_size(warn, large_warning_size);
        warn << ": " << path << "\n";
    }
    if (mixed_flag) {
        bi.items.insert(send_dirs.begin(), send_dirs.end());
        for (auto &c : fs::directory_iterator(path)) {
            if (!c.is_symlink() && c.is_regular_file()) {
                bi.items.insert(c);
            }
        }
        return true;
    }
    bi.total_filesize += dir_file_size;
    return false;
}

const BackupInfo prepare_backup(const Config &config) {
    BackupInfo bi;
    for (auto src : config.srcs) {
        if (fs::is_directory(src)) {
            if (!check_dir(src, bi, config.blacklist)) {
                bi.items.insert(src);
            }
        } else {
            bi.items.insert(src);
        }
    }
    return bi;
}

void BackupInfo::print() {
    auto info{fpr::info()};
    info << "Backed  items:\n";
    for (auto i : items) {
        info << i << "\n";
    }
    info << "Total Size: ";
    format_size(info, total_filesize);
    info << "\n";
}

int main(int argc, char **argv) {
    static const std::string overview{"Automated backup script using rsync."};
    llvm::cl::ParseCommandLineOptions(argc, argv, overview);

    auto config{init_from_config(option::config.getValue())};

    std::time_t time{std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())};
    struct tm *ti{localtime(&time)};
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m%d-%H%M%S", ti);
    config.dest /= std::string{buf};
    config.dest /= "";
    fpr::grn(true) << "Destination: " << config.dest << "\n";

    auto bi{prepare_backup(config)};
    bi.print();

    for (auto i : bi.items) {
        auto cmd{"rsync " + config.rsync_flags + " \"" + i.string() + "\" \"" + config.dest.string() + "\""};
        auto status{::system(cmd.c_str())};
        fpr::asrt(
            status == 0,
            [&cmd, &status]() { fpr::err() << "Exited with error code " << status << ": " << cmd << "\n"; });
    }

    return 0;
}