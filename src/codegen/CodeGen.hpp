#pragma once

#include <string>

namespace fin {

class Program;
class DiagnosticEngine;

// Wave 5. Lowers an analysed Program to a native object file.
//
// Called only after semantic analysis has accepted the program, so this layer
// does no type checking and reports no type errors. What it does report is the
// other thing: a construct it cannot lower yet. That distinction is the whole
// contract here --
//
//   * a well-typed program the backend understands becomes an object file and
//     `true` is returned;
//   * a well-typed program the backend does *not* understand yet is refused,
//     with a diagnostic naming the construct and its line, and `false` is
//     returned.
//
// There is no third outcome, and in particular there is no "emit what I can".
// A dropped statement produces a binary that type-checks, links, runs, and does
// the wrong thing, which is strictly worse than a compiler that says no: the
// error surfaces at the far end of a debugging session instead of at the near
// end of a build. That is why the emitter implements the exhaustive `Visitor`
// rather than StructuralWalk (ADR 0004's own guidance) -- a node type nobody has
// handled is a compile error in *this* compiler, and the ones that are handled
// but unlowerable go through one `unsupported()` helper so every refusal reads
// the same.
//
// `objectPath` is written only on success; on failure any partial file is
// removed, because a stale object left behind by a failed build is a link that
// succeeds against yesterday's code.
bool generateObject(Program& ast, const std::string& objectPath,
                    DiagnosticEngine& diag, int optLevel, bool debugCodegen);

// False in a build configured with FIN_WITH_LLVM=OFF, where generateObject
// always refuses. Separate from the call so the driver can say "this build has
// no backend" once, rather than once per function.
bool backendAvailable();

}  // namespace fin
