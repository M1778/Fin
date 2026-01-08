#include "Lambda.hpp"
#include "../Visitor.hpp"

namespace fin {

QuoteExpression::QuoteExpression(std::unique_ptr<Block> b) : block(std::move(b)) {}
void QuoteExpression::accept(Visitor& v) { v.visit(*this); }

LambdaExpression::LambdaExpression(std::vector<std::unique_ptr<Parameter>> p, 
                 std::unique_ptr<TypeNode> rt, 
                 std::unique_ptr<Block> b)
    : params(std::move(p)), return_type(std::move(rt)), body(std::move(b)) {}

LambdaExpression::LambdaExpression(std::vector<std::unique_ptr<Parameter>> p, 
                 std::unique_ptr<TypeNode> rt, 
                 std::unique_ptr<Expression> expr)
    : params(std::move(p)), return_type(std::move(rt)), expression_body(std::move(expr)) {}
void LambdaExpression::accept(Visitor& v) { v.visit(*this); }

}
