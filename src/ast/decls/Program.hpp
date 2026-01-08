#pragma once
#include "../nodes/ASTNode.hpp"
#include "../stmts/Statement.hpp"
#include <vector>
#include <memory>

namespace fin {

class Program : public ASTNode {
public:
    std::vector<std::unique_ptr<Statement>> statements;
    Program(std::vector<std::unique_ptr<Statement>> s);
    void accept(Visitor& v) override;
};

}
