#include "Corpus.hpp"
#include "utils/Process.hpp"
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

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
    #c_ifdef FIN_TARGET_WINDOWS
    blame run_command("exit 7") == 7;
    #c_else
    blame run_command("exit 7") == 1792;
    #c_endif
    let captured <ProcessResult> = exec_command("echo first&&echo second");
    blame is_success(captured);
    blame equals(captured.output, "first\nsecond\n");
    free_process_result(captured);
    let failed <ProcessResult> = exec_command("exit 7");
    blame !is_success(failed);
    #c_ifdef FIN_TARGET_WINDOWS
    blame failed.exit_code == 7;
    #c_else
    blame failed.exit_code == 1792;
    #c_endif
    blame equals(failed.output, "");
    free_process_result(failed);
    printf("process ok\n");
}
)FIN";
    EXPECT_EQ(buildRuntimeAndRun(source), "process ok\n");
}
