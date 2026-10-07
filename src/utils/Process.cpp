#include "Process.hpp"

#include <cstdlib>
#include <filesystem>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <map>
#else
#include <sys/wait.h>
#endif

namespace fin {
namespace {

std::string quote(const std::string& arg) {
#ifdef _WIN32
    // Microsoft CRT command-line decoding: double backslashes before a quote
    // and before the closing quote, and escape each literal quote.
    std::string result = "\"";
    size_t slashes = 0;
    for (char c : arg) {
        if (c == '\\') { ++slashes; continue; }
        result.append(slashes * (c == '"' ? 2 : 1), '\\');
        slashes = 0;
        if (c == '"') result += '\\';
        result += c;
    }
    result.append(slashes * 2, '\\');
    return result + '"';
#else
    std::string result = "'";
    for (char c : arg) result += c == '\'' ? "'\\''" : std::string(1, c);
    return result + "'";
#endif
}

// Splits FIN_LDFLAGS the way the pre-port shell line consumed it: whitespace
// separates flags. Deliberately dumb -- quoted spaces never survived that line
// either -- and each flag becomes one argv element now that no shell runs.
std::vector<std::string> splitFlags(const char* text) {
    std::vector<std::string> flags;
    std::istringstream stream(text);
    std::string flag;
    while (stream >> flag) flags.push_back(flag);
    return flags;
}

// What the pre-port Driver::runLinker appended after `-o <output>`: the
// self-host compiler's object calls the LLVM C API, so the link needs libLLVM.
std::vector<std::string> defaultLinkLibs() {
#ifdef FIN_LLVM_MAJOR
#define FIN_STRINGIFY_HELPER(x) #x
#define FIN_STRINGIFY(x) FIN_STRINGIFY_HELPER(x)
    const std::string llvmLib = "-lLLVM-" FIN_STRINGIFY(FIN_LLVM_MAJOR);
#undef FIN_STRINGIFY
#undef FIN_STRINGIFY_HELPER
#else
    const std::string llvmLib = "-lLLVM-22";
#endif
#ifdef __APPLE__
    // Apple ld(1) rejects the GNU --as-needed option, and Homebrew's keg
    // ships libLLVM.dylib rather than libLLVM-<major>: -L<keg> -lLLVM -lm,
    // the same line the stage driver emits (finc/driver.fin). Without a keg
    // there is no working line; keep -lLLVM unqualified so ld names the
    // missing library instead of linking against a guessed path.
    const std::string keg = brewLlvmLibDir();
    if (!keg.empty()) return {"-L" + keg, "-lLLVM", "-lm"};
    return {"-lLLVM", "-lm"};
#elif defined(_WIN32)
    // Nothing: the clang/MSVC driver links the CRT itself, so GNU
    // `-l`/`-Wl,` flags would break the link rather than help it, and a
    // plain Fin object references no LLVM symbols (proven by
    // MachineContract.DashOProducesTheNamedExecutable passing on Windows).
    // A self-host object that does call the LLVM C API fails here with
    // unresolved symbols, which Driver::runLinker reports by name with the
    // FIN_CC/FIN_LDFLAGS pointer -- there is no silent default to guess.
    return {};
#else
    return {"-Wl,--as-needed", llvmLib, "-lm"};
#endif
}

}

std::string brewLlvmLibDir() {
#ifdef __APPLE__
    // First Homebrew llvm keg lib dir holding libLLVM.dylib, or "". Mirrors
    // drv_brew_llvm_lib in finc/driver.fin, plus the versioned keg
    // (`llvm@<major>`) the toolchain installs: Apple-silicon brew lives
    // under /opt/homebrew, Intel under /usr/local.
    std::vector<std::string> kegs;
#ifdef FIN_LLVM_MAJOR
    for (const char* root : {"/opt/homebrew", "/usr/local"})
        kegs.push_back(std::string(root) + "/opt/llvm@" +
                       std::to_string(FIN_LLVM_MAJOR) + "/lib");
#endif
    kegs.push_back("/opt/homebrew/opt/llvm/lib");
    kegs.push_back("/usr/local/opt/llvm/lib");
    for (const auto& dir : kegs) {
        std::error_code ec;
        if (std::filesystem::exists(dir + "/libLLVM.dylib", ec)) return dir;
    }
#endif
    return "";
}

int runProcess(const std::vector<std::string>& args,
               const std::string& out, const std::string& err,
               const std::vector<std::pair<std::string, std::string>>& env) {
    if (args.empty()) return -1;
    std::string command;
    for (const auto& arg : args) {
        if (!command.empty()) command += ' ';
        command += quote(arg);
    }
#ifdef _WIN32
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    auto open = [&](const std::string& path) {
        return CreateFileA(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                           &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    };
    if (!out.empty()) startup.hStdOutput = open(out);
    if (!err.empty()) startup.hStdError = err == out ? startup.hStdOutput : open(err);
    auto close = [&] {
        if (!out.empty() && startup.hStdOutput != INVALID_HANDLE_VALUE)
            CloseHandle(startup.hStdOutput);
        if (!err.empty() && err != out && startup.hStdError != INVALID_HANDLE_VALUE)
            CloseHandle(startup.hStdError);
    };
    if (startup.hStdOutput == INVALID_HANDLE_VALUE || startup.hStdError == INVALID_HANDLE_VALUE) {
        close();
        return -1;
    }
    // Windows environment names are case-insensitive; preserve the parent's
    // block, including its hidden drive-current-directory entries.
    struct Less {
        bool operator()(const std::string& a, const std::string& b) const {
            return _stricmp(a.c_str(), b.c_str()) < 0;
        }
    };
    std::map<std::string, std::string, Less> variables;
    LPCH inherited = GetEnvironmentStringsA();
    if (!inherited) { close(); return -1; }
    for (const char* p = inherited; *p; p += std::char_traits<char>::length(p) + 1) {
        std::string entry(p);
        const auto eq = entry.find('=', 1);
        if (eq != std::string::npos) variables[entry.substr(0, eq)] = entry.substr(eq + 1);
    }
    FreeEnvironmentStringsA(inherited);
    for (const auto& [name, value] : env) {
        if (value == "\x01unset") variables.erase(name);
        else variables[name] = value;
    }
    std::string environment;
    for (const auto& [name, value] : variables) environment += name + '=' + value + '\0';
    environment += '\0';
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE,
                                        0, environment.data(), nullptr, &startup, &process);
    close();
    if (!started) return -1;
    DWORD status = 0;
    const bool waited = WaitForSingleObject(process.hProcess, INFINITE) == WAIT_OBJECT_0 &&
                        GetExitCodeProcess(process.hProcess, &status);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return waited ? static_cast<int>(status) : -1;
#else
    std::string prefix = "env";
    for (const auto& [name, value] : env)
        if (value == "\x01unset") prefix += " -u " + quote(name);
    for (const auto& [name, value] : env)
        if (value != "\x01unset") prefix += " " + quote(name + '=' + value);
    command = prefix + " " + command;
    if (!out.empty()) command += " > " + quote(out);
    if (!err.empty()) command += err == out ? " 2>&1" : " 2> " + quote(err);
    const int status = std::system(command.c_str());
    return status != -1 && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

std::vector<std::string> linkCommand(const std::vector<std::string>& objects,
                                     const std::string& output) {
    const char* override = std::getenv("FIN_CC");
#ifdef _WIN32
    const char* defaultDriver = "clang";
#else
    const char* defaultDriver = "cc";
#endif
    std::vector<std::string> args{override && *override ? override : defaultDriver};
    args.insert(args.end(), objects.begin(), objects.end());
    args.insert(args.end(), {"-o", output});
#ifdef _WIN32
    args.insert(args.end(), {"-llegacy_stdio_definitions", "-Xlinker", "/out:" + output});
#endif
    // FIN_LDFLAGS replaces the default library set; an empty or unset value
    // keeps the per-host default from defaultLinkLibs above (empty on
    // Windows: the clang/MSVC driver links the CRT itself).
    const char* ldflags = std::getenv("FIN_LDFLAGS");
    if (ldflags != nullptr && *ldflags != '\0') {
        const auto flags = splitFlags(ldflags);
        args.insert(args.end(), flags.begin(), flags.end());
    } else {
        const auto libs = defaultLinkLibs();
        args.insert(args.end(), libs.begin(), libs.end());
    }
    return args;
}

}
