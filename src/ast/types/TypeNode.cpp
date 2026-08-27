#include "TypeNode.hpp"
#include "../Visitor.hpp"
// For the out-of-line FunctionTypeNode destructor: its `generic_params` holds
// GenericParams, which TypeNode.hpp can only forward-declare.
#include "GenericParam.hpp"

namespace fin {

TypeNode::TypeNode(std::string n) : name(std::move(n)) {}
void TypeNode::accept(Visitor& v) { v.visit(*this); }

FunctionTypeNode::FunctionTypeNode(std::vector<std::unique_ptr<TypeNode>> params, std::unique_ptr<TypeNode> ret)
    : TypeNode("fn"), param_types(std::move(params)), return_type(std::move(ret)) {}
FunctionTypeNode::~FunctionTypeNode() = default;
void FunctionTypeNode::accept(Visitor& v) { v.visit(*this); }

PointerTypeNode::PointerTypeNode(std::unique_ptr<TypeNode> p) 
    : TypeNode("ptr"), pointee(std::move(p)) {}
void PointerTypeNode::accept(Visitor& v) { v.visit(*this); }

ArrayTypeNode::ArrayTypeNode(std::unique_ptr<TypeNode> elem, std::unique_ptr<Expression> s)
    : TypeNode("array"), element_type(std::move(elem)), size(std::move(s)) {}
void ArrayTypeNode::accept(Visitor& v) { v.visit(*this); }

} // namespace fin
