#pragma once
#include "../nodes/ASTNode.hpp"
#include "../types/TypeNode.hpp"
#include <vector>
#include <memory>

namespace fin {

class ArrayLiteral : public Expression {
public:
    std::vector<std::unique_ptr<Expression>> elements;
    // The type inference found: `[int, 4]` for `[1, 2, 3, 4]`, fixed with the
    // extent the literal states by stating its elements. The array half of
    // FunctionCall::resolved_args, under the same record-don't-replace rule
    // and the same spelling guards -- see it, and spellType. The backend
    // builds what was recorded where no declaration set a hint (a comparison
    // operand, a ternary arm); a hint still wins where one is set, and empty
    // where there is nothing to spell.
    std::unique_ptr<TypeNode> resolved_type;
    ArrayLiteral(std::vector<std::unique_ptr<Expression>> e);
    void accept(Visitor& v) override;
};

class ArrayAccess : public Expression {
public:
    std::unique_ptr<Expression> array;
    std::unique_ptr<Expression> index;
    ArrayAccess(std::unique_ptr<Expression> a, std::unique_ptr<Expression> i);
    void accept(Visitor& v) override;
};

}
