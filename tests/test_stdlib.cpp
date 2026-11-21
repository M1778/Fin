// =============================================================================
// Fin Programming Language Compiler
//
// Made with ❤️ for someone special.
//
// This project is genuinely built on love and dedication, a way to express your love can be done through Fin.
// Fin is a semi-object-orientated programming language that specialized in control, giving so much control to the user while asking for the bare minimum.
// Fin is the name of the Angel from BlackNightmare-Stories not yet published.
// “Love is the reason this project exists”
// -----------------------------------------------------------------------------
// Author: M1778
// Repository: https://github.com/M1778/Fin
// Profile: https://github.com/M1778M/
// -----------------------------------------------------------------------------
// Copyright (C) 2027 M1778
//
// This file is part of the Fin Programming Language Compiler.
//
// Fin is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Fin is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Fin.  If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================

#include "utils/Process.hpp"
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "Corpus.hpp"
#include "driver/SearchPaths.hpp"

// The bundled standard library: `lib/std/*.fin`, found with no flag and no
// environment variable, because `bundledLibraryPaths()` puts `<exe dir>/../lib/std`
// on the search path of every build (src/driver/SearchPaths.hpp).
//
// What the library must contain is not a design decision made here. Sixteen of the
// fifty samples lead with `module not found`, and every one of them names a module and
// imports named symbols from it -- so the corpus states the inventory and this file
// only holds it to account. The table below was extracted from the corpus's own
// `import` lines; if a sample gains an import, the row is owed.
//
// Two suites, the same convention as test_soundness.cpp: `Soundness_*` must pass
// forever, `KnownDefect_*` asserts what is wrong today and a failure is good news.
//
// Note what these tests deliberately do *not* assert: the shape of any library type.
// `Collection<T>` having a `get` that returns `T` is tested by
// tests/samples/stdlib/collection.fin and by the eleven samples that use it, which is
// where a shape assertion belongs -- the corpus is the specification (ADR 0008) and a
// second copy of it here would be a second thing to keep in step. What lives here is
// the *inventory* and the *plumbing*: that the module resolves off the default search
// path, and that the names the corpus imports are the names it exports.

namespace fs = std::filesystem;
using namespace fin::testing;

namespace {

class Src {
public:
    explicit Src(const std::string& contents) {
        path_ = uniqueTempPath("fin_stdlib", ".fin");
        std::ofstream f(path_, std::ios::binary);
        f.write(contents.data(), (std::streamsize)contents.size());
    }
    ~Src() { std::error_code ec; fs::remove(path_, ec); }
    std::string str() const { return path_.string(); }

private:
    fs::path path_;
};

// Compiles a string with *no* library flags and no FIN_LIBS, so the only way an
// import can resolve is the bundled path. Passing --fin-libs here would test the
// flag, which test_cli.cpp already does, and would say nothing about the bundle.
FincRun compileBundled(const std::string& code) {
    Src s(code);
    return runFinc({s.str()}, {{"FIN_LIBS", ""}});
}

// The bundled library, compiled to a real executable and run. Returns the program's
// stdout, or the compiler's stderr prefixed with `compile failed:` -- one string, so a
// test that expected output prints the reason it did not get any instead of an empty
// diff.
//
// Here rather than in test_codegen.cpp, which has the same shape twice over, because
// what these tests are about is the *bundle*: `build` there passes no environment and
// declares `printf` in the source, and the fact under test here is that the declaration
// arrives from lib/std/stdio.fin and the source declares nothing.
//
// `extra` goes on the command line between the source and `-o`, for the tests that need
// a second file: `-I <dir>` *adds* to the search paths where `--fin-libs` replaces them,
// and replacing them would take the bundle -- and with it the ambient `printf` those
// programs print through -- out of the run.
std::string buildAndRun(const std::string& code, const std::vector<std::string>& extra,
                       const std::vector<std::string>& runtimeArgs = {}) {
    Src src(code);
    const fs::path exe = uniqueTempPath("fin_stdlib_exe");
    std::vector<std::string> args{src.str()};
    args.insert(args.end(), extra.begin(), extra.end());
    args.push_back("-o");
    args.push_back(exe.string());
    const FincRun c = runFinc(args, {{"FIN_LIBS", ""}});
    if (c.exitCode != 0 || !fs::exists(exe)) {
        return "compile failed: " + stripAnsi(c.err);
    }
    const fs::path outPath = uniqueTempPath("fin_stdlib_out");
    std::vector<std::string> command{exe.string()};
    command.insert(command.end(), runtimeArgs.begin(), runtimeArgs.end());
    const int status = fin::runProcess(command, outPath.string(), outPath.string());
    const std::string out = readProcessOutput(outPath.string());
    std::error_code ec;
    fs::remove(exe, ec);
    fs::remove(outPath, ec);
    if (status != 0) return "program exited non-zero, output: " + out;
    return out;
}

std::string buildBundledAndRun(const std::string& code) { return buildAndRun(code, {}); }

// The same, with `input` fed to the program's standard input. stdin has to be
// redirected before the program starts, which `runProcess` cannot express (its
// redirections are for stdout/stderr), so this spawns the executable itself:
// fork/dup2 on POSIX, CreateProcess with an inherited input handle on Windows.
std::string buildAndRunWithStdin(const std::string& code, const std::string& input) {
    Src src(code);
    const fs::path exe = uniqueTempPath("fin_stdlib_stdin_exe");
    const FincRun c = runFinc({src.str(), "-o", exe.string()}, {{"FIN_LIBS", ""}});
    if (c.exitCode != 0 || !fs::exists(exe)) return "compile failed: " + stripAnsi(c.err);
    const fs::path inPath = uniqueTempPath("fin_stdlib_stdin", ".in");
    const fs::path outPath = uniqueTempPath("fin_stdlib_stdin_out");
    {
        std::ofstream f(inPath, std::ios::binary);
        f.write(input.data(), (std::streamsize)input.size());
    }
    int status = -1;
#ifdef _WIN32
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE inHandle = CreateFileA(inPath.string().c_str(), GENERIC_READ, FILE_SHARE_READ,
                                  &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE outHandle = CreateFileA(outPath.string().c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                   &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (inHandle != INVALID_HANDLE_VALUE && outHandle != INVALID_HANDLE_VALUE) {
        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = inHandle;
        startup.hStdOutput = outHandle;
        startup.hStdError = outHandle;
        PROCESS_INFORMATION process{};
        std::string command = "\"" + exe.string() + "\"";
        if (CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE, 0, nullptr,
                           nullptr, &startup, &process)) {
            WaitForSingleObject(process.hProcess, INFINITE);
            DWORD code = 1;
            GetExitCodeProcess(process.hProcess, &code);
            status = (int)code;
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }
    }
    if (inHandle != INVALID_HANDLE_VALUE) CloseHandle(inHandle);
    if (outHandle != INVALID_HANDLE_VALUE) CloseHandle(outHandle);
#else
    const pid_t pid = ::fork();
    if (pid == 0) {
        const int in = ::open(inPath.string().c_str(), O_RDONLY);
        const int out = ::open(outPath.string().c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (in < 0 || out < 0) _exit(127);
        ::dup2(in, STDIN_FILENO);
        ::dup2(out, STDOUT_FILENO);
        ::dup2(out, STDERR_FILENO);
        ::execl(exe.string().c_str(), exe.string().c_str(), (char*)nullptr);
        _exit(127);
    }
    if (pid > 0) {
        int waitStatus = 0;
        while (::waitpid(pid, &waitStatus, 0) < 0) {}
        status = WIFEXITED(waitStatus) ? WEXITSTATUS(waitStatus) : -1;
    }
#endif
    const std::string out = readProcessOutput(outPath.string());
    std::error_code ec;
    fs::remove(exe, ec);
    fs::remove(inPath, ec);
    fs::remove(outPath, ec);
    if (status != 0) return "program exited non-zero, output: " + out;
    return out;
}

// The same, with a directory of extra modules on the search path.
std::string buildWithModuleAndRun(const std::string& code, const std::string& includeDir) {
    return buildAndRun(code, {"-I", includeDir});
}

// Compiles a program against the bundle and returns its ANSI-stripped diagnostics.
std::string bundledErr(const std::string& code) {
    return stripAnsi(compileBundled(code).err);
}

// finc's own diagnostics for a *build* rather than a check. The `-o` is the whole
// difference: `finc file.fin` stops after the front end, so a codegen refusal and a link
// failure are both absent from its output, and a test that expects either one cannot use
// `compileBundled`.
std::string buildErr(const std::string& code, const std::vector<std::string>& extra = {}) {
    Src src(code);
    const fs::path exe = uniqueTempPath("fin_stdlib_exe");
    std::vector<std::string> args{src.str()};
    args.insert(args.end(), extra.begin(), extra.end());
    args.push_back("-o");
    args.push_back(exe.string());
    const FincRun c = runFinc(args, {{"FIN_LIBS", ""}});
    std::error_code ec;
    fs::remove(exe, ec);
    return stripAnsi(c.err);
}

std::string buildErrWithModule(const std::string& code, const std::string& includeDir) {
    return buildErr(code, {"-I", includeDir});
}

// The backend's trace for a program against the bundle: what it lowered,
// which is the only way from out here to see an instantiation that never
// becomes a symbol. Mirrors test_codegen.cpp's helper, which cannot serve
// here -- that one passes no environment, and these programs resolve their
// imports through the bundle that FIN_LIBS="" keeps on the path.
std::string bundledTrace(const std::string& code) {
    Src src(code);
    const fs::path obj = uniqueTempPath("fin_stdlib_tr", ".o");
    const FincRun r = runFinc({src.str(), "-c", "-o", obj.string(),
                               "--debug-codegen"},
                              {{"FIN_LIBS", ""}});
    std::error_code ec;
    fs::remove(obj, ec);
    return stripAnsi(r.err);
}

// A throwaway directory of .fin modules, for the tests here that need a second file
// the bundle does not provide. test_module_loader.cpp has the same class against
// the loader's C++ API; this copy drives the real binary and the two share nothing
// else.
class TempModuleDir {
public:
    TempModuleDir() {
        dir_ = uniqueTempPath("fin_stdlib_mods");
        fs::create_directories(dir_);
    }
    ~TempModuleDir() { std::error_code ec; fs::remove_all(dir_, ec); }

    void write(const std::string& name, const std::string& contents) {
        const fs::path p = dir_ / name;
        std::ofstream f(p, std::ios::binary);
        f.write(contents.data(), (std::streamsize)contents.size());
    }
    std::string path() const { return dir_.string(); }

private:
    fs::path dir_;
};

size_t errorCount(const std::string& stripped) {
    size_t n = 0;
    for (size_t i = 0; i < stripped.size();) {
        size_t eol = stripped.find('\n', i);
        if (eol == std::string::npos) eol = stripped.size();
        if (stripped.compare(i, 7, "error: ") == 0) ++n;
        i = eol + 1;
    }
    return n;
}

// One row per `import` spelling in the corpus. `path` is written exactly as the
// corpus writes it, `symbols` are the names it selects, and `sample` is where to
// look when a row fails.
//
// The `sample` column is not asserted on -- it is the message a failure prints -- so it
// went wrong quietly: six rows named a sample that does not import them, and one named
// no sample at all ("stdlib/operators.fin's own consumer", which is lambdas.fin). The
// authority is `grep -rn "from operators::std" tests/samples/`, not memory.
struct Row {
    const char* path;      // as written after `from`, e.g. "error::std"
    const char* symbols;   // as written inside the braces
    const char* sample;    // the corpus file that imports it this way
};

const std::vector<Row>& corpusImports() {
    static const std::vector<Row> rows = {
        {"error::std",     "Error",                "enums.fin, readonly.fin, and five more"},
        {"collection::std","Collection",           "prototype_test.fin, stdlib/hashmap.fin"},
        {"hashmap::std",   "HashMap",              "deeptest4.fin, prototype_test.fin"},
        {"operators::std", "Index, IndexAssign",   "stdlib/collection.fin, stdlib/hashmap.fin"},
        {"operators::std", "Addable",              "lambdas.fin"},
        {"stdptr::std",    "rptr",                 "const.fin"},
        {"types::std",     "resolve_type",         "stdlib/prototypes.fin"},
        {"types::std",     "Any",                  "stdlib/stdio.fin"},
        {"types",          "number2str",           "stdlib/memory.fin"},
        {"enums::std",     "getkeyid, keyidof",    "stdlib/typing.fin"},
        {"typing::std",    "IResult",              "stdlib/stdio.fin"},
        {"stdio",          "printf",               "complex.fin, importing.fin"},
    };
    return rows;
}

} // namespace

TEST(Soundness_Stdio, ReadLineAnswersEmptyLastAndLongLinesAndNullAtEof) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    // stdin has no reader anywhere in lib/std; `read_line` is the first. The
    // pinned lines are the four cases a line reader gets wrong: a blank line is
    // an empty string and not null, the last line needs no trailing newline,
    // EOF before any byte is null, and a line longer than the reader's first
    // buffer arrives whole. `len` is counted, not printed, because 5000 copies
    // of 'a' would make a failed diff unreadable.
    const std::string source = R"FIN(
import { read_line } from stdio::std;
import { len, free_str } from strings::std;
fun main() <noret> {
    let first? <string> = read_line();
    if (first == null) { printf("first=null\n"); } else {
        printf("first=%s\n", first?);
        free_str(first?);
    }
    let blank? <string> = read_line();
    if (blank == null) { printf("blank=null\n"); } else {
        printf("blank=%d\n", len(blank?));
        free_str(blank?);
    }
    let wide? <string> = read_line();
    if (wide == null) { printf("long=null\n"); } else {
        printf("long=%d\n", len(wide?));
        free_str(wide?);
    }
    let last? <string> = read_line();
    if (last == null) { printf("last=null\n"); } else {
        printf("last=%s\n", last?);
        free_str(last?);
    }
    let after? <string> = read_line();
    if (after == null) { printf("after=null\n"); } else {
        printf("after=%s\n", after?);
        free_str(after?);
    }
}
)FIN";
    // "one\n" then an empty line then 5000 bytes then "tail" with no newline.
    const std::string input = "one\n\n" + std::string(5000, 'a') + "\ntail";
    EXPECT_EQ(buildAndRunWithStdin(source, input),
              "first=one\nblank=0\nlong=5000\nlast=tail\nafter=null\n")
        << "short lines are not read in file order, or the 5000-byte line was truncated";
}

TEST(Soundness_Stdio, ReadLineOnEmptyInputIsNull) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    EXPECT_EQ(buildAndRunWithStdin(R"FIN(
import { read_line } from stdio::std;
fun main() <noret> {
    let line? <string> = read_line();
    if (line == null) { printf("eof\n"); }
}
)FIN", ""), "eof\n");
}

TEST(Soundness_Stdio, ReadLineStripsCrLfAndRefusesAnEmbeddedNul) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    // A CRLF line drops both bytes, the same way a text-mode reader should.
    EXPECT_EQ(buildAndRunWithStdin(R"FIN(
import { read_line } from stdio::std;
import { equals, len, free_str } from strings::std;
fun main() <noret> {
    let line? <string> = read_line();
    blame line != null;
    blame len(line?) == 4;
    blame equals(line?, "crlf");
    free_str(line?);
    let bare? <string> = read_line();
    blame bare != null;
    blame len(bare?) == 1;
    blame equals(bare?, "\r");
    free_str(bare?);
    printf("crlf ok\n");
}
)FIN", "crlf\r\n\r"), "crlf ok\n");
    // A string is NUL-terminated, so a line carrying a NUL cannot be returned
    // whole. Refusing by name is the only answer that does not silently cut the
    // line at the byte.
    const std::string out = buildAndRunWithStdin(R"FIN(
import { read_line } from stdio::std;
fun main() <noret> {
    let line? <string> = read_line();
    printf("returned\n");
}
)FIN", std::string("a\0b\n", 4));
    EXPECT_NE(out.find("stdin line contains a NUL byte"), std::string::npos) << out;
    EXPECT_EQ(out.find("returned"), std::string::npos) << out;
}

TEST(Soundness_Stdio, ReadAllCarriesEveryByteIncludingNulAndEmptyInput) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    // The binary-safe stdin route. `read_line` refuses a NUL because a `string`
    // cannot hold one, so `read_all` returns the bytes in a `[char]` whose
    // `.length` is explicit -- the same shape `File::read_all` returns, one
    // longer than the content for the terminator. Empty input is a one-element
    // array, not null, so a caller's loop never has to special-case it.
    const std::string source = R"FIN(
import { read_all } from stdio::std;
fun main() <noret> {
    let bytes <[char]> = read_all();
    printf("len=%d\n", bytes.length);
    if (bytes.length == 4) {
        blame bytes[0] == 'a' && bytes[1] == cast<char>(0) && bytes[2] == 'b';
        blame bytes[3] == cast<char>(0);
        printf("nul=preserved\n");
    }
    delete bytes;
}
)FIN";
    EXPECT_EQ(buildAndRunWithStdin(source, std::string("a\0b", 3)), "len=4\nnul=preserved\n");
    EXPECT_EQ(buildAndRunWithStdin(source, ""), "len=1\n");
}

TEST(Soundness_Stdio, PrintableTextIsNotAFormatString) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    EXPECT_EQ(buildBundledAndRun(R"FIN(
import { Printable, print, println } from stdio::std;
struct Label : <Printable> {
    pub fun format_str() <string> { return "100%% complete"; }
}
fun main() <noret> {
    print(Label{});
    println(Label{});
}
)FIN"), "100%% complete100%% complete\n");
}

TEST(Soundness_Stdio, TypedErrorOutputUsesStandardError) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    Src src(R"FIN(
import { Printable, eprint, eprintln, eprint_str, eprintln_str,
         eprint_int, eprintln_int, eprint_bool, eprintln_bool,
         eprint_double, eprintln_double, flush } from stdio::std;
struct Label : <Printable> {
    pub fun format_str() <string> { return "100%"; }
}
fun main() <noret> {
    eprint(Label{});
    eprintln(Label{});
    eprint_str("left"); eprintln_str("right");
    eprint_int(4); eprintln_int(2);
    eprint_bool(true); eprintln_bool(false);
    eprint_double(cast<double>(2.5)); eprintln_double(cast<double>(1.5));
    printf("stdout\n");
    flush();
}
)FIN");
    const fs::path dir = uniqueTempPath("fin_stdio_streams");
    fs::create_directories(dir);
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); }
    } cleanup{dir};
    const auto exe = dir / "program.exe";
    const auto out = dir / "stdout.txt";
    const auto err = dir / "stderr.txt";
    const auto compiled = runFinc({src.str(), "-o", exe.string()}, {{"FIN_LIBS", ""}});
    ASSERT_EQ(compiled.exitCode, 0) << compiled.err;
    ASSERT_EQ(fin::runProcess({exe.string()}, out.string(), err.string()), 0);
    EXPECT_EQ(readProcessOutput(out.string()), "stdout\n");
    EXPECT_EQ(readProcessOutput(err.string()), "100%100%\nleftright\n42\n10\n2.51.5\n");
}

TEST(Soundness_Stdio, BufferedWriteFailuresAreReported) {
#if !defined(FIN_TESTS_HAVE_BACKEND) || !defined(__linux__)
    GTEST_SKIP() << "This error probe needs the Linux /dev/full device and LLVM backend";
#endif
    EXPECT_EQ(buildBundledAndRun(R"FIN(
import { File } from stdio::std;
fun main() <noret> {
    blame !File::write_text("/dev/full", "x", 1);
    blame !File::append_text("/dev/full", "x", 1);
    printf("reported\n");
}
)FIN"), "reported\n");
}

TEST(Soundness_Stdio, FileSizeRefusesAnExtentItsIntApiCannotRepresent) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    const fs::path path = uniqueTempPath("fin_stdio_sparse");
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove(path, ec); }
    } cleanup{path};
    { std::ofstream file(path, std::ios::binary); }
#ifdef _WIN32
    const HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(handle, INVALID_HANDLE_VALUE);
    DWORD returned = 0;
    const bool sparse = DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0,
                                       nullptr, 0, &returned, nullptr) != 0;
    CloseHandle(handle);
    ASSERT_TRUE(sparse);
#endif
    std::error_code error;
    fs::resize_file(path, 2147483648ULL, error);
    ASSERT_FALSE(error) << error.message();
    EXPECT_EQ(buildAndRun(R"FIN(
import { File } from stdio::std;
fun main(args: [string]) <noret> {
    blame args.length == 1;
    blame File::exists(args[0]);
    printf("%d\n", File::size(args[0]));
}
)FIN", {}, {path.string()}), "-1\n");
}

TEST(Soundness_Stdio, StreamRejectsLengthsBeyondItsStorage) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    const auto write = buildBundledAndRun(R"FIN(
import { Stream } from stdio::std;
fun main() <noret> {
    let stream <Stream> = Stream(new [char, 1]{}, 0);
    let data <[char]> = ['x'];
    stream.write(data, 2);
}
)FIN");
    EXPECT_NE(write.find("a write count exceeds its input"), std::string::npos) << write;
    const auto construct = buildBundledAndRun(R"FIN(
import { Stream } from stdio::std;
fun main() <noret> {
    let stream <Stream> = Stream(new [char, 1]{}, 2);
}
)FIN");
    EXPECT_NE(construct.find("stream length exceeds its storage"), std::string::npos) << construct;
}

TEST(Soundness_Stdio, FileBytesAndStreamCleanupPreserveEmbeddedNul) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    const fs::path path = uniqueTempPath("fin_stdio_binary");
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove(path, ec); }
    } cleanup{path};
    {
        std::ofstream file(path, std::ios::binary);
        file.write("a\0b", 3);
    }
    EXPECT_EQ(buildAndRun(R"FIN(
import { File } from stdio::std;
fun main(args: [string]) <noret> {
    blame args.length == 1;
    blame File::size(args[0]) == 3;
    let data <[char]> = File::read_all(args[0]);
    blame data.length == 4;
    blame data[0] == 'a' && data[1] == cast<char>(0) && data[2] == 'b' && data[3] == cast<char>(0);
    delete data;
    let stream <auto> = File::open(args[0]);
    blame stream.remaining() == 3;
    let head <[char]> = stream.read(2);
    blame head.length == 2 && head[0] == 'a' && head[1] == cast<char>(0);
    delete head;
    stream.free_stream();
    stream.free_stream();
    blame stream.capacity() == 0 && stream.remaining() == 0 && stream.tell() == 0;
    let text <[char]> = ['x', 'y'];
    blame stream.write(text, 2) == 2;
    delete text;
    stream.rewind();
    let reread <[char]> = stream.read_all();
    blame reread.length == 2 && reread[0] == 'x' && reread[1] == 'y';
    delete reread;
    stream.free_stream();
    printf("bytes ok\n");
}
)FIN", {}, {path.string()}), "bytes ok\n");
}

// The plumbing, end to end, with the real binary: a module in the bundled library
// resolves with nothing on the command line and nothing in the environment.
//
// test_cli.cpp's BundledLibraries tests cover `bundledLibraryPathsFor` as a function
// over temp trees, which is why they pass on a repository with no `lib/std` at all.
// This one fails until the directory exists and the Driver wires it in, and that is
// the point: a search path nobody reads is not a search path.
TEST(Soundness_BundledStdlib, AModuleInTheBundledLibraryResolvesWithNoFlags) {
    const FincRun r = compileBundled(
        "import { Error } from error::std;\n"
        "fun main() <int> { return 0; }\n");
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(err.find("module not found"), std::string::npos)
        << "the bundled stdlib is not on the default search path. Either lib/std does\n"
           "not exist beside the binary's parent directory, or Driver.cpp stopped\n"
           "calling bundledLibraryPaths(). Both of the tests in test_cli.cpp named\n"
           "BundledLibraries.* pass either way, because they test the rule and not the\n"
           "directory.\n"
        << err;
    EXPECT_EQ(r.exitCode, 0) << err;
}

// The inventory. One EXPECT per corpus import spelling, each naming the sample that
// owes it, so a failure says which sample regresses rather than just "stdlib".
TEST(Soundness_BundledStdlib, EverySymbolTheCorpusImportsIsExported) {
    for (const Row& row : corpusImports()) {
        const std::string code =
            std::string("import { ") + row.symbols + " } from " + row.path + ";\n"
            "fun main() <int> { return 0; }\n";
        const std::string err = stripAnsi(compileBundled(code).err);
        EXPECT_EQ(errorCount(err), 0u)
            << "import { " << row.symbols << " } from " << row.path << ";\n"
            << "wanted by " << row.sample << "\n"
            << err;
    }
}

// The whole-module import forms the corpus uses, which resolve a module without naming
// anything inside it.
//
// `import * from somelib;` was a fourth row here and is gone, because the module is:
// `4d79ae7` deleted `lib/std/somelib/index.fin` with the message "somelib isnt a real
// stdlib module", and a row asserting that a deleted module resolves is a row asserting
// the deletion was a mistake. It was not -- the directory existed only to exercise
// `<base>/<name>/index.fin` resolution, and that candidate is covered against temp trees
// by test_cli.cpp's library-path tests and by test_module_loader.cpp, neither of which
// needs a directory inside the shipped library to do it. `import * from` itself is held by
// Soundness_Imports.ImportStarBindsEveryValueTheModuleDeclares and its two siblings in
// test_cli.cpp, which build their own module rather than borrowing one from the bundle.
//
// What the deletion cost is gone: `tests/samples/importing.fin` now writes
// `import "networking";` and `import * from networking;`, which resolve against
// the shipped library, so neither reports `module not found` anymore. Its
// expectation stays `unimplemented` over an unbuilt construct further down (a
// struct literal codegen refuses), not over these two lines.
TEST(Soundness_BundledStdlib, TheWholeModuleImportFormsResolve) {
    for (const char* code : {
             "import networking;\nfun main() <int> { return 0; }\n",
             "import networking as net;\nfun main() <int> { return 0; }\n",
             "import stdio::std as stdio;\nfun main() <int> { return 0; }\n",
         }) {
        const std::string err = stripAnsi(compileBundled(code).err);
        EXPECT_EQ(errorCount(err), 0u) << code << err;
    }
}

// The `::` tail names a namespace inside the module, and naming one the module
// never declared is an error (#90, ruled strict). The bundled library is
// written *with* `namespace std { ... }` blocks -- the spelling the corpus's
// own drafts use -- so `error::std` resolves and `error::nosuchnamespace`
// reports the unknown name.
TEST(Soundness_BundledStdlib, TheNamespaceSelectorInAnImportIsChecked) {
    // Was KnownDefect_BundledStdlib.TheNamespaceSelectorInAnImportIsNotChecked
    // (#90): parser.y's note on `namespace_block` recorded that the tail was
    // parsed and dropped, so `error::std` and `error::nosuchnamespace` loaded
    // the same file and neither complained.
    const std::string bad = stripAnsi(compileBundled(
        "import { Error } from error::nosuchnamespace;\n"
        "fun main() <int> { return 0; }\n").err);
    EXPECT_NE(errorCount(bad), 0u)
        << "an unknown namespace must be reported\n" << bad;
    EXPECT_NE(bad.find("has no namespace"), std::string::npos) << bad;
    const std::string good = stripAnsi(compileBundled(
        "import { Error } from error::std;\n"
        "fun main() <int> { return 0; }\n").err);
    EXPECT_EQ(errorCount(good), 0u)
        << "`error::std` names the namespace lib/std/error.fin declares\n" << good;

}

// ---------------------------------------------------------------------------
// Calling through a module qualifier.
//
// complex.fin:14 writes `stdio.printf("Big")` with its own comment saying "uses stdio
// printf", and it reported `Type 'module<stdio>' does not have methods`. Reading a
// module member already worked -- visit(MemberAccess&) has a NamespaceType branch and
// `stdio.nosuch` reports `Namespace 'stdio' has no exported member 'nosuch'` -- but
// visit(MethodCall&) went straight to getStructType, which knows nothing about
// namespaces, so a member could be named and not called.
//
// These live here rather than in test_soundness.cpp because they need a module, and a
// module means either the bundle or a two-file fixture. The bundle is the honest choice:
// `stdio` exports `printf` because lib/std/stdio.fin declares it, and if that ever stops
// being true these tests should say so.

TEST(Soundness_Modules, AModuleFunctionIsCallableThroughADot) {
    const std::string err = stripAnsi(compileBundled(
        "import stdio;\n"
        "fun main() <int> { stdio.printf(\"hi\\n\"); return 0; }\n").err);
    EXPECT_EQ(errorCount(err), 0u) << err;
}

TEST(Soundness_Modules, AModuleCallIsCheckedAgainstTheSignatureItResolvedTo) {
    // The failure mode a namespace branch invites: resolve the member, then call it
    // without looking at what it is. `println` takes one argument (lib/std/stdio.fin),
    // so passing none must still be an arity error -- otherwise the branch is a hole in
    // the call checking rather than a route into it.
    //
    // Not `printf`, which is the member complex.fin actually calls: it is variadic, and
    // a variadic signature is exempt from the arity check entirely
    // (checkCallArguments's `!sig.is_vararg` guard, and mutating that guard away kills
    // four corpus samples). A vararg callee would have made this test green against a
    // branch that skipped checking altogether.
    const std::string err = stripAnsi(compileBundled(
        "import stdio;\n"
        "fun main() <int> { stdio.println(); return 0; }\n").err);
    EXPECT_EQ(errorCount(err), 1u) << err;
    EXPECT_NE(err.find("expects 1 arguments, got 0"), std::string::npos)
        << "a call through a module must be checked like any other:\n" << err;
}

TEST(Soundness_Modules, AnUnknownModuleMemberIsReportedWhetherReadOrCalled) {
    // Read and called must agree, and both must name the namespace. Before this the two
    // spellings gave different diagnostics for the same mistake: the read said which
    // module had no such export, the call said the module had no methods.
    for (const char* code : {"import stdio;\nfun main() <int> { let x <int> = stdio.nosuch; return 0; }\n",
                             "import stdio;\nfun main() <int> { stdio.nosuch(); return 0; }\n"}) {
        const std::string err = stripAnsi(compileBundled(code).err);
        EXPECT_NE(err.find("has no exported member 'nosuch'"), std::string::npos) << code << err;
    }
}

TEST(Soundness_Modules, AModuleTypeNamedThroughADotRefusesAndNamesTheBynameRoute) {
    // Was KnownDefect_Modules.AModuleStructIsNotConstructibleThroughADot, #100. The
    // owner ruling kept the refusal and charged it with naming the route that works.
    // A module's types are not addressable through a dot: the dotted type spelling is
    // a parser refusal (`let e <stdio.IOError>` reports `unexpected DOT`), the dot's
    // call path finds no value under the name, and making construction work here
    // would strand the plain call behind a by-name import (`a call to 'IOError' is
    // not lowered yet`). What does work is binding the name plainly and constructing
    // where the binding lands, and both the call and the read must say so
    // (AnUnknownModuleMemberIsReportedWhetherReadOrCalled holds the two spellings
    // together).
    for (const char* code : {"import stdio;\n"
                             "fun main() <int> { stdio.IOError(); return 0; }\n",
                             "import stdio;\n"
                             "fun main() <int> { let e <auto> = stdio.IOError; return 0; }\n"}) {
        const std::string err = stripAnsi(compileBundled(code).err);
        EXPECT_NE(err.find("Namespace 'stdio' has no exported member 'IOError'"),
                  std::string::npos) << code << err;
        EXPECT_NE(err.find("import { IOError } from stdio;"), std::string::npos)
            << "the refusal must name the route that works\n" << code << err;
        EXPECT_NE(err.find("`IOError{"), std::string::npos)
            << "the construction form must be the brace literal, not a plain call:\n"
            << code << err;
    }
}

// ---------------------------------------------------------------------------
// Lowering a call written through a module qualifier (HANDOFF section 6, item 5).
//
// The block above is the front end's half: `stdio.printf("Big")` resolves, and is
// checked. complex.fin still did not *build* -- codegen's `visit(MethodCall&)` asks
// `baseAddress` for the receiver's address, a module has none, and the refusal was `the
// receiver of a call to the method 'printf' on a value with no address is not lowered
// yet`. The backend holds no reference to `NamespaceType` and that is the design, so the
// qualifier is spent in the front end instead: `SemanticAnalyzer::lowerModuleCall`
// resolves the call to a plain `printf("Big")` and leaves it on the node
// (`MethodCall::resolved_call`), and the backend's first statement lowers that.
//
// What these have to say beyond "it compiles", because a rewrite that dropped the
// arguments or reached some other symbol of the same shape compiles just as cleanly:
//
//   * The value arrives, in more than one expression position.
//   * Only a name the root program will declare *for the same declaration* is rewritten.
//     One mechanism puts a module's declaration into the root program -- `#[global]` and
//     its prototype splice (ADR 0021) -- so the gate is `Symbol::is_ambient`, set where
//     the prototype was retained. A wider gate would trade a refusal for a link failure,
//     which is the worse failure because it arrives after a successful compile.
//   * The module's signature still answers for the call, not the file's own. That is the
//     two-`printf` hazard complex.fin is built out of, and the ordering is the whole
//     answer: `checkCallArguments` runs before `lowerModuleCall`, never after.

namespace {

// A module publishing one fixed-arity ambient extern bound to C's `abs`, so a rewritten
// call has a value to be wrong about -- `printf` cannot say this, because a vararg
// signature is exempt from the arity check and its result is `<noret>`. `#[global]` is
// legal only inside `namespace std` (ADR 0021), and the export is what makes the file
// worth importing.
const char* const kAbsModule =
    "namespace std {\n"
    "#[llvm_name=\"abs\"]\n"
    "#[global]\n"
    "@define c_abs(v: int) <int>;\n"
    "}\n"
    "pub fun anchor() <noret> {}\n";

// Two modules publishing one ambient name, same signature, different symbols. Both are
// accepted -- `publishIfGlobal` refuses a second `#[global]` of a name only when the two
// *types* differ -- and one prototype is spliced, so one of the two qualifiers is
// rewritable and it is not the one the writer happens to spell.
const char* const kTwinAsAbs =
    "namespace std {\n"
    "#[llvm_name=\"abs\"]\n"
    "#[global]\n"
    "@define twin(v: int) <int>;\n"
    "}\n"
    "pub fun a_anchor() <noret> {}\n";

// The loser names a symbol nothing defines, deliberately: an assertion that the losing
// qualifier is refused fails loudly if the gate ever widens, and so does the linker. A
// second real symbol would give a plausible answer instead -- the first probe here paired
// `abs` with `labs`, and the mis-called version printed -7 rather than failing, because a
// 32-bit argument in a `long` parameter is an ABI accident and not a diagnosis.
const char* const kTwinAsAbsentSymbol =
    "namespace std {\n"
    "#[llvm_name=\"fin_no_such_symbol\"]\n"
    "#[global]\n"
    "@define twin(v: int) <int>;\n"
    "}\n"
    "pub fun b_anchor() <noret> {}\n";

} // namespace

TEST(Soundness_Modules, AModuleCallIsCheckedAgainstTheModulesSignatureAndNotTheFilesOwn) {
    // complex.fin in miniature: its :5 declares `@define printf(fmt: string, ...) <int>;`
    // and its :3 imports a `stdio` whose `printf` is `<noret>` (lib/std/stdio.fin:111), so
    // one file holds two `printf`s of different return type and the qualified spelling
    // must be the module's.
    //
    // A rewrite performed *before* the check would hand a bare `printf` to
    // `checkCallArguments`, which would find the file's own `<int>` declaration and pass
    // -- so this program compiling is exactly the "silently picks one" the queue entry
    // warns about, and it is why the namespace branch checks first and rewrites second.
    const std::string qualified = bundledErr(
        "import stdio;\n"
        "@define printf(fmt: string, ...) <int>;\n"
        "fun main() <noret> { let a <int> = stdio.printf(\"hi\\n\"); }\n");
    EXPECT_NE(qualified.find("expected 'int', got 'void'"), std::string::npos)
        << "a module-qualified call must be typed by the module's signature. If this\n"
           "compiled, the rewrite ran before the check and the file's own `<int>` printf\n"
           "answered for the module's `<noret>` one.\n"
        << qualified;

    // The other half, and what makes the first evidence rather than a tautology: the
    // *plain* name in the same file is the file's own declaration, and reading it as an
    // `int` is right. A branch that refused both would satisfy the assertion above.
    const std::string plain = bundledErr(
        "import stdio;\n"
        "@define printf(fmt: string, ...) <int>;\n"
        "fun main() <noret> { let a <int> = printf(\"hi\\n\"); }\n");
    EXPECT_EQ(errorCount(plain), 0u)
        << "the plain name is the file's own `<int>` printf and must still type as one:\n"
        << plain;
}

TEST(Soundness_Modules, AnAmbientMemberCalledThroughAQualifierIsStillCheckedForArity) {
    // The rewrite *moves* the argument list into the call it builds, so the check has to
    // have already happened to it. It has: this fails in the front end, before the
    // qualifier is spent and before anything is lowered.
    TempModuleDir d;
    d.write("absmod.fin", kAbsModule);
    const std::string err = buildErrWithModule(
        "import absmod as am;\n"
        "fun main() <noret> { am.c_abs(); }\n", d.path());
    EXPECT_NE(err.find("Function 'am.c_abs' expects 1 arguments, got 0"), std::string::npos)
        << "a call is checked before it is rewritten, and the diagnostic still names the\n"
           "qualifier the program wrote:\n"
        << err;
}

TEST(Soundness_Modules, ANonAmbientImportedExternIsRefusedThroughADot) {
    // Inverted from KnownDefect_Modules.AnImportedExternThatIsNotAmbientIsNotLoweredThroughADot
    // (issue #100): owner ruling keeps the front-end refusal by design. Only a #[global]
    // extern is spliced into the root program, so only a #[global] extern lowers through
    // a module dot; anything else must be re-declared in the file or marked #[global] in
    // its module. A rewrite here would trade a clean refusal for a link failure after a
    // compile that exited 0, which is the worse failure.
    //
    // `io_fflush` is one of lib/std/stdio.fin's own externs and is deliberately not
    // `#[global]`. No prototype for it is spliced into the root program, so there is
    // nothing for a plain `io_fflush(...)` to be a call *to*, and the qualified call
    // keeps the backend's no-address refusal.
    const std::string plain = buildErr(
        "import stdio;\n"
        "fun main() <noret> { let r <int> = stdio.io_fflush(null); }\n");
    {
        Src src("import stdio;\n"
                "fun main() <noret> { let r <int> = stdio.io_fflush(null); }\n");
        const fs::path exe = uniqueTempPath("fin_stdlib_exe");
        const FincRun c = runFinc({src.str(), "-o", exe.string()}, {{"FIN_LIBS", ""}});
        std::error_code ec;
        fs::remove(exe, ec);
        EXPECT_NE(c.exitCode, 0)
            << "a non-ambient imported extern must not lower through a module dot:\n"
            << stripAnsi(c.err);
    }
    EXPECT_NE(plain.find("the receiver of a call to the method 'io_fflush' on a value "
                         "with no address"), std::string::npos)
        << "a non-ambient imported extern keeps the no-address refusal; only #[global]\n"
           "externs are rewritten to a plain call the backend lowers:\n"
        << plain;

    // The half that makes this discriminating rather than decorative: the file declares
    // `io_fflush` itself, so the root program *does* bind that name -- and the call must
    // still refuse. A gate written as "the root program binds this name" instead of
    // "binds this name to this declaration" would rewrite here, and the rewritten call
    // would go to the file's own declaration under the symbol `io_fflush`, which no C
    // library defines: a clean compile followed by `undefined reference`. Refusing before
    // the object file exists is the rule the corpus is held to -- a construct the backend
    // cannot lower is refused, never skipped.
    const std::string declared = buildErr(
        "import stdio;\n"
        "@define io_fflush(handle: &void) <int>;\n"
        "fun main() <noret> { let r <int> = stdio.io_fflush(null); }\n");
    {
        Src src("import stdio;\n"
                "@define io_fflush(handle: &void) <int>;\n"
                "fun main() <noret> { let r <int> = stdio.io_fflush(null); }\n");
        const fs::path exe = uniqueTempPath("fin_stdlib_exe");
        const FincRun c = runFinc({src.str(), "-o", exe.string()}, {{"FIN_LIBS", ""}});
        std::error_code ec;
        fs::remove(exe, ec);
        EXPECT_NE(c.exitCode, 0)
            << "a file declaring the name itself must not make the module's member\n"
               "lowerable through a dot:\n"
            << stripAnsi(c.err);
    }
    EXPECT_NE(declared.find("the receiver of a call to the method 'io_fflush' on a value "
                            "with no address"), std::string::npos)
        << "a file declaring the name itself must not make the module's member\n"
           "lowerable: the two are different declarations, and only a #[global] one is\n"
           "put into the root program by anything other than this file.\n"
        << declared;
}

TEST(Soundness_Modules, AnImportedGenericStructInstantiatesOnDemand) {
    // Was KnownDefect_Modules.AnImportedGenericStructsConstructorIsNotLowered,
    // then AnImportedGenericStructTemplateIsFoundAndChecked: `HashMap::<string,
    // int>()` refused first with "a call to 'HashMap'", then with `#[export]`,
    // then with the `fn(any) -> int` field, then with pointer operators,
    // null-to-scalar conversions, bare `Collection{...}` literals and the
    // denullify. Each landing moved the refusal one step further in -- until
    // the amended ADR 0034 (any-boxing with value semantics) overturned the
    // last one: boxing the string key into `any` for the custom-hasher call
    // lowers, so the program builds. Inverted rather than deleted, because
    // the build is the contract now. Lazy per use, as designed: nothing here
    // instantiates what the root never names.
    const std::string prog =
        "import { HashMap } from hashmap::std;\n"
        "fun main() <noret> { let a <auto> = HashMap::<string, int>(); }\n";
    const std::string trace = bundledTrace(prog);
    EXPECT_NE(trace.find("instantiated struct HashMap<string, int>"),
              std::string::npos)
        << "the imported template must instantiate, not merely be found\n"
        << trace;
    // The instantiated method symbols exist, not just the instance.
    EXPECT_NE(trace.find("declared HashMap<string, int>.get_index"),
              std::string::npos)
        << "instantiation must declare the method symbols the root links\n"
        << trace;
    // And the full `-o` build succeeds: no refusal, old or new.
    const std::string err = buildErr(prog);
    EXPECT_EQ(err.find("error:"), std::string::npos)
        << "GOOD NEWS: the any-boxing refusal is gone and the build succeeds\n"
        << err;
    EXPECT_EQ(err.find("a conversion from 'a pointer' to 'any'"),
              std::string::npos)
        << err;
    // And specifically none of the old misses, any of which would mean the
    // registry lookup regressed rather than the work moved on.
    EXPECT_EQ(err.find("a call to 'HashMap'"), std::string::npos) << err;
    EXPECT_EQ(err.find("the attribute 'export' on struct 'HashMap'"),
              std::string::npos)
        << err;
    EXPECT_EQ(err.find("of type 'fn(any) -> int'"), std::string::npos) << err;
    EXPECT_EQ(err.find("an operator on a pointer"), std::string::npos) << err;
    EXPECT_EQ(err.find("a literal of struct 'Collection'"), std::string::npos)
        << err;
    EXPECT_EQ(err.find("this unary operator"), std::string::npos) << err;
    // And specifically not the turbofish refusal, which is what this said before the
    // generic-constructor unit and which would now name the wrong gap.
    EXPECT_EQ(err.find("explicit generic arguments"), std::string::npos) << err;
}

TEST(Soundness_Modules, AnImportedFinFunctionDotRefusalBlamesTheModuleDefinition) {
    // Was KnownDefect_Modules.AnImportedFinFunctionIsNotLoweredThroughADot, #100. The
    // owner ruling kept the refusal -- the definition is a module body and its AST
    // stays in the loader's astStorage, where the backend never looks, so rewriting
    // the qualifier would only move the refusal onto a different name -- and
    // sharpened it into a blame: the refusal says the definition is in the module's
    // own file at its own line, that a module body does not reach a file which calls
    // it through a qualifier until separate compilation, and that the spellings not
    // held back by that are the by-name plain call and a local wrapper.
    const std::string err = buildErr(
        "import stdio;\n"
        "fun main() <noret> { stdio.println(\"x\"); }\n");
    EXPECT_NE(err.find("the receiver of a call to the method 'println' on a value with no "
                       "address"), std::string::npos)
        << err;
    EXPECT_NE(err.find("stdio.fin"), std::string::npos)
        << "the refusal must blame the file the definition lives in\n" << err;
    EXPECT_NE(err.find("defined at"), std::string::npos) << err;
    EXPECT_NE(err.find("separate compilation"), std::string::npos) << err;
    EXPECT_NE(err.find("import { println }"), std::string::npos)
        << "the by-name route must be named as what lowers\n" << err;
}

#ifdef FIN_TESTS_HAVE_BACKEND

TEST(Soundness_Modules, TheModulesPrintfLowersAndRunsThroughAQualifier) {
    // complex.fin's construct with a value asserted instead of a compile. The sample
    // prints a bare "Big" and so says nothing about its argument; a `%d` does, because
    // nothing but the C library's printf formats one.
    EXPECT_EQ(buildBundledAndRun(
                  "import stdio::std as stdio;\n"
                  "fun main() <noret> { stdio.printf(\"%d\\n\", 41 + 1); }\n"),
              "42\n");
}

TEST(Soundness_Modules, AnImportedStructBynameConstructsAndRuns) {
    // The route the dotted refusal points at, pinned as a run so the help text cannot
    // drift into promising what no longer works: after `import { IOError } from stdio;`
    // the type is where the file itself is bound, and constructing it there is an
    // ordinary struct literal.
    EXPECT_EQ(buildBundledAndRun(
                  "import { IOError } from stdio;\n"
                  "fun main() <noret> {\n"
                  "    let e <IOError> = IOError{};\n"
                  "    printf(\"ok\\n\");\n"
                  "}\n"),
              "ok\n");
}

TEST(Soundness_Modules, AQualifiedCallLowersWhereverAnExpressionGoes) {
    // A rewritten call is an expression, not a statement form: one here initialises a
    // local, one is an argument to another call. Both, because the rewrite is recorded on
    // the node rather than replacing the node in its parent's slot -- if that made the
    // lowering depend on where the call was written, this is where it would show.
    //
    // `abs` is the symbol, so the answers are 5 and 9 rather than -5 and -9: a rewrite
    // that dropped its argument, or reached a different symbol, compiles and prints
    // something else.
    TempModuleDir d;
    d.write("absmod.fin", kAbsModule);
    EXPECT_EQ(buildWithModuleAndRun("import absmod as am;\n"
                                    "fun main() <noret> {\n"
                                    "    let v <int> = am.c_abs(0 - 5);\n"
                                    "    printf(\"%d %d\\n\", v, am.c_abs(0 - 9));\n"
                                    "}\n", d.path()),
              "5 9\n");
}

TEST(Soundness_Modules, AQualifiedCallReachesTheSymbolTheRetainedPrototypeNames) {
    // Two modules publish `twin` under different symbols and exactly one prototype is
    // retained -- the first, in import order (`ModuleLoader::retainAmbientPrototype`). So
    // `amod` is what the root program declares `twin` as, `a.twin(-7)` is 7, and
    // `b.twin(-7)` must refuse rather than reach `abs`: rewriting it would answer a call
    // on `bmod`'s declaration with `amod`'s symbol, and no diagnostic anywhere would
    // mention that the two modules had disagreed.
    //
    // This is the test `ModuleLoader::symbolOf`'s comment names as the one that goes red
    // if the loader's reading of `#[llvm_name]` and `CodeGen_LLVM::symbolNameOf`'s ever
    // drift apart -- the two agreeing is what makes a retained prototype's symbol
    // knowable from the loader at all.
    TempModuleDir d;
    d.write("amod.fin", kTwinAsAbs);
    d.write("bmod.fin", kTwinAsAbsentSymbol);
    EXPECT_EQ(buildWithModuleAndRun("import amod as a;\n"
                                    "import bmod as b;\n"
                                    "fun main() <noret> { printf(\"%d\\n\", a.twin(0 - 7)); }\n",
                                    d.path()),
              "7\n");

    const std::string err = buildErrWithModule(
        "import amod as a;\n"
        "import bmod as b;\n"
        "fun main() <noret> { printf(\"%d\\n\", b.twin(0 - 7)); }\n", d.path());
    EXPECT_NE(err.find("the receiver of a call to the method 'twin' on a value with no "
                       "address"), std::string::npos)
        << "the losing module's qualifier must not be rewritten: the name the root\n"
           "program declares is the other module's declaration, under the other module's\n"
           "symbol. Swapping the two imports swaps which module wins, which is what says\n"
           "the retention is first-wins rather than alphabetical.\n"
        << err;
}

TEST(Soundness_Modules, AModuleBodyCallsItsOwnModulesFunctionWhateverTheImportOrder) {
    // `path::std` and `strings::std` both publish a free `join` with different
    // signatures, and `strings::std`'s `replace` calls `join` on its split parts.
    // That inner call is module-local: `replace("aaa", "aa", "b")` is `ba`
    // whichever import comes first. The backend used to declare the first-loaded
    // module's `join` for the inner call, so the path-first order ran path's
    // `join` on strings' parts and printed garbage.
    for (const char* imports : {"import { join } from path::std;\n"
                                "import { replace } from strings::std;\n",
                                "import { replace } from strings::std;\n"
                                "import { join } from path::std;\n"}) {
        EXPECT_EQ(buildBundledAndRun(
                      std::string(imports) +
                      "fun main() <noret> { printf(\"%s\\n\", replace(\"aaa\", \"aa\", \"b\")); }\n"),
                  "ba\n")
            << "import order must not change what a module body calls:\n" << imports;
    }
}

TEST(Soundness_Modules, AModuleBodyKeepsItsOwnFunctionWhenTheRootCallsTheOtherOnes) {
    // The harder half of the above: the root calls path's `join` directly, so
    // the first-in-load-order entry for `join` exists before strings'
    // `replace` ever emits. The inner call must still reach strings' `join` --
    // an unqualified hit cannot short-circuit module-local resolution.
    EXPECT_EQ(buildBundledAndRun(
                  "import { join } from path::std;\n"
                  "import { replace } from strings::std;\n"
                  "fun main() <noret> {\n"
                  "  let j <string> = join(\"a\", \"b\");\n"
                  "  printf(\"%s %s\\n\", j, replace(\"aaa\", \"aa\", \"b\"));\n"
                  "}\n"),
              "a/b ba\n");
}

TEST(Soundness_Modules, APlainRedeclarationOfAnAmbientNameRefusesWithoutOverwrite) {
    // Was KnownDefect_Modules.RenamingAnAmbientNameInTheRootFileBreaksBothSpellingsAlike,
    // #100. A plain redeclaration of a name a module publishes ambient was taken in
    // silence, and the file's own declaration won: first-wins in declareFunction kept
    // its symbol, the rewrite spent the qualifier on the plain name, and the symbol
    // nothing defined failed at the link -- both spellings broken, no word at compile.
    // The owner ruling made it the duplicate a redeclaration in one file has been
    // since the duplicates fix: refused without `#[overwrite]`, and the opt-in
    // replaces the ambient definition in this file -- whereupon both spellings call
    // the file's own declaration, which is the "under whose symbol" the comment on
    // the old defect filed to the ruling.
    TempModuleDir d;
    d.write("amod.fin", kTwinAsAbs);
    const std::string plain = buildErrWithModule(
        "import amod as a;\n"
        "@define twin(v: int) <int>;\n"
        "fun main() <noret> { printf(\"%d\\n\", twin(0 - 7)); }\n", d.path());
    EXPECT_EQ(plain.find("link failed"), std::string::npos)
        << "the refusal must arrive at compile, not at the link:\n" << plain;
    EXPECT_NE(plain.find("'twin'"), std::string::npos) << plain;
    EXPECT_NE(plain.find("amod.fin"), std::string::npos)
        << "the ambient declaration the file collides with must be named:\n" << plain;
    EXPECT_NE(plain.find("#[overwrite(twin)]"), std::string::npos) << plain;

    // The opt-in, run rather than merely compiled. The file's own declaration replaces
    // the ambient one in this program, and first-wins in the backend keeps it -- so for
    // the file's bare prototype (nothing behind it) to link, it must name the symbol the
    // ambient definition already publishes, which is the second route the refusal
    // spells: twin(-7) through both spellings is C's `abs`, answering 7 because no
    // stub with a foreign symbol is standing where the calls land.
    EXPECT_EQ(buildWithModuleAndRun(
                  "import amod as a;\n"
                  "#[overwrite(twin)]\n"
                  "#[llvm_name=\"abs\"]\n"
                  "@define twin(v: int) <int>;\n"
                  "fun main() <noret> { printf(\"%d %d\\n\", twin(0 - 7), a.twin(0 - 7)); }\n",
                  d.path()),
              "7 7\n");
}

#endif  // FIN_TESTS_HAVE_BACKEND

// ---------------------------------------------------------------------------
// `#[global]` against the real bundle (ADR 0021).
//
// These belong here rather than in test_soundness.cpp for the reason that file's own
// `#[global]` block gives: the interesting cases need a *second* file, and a harness
// that compiles one string has none. The bundle is the second file, and it is the
// honest one -- `printf` is ambient because `lib/std/stdio.fin:109` marks it, and if
// that line is ever deleted these must go red rather than a corpus sample quietly
// gaining a diagnostic.
//
// `compileBundled` clears `FIN_LIBS`, so the only way any of this resolves is the
// bundled path the driver adds for itself. That matters here more than elsewhere:
// the ambient declaration is reached by the driver *preloading* the stdio module
// before it analyses the root file, and a preload against a search path the test
// supplied would say nothing about what a user gets.

TEST(Soundness_GlobalAttribute, TheBundledPrintfResolvesWithNoImport) {
    // The two corpus sites this exists for are `const.fin:68` and `interfaces.fin:18`,
    // which call `printf` bare and declare nothing. ADR 0021 promises both resolve with
    // no edit to either sample; this is that promise as one line, so a failure names
    // the mechanism instead of arriving as two sample expectations flipping.
    const FincRun r = compileBundled("fun main() <noret> { printf(\"hi\\n\"); }\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

TEST(Soundness_GlobalAttribute, ANonGlobalStdNameStillNeedsItsImport) {
    // The ambience half, answered no. `getkeyid` is `pub` inside `#[export]` inside
    // `namespace std` at lib/std/enums.fin:24 -- every property `printf` has except the
    // attribute -- so it is exactly the "everything else" the owner's ruling keeps
    // import-only, and it is the test that says the shared scope carries the marked set
    // and not a module.
    //
    // A stronger claim than it looks: `enums` is not preloaded at all, so this would
    // pass against a broken implementation that published every `pub` name of every
    // *loaded* module. The next test closes that.
    const std::string err = stripAnsi(
        compileBundled("fun main() <noret> { let x <int> = getkeyid(1); }\n").err);
    EXPECT_NE(err.find("Undefined function or type 'getkeyid'"), std::string::npos)
        << "a `pub` name in `namespace std` that is not marked #[global] must still be\n"
           "imported (ADR 0021, the ambience half). If this went green because the\n"
           "shared scope now takes a module's whole `symbols` map, that answers the\n"
           "half the owner answered no.\n"
        << err;
}

TEST(Soundness_GlobalAttribute, TheStdioModulesOwnUnmarkedExportsStayImportOnly) {
    // The case above with the loading question removed. `stdio` *is* preloaded, so
    // `Printable`, `print`, `println`, `IOError` and `Stream` are all analysed and all
    // sitting in a scope the root file's lookup can reach the parent of -- and every one
    // of them must still be invisible, because only the stamped declaration is
    // published. This is what holds `publishIfGlobal` to the stamp rather than to the
    // module: a publish that copied the loaded scope would leave the test above green
    // and every name here would leak.
    for (const char* name : {"print", "println", "Printable", "IOError", "Stream"}) {
        const std::string code =
            std::string("fun main() <noret> { let x <int> = ") + name + "(1); }\n";
        const std::string err = stripAnsi(compileBundled(code).err);
        EXPECT_NE(errorCount(err), 0u)
            << "`" << name << "` is exported by the preloaded stdio module and is not\n"
               "marked #[global], so it must not resolve without an import.\n"
            << err;
    }
}

TEST(Soundness_GlobalAttribute, AnAmbientNameReachesAnImportedModuleToo) {
    // "Visible to every file in the compiler session" includes the files the session
    // loads on the user's behalf, and this is the case the shared scope was built for
    // rather than a consequence of it: a module is analysed by its *own*
    // SemanticAnalyzer, so an implementation that put the ambient binding in the root
    // analyzer's scope would pass every test above and fail here.
    //
    // `helper` calls `printf` and imports nothing. It resolves only if the loader's
    // scope is the parent of the module analyzer's scope as well as the root's.
    // `-I` rather than `--fin-libs`, because the flag *replaces* the library paths
    // and would take the bundle -- and with it the ambient declaration -- out of the
    // run this test is about. An include path is additive and leaves the bundle
    // exactly where the driver puts it (Driver.cpp's configureLoader, steps 1-3).
    TempModuleDir d;
    d.write("helper.fin", "pub fun shout() <noret> { printf(\"loud\\n\"); }\n");
    Src s("import { shout } from helper;\n"
          "fun main() <noret> { shout(); }\n");
    const FincRun r = runFinc({s.str(), "-I", d.path()}, {{"FIN_LIBS", ""}});
    EXPECT_EQ(r.exitCode, 0)
        << "an ambient declaration must reach a module the session loaded, not just\n"
           "the root file.\n"
        << stripAnsi(r.err);
}

TEST(Soundness_GlobalAttribute, AnUnmarkedExternInALoadedModuleIsNotPublished) {
    // The stamp is what publishes, not the declaration form -- and this is the only
    // test in either file that says so, which is why it is written against a module
    // rather than folded into one of the cases above.
    //
    // Measured, not assumed. A mutant that dropped the attribute check from
    // `publishIfGlobal` and published every `@define` passed all seventeen cases in
    // test_soundness.cpp's `#[global]` block and every other case here: the unmarked
    // declarations they look at are a `fun`, an `interface` and two structs, and the
    // one `@define` in `lib/std/stdio.fin` is the marked one. So the leak this rules
    // out had no witness anywhere until this file got a module with a second `@define`
    // in it.
    //
    // `absent` is `@define`d and unmarked, `anchor` is what the import is for -- an
    // import that binds nothing would fail for its own reasons and say nothing about
    // the extern.
    TempModuleDir d;
    d.write("externs.fin",
            "@define absent(fmt: string) <noret>;\n"
            "pub fun anchor() <noret> {}\n");
    Src s("import { anchor } from externs;\n"
          "fun main() <noret> { anchor(); absent(\"x\"); }\n");
    const std::string err = stripAnsi(runFinc({s.str(), "-I", d.path()},
                                              {{"FIN_LIBS", ""}}).err);
    EXPECT_NE(err.find("Undefined function or type 'absent'"), std::string::npos)
        << "an `@define` in a loaded module that carries no #[global] must stay local\n"
           "to that module. #[global] is opt-in per declaration (ADR 0021), so what\n"
           "publishes is the stamp and never the declaration form.\n"
        << err;
}

TEST(Soundness_GlobalAttribute, AFileMayDeclareTheAmbientPrintfItself) {
    // Fourteen corpus samples write `@define printf(fmt: string, ...) <noret>;`
    // verbatim, so the ambient declaration lands on top of a local one in most of the
    // corpus and must not turn any of them into a duplicate-global error. The identical
    // signature is the point: test_soundness.cpp's
    // TwoGlobalsOfOneNameWithDifferentTypesAreRefused is the other side, and this pins
    // that the check compares types rather than counting declarations.
    const FincRun r = compileBundled(
        "@define printf(fmt: string, ...) <noret>;\n"
        "fun main() <noret> { printf(\"hi\\n\"); }\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

// ---------------------------------------------------------------------------
// The backend half of the ambient declaration.
//
// Publishing `printf` into the shared scope is what makes a call to it type-check in a
// file that imports nothing. It is not what makes the call *link*: `declareTopLevel`
// walks the root program's own statements, and the declaration that named the C symbol
// is in `lib/std/stdio.fin`, whose AST the loader keeps in `astStorage` where the
// backend never looks. So `finc hello.fin` exited 0 and `finc hello.fin -o hello`
// refused with `codegen: a call to 'printf' is not lowered yet` -- in a file the
// compiler had just told needed no import.
//
// The driver closes it by splicing a prototype for each ambiently-published extern into
// the root program between the front end and the backend
// (ModuleLoader::appendAmbientPrototypes). These tests are the ones that would go red if
// that splice went away, and they are written as build-and-run because the failure they
// replaced was a *link* failure: a compile that exits 0 proves nothing about it.
//
// Guarded, because a build with FIN_WITH_LLVM=OFF has no backend to link with and ADR
// 0010 keeps that configuration supported. The front-end tests above run either way.
#ifdef FIN_TESTS_HAVE_BACKEND

TEST(Soundness_GlobalAttribute, TheAmbientPrintfLinksAndRuns) {
    // The exit criterion for the ambient name, and the shortest program that states it:
    // no import, no declaration, one call. `const.fin:68` and `interfaces.fin:18` are
    // the corpus sites, and until this held neither could be built.
    EXPECT_EQ(buildBundledAndRun("fun main() <noret> { printf(\"hi\\n\"); }\n"),
              "hi\n");
}

TEST(Soundness_GlobalAttribute, TheAmbientPrintfCallsTheCLibrarysPrintfAndNotItsFinName) {
    // The prototype carries `#[llvm_name="printf"]` and it has to survive being copied,
    // which is not automatic: `CloneVisitor::visit(DefineDeclaration&)` did not clone the
    // `attributes` vector at all -- nine other declaration visits did -- so a clone
    // produced a prototype under the Fin name with no rename on it. Here the two names
    // are the same string and the bug would be invisible, so this asserts on the *format
    // string* instead: a call that reached the C library formats `%d`, and one that
    // reached anything else does not.
    EXPECT_EQ(buildBundledAndRun(
                  "fun main() <noret> { printf(\"%d-%s\\n\", 7, \"seven\"); }\n"),
              "7-seven\n");
}

TEST(Soundness_GlobalAttribute, AFileThatDeclaresPrintfItselfStillGetsOneSymbol) {
    // Fourteen corpus samples write the `@define` themselves while the bundle publishes
    // it, so the splice lands on top of a local declaration in most of the corpus. One
    // declaration has to win: `declareFunction` keeps the first (`if
    // (functions_.count(name)) return;`), and two `llvm::Function::Create` calls for one
    // name would otherwise give the second a `printf.1` that nothing defines.
    EXPECT_EQ(buildBundledAndRun("@define printf(fmt: string, ...) <noret>;\n"
                                 "fun main() <noret> { printf(\"both\\n\"); }\n"),
              "both\n");
}

TEST(Soundness_GlobalAttribute, AnAmbientExternIsSplicedForItsLlvmNameAndNotItsFinName) {
    // The rename, with the two names actually different, which the bundle cannot show:
    // its `printf` is renamed to `printf`. A module publishes `shout` bound to C's
    // `puts`, and the program calls `shout`. If the splice dropped `#[llvm_name]` the
    // object would ask for a symbol called `shout`, and the link would fail -- so this
    // running at all is the assertion.
    //
    // `-I` rather than `--fin-libs`: the flag replaces the library paths and would take
    // the bundle out of the run, and `main` is not the point here.
    TempModuleDir d;
    d.write("shouter.fin",
            "namespace std {\n"
            "#[llvm_name=\"puts\"]\n"
            "#[global]\n"
            "@define shout(msg: string) <noret>;\n"
            "}\n"
            "pub fun anchor() <noret> {}\n");
    Src s("import { anchor } from shouter;\n"
          "fun main() <noret> { shout(\"loud\"); }\n");
    const fs::path exe = uniqueTempPath("fin_stdlib_shout");
    const FincRun c = runFinc({s.str(), "-I", d.path(), "-o", exe.string()},
                              {{"FIN_LIBS", ""}});
    ASSERT_EQ(c.exitCode, 0)
        << "an ambiently-published extern must reach the backend with its #[llvm_name]\n"
           "intact, or the object asks for a symbol nothing defines.\n"
        << stripAnsi(c.err);
    ASSERT_TRUE(fs::exists(exe));
    std::error_code ec;
    fs::remove(exe, ec);
}

TEST(Soundness_GlobalAttribute, AnUnmarkedExternInALoadedModuleIsNotSpliced) {
    // The splice follows the stamp, like the publish does. An implementation that handed
    // the backend every `@define` of every loaded module would pass every test above --
    // the names would all link -- and would quietly put `lib/std`'s externs into the
    // symbol table of a program that imports nothing. `absent` is unmarked, so a call to
    // it must still fail, and in the front end rather than the linker.
    TempModuleDir d;
    d.write("externs.fin",
            "@define absent(msg: string) <noret>;\n"
            "pub fun anchor() <noret> {}\n");
    Src s("import { anchor } from externs;\n"
          "fun main() <noret> { absent(\"x\"); }\n");
    const std::string err = stripAnsi(runFinc({s.str(), "-I", d.path(), "-o",
                                               uniqueTempPath("fin_stdlib_absent")},
                                              {{"FIN_LIBS", ""}}).err);
    EXPECT_NE(err.find("Undefined function or type 'absent'"), std::string::npos) << err;
}

#endif  // FIN_TESTS_HAVE_BACKEND

// ---------------------------------------------------------------------------
// The rewritten library's surface.
//
// These belong here rather than in test_soundness.cpp for the reason the file's header
// gives: what is under test is the *bundle*, and reaching it needs the real binary with
// no library flags. `compileBundled` is that.
//
// What they do NOT assert is any algorithm. `HashMap`'s probe sequence and
// `Collection`'s insert arithmetic were verified by transliterating each module into C
// and running it against a reference, because a call to an imported function is not
// lowered and no Fin program can exercise them. Asserting an algorithm here would mean
// asserting that a type-check succeeded, which says nothing about whether the algorithm
// is right -- so these hold the *shape* to account and the commit messages carry the
// measurements.
//
// Each test pairs a positive case with a negative one. A test that only checks "this
// compiles" passes against a library that dropped the method and gained a
// catch-all, and passes against a module that failed to load in a way that suppressed
// its own diagnostics -- both have happened in this repository. The negative case is
// what makes the positive one evidence.

namespace {

// A program body that imports from the bundle and runs the given statements in `main`.
std::string program(const std::string& imports, const std::string& body) {
    return imports + "fun main() <noret> {\n" + body + "}\n";
}

} // namespace

TEST(Soundness_BundledStdlib, EveryModuleChecksCleanThroughAnImport) {
    // The blanket claim the guide chapter makes, held to account one module at a time so
    // that a failure names the module rather than "the stdlib".
    //
    // Through an *import* and not standalone, deliberately: every module also
    // checks clean as a root file (Soundness_BundledStdlib below sweeps the
    // whole directory), but the route a program actually uses is the import,
    // and that is what this table holds to account. Every module has
    // to be clean by the route a program actually uses.
    for (const char* module : {"error", "collection", "hashmap", "types", "typing",
                               "enums", "operators", "stdptr", "stdio", "strings",
                               "math", "networking", "outcome", "log",
                               "cli", "serde", "json"}) {
        const std::string err =
            bundledErr(std::string("import ") + module + ";\nfun main() <noret> {}\n");
        EXPECT_EQ(errorCount(err), 0u)
            << "lib/std/" << module << ".fin does not check clean through an import.\n"
            << err;
    }
}

TEST(Soundness_BundledStdlib, TheHashMapSurfaceResolves) {
    // Every method the rewritten `HashMap` gained, named at a call. `get_index`,
    // `exists`, `len`, `__get`, `__set` and the two operators are the draft's and are
    // covered by the corpus; these are the additions, and without a test the first thing
    // to notice one had been dropped would be a user's program.
    const std::string err = bundledErr(program(
        "import { HashMap, hash_of } from hashmap::std;\n",
        "  let m <auto> = HashMap::<string, int>();\n"
        "  m[\"a\"] = 1;\n"
        "  let v <int> = m[\"a\"];\n"
        "  let g <int> = m.get_or(\"z\", -1);\n"
        "  let r <bool> = m.remove(\"a\");\n"
        "  let e <bool> = m.is_empty();\n"
        "  let c <int> = m.capacity();\n"
        "  let n <int> = m.slot_count();\n"
        "  let live <bool> = m.is_live(0);\n"
        "  let k <string> = m.key_at(0);\n"
        "  let w <int> = m.value_at(0);\n"
        "  m.clear();\n"
        "  let h <int> = hash_of::<string>(\"a\");\n"));
    EXPECT_EQ(errorCount(err), 0u) << err;
}

TEST(Soundness_BundledStdlib, AHashMapCallIsStillCheckedAgainstItsSignature) {
    // The negative half of the test above. Without it, a `HashMap` that had lost
    // `get_or` and gained a variadic catch-all would pass -- and so would one whose
    // module failed to load in a way that suppressed the diagnostics, which is the
    // failure mode `stdio.fin`'s circular dependency produces when it is the root file.
    const std::string err = bundledErr(program(
        "import { HashMap } from hashmap::std;\n",
        "  let m <auto> = HashMap::<string, int>();\n"
        "  let g <int> = m.get_or(\"z\");\n"));
    EXPECT_NE(err.find("expects 2 arguments, got 1"), std::string::npos)
        << "a method on a library type must be arity-checked like any other:\n" << err;
}

TEST(Soundness_BundledStdlib, TheCollectionSurfaceResolves) {
    const std::string err = bundledErr(program(
        "import { Collection } from collection::std;\n",
        "  let c <&Collection<int>> = new Collection::<int>{};\n"
        "  c.push(1);\n"
        "  let n <int> = c.len();\n"
        "  let e <bool> = c.is_empty();\n"
        "  let cap <int> = c.capacity();\n"
        "  c.reserve(16);\n"
        "  let f <int> = c.first();\n"
        "  let l <int> = c.last();\n"
        "  let i <int> = c.index_of(1);\n"
        "  let has <bool> = c.contains(1);\n"
        "  c.insert(0, 2);\n"
        "  let gone <int> = c.remove(0);\n"
        "  c.set(0, 3);\n"
        "  c.reverse();\n"
        "  let d <&Collection<int>> = new Collection::<int>{};\n"
        "  d.extend(c);\n"
        "  c.clear();\n"));
    EXPECT_EQ(errorCount(err), 0u) << err;
}

TEST(Soundness_BundledStdlib, ACollectionCallIsStillCheckedAgainstItsSignature) {
    const std::string err = bundledErr(program(
        "import { Collection } from collection::std;\n",
        "  let c <&Collection<int>> = new Collection::<int>{};\n"
        "  c.insert(0);\n"));
    EXPECT_NE(err.find("expects 2 arguments, got 1"), std::string::npos) << err;
}

TEST(Soundness_BundledStdlib, TheSmartPointerSurfaceResolves) {
    // `rptr`'s counting members and `wptr`, which the module gained together: the counts
    // are only meaningful because the counters are shared, and `wptr` is the handle that
    // reads a count without incrementing it.
    const std::string err = bundledErr(program(
        "import { rptr, wptr, OwnershipError } from stdptr::std;\n",
        "  let p <rptr<int>> = rptr(5);\n"
        "  let refs <int> = p.refs();\n"
        "  let borrows <int> = p.borrows();\n"
        "  let owned <bool> = p.is_owned();\n"
        "  let borrowed <bool> = p.is_borrowed();\n"
        "  let back <bool> = p.is_givenback();\n"
        "  let a <&rptr<int>> = p.alias();\n"
        "  let ro <&rptr<int>> = p.readonly_view();\n"
        "  let w <wptr<int>> = p.weak();\n"
        "  let alive <bool> = w.is_alive();\n"
        "  let seen <&int> = w.get();\n"
        "  let lent <&rptr<int>> = p.borrow();\n"
        "  lent.set(7);\n"
        "  lent.giveback();\n"
        "  let heir <&rptr<int>> = p.own();\n"
        "  let value <&int> = heir.get();\n"
        "  heir.release();\n"));
    EXPECT_EQ(errorCount(err), 0u) << err;
}

TEST(Soundness_BundledStdlib, ASmartPointerCallIsStillTypeChecked) {
    // `refs()` returns an `int`. Assigning it to a `string` has to fail, or the test
    // above is measuring that the names exist rather than that they mean anything.
    const std::string err = bundledErr(program(
        "import { rptr } from stdptr::std;\n",
        "  let p <rptr<int>> = rptr(5);\n"
        "  let n <string> = p.refs();\n"));
    EXPECT_NE(err.find("expected 'string', got 'int'"), std::string::npos) << err;
}

TEST(Soundness_BundledStdlib, TheErrorSurfaceResolves) {
    // Both arities of the constructor, and the three methods. The second parameter is the
    // point: `Error(msg: string, err_code: int = -1)` is the draft's spelling, and it was
    // written as one parameter for a while on the belief that a defaulted parameter was
    // still required at a call site. That stopped being true at `d7a91df`, and seven
    // samples import this struct, so a regression in either arity is a regression in all
    // of them.
    const std::string err = bundledErr(program(
        "import { Error } from error::std;\n",
        "  let plain <Error> = Error(\"boom\");\n"
        "  let coded <Error> = Error(\"boom\", 7);\n"
        "  let msg <string> = plain.format();\n"
        "  let full <string> = coded.describe();\n"
        "  let has <bool> = coded.has_code();\n"
        "  let id <int> = coded.error_id;\n"
        "  let text <string> = coded.message;\n"));
    EXPECT_EQ(errorCount(err), 0u) << err;
}

TEST(Soundness_BundledStdlib, TheErrorConstructorTakesOneArgumentOrTwoAndNoOther) {
    // The negative half of the arity claim, both directions. Without this the test above
    // passes against a variadic constructor and against one that takes `any...`, and the
    // "between 1 and 2" wording is what says the default is what makes the second one
    // optional rather than the parameter being nullable.
    const std::string none = bundledErr(program(
        "import { Error } from error::std;\n",
        "  let e <Error> = Error();\n"));
    EXPECT_NE(none.find("expects between 1 and 2 arguments, got 0"), std::string::npos)
        << none;

    const std::string three = bundledErr(program(
        "import { Error } from error::std;\n",
        "  let e <Error> = Error(\"boom\", 1, 2);\n"));
    EXPECT_NE(three.find("expects between 1 and 2 arguments, got 3"), std::string::npos)
        << three;

    // And the methods are typed. `describe()` returns the formatted string, so assigning
    // it to an `int` has to fail -- otherwise the surface test above measures that the
    // names exist rather than that they mean anything.
    const std::string typed = bundledErr(program(
        "import { Error } from error::std;\n",
        "  let e <Error> = Error(\"boom\");\n"
        "  let n <int> = e.describe();\n"));
    EXPECT_NE(typed.find("expected 'int', got 'string'"), std::string::npos) << typed;
}

TEST(Soundness_BundledStdlib, TheResultSurfaceResolves) {
    // `typing.fin`'s `implements` block, reached the way a caller reaches it. The receiver
    // of a method on an enum is its first parameter (Soundness_EnumMethodReceiver), so
    // `r.unwrap()` passes nothing and `r.unwrap_or(0)` passes one argument.
    const std::string err = bundledErr(program(
        "import { Result } from typing::std;\n"
        "fun consume(v: int) <noret> {}\n",
        "  let r <Result<int, string>> = Result::Ok(1);\n"
        "  let v <int> = r.unwrap();\n"
        "  let w <int> = r.unwrap_or(0);\n"
        "  r.expect();\n"
        "  let ok <bool> = r.is_ok();\n"
        "  let bad <bool> = r.is_err();\n"
        "  r.select(consume);\n"));
    EXPECT_EQ(errorCount(err), 0u)
        << "Result's implements block is what makes these resolve; if `keyidof` regressed\n"
           "the block stops type-checking and every one of these becomes an unknown method.\n"
        << err;
}

TEST(Soundness_BundledStdlib, AnEnumMethodTakesItsReceiverAsItsFirstParameter) {
    // The negative half, and it pins the receiver rule rather than just an arity. If the
    // first parameter ever stopped being the receiver, `r.unwrap()` would need an
    // argument and `r.unwrap_or(0)` would need two -- so passing one to `unwrap` must be
    // an error today and would silently become correct if the rule changed.
    const std::string err = bundledErr(program(
        "import { Result } from typing::std;\n",
        "  let r <Result<int, string>> = Result::Ok(1);\n"
        "  let v <int> = r.unwrap(r);\n"));
    EXPECT_NE(errorCount(err), 0u)
        << "the receiver of an enum method is its first parameter, so `unwrap` takes no\n"
           "argument at the call. If this went green, Soundness_EnumMethodReceiver moved\n"
           "and typing.fin's signatures need re-reading.\n"
        << err;
}

// ---------------------------------------------------------------------------
// Outcome, the struct that makes recoverable errors runnable (issue #52).
//
// `typing::std` declares `Result<T,E>` with the method names a caller wants and
// payload enums are refused in codegen (ADR 0037), so until now a recoverable
// error was a `blame`-abort or a hand-rolled status code. `outcome::std` is the
// struct version: `Outcome<T>` with `Ok<T>`/`Err<T>` free constructors and
// `is_ok`/`is_err`/`unwrap_or`/`expect` on the struct -- the API exactly as the
// issue writes it, nothing more.
//
// These pair a positive case with a negative one like every surface test above,
// and the runnable half guards itself like every build-and-run here.

TEST(Soundness_Outcome, TheStructSurfaceResolves) {
    // issue #52's API at every call shape: both constructors, all four methods,
    // and the fields the issue publishes. The `let r <auto>` spelling of the
    // constructor's result and the `let bad <Outcome<int>>` spelling of both
    // are deliberate: a caller who writes the type out and one who doesn't
    // must both resolve.
    const std::string err = bundledErr(program(
        "import { Outcome, Ok, Err } from outcome::std;\n",
        "  let r <auto> = Ok::<int>(80);\n"
        "  blame r.is_ok();\n"
        "  let bad <Outcome<int>> = Err::<int>(\"bad port\");\n"
        "  blame bad.is_err();\n"
        "  blame bad.ok == false;\n"
        "  let fallback <int> = bad.unwrap_or(-1);\n"
        "  let msg <string> = bad.message;\n"
        "  bad.expect(\"surface probe\");\n"));
    EXPECT_EQ(errorCount(err), 0u)
        << "Outcome's surface stopped resolving. The struct literal inside the\n"
           "constructors is the module's own, so a failure here is the import\n"
           "route or a declaration, not the literal:\n"
        << err;
}

TEST(Soundness_Outcome, AConstructorArgumentIsChecked) {
    // The negative half: `Ok::<int>` handed a string must still be an argument
    // error, not a silent accept. Without it, a constructor that lost its
    // parameter types to a catch-all would pass the test above.
    const std::string err = bundledErr(program(
        "import { Ok } from outcome::std;\n",
        "  let r <auto> = Ok::<int>(\"x\");\n"));
    EXPECT_NE(err.find("expected 'int', got 'string'"), std::string::npos)
        << "the generic constructor call must keep being checked:\n"
        << err;
}

// ---------------------------------------------------------------------------
// Log, the one shape for tool output (issue #62).
//
// `Level`/`Logger` are the issue's names held on the spellings that lower today
// (lib/std/log.fin's header records why the enum form refused), and every
// non-surface fact the module states is pinned as a run: the filter's four
// ranks, the `LOG_LEVEL` override, the blame with the value for an unparseable
// level, the text shape on stderr and never on stdout, the JSONL record with
// the finc contract's kind-first key style, and the escaping that keeps the
// grid one record per line.
//
// The runner here captures the two streams separately because the module's
// stream discipline (stderr, never stdout) IS the shape, and `buildAndRun`
// above folds both into one file. `env` follows `runFinc`'s env shape so the
// two entry points agree on how a child's shell looks.
//
// Timestamped lines vary with the clock, so the pins that assert a line's
// contents dispatch `timestamps(false)` first and the default prefix is
// asserted by contains rather than by equality.

namespace {

struct RunSplit {
    std::string out;
    std::string err;
    bool failed = false;   // didn't compile, or the run died
};

// Build `code` against the bundle with no library flags, run it with `env`
// additions, and return the two streams separately. `failed` keeps the
// "compile failed:"-style diagnostic convention of `buildAndRun` above, with
// the reason in `err`.
RunSplit buildAndRunSplit(const std::string& code,
                          const std::vector<std::pair<std::string, std::string>>& env) {
    RunSplit r;
    Src src(code);
    const fs::path exe = uniqueTempPath("fin_stdlib_exe");
    const fs::path outPath = uniqueTempPath("fin_stdlib_out");
    const fs::path errPath = uniqueTempPath("fin_stdlib_err");
    const FincRun c = runFinc({src.str(), "-o", exe.string()}, {{"FIN_LIBS", ""}});
    if (c.exitCode != 0 || !fs::exists(exe)) {
        r.err = "compile failed: " + stripAnsi(c.err);
        r.failed = true;
        return r;
    }
    const int status = fin::runProcess({exe.string()}, outPath.string(), errPath.string(), env);
    r.out = readProcessOutput(outPath.string());
    r.err = readProcessOutput(errPath.string());
    std::error_code ec;
    fs::remove(exe, ec);
    fs::remove(outPath, ec);
    fs::remove(errPath, ec);
    if (status != 0) r.failed = true;
    return r;
}

} // namespace

TEST(Soundness_Log, TheSurfaceResolves) {
    // Every name the issue lists, at every shape: the type with its rank field
    // and four member builders, `Logger` with its fields, both init rooms, the
    // switch and the four free functions. `Level::Warn()` is the member-spelling
    // substitute the module header documents; the negative probes below are what
    // keep the positive one from passing against a catch-all.
    const std::string err = bundledErr(program(
        "import { Level, Logger, init, init_json, timestamps, debug, info, warn, error } from log::std;\n",
        "  let lvl <auto> = Level::Warn();\n"
        "  let lg <Logger> = Logger{level: Level::Info(), json: false};\n"
        "  let r <int> = lvl.rank;\n"
        "  let j <bool> = lg.json;\n"
        "  init(Level::Error());\n"
        "  init_json(Level::Warn(), true);\n"
        "  timestamps(false);\n"
        "  debug(\"d\"); info(\"i\"); warn(\"w\"); error(\"e\");\n"));
    EXPECT_EQ(errorCount(err), 0u)
        << "Log's surface stopped resolving. The emission body lives in the module,\n"
           "so a failure here is an import, a declaration, or the env plumbing:\n"
        << err;

    // A static that took an argument would pass the positive probe above.
    const std::string arity = bundledErr(program(
        "import { Level } from log::std;\n",
        "  let bad <auto> = Level::Warn(1);\n"));
    EXPECT_NE(errorCount(arity), 0u) << arity;

    // And `.rank` is an int, so an `info` that gained a usefulness parameter or
    // a rank that became a string both show here.
    const std::string typed = bundledErr(program(
        "import { Level } from log::std;\n",
        "  let s <string> = Level::Warn().rank;\n"));
    EXPECT_NE(typed.find("expected 'string', got 'int'"), std::string::npos) << typed;
}

#ifdef FIN_TESTS_HAVE_BACKEND

TEST(Soundness_Log, TextModeEmitsToStderrAndNeverToStdout) {
    // The stream-discipline half of the issue's acceptance ("mirror finc's
    // stdout-reserved rule"): the lines land on fd 2 with the padded label and
    // the message, stdout stays empty, and a debug line under the default
    // (info) filter writes nothing at all.
    const RunSplit r = buildAndRunSplit(
        "import { Level, timestamps, init, debug, info, warn, error } from log::std;\n"
        "fun main() <noret> {\n"
        "    timestamps(false);\n"
        "    init(Level::Info());\n"
        "    debug(\"hidden\");\n"
        "    info(\"the info line\");\n"
        "    warn(\"the warn line\");\n"
        "    error(\"the error line\");\n"
        "}\n", {});
    ASSERT_FALSE(r.failed) << r.err;
    EXPECT_EQ(r.out, "") << "stdout is reserved; log lines go to stderr:\n" << r.out;
    EXPECT_EQ(r.err,
              "[INFO ] the info line\n"
              "[WARN ] the warn line\n"
              "[ERROR] the error line\n");
}

TEST(Soundness_Log, TheFilterRanksAndTheEnvOverrideWins) {
    // The rank order, held as runs: at `init(Level::Error())` only the error
    // call emits; at `LOG_LEVEL=debug` with no init at all, the debug call
    // does — which pins both the default-resolve path and the filter — and at
    // `LOG_LEVEL=info` with `init(Level::Error())`, the info line gets through.
    // Three regimes, the reason `LOG_LEVEL` exists at all.
    const RunSplit silent = buildAndRunSplit(
        "import { Level, timestamps, init, debug, info, warn, error } from log::std;\n"
        "fun main() <noret> {\n"
        "    timestamps(false);\n"
        "    init(Level::Error());\n"
        "    debug(\"d\"); info(\"i\"); warn(\"w\"); error(\"only error\");\n"
        "}\n", {});
    ASSERT_FALSE(silent.failed) << silent.err;
    EXPECT_EQ(silent.out, "") << silent.out;
    EXPECT_EQ(silent.err.find("d"), std::string::npos) << silent.err;
    EXPECT_EQ(silent.err.find("i"), std::string::npos) << silent.err;
    EXPECT_EQ(silent.err.find("w"), std::string::npos) << silent.err;
    EXPECT_NE(silent.err.find("only error"), std::string::npos) << silent.err;

    const RunSplit chatty = buildAndRunSplit(
        "import { timestamps, debug, info } from log::std;\n"
        "fun main() <noret> {\n"
        "    timestamps(false);\n"
        "    debug(\"from the debug level\");\n"
        "    info(\"from the info level\");\n"
        "}\n", {{"LOG_LEVEL", "debug"}});
    ASSERT_FALSE(chatty.failed) << chatty.err;
    EXPECT_NE(chatty.err.find("from the debug level"), std::string::npos) << chatty.err;
    EXPECT_NE(chatty.err.find("from the info level"), std::string::npos) << chatty.err;

    const RunSplit overridden = buildAndRunSplit(
        "import { Level, timestamps, init, info, error } from log::std;\n"
        "fun main() <noret> {\n"
        "    timestamps(false);\n"
        "    init(Level::Error());\n"
        "    info(\"kept\");\n"
        "    error(\"kept too\");\n"
        "}\n", {{"LOG_LEVEL", "info"}});
    ASSERT_FALSE(overridden.failed) << overridden.err;
    EXPECT_NE(overridden.err.find("kept"), std::string::npos) << overridden.err;
}

TEST(Soundness_Log, ABadLogLevelBlamesAndNamesTheValue) {
    // `debug|info|warn|error`, and a bad value aborts with the value in the
    // message — the issue's "never silent" rule as a run. The abort happens at
    // the first emit unless `init` came first, so both spells are pinned.
    for (const char* body : {
             "import { info } from log::std;\n"
             "fun main() <noret> { info(\"x\"); }\n",
             "import { Level, init } from log::std;\n"
             "fun main() <noret> { init(Level::Info()); }\n",
         }) {
        const RunSplit r = buildAndRunSplit(body, {{"LOG_LEVEL", "verbose"}});
        EXPECT_TRUE(r.failed)
            << "every route to a bad LOG_LEVEL must stop the program:\n" << r.err;
        EXPECT_NE(r.err.find("assertion failed: LOG_LEVEL: expected one of"), std::string::npos)
            << r.err;
        EXPECT_NE(r.err.find("got 'verbose'"), std::string::npos)
            << "the message names the value:\n" << r.err;
    }
}

TEST(Soundness_Log, JsonlModeEmitsTheFincContractShape) {
    // kind first, message next, the escaped message, and the record closed —
    // with the finc-style prefix intact; the timestamp, when it rides, is an
    // appended field precisely so this prefix stays the contract's own. The
    // stdout assertion is the same reserve rule, and the quote/backslash
    // escaping is what keeps the JSONL grid one record per line.
    const RunSplit r = buildAndRunSplit(
        "import { Level, timestamps, init_json, info, warn, error } from log::std;\n"
        "fun main() <noret> {\n"
        "    timestamps(false);\n"
        "    init_json(Level::Warn(), true);\n"
        "    info(\"suppressed\");\n"
        "    warn(\"warn body\\nnext line\");\n"
        "    error(\"quote \\\" and backslash \\\\ here\");\n"
        "}\n", {});
    ASSERT_FALSE(r.failed) << r.err;
    EXPECT_EQ(r.out, "") << r.out;
    EXPECT_NE(r.err.find("{\"kind\":\"log\",\"level\":\"warn\",\"message\":\"warn body\\nnext "
                         "line\"}"), std::string::npos)
        << "the record with a newline inside the message must not split:\n" << r.err;
    EXPECT_NE(r.err.find("quote \\\" and backslash \\\\ here"), std::string::npos)
        << "quote and backslash must arrive escaped:\n" << r.err;
    EXPECT_EQ(r.err.find("suppressed"), std::string::npos)
        << "the level filter answers in json mode too:\n" << r.err;
}

TEST(Soundness_Log, TheTimestampPrefixRidesByDefault) {
    // The `time` (#59) half of the shape: by default an emitted line carries an
    // rfc3339 prefix (which ends where the level label begins), and
    // `timestamps(false)` takes it off. Contains, never equality — the clock
    // moves between runs.
    const RunSplit withTs = buildAndRunSplit(
        "import { Level, init, info } from log::std;\n"
        "fun main() <noret> {\n"
        "    init(Level::Info());\n"
        "    info(\"the line\");\n"
        "}\n", {});
    ASSERT_FALSE(withTs.failed) << withTs.err;
    EXPECT_NE(withTs.err.find("Z [INFO ] the line"), std::string::npos)
        << "an rfc3339 prefix ends at 'Z', then the label:\n" << withTs.err;

    const RunSplit off = buildAndRunSplit(
        "import { Level, timestamps, init, info } from log::std;\n"
        "fun main() <noret> {\n"
        "    timestamps(false);\n"
        "    init(Level::Info());\n"
        "    info(\"the line\");\n"
        "}\n", {});
    ASSERT_FALSE(off.failed) << off.err;
    EXPECT_NE(off.err.find("[INFO ] the line"), std::string::npos) << off.err;
    EXPECT_EQ(off.err.find("2026-"), std::string::npos)
        << "the switch turns the prefix off; nothing else does:\n" << off.err;
}

TEST(Soundness_Serde, TheContractResolvesThroughAnImport) {
    // Issue #57's surface, named at the call: the two interfaces import, and
    // the whole json::std free-function family checks at typed call sites.
    // There is deliberately NO call spelled through an interface method --
    // `Outcome<Self>`-returning interface methods do not dispatch yet, and
    // serde.fin's header records that gap (derive is #53's). A pin that
    // pretended otherwise would pass and prove nothing.
    const std::string err = bundledErr(R"serf(
import { Encode, Decode } from serde::std;
import { json_encode_int, json_decode_int, json_encode_str, json_decode_str,
         json_encode_bool, json_decode_bool, json_encode_float, json_decode_float,
         json_encode_int_array, json_decode_int_array,
         json_encode_intmap, json_decode_intmap,
         json_encode_strmap, json_decode_strmap,
         json_escape, json_join2, json_join3, json_join4 } from json::std;
fun main() <noret> {
    let ti <string> = json_encode_int(42);
    let ri <auto> = json_decode_int(ti);
    let tb <string> = json_encode_bool(true);
    let rb <auto> = json_decode_bool(tb);
    let tf <string> = json_encode_float(cast<double>(1.5));
    let rf <auto> = json_decode_float(tf);
    let tx <string> = json_encode_str("text");
    let rx <auto> = json_decode_str(tx);
    let vals <[int]> = [1];
    let ta <string> = json_encode_int_array(vals, 1);
    let ra <auto> = json_decode_int_array(ta);
    let m <{string, int}> = { "k": 1 };
    let tm <string> = json_encode_intmap(m);
    let rm <auto> = json_decode_intmap(tm);
    let sm <{string, string}> = { "k": "v" };
    let ts2 <string> = json_encode_strmap(sm);
    let rs2 <auto> = json_decode_strmap(ts2);
    let esc <string> = json_escape("e");
    let k2 <string> = json_join2("a", "b");
    let k3 <string> = json_join3("a", "b", "c");
    let k4 <string> = json_join4("a", "b", "c", "d");
}
)serf");
    EXPECT_EQ(errorCount(err), 0u)
        << "the serde contract's names stopped resolving:\n" << err;
}

TEST(Soundness_Serde, ACollectionDecoderRefusesTrailingData) {
    // Issue #149: the scalar decoders refuse a remainder after the value and the
    // collection decoders did not, so `"[1,2] junk"` decoded successfully while
    // `"5 garbage"` did not. `lib/std/json.fin` documents a complete-document
    // contract -- a caller's bad data is a named Err, not a prefix match -- and a
    // decoder that accepts a prefix is the wrong half of that promise.
    //
    // All three collection decoders are asserted, because the fix is one rule and a
    // test that covered only the array would let the other two drift apart again.
    const std::string out = buildBundledAndRun(R"FIN(
import { json_decode_int_array, json_decode_intmap, json_decode_strmap,
         json_decode_int } from json::std;
fun main() <noret> {
    // The scalar is the control: it already refused, and still must.
    printf("scalar=%d\n", cast<int>(json_decode_int("5 junk").ok));
    printf("array=%d\n", cast<int>(json_decode_int_array("[1,2] junk").ok));
    printf("intmap=%d\n", cast<int>(json_decode_intmap("{\"a\":1} junk").ok));
    printf("strmap=%d\n", cast<int>(json_decode_strmap("{\"a\":\"b\"} junk").ok));
    // And a clean document still decodes, so the check is a tail check rather
    // than a refusal of everything.
    printf("array_clean=%d\n", cast<int>(json_decode_int_array("[1,2]").ok));
}
)FIN");
    EXPECT_EQ(out,
              "scalar=0\n"
              "array=0\n"
              "intmap=0\n"
              "strmap=0\n"
              "array_clean=1\n")
        << out;
}

TEST(Soundness_Serde, TheRoundTripTheIssueNamesRuns) {
    // The motivating spell, run: encode 42, decode "42", receive 42 through
    // the `Outcome`, and a caller's bad document answers `Err` naming the
    // kind instead of aborting -- the rule that makes the decode side safe
    // to point at user data. Every string is freed; a double free or a leak
    // of the module's contract would end the run here instead of after it.
    EXPECT_EQ(buildBundledAndRun(R"serf(
import { Encode, Decode } from serde::std;
import { json_encode_int, json_decode_int } from json::std;
import { equals, free_str } from strings::std;
fun main() <noret> {
    let text <string> = json_encode_int(42);
    printf("encoded: %s\n", text);
    let back <auto> = json_decode_int(text);
    printf("decoded: %d\n", back.unwrap_or(-1));
    let refused <auto> = json_decode_int("impossible");
    printf("refused: %d %s\n", refused.is_err(), refused.message);
    free_str(text);
}
)serf"),
        R"serf(encoded: 42
decoded: 42
refused: 1 json_decode_int: expected a JSON integer
)serf")
        << "the issue's own round trip changed bytes";
}

TEST(Soundness_Json, TheScalarsRoundTripByteExact) {
    // All four scalar encoders, each with its printed byte shape and the
    // decode that reads it back. The float leg re-encodes the decoded value:
    // byte-identical output is the %.17g round-trip guarantee made visible
    // (and it is what the stage-agreement harness holds the sample to).
    //
    // The literals are locals, not inline macro arguments: MSVC's default
    // preprocessor does not tokenize raw string literals, and `\"` inside
    // the argument breaks it when EXPECT_EQ stringizes (C2017, C3688
    // suffix 'b'). Catch2 documents the same limitation; the workaround is
    // passing an identifier.
    const std::string scalars_src = R"jsonp(
import { json_encode_int, json_decode_int, json_encode_str, json_decode_str,
         json_encode_bool, json_decode_bool, json_encode_float, json_decode_float } from json::std;
import { equals, free_str } from strings::std;
fun main() <noret> {
    let t <string> = json_encode_int(42);
    let r <auto> = json_decode_int(t);
    printf("int %s %d\n", t, r.unwrap_or(-1));
    free_str(t);
    let n <string> = json_encode_int(-7);
    let rn <auto> = json_decode_int(n);
    printf("neg %s %d\n", n, rn.unwrap_or(0));
    free_str(n);
    let b <string> = json_encode_bool(true);
    let rb <auto> = json_decode_bool(b);
    printf("bool %s %d\n", b, rb.unwrap_or(false));
    free_str(b);
    let f <string> = json_encode_float(cast<double>(2.5));
    let rf <auto> = json_decode_float(f);
    printf("float %s eq=%s\n", f, json_encode_float(rf.unwrap_or(cast<double>(0.0))));
    free_str(f);
    let s <string> = json_encode_str("a\"b\\c\n\t");
    let rs <auto> = json_decode_str(s);
    printf("str %s same=%d\n", s, equals(rs.unwrap_or("###"), "a\"b\\c\n\t"));
    free_str(s);
}
)jsonp";
    const std::string scalars_want = R"jsonp(int 42 42
neg -7 -7
bool true 1
float 2.5 eq=2.5
str "a\"b\\c\n\t" same=1
)jsonp";
    EXPECT_EQ(buildBundledAndRun(scalars_src), scalars_want)
        << "the scalar spellings drifted from the module header's grammar";
}

TEST(Soundness_Json, TheArraysAndPrototypeMapsRoundTripByteExact) {
    // The structural half: the `[int]` pair, the `{string, int}` map whose
    // encode sorts keys (`{"a":1,"b":2}` from a map built in the other
    // order -- the determinism contract), and the `{string, string}` map
    // whose values escape and unescape. Reading back is by key, by index.
    // (Raw literals as locals, for the same MSVC preprocessor reason as
    // TheScalarsRoundTripByteExact.)
    const std::string maps_src = R"jsonp(
import { json_encode_int_array, json_decode_int_array, json_encode_intmap, json_decode_intmap,
         json_encode_strmap, json_decode_strmap } from json::std;
import { equals, free_str } from strings::std;
import { HashMap } from hashmap::std;
fun main() <noret> {
    let arr <[int]> = [1, 2, 3];
    let ta <string> = json_encode_int_array(arr, 3);
    let ra <auto> = json_decode_int_array(ta);
    let a2 <auto> = ra.unwrap_or(arr);
    printf("arr %s len=%d v0=%d v2=%d\n", ta, a2.length, a2[0], a2[2]);
    free_str(ta);
    let m <{string, int}> = { "b": 2, "a": 1 };
    let tm <string> = json_encode_intmap(m);
    let rm <auto> = json_decode_intmap(tm);
    let av <int> = 0;
    let bv <int> = 0;
    if (rm.is_ok()) {
        let mp <HashMap<string, int>> = rm.value;
        av = mp["a"];
        bv = mp["b"];
    }
    printf("map %s ok=%d a=%d b=%d\n", tm, rm.is_ok(), av, bv);
    free_str(tm);
    let sm <{string, string}> = { "key": "value with \"quote\"" };
    let ts <string> = json_encode_strmap(sm);
    let rs <auto> = json_decode_strmap(ts);
    let got <string> = "";
    if (rs.is_ok()) {
        let sp <HashMap<string, string>> = rs.value;
        got = sp.get_or("key", "###");
    }
    printf("smap %s eq=%d\n", ts, equals(got, "value with \"quote\""));
    free_str(ts);
}
)jsonp";
    const std::string maps_want = R"jsonp(arr [1,2,3] len=3 v0=1 v2=3
map {"a":1,"b":2} ok=1 a=1 b=2
smap {"key":"value with \"quote\""} eq=1
)jsonp";
    EXPECT_EQ(buildBundledAndRun(maps_src), maps_want)
        << "the structural spellings or the sorted key order changed";
}

TEST(Soundness_Json, ABadDocumentRefusesWithTheKindNamed) {
    // The decode half of "never silent": six malformed documents in, six
    // `Err` messages out -- leading zeros, a stray trailing byte, an
    // unterminated string, a non-ASCII \u escape, an array cut short, a
    // number interrupted by a period -- every one named, none a blame. The
    // pinned wording is the caller-facing surface; a message rewrite is a
    // deliberate change to it.
    // (Raw literals as locals, for the same MSVC preprocessor reason as
    // TheScalarsRoundTripByteExact.)
    const std::string bads_src = R"jsonp(
import { json_decode_int, json_decode_str, json_decode_float, json_decode_int_array } from json::std;
fun main() <noret> {
    blame json_decode_int("01").is_err() && json_decode_int("4x").is_err(),
        "both integer refusals";
    let e1 <auto> = json_decode_int("01");
    let e2 <auto> = json_decode_int("4x");
    let e3 <auto> = json_decode_str("\"no close");
    let e4 <auto> = json_decode_str("\"\\u0080\"");
    let e5 <auto> = json_decode_int_array("[1,2,");
    let e6 <auto> = json_decode_float("1.2.3");
    printf("%s\n%s\n%s\n%s\n%s\n%s\n", e1.message, e2.message, e3.message, e4.message, e5.message, e6.message);
}
)jsonp";
    const std::string bads_want = R"jsonp(json_decode_int: a JSON integer has no leading zeros
json_decode_int: trailing data after the value
json_decode_str: a string never closed
json_decode_str: a \u escape outside ASCII (v0 utf is a gap)
json_decode_int_array: expected an integer near the end
json_decode_float: trailing data after the value
)jsonp";
    EXPECT_EQ(buildBundledAndRun(bads_src), bads_want)
        << "refusal kinds or wording changed";
}

TEST(Soundness_Json, ANonFiniteDoubleIsRefusedNotEncoded) {
    // RFC 8259's number grammar (section 6) spells finite decimals only:
    // there is no `inf`, `-inf` or `nan` token, and `json.loads("inf")`
    // raises. An encoder that prints `%.17g`'s answer for a non-finite
    // double therefore hands back a document no conforming parser accepts.
    //
    // The behavior chosen here is a refusal, not the `null` substitution
    // JSON.stringify makes: a substituted null turns a computed value into
    // absence and the caller never learns, which AGENTS.md forbids
    // ("unsupported runtime operations must produce diagnostics, never
    // silently disappear"). Go's encoding/json takes the same side, refusing
    // NaN/Inf with an error naming the value. The message names the value and
    // why JSON cannot carry it, and the program dies at the call -- no
    // document exists to parse afterwards.
    //
    // The finite leg is pinned in the same test, byte for byte, so a refusal
    // that moved `%.17g`'s output (the `%.17g` contract in the module header
    // and the stage-agreement corpus) fails here rather than later.
    // (Raw literals as locals, for the same MSVC preprocessor reason as
    // TheScalarsRoundTripByteExact.)
    const std::string finite_src = R"jsonp(
import { json_encode_float } from json::std;
import { free_str } from strings::std;
fun main() <noret> {
    let scale <double> = cast<double>(1.0);
    for (let i <int> = 0; i < 300; i++) { scale = scale * cast<double>(10.0); }
    let a <string> = json_encode_float(cast<double>(0.0));
    let b <string> = json_encode_float(cast<double>(2.5));
    let c <string> = json_encode_float(cast<double>(-1.5));
    let d <string> = json_encode_float(cast<double>(0.1));
    let e <string> = json_encode_float(cast<double>(1.0) / cast<double>(3.0));
    let f <string> = json_encode_float(scale);
    printf("%s %s %s %s %s %s\n", a, b, c, d, e, f);
    free_str(a); free_str(b); free_str(c); free_str(d); free_str(e); free_str(f);
}
)jsonp";
    EXPECT_EQ(buildBundledAndRun(finite_src),
              "0 2.5 -1.5 0.10000000000000001 0.33333333333333331 1.0000000000000002e+300\n")
        << "the finite encoding path must not move";

    // One program per value: `blame` aborts, so three values cannot share a
    // run. The `document:` print after the call must never appear -- it is
    // the proof that no string (in particular no `inf`/`nan` text) was
    // returned to a caller who could have put it in a document.
    struct Bad {
        const char* name;   // the word in the diagnostic
        const char* body;   // the Fin expression that makes the value
    };
    const std::vector<Bad> bads = {
        {"+inf", "fun value() <double> { let z <double> = cast<double>(0.0); "
                 "return cast<double>(1.0) / z; }"},
        {"-inf", "fun value() <double> { let z <double> = cast<double>(0.0); "
                 "return z - cast<double>(1.0) / z; }"},
        {"nan", "fun value() <double> { let z <double> = cast<double>(0.0); "
                "return z / z; }"},
    };
    for (const Bad& bad : bads) {
        SCOPED_TRACE(bad.name);
        const std::string out = buildBundledAndRun(
            std::string("import { json_encode_float } from json::std;\n") + bad.body + "\n"
            "fun main() <noret> {\n"
            "    let text <string> = json_encode_float(value());\n"
            "    printf(\"document: [%s]\\n\", text);\n"
            "}\n");
        EXPECT_NE(out.find("program exited non-zero"), std::string::npos)
            << "a non-finite double must refuse, not encode:\n" << out;
        EXPECT_NE(out.find(std::string("json_encode_float: non-finite double (") + bad.name +
                           ") has no JSON spelling; RFC 8259 numbers are finite"),
                  std::string::npos)
            << "the refusal must name the value and the reason:\n" << out;
        EXPECT_EQ(out.find("document:"), std::string::npos)
            << "no encoded text may reach the caller:\n" << out;
    }
}

TEST(Soundness_Fs, WriteStringStopsAtAnEmbeddedNulAndWriteTextDoesNot) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    // `write_string` is declared `(path, content: string)` and writes through
    // `fputs`, so a Fin `string` holding an embedded NUL is written up to that
    // byte -- a `string` has no length to hand `fwrite`. This test pins today's
    // answer and names the binary route that does preserve the byte:
    // `File::write_text(path, text, count)` takes the count explicitly. If
    // `write_string` ever grows a length-taking sibling, this test is the one to
    // re-read with the limitation note in fs.fin.
    const std::string source = R"fsf(
import { mk_tmp_dir, write_string, file_size, remove_file, remove_dir } from fs::std;
import { concat, from_chars, free_str } from strings::std;
import { File } from stdio::std;
fun main() <noret> {
    let made? <string> = mk_tmp_dir("finfs_nul");
    blame made != null;
    // Not "nul.bin": on Windows that is the NUL device, so the file is never created
    // or removed and the test fails for a reason that has nothing to do with NUL bytes.
    let path <string> = concat(made?, "/nulbyte.bin");
    let bytes <[char]> = ['a', cast<char>(0), 'b'];
    let text <string> = from_chars(bytes, 0, 3);
    blame write_string(path, text);
    // `fputs` stops at the NUL: one byte, and the call still reports success.
    printf("fputs=%d\n", cast<int>(file_size(path)));
    blame File::write_text(path, text, 3);
    printf("fwrite=%d\n", cast<int>(file_size(path)));
    blame remove_file(path) && remove_dir(made?);
    delete bytes;
    free_str(text);
    free_str(path);
    free_str(made?);
}
)fsf";
    EXPECT_EQ(buildBundledAndRun(source), "fputs=1\nfwrite=3\n");
}

TEST(Soundness_Fs, EveryCallRunsInATmpdirAndNothingTouchesTheCwd) {
    // Issue #61's shape, in one run: the scratch directory comes from
    // mkdtemp, so no call touches a fixed path and none of them need (or
    // change) the cwd. Printed lines are the pinned byte facts; the booleans
    // a pin cannot see are blames. Teardown removes everything it made: a
    // run that leaks into /tmp fails its own rmdir chain.
    EXPECT_EQ(buildBundledAndRun(R"fsf(
import { mk_tmp_dir, mkdir_p, walk_dir, write_string, read_to_string, file_size,
         is_dir, is_file, file_exists, rename_path, remove_file, remove_dir, free_file_content } from fs::std;
import { equals, concat, free_str } from strings::std;
fun main() <noret> {
    let made? <string> = mk_tmp_dir("finc_fstest");
    blame made != null, "mkdtemp under /tmp";
    let dir <string> = made?;
    blame mkdir_p(concat(dir, "/a/b/c")), "mkdir_p makes the whole chain";
    if (is_dir(concat(dir, "/a/b"))) { printf("leaf=dir\n"); }
    blame !is_file(concat(dir, "/a/b")), "a directory is not a file";
    let file <string> = concat(dir, "/hello.txt");
    blame write_string(file, "hello fin"), "write_string answers true on a real file";
    printf("size=%d\n", cast<int>(file_size(file)));
    let back? <string> = read_to_string(file);
    blame back != null, "the file it just wrote reads back";
    if (equals(back?, "hello fin")) { printf("read=ok\n"); }
    free_file_content(back);
    blame file_size(file) == 9, "fputs writes nine bytes and no terminator";
    let top <[string]> = walk_dir(dir);
    printf("walk=%d\n", top.length);
    blame top.length == 2, "the tmp dir holds exactly hello.txt and a";
    let k <int> = 0;
    while (k < top.length) { free_str(top[k]); k = k + 1; }
    delete top;
    let leafy <string> = concat(dir, "/a/b/c");
    let moved <string> = concat(leafy, "/moved.txt");
    blame rename_path(file, moved), "rename_path moves within the same tmpdir";
    blame !file_exists(file), "the old name stops existing";
    blame is_file(moved), "the moved thing is still a file";
    blame remove_file(moved), "remove_file answers true for a real file";
    let r1 <bool> = remove_dir(leafy);
    let r2 <bool> = remove_dir(concat(dir, "/a/b"));
    let r3 <bool> = remove_dir(concat(dir, "/a"));
    let r4 <bool> = remove_dir(dir);
    blame r1 && r2 && r3 && r4, "the run removes its own dirs bottom-up";
    printf("gone=ok\n");
    free_str(dir);
    free_str(file);
    free_str(leafy);
    free_str(moved);
}
)fsf"),
        R"fsf(leaf=dir
size=9
read=ok
walk=2
gone=ok
)fsf")
        << "the run's own tmpdir facts changed";
}

TEST(Soundness_Fs, AMissingPathIsRoutineAndNeverAFault) {
    // The deliberate contrast with the networking module: a missing file is
    // not a diagnostic, it is null / -1 / an empty walk, the shapes a caller
    // branches on. Three calls, one nonexistent path family, no abort.
    EXPECT_EQ(buildBundledAndRun(R"fsf(
import { read_to_string, file_size, walk_dir } from fs::std;
fun main() <noret> {
    let missing? <string> = read_to_string("/no/such/fin test file");
    if (missing == null) { printf("read=null\n"); } else { printf("read=value\n"); }
    printf("size=%d\n", cast<int>(file_size("/no/such/fin test file")));
    printf("walk=%d\n", walk_dir("/no/such/fin test dir").length);
}
)fsf"),
        R"fsf(read=null
size=-1
walk=0
)fsf")
        << "a missing path stopped being routine";
}

TEST(Soundness_Fs, FreshDirectoriesKeepTheirNamesAndEntriesSeparate) {
    const std::string source = R"fsf(
import { mk_tmp_dir, mkdir_p, write_string, walk_dir, remove_file, remove_dir, is_dir } from fs::std;
import { concat, equals, free_str, substr, to_chars, from_chars } from strings::std;
#c_ifdef FIN_TARGET_WINDOWS
#[llvm_name="_getcwd"]
@define probe_getcwd(buffer: &char, size: int) <string>;
#[llvm_name="_chdir"]
@define probe_chdir(path: string) <int>;
#[llvm_name="GetACP"]
@define probe_code_page() <uint>;
#c_endif
fun main() <noret> {
    let first? <string> = mk_tmp_dir("finfs_names");
    let second? <string> = mk_tmp_dir("finfs_names");
    blame first != null && second != null;
    blame !equals(first?, second?);
    blame is_dir(first?) && is_dir(second?);
    let name <string> = "a name with spaces.txt";
    let stem <string> = concat(first?, "/");
    let path <string> = concat(stem, name);
    free_str(stem);
    blame write_string(path, "one");
    let entries <[string]> = walk_dir(first?);
    blame entries.length == 1;
    blame equals(entries[0], name);
    let empty <[string]> = walk_dir(second?);
    blame empty.length == 0;
    free_str(entries[0]);
    delete entries;
    delete empty;
#c_ifdef FIN_TARGET_WINDOWS
    let first_chars <[char]> = to_chars(first?);
    if (first_chars.length > 2 && first_chars[1] == ':') {
        let cwd_chars <[char]> = new [char, 32768]{};
        let cwd <string> = probe_getcwd(&cwd_chars[0], 32768);
        blame cwd != null;
        blame probe_chdir(first?) == 0;
        let drive <string> = substr(first?, 0, 2);
        let relative <[string]> = walk_dir(drive);
        blame relative.length == 1;
        blame equals(relative[0], name);
        blame probe_chdir(cwd) == 0;
        free_str(relative[0]);
        delete relative;
        free_str(drive);
        delete cwd_chars;
    }
    delete first_chars;
    if (probe_code_page() == cast<uint>(932)) {
        let name_chars <[char]> = [cast<char>(131), cast<char>(92)];
        let dbcs_name <string> = from_chars(name_chars, 0, 2);
        delete name_chars;
        let dbcs_stem <string> = concat(first?, "/");
        let dbcs_dir <string> = concat(dbcs_stem, dbcs_name);
        free_str(dbcs_stem);
        free_str(dbcs_name);
        blame mkdir_p(dbcs_dir);
        let dbcs_prefix <string> = concat(dbcs_dir, "/");
        let dbcs_file <string> = concat(dbcs_prefix, name);
        free_str(dbcs_prefix);
        blame write_string(dbcs_file, "two");
        let dbcs_entries <[string]> = walk_dir(dbcs_dir);
        blame dbcs_entries.length == 1;
        blame equals(dbcs_entries[0], name);
        free_str(dbcs_entries[0]);
        delete dbcs_entries;
        blame remove_file(dbcs_file) && remove_dir(dbcs_dir);
        free_str(dbcs_file);
        free_str(dbcs_dir);
    }
#c_endif
    blame remove_file(path);
    blame remove_dir(first?) && remove_dir(second?);
    free_str(path);
    free_str(first?);
    free_str(second?);
    printf("separate=ok\n");
}
)fsf";
    EXPECT_EQ(buildBundledAndRun(source), "separate=ok\n");
}

TEST(Soundness_Fs, CopiesEmbeddedNulBytesAndSupportsTheSamePath) {
    const std::string source = R"fsf(
import { mk_tmp_dir, copy_file, file_size, remove_file, remove_dir } from fs::std;
import { concat, free_str } from strings::std;
#[llvm_name="fopen"]
@define probe_open(path: string, mode: string) <&void>;
#[llvm_name="fclose"]
@define probe_close(stream: &void) <int>;
#[llvm_name="fwrite"]
@define probe_write(data: &char, size: ulong, count: ulong, stream: &void) <ulong>;
#[llvm_name="fread"]
@define probe_read(data: &char, size: ulong, count: ulong, stream: &void) <ulong>;
fun main() <noret> {
    let made? <string> = mk_tmp_dir("finfs_binary");
    blame made != null;
    let source <string> = concat(made?, "/source.bin");
    let target <string> = concat(made?, "/target.bin");
    let bytes <[char]> = ['a', cast<char>(0), 'b', '\n'];
    let file <&void> = probe_open(source, "wb");
    blame file != null;
    blame probe_write(&bytes[0], cast<ulong>(1), cast<ulong>(4), file) == cast<ulong>(4);
    blame probe_close(file) == 0;
    blame copy_file(source, target);
    blame file_size(target) == cast<long>(4);
    file = probe_open(target, "rb");
    blame file != null;
    let read <[char]> = new [char, 4]{};
    blame probe_read(&read[0], cast<ulong>(1), cast<ulong>(4), file) == cast<ulong>(4);
    blame probe_close(file) == 0;
    blame read[0] == 'a' && read[1] == cast<char>(0) && read[2] == 'b' && read[3] == '\n';
    blame copy_file(source, source);
    blame file_size(source) == cast<long>(4);
    file = probe_open(source, "rb");
    blame file != null;
    blame probe_read(&read[0], cast<ulong>(1), cast<ulong>(4), file) == cast<ulong>(4);
    blame probe_close(file) == 0;
    blame read[0] == 'a' && read[1] == cast<char>(0) && read[2] == 'b' && read[3] == '\n';
    delete bytes;
    delete read;
    blame remove_file(source) && remove_file(target) && remove_dir(made?);
    free_str(source);
    free_str(target);
    free_str(made?);
    printf("binary=ok\n");
}
)fsf";
    EXPECT_EQ(buildBundledAndRun(source), "binary=ok\n");
}

TEST(Soundness_Fs, LongBasenamesRetainTheirWholeDirectoryEntry) {
    const std::string source = R"fsf(
import { mk_tmp_dir, write_string, walk_dir, remove_file, remove_dir } from fs::std;
import { concat, repeat, equals, free_str } from strings::std;
fun main() <noret> {
    let made? <string> = mk_tmp_dir("finlong");
    blame made != null;
    let name <string> = repeat("a", 120);
    let base <string> = concat(made?, "/");
    let path <string> = concat(base, name);
    free_str(base);
    blame write_string(path, "");
    let entries <[string]> = walk_dir(made?);
    blame entries.length == 1;
    blame equals(entries[0], name);
    free_str(entries[0]);
    delete entries;
    blame remove_file(path) && remove_dir(made?);
    free_str(path);
    free_str(name);
    free_str(made?);
    printf("long-name=ok\n");
}
)fsf";
    EXPECT_EQ(buildBundledAndRun(source), "long-name=ok\n");
}

#endif  // FIN_TESTS_HAVE_BACKEND

#ifdef FIN_TESTS_HAVE_BACKEND

TEST(Soundness_Outcome, TheIssueExampleRunsAndSaysSo) {
    // The issue's normative program, verbatim but for the module spelling that
    // ships (`outcome::std`). Every line is a `blame` on a measured value: an
    // Ok carries 80 through `unwrap_or` and an Err falls back to `-1`, so a
    // constructor that swapped its fields, or an `unwrap_or` that read the
    // field it should not, compiles exactly as cleanly and fails exactly here.
    EXPECT_EQ(buildBundledAndRun(
                  "import { Outcome, Ok, Err } from outcome::std;\n"
                  "fun parse_port(text: string) <Outcome<int>> {\n"
                  "    if (text == \"80\") { return Ok::<int>(80); }\n"
                  "    return Err::<int>(\"bad port\");\n"
                  "}\n"
                  "fun main() <noret> {\n"
                  "    let r <Outcome<int>> = parse_port(\"80\");\n"
                  "    blame r.is_ok();\n"
                  "    blame r.unwrap_or(-1) == 80;\n"
                  "    let bad <Outcome<int>> = parse_port(\"xx\");\n"
                  "    blame bad.is_err();\n"
                  "    blame bad.unwrap_or(-1) == -1;\n"
                  "}\n"),
              "");
}

TEST(Soundness_Outcome, ExpectOnAnErrAbortsAndUnreachableCodeStaysUnreached) {
    // `expect(msg)` is `blame self.ok, msg`: the abort happens where the Err
    // was, the message reaches stderr, and the `unreachable` print after it
    // must not be there. The program-exited form of `buildBundledAndRun`'s
    // return names both halves, so this is made to fail on the abort itself.
    const std::string out = buildBundledAndRun(
        "import { Outcome, Err } from outcome::std;\n"
        "fun main() <noret> {\n"
        "    let bad <Outcome<int>> = Err::<int>(\"input only reached expect\");\n"
        "    bad.expect(\"port must be numeric\");\n"
        "    printf(\"unreachable\\n\");\n"
        "}\n");
    EXPECT_NE(out.find("program exited non-zero"), std::string::npos)
        << "an Err's `expect` must abort, not return:\n" << out;
    EXPECT_NE(out.find("assertion failed: port must be numeric"), std::string::npos)
        << "the caller's message is the whole point of the abort:\n" << out;
    EXPECT_EQ(out.find("unreachable"), std::string::npos)
        << "whatever follows an aborted `expect` must not run:\n" << out;
}

TEST(Soundness_Outcome, ThePropagationPatternRelaysAnErrInExecutableForm) {
    // The one pattern the module states, as an executable: a caller relays an
    // Err through `if (!r.ok) { return Err::<int>(r.message); }` and carries a
    // value on with `r.value` (lib/std/outcome.fin's doc comment). Both runs
    // are blamed, so a relay that swapped `ok`'s sense or dropped the value
    // fails here rather than in a caller.
    EXPECT_EQ(buildBundledAndRun(
                  "import { Outcome, Ok, Err } from outcome::std;\n"
                  "fun parse_port(text: string) <Outcome<int>> {\n"
                  "    if (text == \"80\") { return Ok::<int>(80); }\n"
                  "    return Err::<int>(\"bad port\");\n"
                  "}\n"
                  "fun relay(text: string) <Outcome<int>> {\n"
                  "    let r <Outcome<int>> = parse_port(text);\n"
                  "    if (!r.ok) { return Err::<int>(r.message); }\n"
                  "    return Ok::<int>(r.value);\n"
                  "}\n"
                  "fun main() <noret> {\n"
                  "    let good <Outcome<int>> = relay(\"80\");\n"
                  "    let bad <Outcome<int>> = relay(\"xx\");\n"
                  "    blame good.unwrap_or(-1) == 80;\n"
                  "    blame bad.is_err();\n"
                  "    blame bad.unwrap_or(-1) == -1;\n"
                  "}\n"),
              "");
}

TEST(Soundness_Testing, APropertyFindsTheInjectedBugAndNamesTheMinimalInput) {
    // The acceptance test issue #55 asks for: `for_all` over a property with a
    // planted bug aborts the run, and the blame message carries BOTH the first
    // failing input and the shrinker's answer. gen is the identity and the
    // holds-property is `x < 8`, violated from input 100 on -- 100 is the
    // first `gen(100 + i)` the runner sees. The halving chain documented in
    // lib/std/testing.fin's header walks 100 -> 50 -> 25 -> 12 and stops when
    // 6 holds, so the message must name 12, not 8 (a halving shrinker is not a
    // decrement search) and not 0.
    const std::string out = buildBundledAndRun(
        "import { for_all } from testing::std;\n"
        "fun main() <noret> {\n"
        "    for_all(100, 10, (x: int) <int> => x, (x: int) <bool> => x < 8);\n"
        "    printf(\"unreachable\\n\");\n"
        "}\n");
    EXPECT_NE(out.find("program exited non-zero"), std::string::npos)
        << "a failing property aborts the run, that is what a runner does:\n" << out;
    EXPECT_NE(out.find("for_all: property failed at input 100, minimally at 12 (halving shrink)"),
              std::string::npos)
        << "both the first failing input and the minimal one are the message:\n" << out;
    EXPECT_EQ(out.find("unreachable"), std::string::npos)
        << "whatever follows an aborted property must not run:\n" << out;
}

TEST(Soundness_Testing, BenchRefusesUntilTheSubSecondClockLands) {
    // `bench` is the honest stub issue #55 told the assignee to ship: ns/op
    // needs the sub-second clock issue #59 owns, and whole-second wall timing
    // would print a magnitude it does not know. So it refuses loudly instead,
    // through the module's own blame vocabulary, and never calls the body --
    // a refusal must not launder one measured iteration into a claimed rate.
    const std::string out = buildBundledAndRun(
        "import { bench } from testing::std;\n"
        "fun tick() <noret> {\n"
        "    printf(\"tick\\n\");\n"
        "}\n"
        "fun main() <noret> {\n"
        "    bench(\"noop\", 10, tick);\n"
        "    printf(\"unreachable\\n\");\n"
        "}\n");
    EXPECT_NE(out.find("program exited non-zero"), std::string::npos)
        << "bench refuses, it does not guess:\n" << out;
    EXPECT_NE(
        out.find("bench refuses: ns/op needs sub-second timing (issue #59); sec resolution "
                 "lies about the magnitude"),
        std::string::npos)
        << "the refusal names the owner of the gap:\n" << out;
    EXPECT_EQ(out.find("tick"), std::string::npos)
        << "a refused bench never runs the body:\n" << out;
    EXPECT_EQ(out.find("unreachable"), std::string::npos)
        << "whatever follows a refused bench must not run:\n" << out;
}

#endif  // FIN_TESTS_HAVE_BACKEND

TEST(Soundness_BundledStdlib, TheIOResultAndFileSurfacesResolve) {
    const std::string err = bundledErr(program(
        "import { IOResult, File, Stream, PathLike, eprintln_str, println_str,\n"
        "         print_int, flush } from stdio::std;\n",
        "  let r <IOResult<int>> = IOResult::Ok(1);\n"
        "  let v <int> = r.unwrap();\n"
        "  let w <int> = r.unwrap_or(0);\n"
        "  let ok <bool> = r.is_ok();\n"
        "  let path <PathLike> = \"/tmp/fin_stdlib_surface\";\n"
        "  let there <bool> = File::exists(path);\n"
        "  let size <int> = File::size(path);\n"
        "  let wrote <bool> = File::write_text(path, \"x\", 1);\n"
        "  let more <bool> = File::append_text(path, \"y\", 1);\n"
        "  let bytes <[char]> = File::read_all(path);\n"
        "  let s <Stream> = File::open(path);\n"
        "  let t <int> = s.tell();\n"
        "  let left <int> = s.remaining();\n"
        "  s.rewind();\n"
        "  let read <[char]> = s.read_all();\n"
        "  let n <int> = s.write(bytes, 1);\n"
        "  let gone <bool> = File::remove(path);\n"
        "  println_str(\"a\"); eprintln_str(\"b\"); print_int(1); flush();\n"));
    EXPECT_EQ(errorCount(err), 0u) << err;
}

TEST(Soundness_Strings, ParseFloatAcceptsTheGrammarAndRefusesByOutcome) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    // A float parser's failure mode is answering a number for text that is not
    // one, so the table is a literal-to-expected pair for every shape the
    // grammar has: sign, a bare zero, a signed zero, a fraction, an exponent in
    // both directions, and the three spellings libc's `strtod` accepts that a
    // Fin decimal should not (`inf`, `nan`, trailing garbage). Each refusal is
    // `Ok == false` with a message, never a blame: the caller's data is not the
    // program's fault.
    EXPECT_EQ(buildBundledAndRun(R"FIN(
import { parse_float, equals, free_str } from strings::std;
import { Outcome } from outcome::std;
fun check(text: string, expected: double, label: string) <noret> {
    let r <Outcome<double>> = parse_float(text);
    blame r.ok, label;
    blame r.value == expected, label;
}
fun refuse(text: string, why: string) <noret> {
    let r <Outcome<double>> = parse_float(text);
    blame !r.ok, text;
    blame equals(r.message, why), text;
}
fun main() <noret> {
    check("0", cast<double>(0.0), "zero");
    check("-0.0", cast<double>(0.0), "negative zero is zero");
    check("1", cast<double>(1.0), "+1");
    check("+1", cast<double>(1.0), "explicit plus");
    check("-2.5", cast<double>(0.0) - cast<double>(2.5), "negative fraction");
    check("0.125", cast<double>(1.0) / cast<double>(8.0), "binary fraction exact");
    check("1.5e-3", cast<double>(15.0) / cast<double>(10000.0), "negative exponent");
    check("1e10", cast<double>(10000000000.0), "positive exponent");
    check("2E+3", cast<double>(2000.0), "uppercase exponent with plus");
    refuse("", "cannot parse an empty string as a float");
    refuse("abc", "invalid character in float string");
    refuse("1.2.3", "invalid character in float string");
    refuse("1e999", "float overflow in parse_float");
    refuse("1e-999", "float underflow in parse_float");
    refuse("inf", "invalid character in float string");
    refuse("nan", "invalid character in float string");
    refuse(" 1", "invalid character in float string");
    refuse("1 ", "invalid character in float string");
    refuse("-", "invalid character in float string");
    refuse(".5", "invalid character in float string");
    refuse("1.", "invalid character in float string");
    refuse("0x10", "invalid character in float string");
    printf("parse_float ok\n");
}
)FIN"), "parse_float ok\n");
}

TEST(Soundness_BundledStdlib, TheStringsAndMathSurfacesResolve) {
    // The two modules with no draft behind them, so nothing else in the tree would
    // notice if either went missing -- which is exactly why they are held here.
    const std::string err = bundledErr(program(
        "import { Collection } from collection::std;\n"
        "import { len, equals, find, substr, trim, to_upper, split, join,\n"
        "         concat, free_str, to_chars, from_chars } from strings::std;\n"
        "import { min, max, clamp, abs, signum, gcd, lcm, ipow, isqrt,\n"
        "         floor_div, floor_mod, PI, math_sqrt } from math::std;\n",
        "  let n <int> = len(\"hi\");\n"
        "  let eq <bool> = equals(\"a\", \"a\");\n"
        "  let at <int> = find(\"hello\", \"ell\");\n"
        "  let sub <string> = substr(\"hello\", 1, 3);\n"
        "  let t <string> = trim(\"  x  \");\n"
        "  let up <string> = to_upper(\"x\");\n"
        "  let cat <string> = concat(\"a\", \"b\");\n"
        "  let parts <&Collection<string>> = split(\"a,b\", ',');\n"
        "  let joined <string> = join(parts, \",\");\n"
        "  let bytes <[char]> = to_chars(\"hi\");\n"
        "  let back <string> = from_chars(bytes, 0, 2);\n"
        "  free_str(cat);\n"
        "  let lo <int> = min::<int>(1, 2);\n"
        "  let hi <int> = max::<int>(1, 2);\n"
        "  let cl <int> = clamp::<int>(5, 0, 3);\n"
        "  let a <int> = abs::<int>(-1);\n"
        "  let s <int> = signum::<int>(-1);\n"
        "  let g <int> = gcd(12, 18);\n"
        "  let l <int> = lcm(4, 6);\n"
        "  let p <int> = ipow(2, 8);\n"
        "  let q <int> = isqrt(15);\n"
        "  let fd <int> = floor_div(-7, 2);\n"
        "  let fm <int> = floor_mod(-7, 2);\n"
        "  let pi <double> = PI;\n"
        "  let r <double> = math_sqrt(cast<double>(2.0));\n"));
    EXPECT_EQ(errorCount(err), 0u) << err;
}

TEST(Soundness_BundledStdlib, AModuleNamedStringCannotExist) {
    // Why the module is `strings`. This is a lexer fact and not a naming preference: the
    // token is TYPE_STRING before any module resolution happens, so `from string::std`
    // cannot parse whatever a file called `string.fin` contains. Asserted so that a
    // future reader does not "fix" the name.
    const std::string err = bundledErr(
        "import { len } from string::std;\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(err.find("unexpected TYPE_STRING"), std::string::npos)
        << "`string` is a keyword, so a module can never carry that name. If this went\n"
           "green the lexer changed and lib/std/strings.fin's header needs re-reading.\n"
        << err;
}

TEST(Soundness_BundledStdlib, EveryModuleChecksCleanStandalone) {
    // Inverted from KnownDefect_BundledStdlib.TwoModulesDoNotCheckStandalone:
    // the driver no longer preloads the ambient stdio prelude when the root
    // file is itself a library module, so the preload-induced cycle on
    // stdio/error (and enums) is gone and every module checks clean as a
    // root file. Enumerated from the directory rather than hardcoded, so a
    // module gained or lost updates the coverage without a second edit.
    // NOTE for the stdlib/gaps lane: the caveat in
    // docs/guide/12-standard-library-tour.md still describes the old cycle;
    // that file is yours, please drop it.
    // Normalised at the join: CMake bakes FIN_TESTS_DIR with forward slashes,
    // and `operator/` appends with the native separator, so without this the
    // entries below reach finc as mixed spellings like
    // `D:/a/Fin/Fin/tests\..\lib\std\enums.fin`. The compiler answers identity
    // questions canonically, but nothing should hand it a spelling it built by
    // joining.
    const fs::path libDir = (fs::path(testsDir()) / ".." / "lib" / "std").lexically_normal();
    bool sawAny = false;
    for (const auto& entry : fs::directory_iterator(libDir)) {
        if (entry.path().extension() != ".fin") continue;
        sawAny = true;
        const std::string err = stripAnsi(runFinc({entry.path().string()}, {{"FIN_LIBS", ""}}).err);
        EXPECT_EQ(errorCount(err), 0u)
            << entry.path().filename().string() << " does not check clean standalone.\n"
            << err;
    }
    EXPECT_TRUE(sawAny) << "no .fin files under " << libDir.string();
}

TEST(Soundness_Random, RandBuildsAndRepeatsTheSeededSequence) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    const std::string source = R"FIN(
import { seed, rand, random_int, random_float, random_bool } from random::std;
fun main() <noret> {
    seed(cast<uint>(42));
    let first <int> = rand();
    let second <int> = rand();
    blame first >= 0 && second >= 0;
    seed(cast<uint>(42));
    blame rand() == first && rand() == second;
    for (let i <int> = 0; i < 256; i++) {
        let number <int> = random_int(-7, 11);
        blame number >= -7 && number <= 11;
        let fraction <float> = random_float();
        blame fraction >= 0.0 && fraction < 1.0;
        random_bool();
    }
    printf("seeded random ok\n");
}
)FIN";
    for (const std::string level : {"-O0", "-O2"}) {
        SCOPED_TRACE(level);
        EXPECT_EQ(buildAndRun(source, {level}), "seeded random ok\n");
    }
}

TEST(Soundness_BTreeMap, MissingComparatorBlamesInsteadOfCallingNull) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    for (const std::string operation : {"insert(2, 7)", "find(1)", "remove(1)"}) {
        SCOPED_TRACE(operation);
        const std::string source =
            "import { BTreeMap } from btreemap::std;\n"
            "fun main() <noret> {\n"
            "    let values <BTreeMap<int, int>> = BTreeMap::<int, int>{};\n"
            "    values.insert(1, 5);\n"
            "    values." + operation + ";\n}\n";
        for (const std::string level : {"-O0", "-O2"}) {
            SCOPED_TRACE(level);
            const std::string result = buildAndRun(source, {level});
            EXPECT_EQ(result.find("program exited non-zero"), 0u) << result;
            EXPECT_NE(result.find("BTreeMap needs a comparator; use new_btree_map"),
                      std::string::npos) << result;
        }
    }
}

TEST(Soundness_BTreeMap, ComparatorConstructionKeepsOrderingAndCleanup) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    const std::string source = R"FIN(
import { BTreeMap } from btreemap::std;
fun main() <noret> {
    let values <BTreeMap<int, int>> = BTreeMap::<int, int>::new_btree_map(
        (a: int, b: int) <bool> => a < b);
    blame values.insert(3, 30);
    blame values.insert(1, 10);
    blame values.insert(2, 20);
    blame !values.insert(2, 21);
    blame values.key_at(0) == 1 && values.key_at(2) == 3;
    blame values.find(2) == 1 && values.value_at(1) == 21;
    blame values.remove(2) && values.find(2) == -1;
    values.free_btree_map();
    printf("ordered map ok\n");
}
)FIN";
    for (const std::string level : {"-O0", "-O2"}) {
        SCOPED_TRACE(level);
        EXPECT_EQ(buildAndRun(source, {level}), "ordered map ok\n");
    }
}

TEST(Soundness_Random, InclusiveRangesCoverTheFullIntDomainWithoutOverflow) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    const std::string source = R"FIN(
import { seed, random_int } from random::std;
fun main() <noret> {
    seed(cast<uint>(42));
    blame random_int(-2147483648, -2147483648) == -2147483648;
    blame random_int(2147483647, 2147483647) == 2147483647;
    blame random_int(7, -7) == 7;
    let first <int> = random_int(-2147483648, 2147483647);
    seed(cast<uint>(42));
    blame random_int(-2147483648, 2147483647) == first;
    let negative <bool> = false;
    let positive <bool> = false;
    let high <bool> = false;
    for (let i <int> = 0; i < 1024; i++) {
        let whole <int> = random_int(-2147483648, 2147483647);
        if (whole < 0) { negative = true; }
        if (whole > 0) { positive = true; }
        let crossing <int> = random_int(-2147483648, 0);
        blame crossing <= 0;
        let upper <int> = random_int(0, 2147483647);
        blame upper >= 0;
        if (upper > 1073741823) { high = true; }
        let bottom <int> = random_int(-2147483648, -2147483647);
        blame bottom == -2147483648 || bottom == -2147483647;
        let top <int> = random_int(2147483646, 2147483647);
        blame top == 2147483646 || top == 2147483647;
        let ordinary <int> = random_int(-7, 11);
        blame ordinary >= -7 && ordinary <= 11;
    }
    blame negative && positive && high;
    printf("random ranges ok\n");
}
)FIN";
    for (const std::string level : {"-O0", "-O2"}) {
        SCOPED_TRACE(level);
        EXPECT_EQ(buildAndRun(source, {level}), "random ranges ok\n");
    }
}

TEST(Soundness_Random, FifteenBitRuntimeCoversEndpointsAndRejectsIncompleteBuckets) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    const fs::path dir = uniqueTempPath("fin_random_runtime");
    fs::create_directories(dir);
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); }
    } cleanup{dir};
    const fs::path input = dir / "main.fin";
    const fs::path native = dir / "random.c";
    const fs::path object = dir / "main.o";
    const fs::path executable = dir / "program.exe";
    const fs::path output = dir / "output.txt";
    const fs::path error = dir / "error.txt";
    {
        std::ofstream file(input);
        file << R"FIN(
import { random_int } from random::std;
@define fin_random_draws() <int>;
fun main() <noret> {
    blame random_int(-2147483648, 2147483647) == -2147483648;
    blame random_int(-2147483648, 2147483647) == 2147483647;
    blame random_int(0, 2147483647) == 2147483647;
    blame random_int(0, 2) == 2;
    blame fin_random_draws() == 15;
    blame random_int(2147483647, 2147483647) == 2147483647;
    blame random_int(7, -7) == 7;
    blame fin_random_draws() == 15;
    printf("random endpoints and rejection ok\n");
}
)FIN";
    }
    {
        std::ofstream file(native);
        // Each source value fits the Windows C runtime's RAND_MAX. The
        // fourth word is 2^32-1: it belongs to the rejected tail for span 3.
        file << R"C(
static const int draws[] = {
    0, 0, 0,
    32767, 32767, 3,
    32767, 32767, 3,
    32767, 32767, 3,
    0, 0, 2
};
static int consumed;
int rand(void) {
    if (consumed >= 15) return 0;
    return draws[consumed++];
}
int fin_random_draws(void) { return consumed; }
)C";
    }
    for (const std::string level : {"-O0", "-O2"}) {
        SCOPED_TRACE(level);
        const auto compiled = runFinc({input.string(), level, "-c", "-o", object.string()},
                                      {{"FIN_LIBS", ""}});
        ASSERT_EQ(compiled.exitCode, 0) << stripAnsi(compiled.err);
        ASSERT_EQ(fin::runProcess(fin::linkCommand({object.string(), native.string()},
                                                  executable.string()),
                                  output.string(), error.string()), 0)
            << readProcessOutput(error.string());
        ASSERT_EQ(fin::runProcess({executable.string()}, output.string(), error.string()), 0)
            << readProcessOutput(error.string());
        EXPECT_EQ(readProcessOutput(output.string()), "random endpoints and rejection ok\n");
    }
}

TEST(Soundness_Encoding, Base64RejectsMalformedPaddingStructure) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    const std::string source = R"FIN(
import { base64_decode, free_encoded } from encoding::std;
fun main(args: [string]) <noret> {
    let decoded <string> = base64_decode(args[0]);
    free_encoded(decoded);
    printf("accepted\n");
}
)FIN";
    const std::vector<std::string> malformed = {
        "=WFu", "T=Fu", "TW=u", "TQ=A", "====", "A===", "===A", "==AA",
        "TWFu====", "TQ==TQ==", "TWE=TWFu", "TWFuTW=u", "TWFuTQ=A"
    };
    for (const std::string level : {"-O0", "-O2"}) {
        SCOPED_TRACE(level);
        for (const auto& value : malformed) {
            SCOPED_TRACE(value);
            const std::string result = buildAndRun(source, {level}, {value});
            EXPECT_EQ(result.find("program exited non-zero"), 0u) << result;
            EXPECT_NE(result.find("invalid base64 padding"), std::string::npos) << result;
        }
    }
}

TEST(Soundness_Encoding, Base64ValidTailsPreserveDecodedBytesAndTerminator) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    const fs::path dir = uniqueTempPath("fin_base64_bytes");
    fs::create_directories(dir);
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); }
    } cleanup{dir};
    const fs::path input = dir / "main.fin";
    const fs::path native = dir / "bytes.c";
    const fs::path object = dir / "main.o";
    const fs::path executable = dir / "program.exe";
    const fs::path output = dir / "output.txt";
    const fs::path error = dir / "error.txt";
    {
        std::ofstream file(input);
        file << R"FIN(
import { base64_decode, free_encoded } from encoding::std;
@define fin_b64_matches(bytes: string, index: int) <int>;
fun check(encoded: string, index: int) <noret> {
    let decoded <string> = base64_decode(encoded);
    blame fin_b64_matches(decoded, index) == 1;
    free_encoded(decoded);
}
fun main() <noret> {
    let absent <string>;
    check(absent, 0);
    check("", 0);
    check("TQ==", 1);
    check("TWE=", 2);
    check("TWFu", 3);
    check("Zm9vYg==", 4);
    check("Zm9vYmE=", 5);
    check("Zm9vYmFy", 6);
    check("/w==", 7);
    check("/wA=", 8);
    check("/wAB", 9);
    // Structural validity is separate from canonical unused pad bits.
    check("TR==", 1);
    check("TWF=", 2);
    printf("base64 byte tails ok\n");
}
)FIN";
    }
    {
        std::ofstream file(native);
        file << R"C(
#include <string.h>
int fin_b64_matches(const unsigned char* bytes, int index) {
    static const unsigned char expected[][7] = {
        {0}, {'M',0}, {'M','a',0}, {'M','a','n',0},
        {'f','o','o','b',0}, {'f','o','o','b','a',0},
        {'f','o','o','b','a','r',0}, {255,0}, {255,0,0}, {255,0,1,0}
    };
    static const unsigned sizes[] = {1,2,3,4,5,6,7,2,3,4};
    if (!bytes || index < 0 || index >= 10) return 0;
    return memcmp(bytes, expected[index], sizes[index]) == 0;
}
)C";
    }
    for (const std::string level : {"-O0", "-O2"}) {
        SCOPED_TRACE(level);
        const auto compiled = runFinc({input.string(), level, "-c", "-o", object.string()},
                                      {{"FIN_LIBS", ""}});
        ASSERT_EQ(compiled.exitCode, 0) << stripAnsi(compiled.err);
        ASSERT_EQ(fin::runProcess(fin::linkCommand({object.string(), native.string()}, executable.string()),
                                  output.string(), error.string()), 0)
            << readProcessOutput(error.string());
        ASSERT_EQ(fin::runProcess({executable.string()}, output.string(), error.string()), 0)
            << readProcessOutput(error.string());
        EXPECT_EQ(readProcessOutput(output.string()), "base64 byte tails ok\n");
    }
}

namespace {

// Supplemental Windows-branch execution on POSIX uses a child-local DBCS
// fixture. Native Windows uses its real active code page instead. Both routes
// compile the public path operations, without changing the compiler's target.
std::string selectedWindowsPathRun(const std::string& body, const std::string& level) {
    const fs::path dir = uniqueTempPath("fin_windows_paths");
    fs::create_directories(dir);
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); }
    } cleanup{dir};
    const fs::path input = dir / "main.fin";
    const fs::path native = dir / "dbcs.c";
    const fs::path object = dir / "main.o";
    const fs::path executable = dir / "program.exe";
    const fs::path output = dir / "output.txt";
    const fs::path error = dir / "error.txt";
    {
        std::ifstream module(fs::path(testsDir()).parent_path() / "lib/std/path.fin");
        if (!module.good()) return "missing path module";
        std::ofstream file(input);
        file << "#cdef FIN_TARGET_WINDOWS\n" << module.rdbuf() << '\n' << body;
    }
    {
        std::ofstream file(native);
        file << R"C(
#ifndef _WIN32
int IsDBCSLeadByteEx(unsigned cp, unsigned char byte) {
    (void)cp;
    return byte == 0x83;
}
#endif
const char* fin_path_byte_fixture(int index) {
    static const unsigned char values[][24] = {
        {0x83,0x5c,0},
        {'C',':',0x5c,'w','o','r','k',0x5c,0x83,0x5c,0},
        {0x83,0x5c,'/','c','h','i','l','d',0},
        {'C',':',0x5c,0x83,0x5c,0x5c,'.','.',0x5c,'f','i','l','e',0}
    };
    return index >= 0 && index < 4 ? (const char*)values[index] : "";
}
)C";
    }
    const auto compiled = runFinc({input.string(), level, "-c", "-o", object.string()},
                                  {{"FIN_LIBS", ""}});
    if (compiled.exitCode != 0) return "compile failed: " + stripAnsi(compiled.err);
    const int linked = fin::runProcess(fin::linkCommand({object.string(), native.string()},
                                                       executable.string()),
                                      output.string(), error.string());
    if (linked != 0) return "link failed: " + readProcessOutput(error.string());
    const int status = fin::runProcess({executable.string()}, output.string(), error.string());
    return status == 0 ? readProcessOutput(output.string())
                       : "program exited non-zero: " + readProcessOutput(error.string());
}

} // namespace

TEST(Soundness_Path, OwnedResultsDoNotBindToNativePathSymbols) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    const fs::path dir = uniqueTempPath("fin_native_path_symbols");
    fs::create_directories(dir);
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); }
    } cleanup{dir};
    const fs::path native = dir / "paths.c";
    const fs::path input = dir / "main.fin";
    const fs::path object = dir / "main.o";
    const fs::path executable = dir / "program.exe";
    const fs::path output = dir / "output.txt";
    const fs::path error = dir / "error.txt";
    {
        std::ofstream file(native);
        // Model libc's strong symbols and borrowed results on every platform.
        file << "char* basename(char* path) { (void)path; return \"native basename\"; }\n"
             << "char* dirname(char* path) { (void)path; return \"native dirname\"; }\n";
        ASSERT_TRUE(file.good());
    }
    const std::string source = R"FIN(
import { basename, dirname, free_path } from path::std;
import { equals } from strings::std;
fun main() <noret> {
    let name <string> = basename("/a/b/");
    let parent <string> = dirname("/a/b/");
    blame equals(name, "b"), "Fin basename must not bind to the native symbol";
    blame equals(parent, "/a"), "Fin dirname must not bind to the native symbol";
    free_path(name);
    free_path(parent);
    printf("owned path symbols ok\n");
}
)FIN";
    {
        std::ofstream file(input);
        file << source;
        ASSERT_TRUE(file.good());
    }
    for (const std::string level : {"-O0", "-O2"}) {
        SCOPED_TRACE(level);
        const auto compiled = runFinc({input.string(), level, "-c", "-o", object.string()},
                                      {{"FIN_LIBS", ""}});
        ASSERT_EQ(compiled.exitCode, 0) << stripAnsi(compiled.err);
        ASSERT_EQ(fin::runProcess(fin::linkCommand({object.string(), native.string()},
                                                  executable.string()),
                                  output.string(), error.string()), 0)
            << readProcessOutput(error.string());
        ASSERT_EQ(fin::runProcess({executable.string()}, output.string(), error.string()), 0)
            << readProcessOutput(error.string());
        EXPECT_EQ(readProcessOutput(output.string()), "owned path symbols ok\n");
    }
}

TEST(Soundness_Path, WindowsPrefixesAndDbcsTrailBytesRemainIntact) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    const std::string body = R"FIN(
import { equals } from strings::std;
@define IsDBCSLeadByteEx(cp: uint, byte: char) <int>;
@define fin_path_byte_fixture(index: int) <string>;
fun expect(actual: string, expected: string) <noret> {
    blame equals(actual, expected);
    free_path(actual);
}
fun main() <noret> {
    blame is_absolute("C:\\work\\file.txt");
    blame is_absolute("\\\\server\\share");
    blame !is_absolute("C:child") && !is_absolute("\\child");
    expect(basename("C:\\work\\file.txt"), "file.txt");
    expect(dirname("C:\\work\\file.txt"), "C:\\work");
    expect(join("D:\\base", "C:\\work\\file.txt"), "C:\\work\\file.txt");
    expect(join("C:\\base", "\\child"), "C:\\child");
    expect(join("C:\\base", "c:child"), "c:\\base/child");
    expect(join("C:\\base", "D:child"), "D:child");
    expect(join("C:", "child"), "C:child");
    expect(basename("\\\\server\\share\\"), "\\\\server\\share");
    expect(dirname("\\\\server\\share\\file"), "\\\\server\\share");
    expect(extname("\\\\server\\share.name\\"), "");
    expect(stem("\\\\server\\share.name\\"), "\\\\server\\share.name");
    expect(normalize("C:a\\..\\..\\b"), "C:../b");
    expect(normalize("\\\\server\\share\\a\\..\\..\\b"), "//server/share/b");
    expect(basename("\\\\"), "\\");
    expect(dirname("\\\\"), "\\");
    expect(normalize("\\\\"), "/");
    // CP932's 0x83 0x5c character ends in a byte that resembles a separator.
    // Exercise it only when the native ACP recognizes its lead byte. The
    // supplemental POSIX shim always selects it without changing host ACP.
    if (IsDBCSLeadByteEx(cast<uint>(0), cast<char>(131)) != 0) {
        let name <string> = fin_path_byte_fixture(0);
        let input <string> = fin_path_byte_fixture(1);
        expect(basename(input), name);
        expect(dirname(input), "C:\\work");
        expect(join(name, "child"), fin_path_byte_fixture(2));
        expect(normalize(fin_path_byte_fixture(3)), "C:/file");
    }
    printf("Windows path prefixes ok\n");
}
)FIN";
    for (const std::string level : {"-O0", "-O2"}) {
        SCOPED_TRACE(level);
        EXPECT_EQ(selectedWindowsPathRun(body, level), "Windows path prefixes ok\n");
    }
}

TEST(Soundness_Path, WindowsNamespacesAndIncompleteUncHaveNamedRefusals) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    const std::vector<std::pair<std::string, std::string>> cases = {
        {R"(\\\\?\\C:\\file)", "Windows path namespace is not supported"},
        {R"(\\\\.\\pipe\\name)", "Windows path namespace is not supported"},
        {"//?/C:/file", "Windows path namespace is not supported"},
        {R"(\\\\server)", "incomplete UNC path"},
        {"//server/", "incomplete UNC path"},
        {"///server/share", "incomplete UNC path"},
    };
    for (const auto& [path, diagnostic] : cases) {
        SCOPED_TRACE(path);
        for (const std::string operation : {"is_absolute", "basename", "dirname", "normalize"}) {
            SCOPED_TRACE(operation);
            const std::string body = "fun main() <noret> { " + operation + "(\"" + path + "\"); }\n";
            for (const std::string level : {"-O0", "-O2"}) {
                SCOPED_TRACE(level);
                const std::string result = selectedWindowsPathRun(body, level);
                EXPECT_EQ(result.find("program exited non-zero:"), 0u) << result;
                EXPECT_NE(result.find(diagnostic), std::string::npos) << result;
            }
        }
    }
}


// ---------------------------------------------------------------------------
// math::std and argparse::std.
//
// Neither module has a draft under tests/samples/stdlib, and before these tests
// the whole tree touched them only through one signature probe:
// `grep -rn "math::std\|argparse::std" tests/` found the tour's table rows and a
// bare `let q <int> = isqrt(15);` in Soundness_BundledStdlib. A module whose
// header promises "the largest n with n*n <= value" and one whose header
// promises "unmatched dash-args fail parse" therefore had no executable
// assertion behind either promise.
// ---------------------------------------------------------------------------

TEST(Soundness_Math, TheMinimumDividedByMinusOneDoesNotTrap) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    // `-2147483648 / -1` is 2^31, one past the largest int, and the hardware
    // division traps with SIGFPE before any comparison sees it. That is the one
    // input in floor_div's and floor_mod's whole domain that cannot answer a
    // representable value the usual way: the quotient refuses by name, and the
    // remainder is exactly 0 because -1 divides the minimum evenly. A crash is
    // not a refusal, and the module refuses elsewhere (isqrt, gcd) rather than
    // trapping.
    EXPECT_EQ(buildBundledAndRun(R"FIN(
import { floor_div, floor_mod } from math::std;
fun main() <noret> {
    blame floor_mod(-2147483648, -1) == 0;
    blame floor_mod(-2147483648, 2) == 0;
    blame floor_mod(-7, 2) == 1;
    blame floor_div(2147483647, -1) == -2147483647;
    blame floor_div(-2147483647, -1) == 2147483647;
    printf("edges ok\n");
}
)FIN"), "edges ok\n");
    const std::string out = buildBundledAndRun(
        "import { floor_div } from math::std;\n"
        "fun main() <noret> {\n"
        "    floor_div(-2147483648, -1);\n"
        "    printf(\"unreachable\\n\");\n"
        "}\n");
    EXPECT_NE(out.find("floor_div quotient is not representable as an int"), std::string::npos)
        << out;
    EXPECT_EQ(out.find("unreachable"), std::string::npos) << out;
}

TEST(Soundness_Math, IsqrtAnswersEveryNonnegativeIntAndNeverSpins) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    // `isqrt` is a loop over `(guess + 1) * (guess + 1) <= value`. The documented
    // domain is every nonnegative `int`, and the header itself names the
    // boundary a wrong rounding gets wrong (`isqrt(15)` is 3, `isqrt(16)` is 4).
    // The upper end of the domain is the part the loop actually has to survive:
    // 46340 * 46340 is 2147395600 and 46341 * 46341 does not fit in an `int`, so
    // a loop that squares the guess overflows to a negative product and never
    // exits. Every pin below is a measured value, so an overflow reads as a
    // wrong answer instead of a hang.
    EXPECT_EQ(buildBundledAndRun(R"FIN(
import { isqrt } from math::std;
fun main() <noret> {
    blame isqrt(0) == 0;
    blame isqrt(1) == 1;
    blame isqrt(2) == 1;
    blame isqrt(3) == 1;
    blame isqrt(4) == 2;
    blame isqrt(15) == 3;
    blame isqrt(16) == 4;
    blame isqrt(24) == 4;
    blame isqrt(25) == 5;
    // The last value whose root squares without overflowing, then the last two
    // `int`s: 46340 is the answer at both ends, and the loop must stop there.
    blame isqrt(2147395599) == 46339;
    blame isqrt(2147395600) == 46340;
    blame isqrt(2147395601) == 46340;
    blame isqrt(2147483647) == 46340;
    printf("isqrt ok\n");
}
)FIN"),
              "isqrt ok\n");
}

TEST(Soundness_Math, ADegreeOfNegativeIntInputIsStillRefused) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    // The one refusal `isqrt` states: no integer square root of a negative
    // number. It must abort rather than answer a made-up root, which is the
    // shape a `while` with a signed bound would otherwise take.
    const std::string out = buildBundledAndRun(
        "import { isqrt } from math::std;\n"
        "fun main() <noret> {\n"
        "    isqrt(0 - 1);\n"
        "    printf(\"unreachable\\n\");\n"
        "}\n");
    EXPECT_EQ(out.find("program exited non-zero"), 0u) << out;
    EXPECT_NE(out.find("no integer square root of a negative number"), std::string::npos) << out;
    EXPECT_EQ(out.find("unreachable"), std::string::npos) << out;
}

TEST(Soundness_ArgParse, AParseCountLargerThanItsArrayRefusesInsteadOfReadingPastIt) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    // `parse(args, count)` walks `args[i]` for every `i < count`. A `count` the
    // caller measured from somewhere other than this array is the ordinary way
    // to be wrong here (`main(args)` hands one number, a sliced buffer wants
    // another), and reading past the array's extent is a segfault in a language
    // whose parse failures are supposed to be `error_msg` + `false`. The
    // module's own style is the answer: set the message, answer false, let the
    // caller print it. Nothing here may crash, and nothing may print
    // "unreachable".
    const std::string out = buildBundledAndRun(R"FIN(
import { ArgParser, new_arg_parser, free_arg_parser } from argparse::std;
import { equals } from strings::std;
fun main() <noret> {
    let parser <ArgParser> = new_arg_parser("prog", "d");
    parser.add_flag("verbose", "-v", "noisy");
    let short <[string]> = new [string, 1]{};
    short[0] = "-v";
    let ok <bool> = parser.parse(short, 4);
    blame !ok;
    blame equals(parser.error_msg, "parse count exceeds the argument array");
    // A count that fits is still honoured, so the refusal is about the extent
    // and not a blanket rejection.
    let again <bool> = parser.parse(short, 1);
    blame again;
    blame parser.get_flag("verbose");
    free_arg_parser(parser);
    printf("argparse extent ok\n");
}
)FIN");
    EXPECT_EQ(out, "argparse extent ok\n");
}

TEST(Soundness_ArgParse, ANegativeCountRefusesInsteadOfParsingNothing) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    // `while (i < count)` with a negative `count` runs zero times and answers
    // true, so a caller whose extent arithmetic went below zero is told their
    // command line parsed. Refusing is the only answer that does not launder a
    // bad extent into a successful empty parse.
    const std::string out = buildBundledAndRun(R"FIN(
import { ArgParser, new_arg_parser, free_arg_parser } from argparse::std;
import { equals } from strings::std;
fun main() <noret> {
    let parser <ArgParser> = new_arg_parser("prog", "d");
    parser.add_flag("verbose", "-v", "noisy");
    let args <[string]> = ["-v"];
    let ok <bool> = parser.parse(args, 0 - 1);
    blame !ok;
    blame equals(parser.error_msg, "parse count cannot be negative");
    free_arg_parser(parser);
    printf("argparse negative ok\n");
}
)FIN");
    EXPECT_EQ(out, "argparse negative ok\n");
}

TEST(Soundness_ArgParse, ANullArgumentRefusesInsteadOfReachingStrncmp) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    // `new [string, n]{}` zero-fills, so a caller who allocates a table and only
    // fills part of it hands `parse` a null element. The positional check calls
    // `arg_strncmp(arg, "-", 1)` on whatever is there, and passing null to libc
    // is a crash rather than a refusal. Every other parse failure in this module
    // is `error_msg` + `false`; a null argument is one more of those.
    const std::string out = buildBundledAndRun(R"FIN(
import { ArgParser, new_arg_parser, free_arg_parser } from argparse::std;
import { equals } from strings::std;
fun main() <noret> {
    let parser <ArgParser> = new_arg_parser("prog", "d");
    parser.add_flag("verbose", "-v", "noisy");
    let args <[string]> = new [string, 2]{};
    args[0] = "-v";
    let ok <bool> = parser.parse(args, 2);
    blame !ok;
    blame equals(parser.error_msg, "parse argument is null");
    free_arg_parser(parser);
    printf("argparse null ok\n");
}
)FIN");
    EXPECT_EQ(out, "argparse null ok\n");
}

TEST(Soundness_Math, GcdNeverAnswersANegativeOrAWrappedMagnitude) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    // `gcd`'s header says why it takes absolute values: "without it `gcd(-12, 18)`
    // would be -6". So a non-negative answer is the module's stated contract, and
    // the one input that breaks it is the `int` edge: gcd(-2147483648, 0) is
    // 2^31, which does not fit in an `int`, and negating the minimum wraps to
    // itself. This module refuses an unrepresentable result elsewhere
    // (`ipow`'s negative exponent, `isqrt`'s negative radicand), so the edge is
    // held to the same rule: name it, do not answer a wrong sign.
    const std::string out = buildBundledAndRun(R"FIN(
import { gcd, lcm } from math::std;
fun main() <noret> {
    blame gcd(12, 18) == 6;
    blame gcd(-12, 18) == 6;
    blame gcd(12, -18) == 6;
    blame gcd(-12, -18) == 6;
    blame gcd(0, 5) == 5;
    blame gcd(5, 0) == 5;
    blame gcd(0, 0) == 0;
    blame gcd(1, 0) == 1;
    // Multiples of the minimum's magnitude are still representable.
    blame gcd(-2147483648, 4) == 4;
    blame gcd(-2147483648, 8) == 8;
    printf("gcd ok\n");
}
)FIN");
    EXPECT_EQ(out, "gcd ok\n");
}

TEST(Soundness_Math, GcdOfTheMinimumsMagnitudeBlamesRatherThanWrapping) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    // gcd(-2147483648, 0) is 2^31. The alternative to refusing is returning
    // -2147483648 -- a negative "greatest common divisor", which is a wrong
    // answer instead of an honest refusal.
    const std::string out = buildBundledAndRun(
        "import { gcd } from math::std;\n"
        "fun main() <noret> {\n"
        "    gcd(-2147483648, 0);\n"
        "    printf(\"unreachable\\n\");\n"
        "}\n");
    EXPECT_EQ(out.find("program exited non-zero"), 0u) << out;
    EXPECT_NE(out.find("gcd of these inputs is not representable as an int"), std::string::npos)
        << out;
    EXPECT_EQ(out.find("unreachable"), std::string::npos) << out;
}

// An imported `const` is a real global, not a name that only its own module can see.
//
// `declareGlobals` walked only the root program's statements, so a module-scope `let` or
// `const` never became an llvm::GlobalVariable. A function inside the module could not
// read it either -- the reference to the name reached codegen and was refused:
//
//   error: codegen: the name 'PREC_OR' is not lowered yet
//
// 168 `pub const` declarations across lib/std are unusable from outside their module
// because of this: parse's precedence table, token's kinds, ascii's classes. The fix walks
// the loaded modules after the root, matching the first-wins rule `declareInterfaces`
// already follows, so a root declaration of the same name keeps its storage.
TEST(Soundness_Modules, AnImportedConstIsReadableFromTheImportingProgram) {
    TempModuleDir d;
    d.write("constmod.fin",
            "namespace constmod {\n"
            "pub const PREC_OR <int> = 1;\n"
            "pub const PREC_AND <int> = 2;\n"
            "}\n");
    EXPECT_EQ(buildWithModuleAndRun("import { PREC_OR, PREC_AND } from constmod::constmod;\n"
                                    "fun main() <noret> {\n"
                                    "    printf(\"%d %d\\n\", PREC_OR, PREC_AND);\n"
                                    "}\n", d.path()),
              "1 2\n");
}

TEST(Soundness_Modules, AModuleScopeLetIsReadableThroughItsOwnFunction) {
    // The `let` form, reached through the module's own accessor rather than by name. This
    // is the shape zhttp needed for a process-wide default and had to work around with an
    // out-parameter: `let z_t <int> = 30000;` in a module, read by a function in that
    // module, called from the root.
    TempModuleDir d;
    d.write("statemod.fin",
            "namespace statemod {\n"
            "let counter <int> = 30000;\n"
            "#[export] pub fun get_counter() <int> { return counter; }\n"
            "}\n");
    EXPECT_EQ(buildWithModuleAndRun("import { get_counter } from statemod::statemod;\n"
                                    "fun main() <noret> {\n"
                                    "    printf(\"%d\\n\", get_counter());\n"
                                    "}\n", d.path()),
              "30000\n");
}

TEST(Soundness_Modules, ARootDeclarationAndAModuleDeclarationDoNotBothAllocate) {
    // Two units may declare the same name: `shared` here exists in the root and in a
    // loaded module. This pins what happens rather than deciding what should.
    //
    // Observed: the root's value answers everywhere, so the module's accessor returns 3
    // and not its own 900. That is a *name resolution* result -- the module's reference to
    // `shared` binds to the root's global -- not a storage result: the backend allocates
    // one symbol, and the module's accessor reads it.
    //
    // Whether a module's own declaration should shadow the root's inside that module is an
    // open language question, not a codegen bug, so it is recorded here instead of being
    // settled by an implementation accident. What this test does prove is the invariant
    // that matters for the imported-const fix above: a duplicate does not allocate two
    // symbols and does not fail the link.
    TempModuleDir d;
    d.write("sharedmod.fin",
            "namespace sharedmod {\n"
            "pub const shared <int> = 900;\n"
            "#[export] pub fun module_shared() <int> { return shared; }\n"
            "}\n");
    const std::string out = buildWithModuleAndRun(
        "import { module_shared } from sharedmod::sharedmod;\n"
        "const shared <int> = 3;\n"
        "fun main() <noret> {\n"
        "    printf(\"%d %d\\n\", shared, module_shared());\n"
        "}\n", d.path());
    EXPECT_EQ(out, "3 3\n")
        << "today the root's global answers for both names. If a ruling makes the module's "
           "own declaration shadow the root's inside that module, this becomes \"3 900\" "
           "and that is the line to change.";
}
