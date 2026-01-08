#include "../CloneVisitor.hpp"

namespace fin {

void CloneVisitor::visit(Program& node) {
    auto res = std::make_unique<Program>(cloneVector(node.statements));
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(FunctionDeclaration& node) {
    auto res = std::make_unique<FunctionDeclaration>(
        node.name,
        cloneVector(node.params),
        clone(node.return_type.get()),
        clone(node.body.get())
    );
    res->is_public = node.is_public;
    res->is_static = node.is_static;
    res->generic_params = cloneVector(node.generic_params);
    res->attributes = cloneVector(node.attributes);
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(VariableDeclaration& node) {
    auto res = std::make_unique<VariableDeclaration>(
        node.is_mutable, 
        node.name, 
        clone(node.type.get()), 
        clone(node.initializer.get())
    );
    res->is_public = node.is_public;
    res->attributes = cloneVector(node.attributes);
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(StructDeclaration& node) {
    auto res = std::make_unique<StructDeclaration>(
        node.name,
        cloneVector(node.members),
        node.is_public
    );
    res->methods = cloneVector(node.methods);
    res->operators = cloneVector(node.operators);
    res->constructors = cloneVector(node.constructors); 
    if (node.destructor) res->destructor = clone(node.destructor.get());
    
    res->generic_params = cloneVector(node.generic_params);
    res->attributes = cloneVector(node.attributes);
    
    res->parents = cloneVector(node.parents);
    res->is_class = node.is_class;
    
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(ClassDeclaration& node) {
    auto res = std::make_unique<ClassDeclaration>(
        node.name,
        cloneVector(node.members),
        node.is_public
    );
    res->methods = cloneVector(node.methods);
    res->operators = cloneVector(node.operators);
    res->constructors = cloneVector(node.constructors); 
    if (node.destructor) res->destructor = clone(node.destructor.get());
    
    res->generic_params = cloneVector(node.generic_params);
    res->attributes = cloneVector(node.attributes);
    
    res->parents = cloneVector(node.parents);
    
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(InterfaceDeclaration& node) {
    auto res = std::make_unique<InterfaceDeclaration>(
        node.name,
        cloneVector(node.members),
        cloneVector(node.methods),
        cloneVector(node.operators),
        cloneVector(node.constructors),
        node.destructor ? clone(node.destructor.get()) : nullptr,
        node.is_public
    );
    res->attributes = cloneVector(node.attributes);
    res->generic_params = cloneVector(node.generic_params);
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(EnumDeclaration& node) {
    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> vals;
    for(auto& v : node.values) vals.push_back({v.first, clone(v.second.get())});
    auto res = std::make_unique<EnumDeclaration>(node.name, std::move(vals), node.is_public);
    res->attributes = cloneVector(node.attributes);
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(DefineDeclaration& node) {
    auto res = std::make_unique<DefineDeclaration>(
        node.name,
        cloneVector(node.params),
        clone(node.return_type.get()),
        node.is_vararg
    );
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(MacroDeclaration& node) {
    auto res = std::make_unique<MacroDeclaration>(
        node.name,
        node.params, 
        clone(node.body.get())
    );
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(OperatorDeclaration& node) {
    auto res = std::make_unique<OperatorDeclaration>(
        node.op,
        cloneVector(node.params),
        clone(node.return_type.get()),
        clone(node.body.get()),
        node.is_public
    );
    res->generic_params = cloneVector(node.generic_params);
    if(node.implements_expr) res->implements_expr = clone(node.implements_expr.get());
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(ImportModule& node) {
    auto res = std::make_unique<ImportModule>(node.source, node.is_package, node.alias, node.targets);
    res->setLoc(node.loc); result = std::move(res);
}

void CloneVisitor::visit(ConstructorDeclaration& node) {
    auto res = std::make_unique<ConstructorDeclaration>(
        node.name,
        cloneVector(node.params),
        clone(node.body.get()),
        clone(node.return_type.get())
    );
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(DestructorDeclaration& node) {
    auto res = std::make_unique<DestructorDeclaration>(
        node.name,
        clone(node.body.get())
    );
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(Parameter& node) {
    auto res = std::make_unique<Parameter>(
        node.name, 
        clone(node.type.get()), 
        clone(node.default_value.get()), 
        node.is_vararg
    );
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(StructMember& node) {
    auto res = std::make_unique<StructMember>(
        node.name, 
        clone(node.type.get()), 
        node.is_public
    );
    res->attributes = cloneVector(node.attributes);
    res->default_value = clone(node.default_value.get());
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(TypeDefinition& node) {
    auto res = std::make_unique<TypeDefinition>(
        node.name,
        clone(node.aliased_type.get())
    );
    res->generic_params = cloneVector(node.generic_params);
    res->attributes = cloneVector(node.attributes);
    res->is_public = node.is_public;
    res->has_implements = node.has_implements;
    res->implements_list = cloneVector(node.implements_list);
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(SpecialDeclaration& node) {
    auto res = std::make_unique<SpecialDeclaration>(
        node.name,
        cloneVector(node.params),
        clone(node.return_type.get()),
        clone(node.body.get())
    );
    res->attributes = cloneVector(node.attributes);
    res->setLoc(node.loc);
    result = std::move(res);
}

}
