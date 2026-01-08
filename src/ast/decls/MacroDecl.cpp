#include "MacroDecl.hpp"
#include "../Visitor.hpp"

namespace fin {

MacroDeclaration::MacroDeclaration(std::string n, std::vector<MacroParam> p, std::unique_ptr<Block> b)
    : name(std::move(n)), params(std::move(p)), body(std::move(b)) {}
void MacroDeclaration::accept(Visitor& v) { v.visit(*this); }

}
