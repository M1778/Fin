#include <exception>
#include <string>
#include <vector>
#include <fmt/core.h>

#include "driver/Driver.hpp"
#include "driver/SearchPaths.hpp"
#include "driver/Version.hpp"

namespace {

// `--help` and `--version` are output the caller asked for, so they go to
// stdout. Everything else the compiler says goes to stderr, because stdout is
// reserved (ADR 0009).
void printUsage(std::FILE* out) {
    fmt::print(out, "Usage: finc <file.fin> [options]\n");
    fmt::print(out, "Options:\n");
    fmt::print(out, "  -o <path>              Output path (accepted, unused until codegen)\n");
    fmt::print(out, "  -I, --include <path>   Add a module search path\n");
    fmt::print(out, "  --fin-libs <paths>     Library search paths, '{}'-separated;\n", fin::kSearchPathSeparator);
    fmt::print(out, "                         replaces $FIN_LIBS rather than adding to it\n");
    fmt::print(out, "  --diagnostics=<fmt>    Diagnostic format: human (default) or json\n");
    fmt::print(out, "  --color=<when>         auto (default), always or never\n");
    fmt::print(out, "  --debug-ast            Print the parsed AST\n");
    fmt::print(out, "  --debug-sema           Print semantic analysis details\n");
    fmt::print(out, "  --no-check             Skip semantic analysis (unsafe)\n");
    fmt::print(out, "  --version              Print version and machine contract version\n");
    fmt::print(out, "  --help                 Show this message\n");
    fmt::print(out, "\nExit codes: 0 success, 1 diagnostics, 2 usage, 3 internal error.\n");
}

// `--diagnostics=` and `--color=` have to be honoured for a mistake in argv as
// well, and the flag can appear *after* the mistake it has to render — so they
// are read in a pre-pass over the whole command line, before anything is
// validated.
//
// Without this, an unknown flag was reported as plain text even in JSON mode.
// That is the one argv error a programmatic caller is most likely to hit: a
// `finn` built against a newer `finc` passes a flag this one does not have, and
// got an unparseable stderr at precisely the moment it needed to explain a
// version mismatch. ADR 0009 promises no non-JSON byte reaches stderr in JSON
// mode, and the promise has to hold before the command line is understood, not
// only after.
//
// An unrecognised *value* — `--diagnostics=xml` — is deliberately left to the
// real parse below. It cannot be honoured here, so it is reported in whatever
// format the pre-pass did settle.
void prescanOutputFlags(const std::vector<std::string>& args,
                        fin::DiagnosticFormat& format, fin::ColorMode& color) {
    for (const auto& arg : args) {
        if (arg == "--diagnostics=json")       format = fin::DiagnosticFormat::Json;
        else if (arg == "--diagnostics=human") format = fin::DiagnosticFormat::Human;
        else if (arg == "--color=always")      color = fin::ColorMode::Always;
        else if (arg == "--color=never")       color = fin::ColorMode::Never;
        else if (arg == "--color=auto")        color = fin::ColorMode::Auto;
    }
}

// One diagnostic with no source location, plus the summary, in the requested
// format. Both the usage path and the internal-error path need exactly this.
int reportAndExit(fin::DiagnosticFormat format, fin::ColorMode color,
                  fin::ExitCode exit, const std::string& msg,
                  const std::string& help) {
    fin::DiagnosticEngine diag("", "");
    diag.setFormat(format);
    diag.setColorMode(color);
    diag.reportError(msg, help);
    const int code = static_cast<int>(exit);
    // The summary is what tells a consumer the run ended deliberately rather
    // than died, so every exit path owes one.
    diag.emitSummary(code);
    return code;
}

int usageError(fin::DiagnosticFormat format, fin::ColorMode color,
               const std::string& msg) {
    return reportAndExit(format, color, fin::ExitCode::Usage, msg,
                         "run `finc --help` for usage.");
}

// Exit 3 is the one a consumer least wants as an unparseable blob: it is the
// code that says "this is a compiler bug, do not blame the user's source". It
// went out as a raw line with no summary, so in JSON mode a crash was
// indistinguishable from a truncated stream. The format is read before the try
// block for this reason — the handler cannot recover it from argv afterwards.
int internalError(fin::DiagnosticFormat format, fin::ColorMode color,
                  const std::string& what) {
    return reportAndExit(format, color, fin::ExitCode::Internal,
                         "internal error: " + what,
                         "this is a bug in finc, not in the source being compiled.");
}

} // namespace

int main(int argc, char** argv) {
    // Read before the try, so the handler for an internal error can still
    // render in the format the caller asked for.
    std::vector<std::string> args(argv + 1, argv + argc);
    fin::DiagnosticFormat format = fin::DiagnosticFormat::Human;
    fin::ColorMode color = fin::ColorMode::Auto;
    prescanOutputFlags(args, format, color);

    try {
        if (args.empty()) {
            printUsage(stderr);
            return static_cast<int>(fin::ExitCode::Usage);
        }

        fin::CompilerOptions opts;
        bool sawInput = false;

        opts.diagFormat = format;
        opts.colorMode = color;
        const auto fail = [&](const std::string& msg) {
            return usageError(format, color, msg);
        };

        for (size_t i = 0; i < args.size(); ++i) {
            const std::string& arg = args[i];

            if (arg.empty()) return fail("empty argument");

            if (arg == "--help" || arg == "-h") {
                printUsage(stdout);
                return static_cast<int>(fin::ExitCode::Success);
            }
            if (arg == "--version") {
                fmt::print("finc {} (contract {})\n",
                           fin::kFincVersion, fin::kFincContractVersion);
                return static_cast<int>(fin::ExitCode::Success);
            }
            if (arg == "--debug-ast")  { opts.debugParser = true;   continue; }
            if (arg == "--debug-sema") { opts.debugSema = true;     continue; }
            if (arg == "--no-check")   { opts.skipSemantics = true; continue; }

            if (arg == "-I" || arg == "--include") {
                if (i + 1 >= args.size()) return fail("missing path for " + arg);
                opts.includePaths.push_back(args[++i]);
                continue;
            }
            // `finn` passes this to name the environment a build compiles
            // against. Repeatable, and accepted in both spellings, because a
            // caller assembling argv programmatically can then avoid the
            // separator entirely — which is the only way to pass a path that
            // contains one.
            if (arg == "--fin-libs") {
                if (i + 1 >= args.size()) return fail("missing paths for --fin-libs");
                opts.finLibsGiven = true;
                for (auto& p : fin::splitSearchPaths(args[++i])) opts.finLibPaths.push_back(p);
                continue;
            }
            if (arg.rfind("--fin-libs=", 0) == 0) {
                opts.finLibsGiven = true;
                for (auto& p : fin::splitSearchPaths(arg.substr(11))) opts.finLibPaths.push_back(p);
                continue;
            }
            if (arg == "-o") {
                if (i + 1 >= args.size()) return fail("missing path for -o");
                opts.outputPath = args[++i];
                opts.outputPathGiven = true;
                continue;
            }
            if (arg.rfind("--diagnostics=", 0) == 0) {
                std::string value = arg.substr(14);
                if (value == "human")     opts.diagFormat = fin::DiagnosticFormat::Human;
                else if (value == "json") opts.diagFormat = fin::DiagnosticFormat::Json;
                else return fail("unknown diagnostics format '" + value +
                                       "' (expected 'human' or 'json')");
                continue;
            }
            if (arg.rfind("--color=", 0) == 0) {
                std::string value = arg.substr(8);
                if (value == "auto")        opts.colorMode = fin::ColorMode::Auto;
                else if (value == "always") opts.colorMode = fin::ColorMode::Always;
                else if (value == "never")  opts.colorMode = fin::ColorMode::Never;
                else return fail("unknown color mode '" + value +
                                       "' (expected 'auto', 'always' or 'never')");
                continue;
            }

            // An unrecognised flag is an error, never ignored: a toolchain whose
            // flags fail silently is the worst failure mode for a caller that
            // builds argv programmatically.
            if (arg[0] == '-') return fail("unknown option '" + arg + "'");

            if (sawInput) {
                return fail("unexpected second input file '" + arg +
                                  "' (already compiling '" + opts.inputFile + "')");
            }
            opts.inputFile = arg;
            sawInput = true;
        }

        if (!sawInput) return fail("no input file specified");

        fin::Driver driver(opts);
        return driver.compile();
    } catch (const std::exception& e) {
        return internalError(format, color, e.what());
    } catch (...) {
        return internalError(format, color, "unknown exception");
    }
}
