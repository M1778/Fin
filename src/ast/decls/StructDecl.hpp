#pragma once
#include "../nodes/ASTNode.hpp"
#include "FunctionDecl.hpp"
#include "../types/TypeNode.hpp"
#include "../types/GenericParam.hpp"
#include "../types/Attribute.hpp"
#include <vector>
#include <string>
#include <memory>
#include <utility>

namespace fin {

class StructMember : public ASTNode {
public:
    std::string name;
    std::unique_ptr<TypeNode> type;
    std::unique_ptr<Expression> default_value;
    bool is_public;
    // `readonly v1 <int>` -- readable from anywhere, assignable only from
    // inside the declaring type (tests/samples/readonly.fin:9, :20, :29, :36 and
    // stdlib/stdptr.fin:16). Nothing enforces it yet; the parser records it so
    // that the pass which does has somewhere to read it from.
    bool is_readonly = false;
    std::vector<std::unique_ptr<Attribute>> attributes;
    StructMember(std::string n, std::unique_ptr<TypeNode> t, bool pub);
    void accept(Visitor&) override;
};

class StructDeclaration : public Statement {
public:
    std::string name;
    std::vector<std::unique_ptr<StructMember>> members;
    std::vector<std::unique_ptr<FunctionDeclaration>> methods;
    std::vector<std::unique_ptr<OperatorDeclaration>> operators;
    std::vector<std::unique_ptr<ConstructorDeclaration>> constructors;
    std::unique_ptr<DestructorDeclaration> destructor;
    std::vector<std::unique_ptr<GenericParam>> generic_params;
    std::vector<std::unique_ptr<Attribute>> attributes;
    std::vector<std::unique_ptr<TypeNode>> parents;
    bool is_public;
    bool is_class = false; // True if declared with 'class' keyword
    // `struct Stream;` -- the name is declared and the body comes later
    // (tests/samples/stdlib/stdio.fin:42). Distinguishes a forward declaration
    // from a struct whose body is genuinely empty.
    bool is_forward_declaration = false;
    // Parse-time only: the default visibility set by the most recent `pub:` /
    // `priv:` label in this body. It lives on the accumulator node so that each
    // body has its own, rather than in a parser-global that a nested body would
    // inherit. Meaningless after parsing -- every member already carries the
    // `is_public` this produced.
    bool label_public = false;
    
    StructDeclaration(std::string n, std::vector<std::unique_ptr<StructMember>> m, bool pub);
    void accept(Visitor& v) override;
};

class InterfaceDeclaration : public Statement {
public:
    std::string name;
    std::vector<std::unique_ptr<StructMember>> members;
    std::vector<std::unique_ptr<FunctionDeclaration>> methods;
    std::vector<std::unique_ptr<OperatorDeclaration>> operators;
    std::vector<std::unique_ptr<ConstructorDeclaration>> constructors;
    std::unique_ptr<DestructorDeclaration> destructor;
    
    std::vector<std::unique_ptr<Attribute>> attributes;
    std::vector<std::unique_ptr<GenericParam>> generic_params;
    bool is_public;
    bool label_public = false; // see StructDeclaration::label_public
    
    InterfaceDeclaration(std::string n, 
                         std::vector<std::unique_ptr<StructMember>> m, 
                         std::vector<std::unique_ptr<FunctionDeclaration>> f, 
                         std::vector<std::unique_ptr<OperatorDeclaration>> o,
                         std::vector<std::unique_ptr<ConstructorDeclaration>> c,
                         std::unique_ptr<DestructorDeclaration> d,
                         bool pub);
    void accept(Visitor& v) override;
};

// One member of an enum as the parser reads it: `First = 1`
// (tests/samples/literal_interface.fin:14), `RGB(uint{8}, uint{8}, uint{8})`
// (enums.fin:8) and `pub Ok <T>` (stdlib/stdio.fin:51) are all members. Only the
// parser holds these; EnumDeclaration splits them into `values` and
// `member_payloads` below.
struct EnumMember {
    std::string name;
    std::unique_ptr<Expression> value;              // `= 1`, else null
    std::vector<std::unique_ptr<TypeNode>> payload; // `(T)` or `<T>`, else empty
    bool is_public = false;                         // `pub Ok <T>`
};

// What a member carries besides its value: the payload types and its visibility.
// tests/samples/enums.fin:8 (`RGB(uint{8}, uint{8}, uint{8})`),
// stdlib/typing.fin:15 (`Ok(T)`) and stdlib/stdio.fin:50 (`pub Err <IOError>`).
struct EnumPayload {
    std::string name;
    std::vector<std::unique_ptr<TypeNode>> types;
    bool is_public = false;
};

class EnumDeclaration : public Statement {
public:
    std::string name;
    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> values;
    std::vector<std::unique_ptr<Attribute>> attributes;
    bool is_public;
    // `pub enum Result <T: Any<...>, U: ErrorLike>` -- tests/samples/stdlib/typing.fin:14.
    std::vector<std::unique_ptr<GenericParam>> generic_params;
    // One entry per member, in the same order as `values`, so member i's payload
    // is `member_payloads[i]`. `values` still holds name/value pairs and its type
    // is unchanged, because every pass that reads an enum today reads that; a
    // payload or a `pub` on a member is only visible here. An entry with an empty
    // `types` is a member without a payload.
    std::vector<EnumPayload> member_payloads;
    EnumDeclaration(std::string n, std::vector<std::pair<std::string, std::unique_ptr<Expression>>> v, bool pub);
    void accept(Visitor& v) override;
};

}
