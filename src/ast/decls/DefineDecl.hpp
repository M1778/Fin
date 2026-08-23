#pragma once
#include "../nodes/ASTNode.hpp"
#include "../nodes/Parameter.hpp"
#include "../types/TypeNode.hpp"
#include "../types/Attribute.hpp"
#include <vector>
#include <string>
#include <memory>

namespace fin {

class DefineDeclaration : public Statement {
public:
    std::string name;
    std::vector<std::unique_ptr<Parameter>> params;
    std::unique_ptr<TypeNode> return_type;
    bool is_vararg;
    // `#[llvm_name="c_printf"]` above an `@define`: tests/samples/stdlib/stdio.fin:11
    // is how an extern is bound to its C symbol, so an @define that cannot hold
    // an attribute cannot say what it links against.
    std::vector<std::unique_ptr<Attribute>> attributes;

    DefineDeclaration(std::string n, std::vector<std::unique_ptr<Parameter>> p, std::unique_ptr<TypeNode> rt, bool v);
    void accept(Visitor& v) override;
};

}
