#pragma once
#include "Expression.hpp"
#include <vector>
#include <memory>
#include <utility>

namespace fin {

class PrototypeLiteral : public Expression {
public:
    // pair of key-expression and value-expression
    std::vector<std::pair<std::unique_ptr<Expression>, std::unique_ptr<Expression>>> elements;
    PrototypeLiteral(std::vector<std::pair<std::unique_ptr<Expression>, std::unique_ptr<Expression>>> e)
        : elements(std::move(e)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

}
