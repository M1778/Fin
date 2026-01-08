#pragma once
#include "../nodes/ASTNode.hpp"
#include <string>

namespace fin {

class Literal : public Expression {
public:
    std::string value;
    ASTTokenKind kind;
    Literal(std::string v, ASTTokenKind k);
    void accept(Visitor& v) override;
};

}
