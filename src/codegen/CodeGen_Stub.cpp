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
// that cannot get LLVM 18 still get a checker (ADR 0010).

namespace fin {

bool backendAvailable() { return false; }

bool generateObject(Program& ast, const std::string& objectPath,
                    DiagnosticEngine& diag, int optLevel, bool debugCodegen) {
    (void)ast;
    (void)objectPath;
    (void)optLevel;
    (void)debugCodegen;
    diag.reportError(
        "codegen: this finc was built without a backend",
        "configure with -DFIN_WITH_LLVM=ON and an LLVM 18 development install; "
        "without one finc can check a program but not emit one");
    return false;
}

}  // namespace fin
