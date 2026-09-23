#pragma once
#include "Expression.hpp"
#include "../types/TypeNode.hpp"
#include <vector>
#include <memory>
#include <utility>

namespace fin {

class PrototypeLiteral : public Expression {
public:
    std::vector<std::pair<std::unique_ptr<Expression>, std::unique_ptr<Expression>>> elements;
    std::unique_ptr<TypeNode> resolved_type;
    PrototypeLiteral(std::vector<std::pair<std::unique_ptr<Expression>, std::unique_ptr<Expression>>> e)
        : elements(std::move(e)) {}
    void accept(Visitor& v) override;
};

}
