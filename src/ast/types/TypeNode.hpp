#pragma once

#include "../nodes/ASTNode.hpp"
#include <string>
#include <vector>
#include <memory>

namespace fin {

class TypeNode : public ASTNode {
public:
    std::string name; 
    std::vector<std::unique_ptr<TypeNode>> generics;
    int pointer_depth = 0; 
    bool is_array = false;
    std::unique_ptr<Expression> array_size = nullptr;
    TypeNode(std::string n);
    void accept(Visitor& v) override;
};

class FunctionTypeNode : public TypeNode {
public:
    std::vector<std::unique_ptr<TypeNode>> param_types;
    std::unique_ptr<TypeNode> return_type;
    FunctionTypeNode(std::vector<std::unique_ptr<TypeNode>> params, std::unique_ptr<TypeNode> ret);
    void accept(Visitor& v) override;
};

class PointerTypeNode : public TypeNode {
public:
    std::unique_ptr<TypeNode> pointee;
    PointerTypeNode(std::unique_ptr<TypeNode> p);
    void accept(Visitor& v) override;
};

class ArrayTypeNode : public TypeNode {
public:
    std::unique_ptr<TypeNode> element_type;
    std::unique_ptr<Expression> size; // Optional
    ArrayTypeNode(std::unique_ptr<TypeNode> elem, std::unique_ptr<Expression> s = nullptr);
    void accept(Visitor& v) override;
};

} // namespace fin
