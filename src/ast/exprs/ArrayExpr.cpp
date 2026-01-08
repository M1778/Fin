#include "ArrayExpr.hpp"
#include "../Visitor.hpp"

namespace fin {

ArrayLiteral::ArrayLiteral(std::vector<std::unique_ptr<Expression>> e) : elements(std::move(e)) {}
void ArrayLiteral::accept(Visitor& v) { v.visit(*this); }

ArrayAccess::ArrayAccess(std::unique_ptr<Expression> a, std::unique_ptr<Expression> i) 
    : array(std::move(a)), index(std::move(i)) {}
void ArrayAccess::accept(Visitor& v) { v.visit(*this); }

}
