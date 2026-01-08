#pragma once
#include "../nodes/ASTNode.hpp"
#include "../types/TypeNode.hpp"
#include "FunctionDecl.hpp"
#include <vector>
#include <string>
#include <memory>

namespace fin {

class ImplementsBlock : public Statement {
public:
    std::string target_type;
    std::unique_ptr<TypeNode> interface_type;
    std::vector<std::unique_ptr<FunctionDeclaration>> methods;
    std::vector<std::unique_ptr<OperatorDeclaration>> operators;
    
    ImplementsBlock(std::string target, std::unique_ptr<TypeNode> iface)
        : target_type(std::move(target)), interface_type(std::move(iface)) {}
    
    void accept(Visitor& v) override;
};

}
