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

#include "Corpus.hpp"
#include "utils/Process.hpp"
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

std::string buildRuntimeAndRun(const std::string& source) {
    namespace fs = std::filesystem;
    using namespace fin::testing;
    const fs::path dir = uniqueTempPath("fin_runtime_portability");
    fs::create_directories(dir);
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); }
    } cleanup{dir};
    const fs::path input = dir / "main.fin";
    const fs::path exe = dir / "program.exe";
    const fs::path output = dir / "output.txt";
    {
        std::ofstream file(input, std::ios::binary);
        file << source;
    }
    const auto compiled = runFinc({input.string(), "-o", exe.string()}, {{"FIN_LIBS", ""}});
    if (compiled.exitCode != 0) return "compile failed: " + stripAnsi(compiled.err);
    const int status = fin::runProcess({exe.string()}, output.string(), output.string());
    const std::string text = readProcessOutput(output.string());
    if (status != 0) return "run failed: " + std::to_string(status) + "\n" + text;
    return text;
}

}

TEST(Soundness_RuntimePortability, TimeMeasuresSleepAndFormatsTheEpoch) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    const std::string source = R"FIN(
import { now, sleep_sec, sleep_ms, Instant, Duration, format_rfc3339, parse_rfc3339, free_rfc3339 } from time::std;
import { equals } from strings::std;
fun main() <noret> {
    blame now() > 1700000000;
    let start <Instant> = Instant::now();
    sleep_sec(cast<uint>(0));
    sleep_ms(cast<uint>(20));
    let duration <Duration> = start.elapsed();
    blame duration.secs >= 0;
    blame duration.nanos >= 0 && duration.nanos < 1000000000;
    blame duration.as_ms() >= 10;
    blame start.elapsed_ns() >= 10000000;
    let deadline <Instant> = Instant::deadline_after_ms(0);
    blame deadline.is_expired();
    let future <Instant> = Instant::deadline_after_ms(10000);
    blame future.nanos >= start.nanos + 10000000000 && !future.is_expired();
    let epoch <string> = format_rfc3339(0);
    blame equals(epoch, "1970-01-01T00:00:00Z");
    blame parse_rfc3339(epoch) == 0;
    free_rfc3339(epoch);
    printf("time ok\n");
}
)FIN";
    EXPECT_EQ(buildRuntimeAndRun(source), "time ok\n");
}

TEST(Soundness_RuntimePortability, EnvironmentRoundTripsAndRejectsInvalidNames) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    const std::string source = R"FIN(
import { get_env, has_env, set_env, unset_env, setenv, get_pid } from env::std;
import { equals } from strings::std;
fun main() <noret> {
    let key <string> = "FIN_RUNTIME_PORTABILITY_TEST";
    blame get_pid() > 0;
    blame unset_env(key);
    blame !has_env(key);
    blame equals(get_env(key, "fallback"), "fallback");
    blame set_env(key, "first value");
    blame has_env(key);
    blame equals(get_env(key, "fallback"), "first value");
    blame setenv(key, "ignored", 0) == 0;
    blame equals(get_env(key, "fallback"), "first value");
    blame set_env(key, "replacement");
    blame equals(get_env(key, "fallback"), "replacement");
    blame !set_env("", "invalid");
    blame !set_env("invalid=name", "invalid");
    blame !unset_env("");
    blame !unset_env("invalid=name");
    #c_ifdef FIN_TARGET_WINDOWS
    blame !set_env(key, "");
    blame equals(get_env(key, "fallback"), "replacement");
    #c_else
    blame set_env(key, "");
    blame has_env(key);
    blame equals(get_env(key, "fallback"), "");
    #c_endif
    blame unset_env(key);
    blame unset_env(key);
    blame !has_env(key);
    printf("environment ok\n");
}
)FIN";
    EXPECT_EQ(buildRuntimeAndRun(source), "environment ok\n");
}

TEST(Soundness_RuntimePortability, ProcessCapturesOutputAndPreservesFailureStatus) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    const std::string source = R"FIN(
import { run_command, exec_command, ProcessResult, is_success, free_process_result } from process::std;
import { equals } from strings::std;
fun main() <noret> {
    blame run_command("exit 0") == 0;
    // 7 on every platform. This asserted 1792 on POSIX, which is 7 << 8 -- the raw wait
    // status `system` returns. Pinning the encoding as if it were the answer is what let
    // the defect stand: the test agreed with the bug. Issue #126.
    blame run_command("exit 7") == 7;
    let captured <ProcessResult> = exec_command("echo first&&echo second");
    blame is_success(captured);
    blame equals(captured.output, "first\nsecond\n");
    free_process_result(captured);
    let failed <ProcessResult> = exec_command("exit 7");
    blame !is_success(failed);
    blame failed.exit_code == 7;
    blame equals(failed.output, "");
    free_process_result(failed);
    printf("process ok\n");
}
)FIN";
    EXPECT_EQ(buildRuntimeAndRun(source), "process ok\n");
}

TEST(Soundness_RuntimePortability, DeadlineOffsetsAreCheckedAgainstTheClockRange) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    namespace fs = std::filesystem;
    using namespace fin::testing;
    const fs::path dir = uniqueTempPath("fin_deadline_bounds");
    fs::create_directories(dir);
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); }
    } cleanup{dir};
    const fs::path input = dir / "main.fin";
    const fs::path native = dir / "clock.c";
    const fs::path object = dir / "main.o";
    const fs::path executable = dir / "program.exe";
    const fs::path output = dir / "output.txt";
    const fs::path error = dir / "error.txt";
    {
        std::ofstream file(native);
        // The clock leaves exactly 2,775,807 representable nanoseconds.
        // 2ms is valid; 3ms overflows addition without overflowing ms*1e6.
        file << R"C(
#ifdef _WIN32
#include <windows.h>
int fin_deadline_frequency(long long* frequency) {
    *frequency = 1000000000LL;
    return 1;
}
int fin_deadline_counter(long long* ticks) {
    *ticks = 9223372036852000000LL;
    return 1;
}
int fin_deadline_host_clock_ready(void) {
    LARGE_INTEGER frequency, ticks;
    return QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0 &&
           QueryPerformanceCounter(&ticks) && ticks.QuadPart >= 0;
}
#else
#include <time.h>
int fin_deadline_clock_gettime(clockid_t id, struct timespec* result) {
    (void)id;
    result->tv_sec = 9223372036LL;
    result->tv_nsec = 852000000;
    return 0;
}
int fin_deadline_host_clock_ready(void) {
    struct timespec now;
    return clock_gettime(CLOCK_MONOTONIC, &now) == 0 && now.tv_sec >= 0;
}
#endif
)C";
    }
    // Test clocks must not replace host API symbols used by the CRT/import library.
    std::ifstream module(fs::path(testsDir()).parent_path() / "lib/std/time.fin");
    ASSERT_TRUE(module.good());
    std::string testModule((std::istreambuf_iterator<char>(module)),
                           std::istreambuf_iterator<char>());
    for (const auto& [symbol, replacement] : std::vector<std::pair<std::string, std::string>>{
             {"QueryPerformanceCounter", "fin_deadline_counter"},
             {"QueryPerformanceFrequency", "fin_deadline_frequency"},
             {"clock_gettime", "fin_deadline_clock_gettime"}}) {
        const std::string attribute = "#[llvm_name=\"" + symbol + "\"]";
        const auto at = testModule.find(attribute);
        ASSERT_NE(at, std::string::npos) << symbol;
        ASSERT_EQ(testModule.find(attribute, at + attribute.size()), std::string::npos) << symbol;
        testModule.replace(at, attribute.size(), "#[llvm_name=\"" + replacement + "\"]");
    }
    const fs::path modules = dir / "lib";
    fs::create_directories(modules);
    {
        std::ofstream file(modules / "time.fin");
        file << testModule;
        ASSERT_TRUE(file.good());
    }
    const std::vector<std::pair<std::string, std::string>> cases = {
        {R"FIN(
blame fin_deadline_host_clock_ready() == 1, "the real host clock remains callable";
let zero <Instant> = Instant::deadline_after_ms(0);
let one <Instant> = Instant::deadline_after_ms(1);
let two <Instant> = Instant::deadline_after_ms(2);
blame zero.nanos == 9223372036852000000;
blame one.nanos == 9223372036853000000;
blame two.nanos == 9223372036854000000;
blame zero.is_expired() && !one.is_expired() && !two.is_expired();
printf("deadline bounds ok\n");
)FIN", ""},
        {"Instant::deadline_after_ms(-1);", "a deadline offset is nonnegative"},
        {"Instant::deadline_after_ms(9223372036855);", "deadline exceeds the Instant nanosecond range"},
        {"Instant::deadline_after_ms(3);", "deadline exceeds the Instant nanosecond range"},
    };
    for (const auto& [body, diagnostic] : cases) {
        SCOPED_TRACE(body);
        {
            std::ofstream file(input);
            file << "import { Instant } from time::std;\n"
                 << "@define fin_deadline_host_clock_ready() <int>;\n"
                 << "fun main() <noret> {\n" << body << "\n}\n";
        }
        for (const std::string level : {"-O0", "-O2"}) {
            SCOPED_TRACE(level);
            const auto compiled = runFinc({input.string(), level, "-I", modules.string(),
                                          "-c", "-o", object.string()}, {{"FIN_LIBS", ""}});
            ASSERT_EQ(compiled.exitCode, 0) << stripAnsi(compiled.err);
            ASSERT_EQ(fin::runProcess(fin::linkCommand({object.string(), native.string()},
                                                      executable.string()),
                                      output.string(), error.string()), 0)
                << readProcessOutput(error.string());
            const int status = fin::runProcess({executable.string()}, output.string(), error.string());
            if (diagnostic.empty()) {
                EXPECT_EQ(status, 0) << readProcessOutput(error.string());
                EXPECT_EQ(readProcessOutput(output.string()), "deadline bounds ok\n");
            } else {
                EXPECT_NE(status, 0);
                EXPECT_NE(readProcessOutput(error.string()).find(diagnostic), std::string::npos)
                    << readProcessOutput(error.string());
            }
        }
    }
}
