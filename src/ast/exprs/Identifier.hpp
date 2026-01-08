#pragma once
#include "../nodes/ASTNode.hpp"
#include <string>

namespace fin {

class Identifier : public Expression {
public:
    std::string name;
    Identifier(std::string n);
    void accept(Visitor& v) override;
};

}
