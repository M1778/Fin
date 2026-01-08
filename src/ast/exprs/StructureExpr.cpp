#include "StructureExpr.hpp"
#include "../Visitor.hpp"

namespace fin {

StructInstantiation::StructInstantiation(std::string n, 
                    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> f,
                    std::vector<std::unique_ptr<TypeNode>> g)
    : struct_name(std::move(n)), fields(std::move(f)), generic_args(std::move(g)) {}
void StructInstantiation::accept(Visitor& v) { v.visit(*this); }

MemberAccess::MemberAccess(std::unique_ptr<Expression> obj, std::string m) 
    : object(std::move(obj)), member(std::move(m)) {}
void MemberAccess::accept(Visitor& v) { v.visit(*this); }

NewExpression::NewExpression(std::unique_ptr<TypeNode> t, std::vector<std::unique_ptr<Expression>> a)
    : type(std::move(t)), args(std::move(a)) {}
NewExpression::NewExpression(std::unique_ptr<TypeNode> t, std::vector<std::pair<std::string, std::unique_ptr<Expression>>> f)
    : type(std::move(t)), init_fields(std::move(f)) {}
void NewExpression::accept(Visitor& v) { v.visit(*this); }

}
