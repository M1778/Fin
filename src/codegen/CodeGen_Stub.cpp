#include "CodeGen.hpp"

#include "../ast/decls/Program.hpp"
#include "../diagnostics/DiagnosticEngine.hpp"

// The backend for a build configured with FIN_WITH_LLVM=OFF.
//
// It refuses rather than doing nothing, and the difference matters: returning
// `true` here would make `finc x.fin -o x` exit 0 having written no file, so a
// script that checks the exit status would proceed to run an artifact that is not
// there. A caller is entitled to learn that this finc cannot link, once, in a
// sentence naming the switch that would fix it.
//
// Everything else this build does -- lexing, parsing, macro expansion, semantic
// analysis, every diagnostic -- is unaffected, which is the point: the platforms
// that cannot get LLVM 22 still get a checker (ADR 0010).
//
// The major is spelled here rather than passed in as a compile definition. This
// file is compiled exactly when FIN_WITH_LLVM=OFF, and on that path
// find_package(LLVM) never ran -- so FIN_LLVM_MAJOR would be a number nothing had
// checked against anything, which is worse than a literal. What keeps it in step
// with ADR 0010's pin is Soundness_Codegen.TheNoBackendHelpNamesThePinnedLlvmMajor,
// which reads both files and is not a BACKEND_TEST, so it runs in either build.

namespace fin {

bool backendAvailable() { return false; }

bool generateObject(Program& ast, const std::string& objectPath,
                    DiagnosticEngine& diag, int optLevel, bool debugCodegen,
                    const std::string& sourceName) {
    (void)ast;
    (void)objectPath;
    (void)optLevel;
    (void)debugCodegen;
    // Unread here for the same reason the rest is: this build emits nothing, so it
    // has nothing to put a source name into. Named in the signature all the same,
    // because the two definitions of one declaration have to agree.
    (void)sourceName;
    diag.reportError(
        "codegen: this finc was built without a backend",
        "configure with -DFIN_WITH_LLVM=ON and an LLVM 22 development install; "
        "without one finc can check a program but not emit one");
    return false;
}

}  // namespace fin
