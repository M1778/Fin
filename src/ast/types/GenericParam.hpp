#pragma once

#include "../nodes/ASTNode.hpp"
#include "TypeNode.hpp" // GenericParam holds a TypeNode constraint
#include <string>
#include <memory>

namespace fin {

class GenericParam : public ASTNode {
public:
    std::string name;
    std::unique_ptr<TypeNode> constraint;
    GenericParam(std::string n, std::unique_ptr<TypeNode> c = nullptr);
    void accept(Visitor& v) override;
};

} // namespace fin
