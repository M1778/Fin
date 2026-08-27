#pragma once
#include "../nodes/ASTNode.hpp"
#include <memory>

namespace fin {

class UnaryOp : public Expression {
public:
    ASTTokenKind op;
    std::unique_ptr<Expression> operand;
    // Was the operator written *after* its operand? A fact about the source text,
    // set by whichever production matched, never inferred from `op`.
    //
    // `++` and `--` are the only operators the grammar spells in both positions, and
    // they are the reason this exists: `i++` and `++i` were the same node, so a
    // consumer that needed the expression's value -- the old value for one, the new
    // value for the other -- had nothing to read and the backend refused both. In
    // statement position the two are identical, which is why eight increments in the
    // corpus never noticed.
    //
    // Set truthfully for every operator rather than only for those two. `?` is
    // always postfix and `!` is always prefix, so neither flag can decide anything,
    // but a field that is only meaningful for some operators is a field every reader
    // has to know the exceptions to. Default false, because prefix is the position
    // that reads correctly in a statement.
    bool is_postfix = false;
    UnaryOp(ASTTokenKind o, std::unique_ptr<Expression> e, bool postfix = false);
    void accept(Visitor& v) override;
};

}
