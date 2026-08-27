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
//
// `sourceName` is the path the program was read from, and it is here because a
// failed `blame` prints it: `blame_assert.fin:5: assertion failed: ...` is a
// location a person can act on and `:5` alone is not. It is *not* recoverable from
// anything this function already receives, which is why it is a parameter --
//
//   * a node's `loc` cannot carry it: the lexer initialises every location with
//     `loc.initialize(nullptr, 1, 1)` (src/lexer/lexer.l:38, :88), so
//     `position::filename` is null for every node in the tree;
//   * `Program` and the AST have no path field at all;
//   * DiagnosticEngine has one and keeps it private, with no accessor;
//   * `objectPath` is the output and not the input -- under `-c -o /tmp/x.o` it
//     shares neither stem nor directory with the source.
//
// Defaulted to `<input>`, which is DiagnosticEngine's own default filename and so
// already the tree's spelling for "the path is not known here". That keeps every
// existing caller compiling and makes the degraded output honest rather than
// invented: a driver that has the path passes it, and one that does not says so in
// the words the rest of the compiler already uses.
bool generateObject(Program& ast, const std::string& objectPath,
                    DiagnosticEngine& diag, int optLevel, bool debugCodegen,
                    const std::string& sourceName = "<input>");

// False in a build configured with FIN_WITH_LLVM=OFF, where generateObject
// always refuses. Separate from the call so the driver can say "this build has
// no backend" once, rather than once per function.
bool backendAvailable();

}  // namespace fin
