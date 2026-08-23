#pragma once
#include "../nodes/ASTNode.hpp"
#include "../types/TypeNode.hpp"
#include <string>
#include <vector>
#include <memory>
#include <utility>

namespace fin {

class StructInstantiation : public Expression {
public:
    std::string struct_name;
    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> fields;
    std::vector<std::unique_ptr<TypeNode>> generic_args;
    StructInstantiation(std::string n, 
                        std::vector<std::pair<std::string, std::unique_ptr<Expression>>> f,
                        std::vector<std::unique_ptr<TypeNode>> g = {});
    void accept(Visitor& v) override;
};

class MemberAccess : public Expression {
public:
    std::unique_ptr<Expression> object;
    std::string member;
    // Written `Type::name` rather than `value.name`: the qualifier is a type and
    // the member is an enum member, not a field of an object
    // (tests/samples/literal_interface.fin:19, enums.fin:19, extern_as.fin:44).
    bool is_static = false;
    MemberAccess(std::unique_ptr<Expression> obj, std::string m);
    void accept(Visitor& v) override;
};

class NewExpression : public Expression {
public:
    std::unique_ptr<TypeNode> type;
    std::vector<std::unique_ptr<Expression>> args;
    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> init_fields;
    NewExpression(std::unique_ptr<TypeNode> t, std::vector<std::unique_ptr<Expression>> a = {});
    NewExpression(std::unique_ptr<TypeNode> t, std::vector<std::pair<std::string, std::unique_ptr<Expression>>> f);
    void accept(Visitor& v) override;
};

}
