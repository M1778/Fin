#include "VariableDecl.hpp"
#include "../Visitor.hpp"

namespace fin {

VariableDeclaration::VariableDeclaration(bool mut, std::string n, std::unique_ptr<TypeNode> t, std::unique_ptr<Expression> init)
    : is_mutable(mut), name(std::move(n)), type(std::move(t)), initializer(std::move(init)) {}
void VariableDeclaration::accept(Visitor& v) { v.visit(*this); }

DeleteStatement::DeleteStatement(std::unique_ptr<Expression> e) : expr(std::move(e)) {}
void DeleteStatement::accept(Visitor& v) { v.visit(*this); }

ExpressionStatement::ExpressionStatement(std::unique_ptr<Expression> e) : expr(std::move(e)) {}
void ExpressionStatement::accept(Visitor& v) { v.visit(*this); }

}
