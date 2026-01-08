#pragma once
#include "../nodes/ASTNode.hpp"
#include "Statement.hpp"
#include "../types/TypeNode.hpp"
#include <string>
#include <memory>

namespace fin {

class TryCatch : public Statement {
public:
    std::unique_ptr<Block> try_block;
    std::string catch_var;
    std::unique_ptr<TypeNode> catch_type;
    std::unique_ptr<Block> catch_block;
    TryCatch(std::unique_ptr<Block> t, std::string cv, std::unique_ptr<TypeNode> ct, std::unique_ptr<Block> cb);
    void accept(Visitor& v) override;
};

class BlameStatement : public Statement {
public:
    std::unique_ptr<Expression> error_expr;
    BlameStatement(std::unique_ptr<Expression> e);
    void accept(Visitor& v) override;
};

}
