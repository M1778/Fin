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

void CloneVisitor::visit(Attribute& node) {
    // Attribute has two constructors -- flag form and key=value form -- and
    // is_flag is what says which one this node came from.
    auto res = node.is_flag ? std::make_unique<Attribute>(node.name, true)
                            : std::make_unique<Attribute>(node.name, node.value_str);
    res->is_flag = node.is_flag;
    res->value_str = node.value_str;
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(GenericParam& node) {
    auto res = std::make_unique<GenericParam>(node.name, clone(node.constraint.get()));
    res->setLoc(node.loc);
    result = std::move(res);
}

}
