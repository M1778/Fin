#pragma once
#include "../nodes/ASTNode.hpp"
#include "../stmts/Statement.hpp" // For Block
#include "../types/Attribute.hpp"
#include <vector>
#include <string>
#include <memory>

namespace fin {

struct MacroParam {
    std::string name;
    std::string type; // "expr", "block", "ident"
    bool is_vararg = false; 
};

struct MacroRule {
    std::string pattern; // Simplified pattern
    std::unique_ptr<ASTNode> expansion;
};

class MacroDeclaration : public Statement {
public:
    std::string name;
    std::vector<MacroParam> params; // For legacy @macro
    std::unique_ptr<Block> body;    // For legacy @macro
    std::vector<MacroRule> rules;   // For new Rust-like macros
    bool is_rust_style = false;
    std::vector<std::unique_ptr<Attribute>> attributes;

    MacroDeclaration(std::string n, std::vector<MacroParam> p, std::unique_ptr<Block> b);
    MacroDeclaration(std::string n, std::vector<MacroRule> r);
    void accept(Visitor& v) override;
};

}
