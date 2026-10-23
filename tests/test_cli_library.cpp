#include "Corpus.hpp"
#include "utils/Process.hpp"
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct CliRun {
    int status = -1;
    std::string out;
    std::string err;
};

CliRun runCliLibrary(const std::string& source, const std::vector<std::string>& args = {}) {
    namespace fs = std::filesystem;
    using namespace fin::testing;
    const fs::path dir = uniqueTempPath("fin_cli_library");
    fs::create_directories(dir);
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); }
    } cleanup{dir};
    const fs::path input = dir / "main.fin";
    const fs::path exe = dir / "program.exe";
    const fs::path output = dir / "output.txt";
    const fs::path error = dir / "error.txt";
    {
        std::ofstream file(input, std::ios::binary);
        file << source;
    }
    const auto compiled = runFinc({input.string(), "-o", exe.string()}, {{"FIN_LIBS", ""}});
    if (compiled.exitCode != 0) return {-1, "", "compile failed: " + stripAnsi(compiled.err)};
    std::vector<std::string> command{exe.string()};
    command.insert(command.end(), args.begin(), args.end());
    const int status = fin::runProcess(command, output.string(), error.string());
    return {status, readProcessOutput(output.string()), readProcessOutput(error.string())};
}

}

TEST(Soundness_CliLibrary, ProcessArgumentsPreserveEmptyLongAndNumerousValues) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    const std::string source = R"FIN(
import { Command, Match, Outcome, free_match, free_command } from cli::std;
import { equals } from strings::std;
fun main(args: [string]) <noret> {
    let command <Command> = Command::create("argv", "argv preservation");
    let result <Outcome<Match>> = command.parse_args();
    blame result.is_ok();
    let matched <Match> = result.value;
    blame matched.pos_count == args.length;
    for (let i <int> = 0; i < args.length; i++) {
        blame equals(matched.positional(i), args[i]);
    }
    free_match(matched);
    free_command(command);
    printf("argv ok\n");
}
)FIN";
    std::vector<std::string> args{"", std::string(5000, 'q'), "spaces and \"quotes\"", "back\\slash\\"};
    for (int i = 0; i < 300; ++i) args.push_back("argument " + std::to_string(i));
    const auto run = runCliLibrary(source, args);
    ASSERT_EQ(run.status, 0) << run.err;
    EXPECT_EQ(run.out, "argv ok\n");
    EXPECT_TRUE(run.err.empty()) << run.err;
}

TEST(Soundness_CliLibrary, ExplicitArgumentsKeepEmptyValuesAndHonorTheDelimiter) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    const std::string source = R"FIN(
import { Command, Match, Outcome, free_match, free_command } from cli::std;
import { equals } from strings::std;
fun main() <noret> {
    let command <Command> = Command::create("parse", "explicit arguments");
    command.add_arg("name", 'n', true, "default", "a name");
    let args <[string]> = ["--name", "", "--", "--help", "--name", "value", ""];
    let result <Outcome<Match>> = command.parse(args);
    delete args;
    blame result.is_ok();
    let matched <Match> = result.value;
    blame matched.is_set("name");
    blame equals(matched.get("name"), "");
    blame matched.pos_count == 4;
    blame equals(matched.positional(0), "--help");
    blame equals(matched.positional(1), "--name");
    blame equals(matched.positional(2), "value");
    blame equals(matched.positional(3), "");
    free_match(matched);
    free_command(command);
    printf("parse ok\n");
}
)FIN";
    const auto run = runCliLibrary(source);
    ASSERT_EQ(run.status, 0) << run.err;
    EXPECT_EQ(run.out, "parse ok\n");
    EXPECT_TRUE(run.err.empty()) << run.err;
}

TEST(Soundness_CliLibrary, RegistrationsGrowWithoutDroppingOptionsOrCommands) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    const std::string source = R"FIN(
import { Command, Match, Outcome, free_match, free_command } from cli::std;
import { equals } from strings::std;
fun main() <noret> {
    let command <Command> = Command::create("grow", "registration growth");
    for (let i <int> = 0; i < 80; i++) { command.arg("unused", false); }
    command.add_arg("last", 'z', true, "default", "last registration");
    let children <[&Command]> = new [&Command, 40]{};
    for (let i <int> = 0; i < children.length; i++) {
        children[i] = Command::make_sub("unused", "unused command");
        command.sub(children[i]);
    }
    let last <&Command> = Command::make_sub("last-command", "last command");
    command.sub(last);
    let args <[string]> = ["last-command", "-z", "kept"];
    let result <Outcome<Match>> = command.parse(args);
    delete args;
    blame result.is_ok();
    let matched <Match> = result.value;
    blame matched.is_set("last");
    blame equals(matched.get("last"), "kept");
    blame equals(matched.subcommand(), "last-command");
    free_match(matched);
    for (let i <int> = 0; i < children.length; i++) {
        free_command(*children[i]);
        delete children[i];
    }
    delete children;
    free_command(*last);
    delete last;
    free_command(command);
    printf("registrations ok\n");
}
)FIN";
    const auto run = runCliLibrary(source);
    ASSERT_EQ(run.status, 0) << run.err;
    EXPECT_EQ(run.out, "registrations ok\n");
    EXPECT_TRUE(run.err.empty()) << run.err;
}

TEST(Soundness_CliLibrary, TheDelimiterBypassesRegisteredSubcommands) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    const std::string source = R"FIN(
import { Command, Match, Outcome, free_command, free_parse_result } from cli::std;
import { equals } from strings::std;
fun expect_positional(command: &Command, args: [string], value: string) <noret> {
    let result <Outcome<Match>> = command.parse(args);
    delete args;
    blame result.is_ok();
    blame equals(result.value.subcommand(), "");
    blame result.value.pos_count == 1;
    blame equals(result.value.positional(0), value);
    free_parse_result(result);
}
fun main() <noret> {
    let command <Command> = Command::create("delimiter", "positional arguments");
    let child <&Command> = Command::make_sub("run", "registered command");
    command.sub(child);
    expect_positional(&command, ["--", "--help"], "--help");
    expect_positional(&command, ["--", "run"], "run");
    free_command(*child);
    delete child;
    free_command(command);
    printf("delimiter ok\n");
}
)FIN";
    const auto run = runCliLibrary(source);
    ASSERT_EQ(run.status, 0) << run.err;
    EXPECT_EQ(run.out, "delimiter ok\n");
    EXPECT_TRUE(run.err.empty()) << run.err;
}

TEST(Soundness_CliLibrary, DefaultsAndRecoverableErrorsSurviveRepeatedParses) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    const std::string source = R"FIN(
import { Command, Match, Outcome, free_command, free_parse_result } from cli::std;
import { equals } from strings::std;
fun expect_error(command: &Command, args: [string], message: string) <noret> {
    let result <Outcome<Match>> = command.parse(args);
    delete args;
    blame result.is_err();
    blame equals(result.message, message);
    free_parse_result(result);
}
fun main() <noret> {
    let command <Command> = Command::create("errors", "recoverable errors");
    command.add_arg("name", 'n', true, "default", "a name");
    let child <&Command> = Command::make_sub("run", "run the command");
    command.sub(child);
    for (let i <int> = 0; i < 20; i++) {
        expect_error(&command, ["--name"], "missing value for '--name'");
        expect_error(&command, ["--unknown"], "unknown option '--unknown'");
        expect_error(&command, ["unknown"], "unknown command 'unknown'");
        expect_error(&command, ["--name=value"], "unknown option '--name=value'");
        expect_error(&command, ["-abc"], "unknown option '-abc'");
        let args <[string]> = ["run"];
        let result <Outcome<Match>> = command.parse(args);
        delete args;
        blame result.is_ok();
        blame equals(result.value.get("name"), "default");
        blame !result.value.is_set("name");
        blame equals(result.value.subcommand(), "run");
        free_parse_result(result);
    }
    free_command(*child);
    delete child;
    free_command(command);
    printf("errors ok\n");
}
)FIN";
    const auto run = runCliLibrary(source);
    ASSERT_EQ(run.status, 0) << run.err;
    EXPECT_EQ(run.out, "errors ok\n");
    EXPECT_TRUE(run.err.empty()) << run.err;
}

TEST(Soundness_CliLibrary, HelpAndVersionReserveStdoutAndExitSuccessfully) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable library checks need the LLVM backend";
#endif
    const std::string source = R"FIN(
import { Command, Match, Outcome } from cli::std;
fun main() <noret> {
    let command <Command> = Command::create("example", "example command");
    command.version = "1.2.3";
    command.add_arg("name", 'n', true, "default", "a name");
    let result <Outcome<Match>> = command.parse_args();
    printf("parse returned\n");
}
)FIN";
    const auto help = runCliLibrary(source, {"--help"});
    ASSERT_EQ(help.status, 0) << help.err;
    EXPECT_EQ(help.out, "usage: example [options] [args]\nexample command\noptions:\n"
        "  -h, --help      show this help and exit\n"
        "  -v, --version   show the version and exit\n"
        "  -n, --name <value>  a name\n\n");
    EXPECT_TRUE(help.err.empty()) << help.err;
    const auto version = runCliLibrary(source, {"--version"});
    ASSERT_EQ(version.status, 0) << version.err;
    EXPECT_EQ(version.out, "example 1.2.3\n");
    EXPECT_TRUE(version.err.empty()) << version.err;
}
