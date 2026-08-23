#pragma once
#include "Type.hpp"
#include <vector>
#include <unordered_map>
#include "FunctionType.hpp" // Needed for constructors often, but let's check circular deps. 
// StructType uses TypePtr which is opaque, but addConstructor implementation uses FunctionType.
// Headers are safe if only using TypePtr, but methods need full definitions. 
// We include FunctionType.hpp here for convenience if it's used in headers (not really, only in cpp).
// Actually, let's keep it minimal. Forward decl is enough for arguments, but member definition needs it?
// member 'constructors' is vector<TypePtr>, so it's fine. 
// But 'addConstructor' takes TypePtr. So we are good.
// However, 'implements' uses 'methods' which are TypePtr.

namespace fin {

struct FieldInfo {
    TypePtr type;
    bool is_public;
};

class StructType : public Type {
public:
    std::string name;
    std::vector<TypePtr> generic_args;
    std::vector<TypePtr> parents;
    bool is_interface = false;
    
    std::unordered_map<std::string, FieldInfo> fields;
    // Name -> the method's FunctionType. It held only a return type until a method
    // call needed a signature to be checked against, which is why the map's value is
    // still a plain TypePtr: clone() and substitute() below already do the right thing
    // with a FunctionType, and `implements` compares names. The receiver is not among
    // the parameters -- see SemanticAnalyzer::buildMethodSignature for why.
    std::unordered_map<std::string, TypePtr> methods;
    // Operator key (ASTTokenKind) -> the operator's FunctionType, the receiver not
    // among its parameters. This held a bare return type until a subscript needed a
    // parameter to be checked against; Analyzer_Decl.cpp's comment on the old
    // registration said building a signature there "would be dead. Booked, not done",
    // and Soundness_IndexOperator is what made it live.
    std::unordered_map<int, TypePtr> operators;
    std::vector<TypePtr> constructors; 
    bool has_destructor = false;

    StructType(std::string n, std::vector<TypePtr> args = {}) 
        : name(std::move(n)), generic_args(std::move(args)) {}

    void defineField(std::string n, TypePtr t, bool pub = false) { fields[n] = {t, pub}; }
    void defineMethod(std::string n, TypePtr t) { methods[n] = t; }
    void defineOperator(int op, TypePtr t) { operators[op] = t; }
    void addConstructor(TypePtr t) { constructors.push_back(t); }

    TypePtr getFieldType(const std::string& n);
    bool isFieldPublic(const std::string& n);
    // The method's whole signature, or null when the type has no such method.
    // Walks `parents` exactly as getMethodReturnType does, so an inherited method is
    // as checkable as a declared one.
    TypePtr getMethodType(const std::string& n);
    // Its return type. Kept as its own accessor because that is what most callers
    // want, and because it tolerates a value that is not a FunctionType: nothing in
    // the language reaches that today, but a `methods` entry that lost its parameters
    // should degrade to an unchecked call rather than to a null dereference.
    TypePtr getMethodReturnType(const std::string& n);
    // The operator's whole signature, or null when neither this type nor any parent
    // declares it. Walks `parents` for the same reason getMethodType does: an
    // inherited operator is as declared as a written one, and a struct gets its
    // conformance to Index/IndexAssign (stdlib/operators.fin) through an interface
    // parent.
    TypePtr getOperatorType(int op) const;
    // Its return type, and null-tolerant in the same way getMethodReturnType is.
    TypePtr getOperatorReturnType(int op) const;

    std::string toString() const override;
    bool equals(const Type& other) const override;
    TypePtr substitute(const TypeMap& mapping, TypePtr selfReplacement = nullptr) override;
    TypePtr clone() const override;

    TypePtr instantiate(const std::vector<TypePtr>& concreteArgs);
    bool implements(const StructType* interface) const;
};

}
