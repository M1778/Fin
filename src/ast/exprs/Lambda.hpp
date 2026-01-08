#pragma once
#include "../nodes/ASTNode.hpp"
#include "../nodes/Parameter.hpp"
#include "../types/TypeNode.hpp"
#include <vector>
#include <memory>

namespace fin {

class Block; // Forward Declaration

class QuoteExpression : public Expression {
public:
    std::unique_ptr<Block> block;
    QuoteExpression(std::unique_ptr<Block> b);
    void accept(Visitor& v) override;
};

class LambdaExpression : public Expression {
public:
    std::vector<std::unique_ptr<Parameter>> params;
    std::unique_ptr<TypeNode> return_type;
    std::unique_ptr<Block> body;
    std::unique_ptr<Expression> expression_body;
    
    LambdaExpression(std::vector<std::unique_ptr<Parameter>> p, 
                     std::unique_ptr<TypeNode> rt, 
                     std::unique_ptr<Block> b);

    LambdaExpression(std::vector<std::unique_ptr<Parameter>> p, 
                     std::unique_ptr<TypeNode> rt, 
                     std::unique_ptr<Expression> expr);
    void accept(Visitor& v) override;
};

}
