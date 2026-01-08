#include "MacroDecl.hpp"
#include "../Visitor.hpp"

namespace fin {

MacroDeclaration::MacroDeclaration(std::string n, std::vector<MacroParam> p, std::unique_ptr<Block> b)
    : name(std::move(n)), params(std::move(p)), body(std::move(b)), is_rust_style(false) {}

MacroDeclaration::MacroDeclaration(std::string n, std::vector<MacroRule> r)
    : name(std::move(n)), rules(std::move(r)), is_rust_style(true) {}

void MacroDeclaration::accept(Visitor& v) { v.visit(*this); }

}
