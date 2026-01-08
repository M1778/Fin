#pragma once
#include "ASTNode.hpp"
#include "../types/TypeNode.hpp"
#include <string>
#include <memory>

namespace fin {

class Parameter : public ASTNode {
public:
    std::string name;
    std::unique_ptr<TypeNode> type;
    std::unique_ptr<Expression> default_value;
    bool is_vararg = false;
    Parameter(std::string n, std::unique_ptr<TypeNode> t, std::unique_ptr<Expression> d, bool v);
    void accept(Visitor&) override;
};

}
