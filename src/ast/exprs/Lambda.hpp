#pragma once
#include "../nodes/ASTNode.hpp"
#include "../nodes/Parameter.hpp"
#include "../types/TypeNode.hpp"
#include "../types/GenericParam.hpp" // a generic lambda's parameters
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
    // `<T: Castable>(m: T) <T> => m` and `fun <Generic: Addable>(a: Generic, ...)`
    // -- tests/samples/lambdas.fin:69 and :71, the file's "Lambda with generics"
    // case. Empty for every non-generic lambda.
    std::vector<std::unique_ptr<GenericParam>> generic_params;
    
    LambdaExpression(std::vector<std::unique_ptr<Parameter>> p, 
                     std::unique_ptr<TypeNode> rt, 
                     std::unique_ptr<Block> b);

    LambdaExpression(std::vector<std::unique_ptr<Parameter>> p, 
                     std::unique_ptr<TypeNode> rt, 
                     std::unique_ptr<Expression> expr);
    void accept(Visitor& v) override;
};

}
