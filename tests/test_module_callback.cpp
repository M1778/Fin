// SPDX-License-Identifier: GPL-3.0-or-later
//
// A function named in an imported module, used as a value, lowers as a
// closure pair -- the wrapper beside a null env -- so that calling through the
// value is the env-first call every other `fn` value makes.
//
// The failure this file exists to catch is a refusal, and it is a refusal
// whose cause is invisible from the source. A module's own declaration is
// registered under a module-qualified key, so the bare-name lookup that finds
// a root program's function misses it. Codegen then says "the name 'X' is not
// lowered yet" and points at the *call site* -- a line that mentions nothing
// but a callback argument. The usual reason for that refusal is a runtime
// function value, which is not what happened here.
//
// The case matters because a C callback is how a portable module hands a
// function to the platform: the compiler's own `lib/std/sync.fin` names
// `sync_run_body_win` for `_beginthreadex`, and a library that wraps a native
// API names its adapter the same way. A compiler that cannot lower those
// cannot ship a thread on Windows at all.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Corpus.hpp"
#include "utils/Process.hpp"

namespace fs = std::filesystem;
using namespace fin::testing;

namespace {

// Writes a file under `dir`, creating the parent directory when the name has
// one. A failed write shows up as a failed compile, and the diagnostic says
// which file was missing.
void writeFile(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(contents.data(), (std::streamsize)contents.size());
}

// The module: a C function that takes a callback, and the callback the module
// itself declares. The callback is named -- not written as a lambda in the
// call argument -- because a named function is what a module publishes to the
// platform, and because the lambda spelling has its own lowering path that
// would hide the bug.
//
// `qsort` is the C function because it is in every libc, so the test needs no
// companion C file to link, and because it really does call the pointer it is
// given: the run below is a real round trip through the callback.
std::string callbackModule() {
    return "namespace demo {\n"
           "// The callback. It compares two bytes; the value it returns is what\n"
           "// qsort would use to order them.\n"
           "pub fun compare(a: &char, b: &char) <int> { return 0; }\n"
           "\n"
           "#[llvm_name=\"qsort\"]\n"
           "@define c_qsort(base: &void, count: ulong, size: ulong,\n"
           "                compare: fn(&char, &char) => int) <noret>;\n"
           "\n"
           "#[export]\n"
           "pub fun sort_them() <noret> {\n"
           "    let buf <[char]> = new [char, 4]{};\n"
           "    c_qsort(&buf[0], cast<ulong>(4), cast<ulong>(1), compare);\n"
           "    delete buf;\n"
           "}\n"
           "}\n";
}

// The program: it imports the module and asks it to make the call. The
// import matters -- the module is what puts the callback in another
// compilation unit's namespace.
std::string callbackProgram() {
    return "import { sort_them } from demo::demo;\n"
           "fun main() <noret> {\n"
           "    sort_them();\n"
           "    printf(\"sorted\\n\");\n"
           "}\n";
}

} // namespace

TEST(ModuleCallback, AFunctionOfAnImportedModuleUsedAsACallbackLowers) {
    // The refusal is "the name 'compare' is not lowered yet", and it appears
    // in the module's own body -- the call to c_qsort(compare). The fix
    // resolves the owning module's declaration the way a call does, so the
    // lookup finds the module-qualified entry.
    const fs::path dir = uniqueTempPath("fin_module_callback");
    fs::create_directories(dir);

    writeFile(dir / "demo.fin", callbackModule());
    writeFile(dir / "main.fin", callbackProgram());

    const fs::path exe = dir / "callback.exe";
    const auto compiled = runFinc({(dir / "main.fin").string(),
                                   "-I", dir.string(),
                                   "-o", exe.string()});
    ASSERT_EQ(compiled.exitCode, 0)
        << "a module's own function must lower as a callback: " << stripAnsi(compiled.err);

    const fs::path output = dir / "output.txt";
    const int status = fin::runProcess({exe.string()}, output.string(), output.string());
    const std::string text = readProcessOutput(output.string());
    ASSERT_EQ(status, 0) << "the callback must run: " << text;
    EXPECT_NE(text.find("sorted"), std::string::npos)
        << "the C side must have called back into the module: " << text;
}

TEST(ModuleCallback, TheSameCallbackInTheRootProgramStillLowers) {
    // The root program was never broken; this is the control. If it ever
    // fails, the fix reached further than the module case and changed the
    // root program's own lowering.
    const fs::path dir = uniqueTempPath("fin_module_callback_root");
    fs::create_directories(dir);

    writeFile(dir / "main.fin",
              "pub fun compare(a: &char, b: &char) <int> { return 0; }\n"
              "#[llvm_name=\"qsort\"]\n"
              "@define c_qsort(base: &void, count: ulong, size: ulong,\n"
              "                compare: fn(&char, &char) => int) <noret>;\n"
              "fun main() <noret> {\n"
              "    let buf <[char]> = new [char, 4]{};\n"
              "    c_qsort(&buf[0], cast<ulong>(4), cast<ulong>(1), compare);\n"
              "    delete buf;\n"
              "    printf(\"sorted\\n\");\n"
              "}\n");

    const fs::path exe = dir / "root.exe";
    const auto compiled = runFinc({(dir / "main.fin").string(), "-o", exe.string()});
    ASSERT_EQ(compiled.exitCode, 0)
        << "a root program's function must lower as a callback: " << stripAnsi(compiled.err);

    const fs::path output = dir / "output.txt";
    const int status = fin::runProcess({exe.string()}, output.string(), output.string());
    const std::string text = readProcessOutput(output.string());
    ASSERT_EQ(status, 0) << "the callback must run: " << text;
    EXPECT_NE(text.find("sorted"), std::string::npos) << text;
}
