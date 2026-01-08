#pragma once
#include "../nodes/ASTNode.hpp"
#include "../nodes/Parameter.hpp"
#include "../types/TypeNode.hpp"
#include <vector>
#include <string>
#include <memory>

namespace fin {

class DefineDeclaration : public Statement {
public:
    std::string name;
    std::vector<std::unique_ptr<Parameter>> params;
    std::unique_ptr<TypeNode> return_type;
    bool is_vararg;
    
    DefineDeclaration(std::string n, std::vector<std::unique_ptr<Parameter>> p, std::unique_ptr<TypeNode> rt, bool v);
    void accept(Visitor& v) override;
};

}
