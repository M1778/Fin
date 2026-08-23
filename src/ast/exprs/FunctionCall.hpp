#pragma once
#include "../nodes/ASTNode.hpp"
#include "../types/TypeNode.hpp" 
#include <string>
#include <vector>
#include <memory>

namespace fin {

class FunctionCall : public Expression {
public:
    std::string name;
    std::vector<std::unique_ptr<Expression>> args;
    std::vector<std::unique_ptr<TypeNode>> generic_args;
    // Written `@name(args)`: a call of a special function, which is resolved by
    // the compiler and not by name lookup in the program
    // (tests/samples/stdlib/memory.fin:14, stdlib/enums.fin:18,
    // literal_interface.fin:6). `name` never keeps the `@`.
    bool is_special = false;
    FunctionCall(std::string n, std::vector<std::unique_ptr<Expression>> a);
    void accept(Visitor& v) override;
};

class MethodCall : public Expression {
public:
    std::unique_ptr<Expression> object;
    std::string method_name;
    std::vector<std::unique_ptr<Expression>> args;
    std::vector<std::unique_ptr<TypeNode>> generic_args;
    MethodCall(std::unique_ptr<Expression> obj, std::string name, 
               std::vector<std::unique_ptr<Expression>> a,
               std::vector<std::unique_ptr<TypeNode>> g = {});
    void accept(Visitor& v) override;
};

class StaticMethodCall : public Expression {
public:
    std::unique_ptr<TypeNode> target_type; 
    std::string method_name;               
    std::vector<std::unique_ptr<Expression>> args;
    std::vector<std::unique_ptr<TypeNode>> generic_args; 

    StaticMethodCall(std::unique_ptr<TypeNode> target, std::string name, 
                     std::vector<std::unique_ptr<Expression>> a,
                     std::vector<std::unique_ptr<TypeNode>> g = {});
          
    void accept(Visitor& v) override;
};

class MacroCall : public Expression {
public:
    std::string name;
    std::vector<std::unique_ptr<Expression>> args;
    MacroCall(std::string n, std::vector<std::unique_ptr<Expression>> a);
    void accept(Visitor& v) override;
};

}
