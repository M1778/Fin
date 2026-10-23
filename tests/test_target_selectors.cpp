// SPDX-License-Identifier: GPL-3.0-or-later
//
// Target selectors (ADR 0051, addendum): every source file, and every imported
// module, starts preprocessing with presence-only definitions for the compiler's
// native target -- exactly one of FIN_TARGET_WINDOWS / FIN_TARGET_DARWIN /
// FIN_TARGET_LINUX, and exactly one of FIN_TARGET_X86_64 / FIN_TARGET_ARM64.
//
// The failure this file exists to catch is silent and severe. A selector that
// is not defined leaves a library on its fallback branch: a Windows build links
// POSIX symbols that UCRT does not export, a macOS build picks the Linux
// clock id, and the program compiles and then misbehaves in ways that look
// like unrelated bugs. Nothing in the front end reports it, so the only place
// the mistake can be caught is here.
//
// The selectors describe the target ABI the compiler was built for. The
// compiler is pinned to its host (ADR 0051), so the host that built this test
// binary is the host whose selectors the compiler must publish.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Corpus.hpp"
#include "preprocessor/Preprocessor.hpp"
#include "utils/Process.hpp"

namespace fs = std::filesystem;
using namespace fin::testing;

namespace {

// The selector this host must publish. An unrecognised host has no expected
// answer, so the tests say so instead of guessing.
std::string hostOsSelector() {
#if defined(_WIN32)
    return "FIN_TARGET_WINDOWS";
#elif defined(__APPLE__) && defined(__MACH__)
    return "FIN_TARGET_DARWIN";
#elif defined(__linux__)
    return "FIN_TARGET_LINUX";
#else
    return "";
#endif
}

std::string hostArchSelector() {
#if defined(_M_ARM64) || defined(__aarch64__)
    return "FIN_TARGET_ARM64";
#elif defined(_M_X64) || defined(__x86_64__)
    return "FIN_TARGET_X86_64";
#else
    return "";
#endif
}

const std::vector<std::string>& everySelector() {
    static const std::vector<std::string> selectors = {
        "FIN_TARGET_WINDOWS", "FIN_TARGET_DARWIN", "FIN_TARGET_LINUX",
        "FIN_TARGET_X86_64", "FIN_TARGET_ARM64"};
    return selectors;
}

// Preprocesses a two-branch body and reports which branch survived. This is
// the whole contract of a presence-only definition: the branch is selected,
// and the other one is gone rather than left as dead text.
bool branchTaken(const std::string& selector) {
    fin::Preprocessor pp;
    const std::string out = pp.process("#c_ifdef " + selector +
                                       "\nkept\n#else\ndropped\n#c_endif\n");
    return out.find("kept") != std::string::npos;
}

// The number the probe module reports for the host it was compiled for. The
// module picks its branch with the same selectors the compiler publishes, so
// the answer is only right when the module was preprocessed with them.
int expectedOsCode() {
    const std::string os = hostOsSelector();
    if (os == "FIN_TARGET_LINUX") return 1;
    if (os == "FIN_TARGET_DARWIN") return 2;
    if (os == "FIN_TARGET_WINDOWS") return 3;
    return 0;
}

int expectedArchCode() {
    const std::string arch = hostArchSelector();
    if (arch == "FIN_TARGET_X86_64") return 10;
    if (arch == "FIN_TARGET_ARM64") return 20;
    return 0;
}

// Writes a file under `dir`, creating the parent directory when the name has
// one. Returns nothing: a failed write shows up as a failed compile, and the
// diagnostic says which file was missing.
void writeFile(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(contents.data(), (std::streamsize)contents.size());
}

} // namespace

TEST(TargetSelectors, TheHostOperatingSystemAndArchitectureAreBothDefined) {
    const std::string os = hostOsSelector();
    const std::string arch = hostArchSelector();
    ASSERT_FALSE(os.empty()) << "this host has no known operating-system selector";
    ASSERT_FALSE(arch.empty()) << "this host has no known architecture selector";

    EXPECT_TRUE(branchTaken(os)) << os << " must be defined for the compiler's host";
    EXPECT_TRUE(branchTaken(arch)) << arch << " must be defined for the compiler's host";
}

TEST(TargetSelectors, ExactlyOneOperatingSystemAndOneArchitectureAreDefined) {
    // Two operating-system selectors at once would let a library pick a branch
    // that is not the host's; none at all would leave it on its fallback. Both
    // are the same class of mistake, so both are refused here.
    const std::string os = hostOsSelector();
    const std::string arch = hostArchSelector();

    int osCount = 0;
    int archCount = 0;
    for (const std::string& name : everySelector()) {
        const bool defined = branchTaken(name);
        if (name == os) {
            EXPECT_TRUE(defined) << name << " must be defined on this host";
            osCount++;
        } else if (name == arch) {
            EXPECT_TRUE(defined) << name << " must be defined on this host";
            archCount++;
        } else {
            EXPECT_FALSE(defined) << name << " must not be defined on this host";
        }
    }
    EXPECT_EQ(osCount, 1);
    EXPECT_EQ(archCount, 1);
}

TEST(TargetSelectors, AnImportedModuleSelectsTheSameBranchAsItsImporter) {
    // The loader preprocesses every imported module too (ADR 0051). A module
    // that selected its fallback branch would disagree with the file that
    // imported it, and the disagreement is the bug: one file links POSIX
    // symbols while the rest of the program assumes the Windows ABI.
    const fs::path dir = uniqueTempPath("fin_target_selectors");
    fs::create_directories(dir);

    writeFile(dir / "probe.fin",
              "namespace std {\n"
              "#[export]\n"
              "pub fun os_code() <int> {\n"
              "#c_ifdef FIN_TARGET_LINUX\n"
              "    return 1;\n"
              "#c_else\n"
              "#c_ifdef FIN_TARGET_DARWIN\n"
              "    return 2;\n"
              "#c_else\n"
              "#c_ifdef FIN_TARGET_WINDOWS\n"
              "    return 3;\n"
              "#c_else\n"
              "    return 0;\n"
              "#c_endif\n"
              "#c_endif\n"
              "#c_endif\n"
              "}\n"
              "#[export]\n"
              "pub fun arch_code() <int> {\n"
              "#c_ifdef FIN_TARGET_X86_64\n"
              "    return 10;\n"
              "#c_else\n"
              "#c_ifdef FIN_TARGET_ARM64\n"
              "    return 20;\n"
              "#c_else\n"
              "    return 0;\n"
              "#c_endif\n"
              "#c_endif\n"
              "}\n"
              "}\n");

    writeFile(dir / "main.fin",
              "import { os_code, arch_code } from probe::std;\n"
              "fun main() <noret> {\n"
              "    printf(\"%d %d\\n\", os_code(), arch_code());\n"
              "}\n");

    const fs::path exe = dir / "probe.exe";
    // `-I` adds the probe's directory to the search paths and keeps the bundled
    // library, which is where `printf` comes from. `--fin-libs` would replace
    // the bundle instead of adding to it.
    const auto compiled = runFinc({(dir / "main.fin").string(),
                                   "-I", dir.string(),
                                   "-o", exe.string()});
    ASSERT_EQ(compiled.exitCode, 0)
        << "the probe must compile: " << stripAnsi(compiled.err);

    const fs::path output = dir / "output.txt";
    const int status = fin::runProcess({exe.string()}, output.string(), output.string());
    const std::string text = readProcessOutput(output.string());
    ASSERT_EQ(status, 0) << "the probe must run: " << text;

    const int expectedOs = expectedOsCode();
    const int expectedArch = expectedArchCode();
    ASSERT_NE(expectedOs, 0) << "this host has no known operating-system selector";
    ASSERT_NE(expectedArch, 0) << "this host has no known architecture selector";
    EXPECT_EQ(text, std::to_string(expectedOs) + " " + std::to_string(expectedArch) + "\n")
        << "the imported module selected a different branch than the host";
}

TEST(TargetSelectors, WindowsSyncCleanupDoesNotRequireAPosixDestructor) {
    const fs::path source = fs::path(testsDir()).parent_path() / "lib/std/sync.fin";
    std::ifstream file(source);
    ASSERT_TRUE(file.good());
    const std::string contents((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    fin::Preprocessor pp;
    const auto windows = pp.process("#cdef FIN_TARGET_WINDOWS\n" + contents);
    EXPECT_EQ(windows.find("sync_mutex_destroy"), std::string::npos)
        << "SRW locks need no pthread_mutex_destroy declaration or call";
    EXPECT_NE(windows.find("delete m;"), std::string::npos)
        << "the lock buffer must still be released";
}
