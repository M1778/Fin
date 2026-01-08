#include "MacroExpander.hpp"

namespace fin {

MacroExpander::MacroExpander(DiagnosticEngine& diag, Scope* scope)
    : diag(diag), currentScope(scope) {}

void MacroExpander::expand(Program& node) {
    node.accept(*this);
}

} // namespace fin
