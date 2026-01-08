#include "../CloneVisitor.hpp"

namespace fin {

void CloneVisitor::visit(TypeNode& node) {
    auto res = std::make_unique<TypeNode>(node.name);
    res->generics = cloneVector(node.generics);
    res->annotations = cloneVector(node.annotations);
    res->is_prototype = node.is_prototype;
    res->pointer_depth = node.pointer_depth; // Copy depth
    res->is_array = node.is_array;
    if (node.array_size) {
        res->array_size = clone(node.array_size.get());
    }
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(FunctionTypeNode& node) {
    auto res = std::make_unique<FunctionTypeNode>(
        cloneVector(node.param_types),
        clone(node.return_type.get())
    );
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(PointerTypeNode& node) {
    auto res = std::make_unique<PointerTypeNode>(clone(node.pointee.get()));
    res->setLoc(node.loc); result = std::move(res);
}

void CloneVisitor::visit(ArrayTypeNode& node) {
    auto res = std::make_unique<ArrayTypeNode>(clone(node.element_type.get()), clone(node.size.get()));
    res->setLoc(node.loc); result = std::move(res);
}

}
