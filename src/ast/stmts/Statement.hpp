#pragma once
#include "../nodes/ASTNode.hpp"
#include <vector>
#include <memory>

namespace fin {

class Block : public Statement {
public:
    std::vector<std::unique_ptr<Statement>> statements;
    Block(std::vector<std::unique_ptr<Statement>> s);
    void accept(Visitor& v) override;
};

}
