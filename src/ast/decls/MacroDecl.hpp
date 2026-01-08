#pragma once
#include "../nodes/ASTNode.hpp"
#include "../stmts/Statement.hpp" // For Block
#include <vector>
#include <string>
#include <memory>

namespace fin {

struct MacroParam {
    std::string name;
    std::string type; // "expr", "block", "ident"
    bool is_vararg = false; 
};

class MacroDeclaration : public Statement {
public:
    std::string name;
    std::vector<MacroParam> params; // Stores macro parameters
    std::unique_ptr<Block> body;
    
    MacroDeclaration(std::string n, std::vector<MacroParam> p, std::unique_ptr<Block> b);
    void accept(Visitor& v) override;
};

}
