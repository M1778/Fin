#include "FunctionCall.hpp"
#include "../Visitor.hpp"

namespace fin {

FunctionCall::FunctionCall(std::string n, std::vector<std::unique_ptr<Expression>> a) 
    : name(std::move(n)), args(std::move(a)) {}
void FunctionCall::accept(Visitor& v) { v.visit(*this); }

MethodCall::MethodCall(std::unique_ptr<Expression> obj, std::string name, 
           std::vector<std::unique_ptr<Expression>> a,
           std::vector<std::unique_ptr<TypeNode>> g)
    : object(std::move(obj)), method_name(std::move(name)), args(std::move(a)), generic_args(std::move(g)) {}
void MethodCall::accept(Visitor& v) { v.visit(*this); }

StaticMethodCall::StaticMethodCall(std::unique_ptr<TypeNode> target, std::string name, 
                 std::vector<std::unique_ptr<Expression>> a,
                 std::vector<std::unique_ptr<TypeNode>> g)
    : target_type(std::move(target)), method_name(std::move(name)), 
      args(std::move(a)), generic_args(std::move(g)) {}
      
void StaticMethodCall::accept(Visitor& v) { v.visit(*this); }

MacroCall::MacroCall(std::string n, std::vector<std::unique_ptr<Expression>> a)
    : name(std::move(n)), args(std::move(a)) {}
void MacroCall::accept(Visitor& v) { v.visit(*this); }

}
