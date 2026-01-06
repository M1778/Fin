#include "CloneVisitor.hpp"

namespace fin {

// Helper to clone vectors of unique_ptrs
template <typename T>
std::vector<std::unique_ptr<T>> cloneVector(const std::vector<std::unique_ptr<T>>& src, CloneVisitor& v) {
    std::vector<std::unique_ptr<T>> dest;
    for (const auto& item : src) {
        dest.push_back(v.clone(item.get()));
    }
    return dest;
}

// Helper to clone TypeNodes
std::unique_ptr<TypeNode> cloneType(const TypeNode* src, CloneVisitor& v) {
    if (!src) return nullptr;
    // Handle TypeNode subclasses
    // TypeNode accept calls(TypeNode), we can use v.clone
    // FunctionTypeNode calls(FunctionTypeNode).
    // v.clone handles polymorphism via accept.
    return v.clone(src);
}

// --- Implementations ---

void CloneVisitor::visit(Program& node) {
    auto res = std::make_unique<Program>(cloneVector(node.statements, *this));
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(Block& node) {
    auto res = std::make_unique<Block>(cloneVector(node.statements, *this));
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
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(FunctionDeclaration& node) {
    auto res = std::make_unique<FunctionDeclaration>(
        node.name,
        cloneVector(node.params, *this),
        clone(node.return_type.get()),
        clone(node.body.get())
    );
    res->is_public = node.is_public;
    res->is_static = node.is_static;
    res->generic_params = cloneVector(node.generic_params, *this);
    res->attributes = cloneVector(node.attributes, *this);
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(ReturnStatement& node) {
    auto res = std::make_unique<ReturnStatement>(clone(node.value.get()));
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(ExpressionStatement& node) {
    auto res = std::make_unique<ExpressionStatement>(clone(node.expr.get()));
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(IfStatement& node) {
    auto res = std::make_unique<IfStatement>(
        clone(node.condition.get()),
        clone(node.then_block.get()),
        clone(node.else_stmt.get())
    );
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(WhileLoop& node) {
    auto res = std::make_unique<WhileLoop>(
        clone(node.condition.get()),
        clone(node.body.get())
    );
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(ForLoop& node) {
    auto res = std::make_unique<ForLoop>(
        clone(node.init.get()),
        clone(node.condition.get()),
        clone(node.increment.get()),
        clone(node.body.get())
    );
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(ForeachLoop& node) {
    auto res = std::make_unique<ForeachLoop>(
        node.var_name,
        clone(node.var_type.get()),
        clone(node.iterable.get()),
        clone(node.body.get())
    );
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(BinaryOp& node) {
    auto res = std::make_unique<BinaryOp>(
        clone(node.left.get()),
        node.op,
        clone(node.right.get())
    );
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(UnaryOp& node) {
    auto res = std::make_unique<UnaryOp>(
        node.op,
        clone(node.operand.get())
    );
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(Literal& node) {
    auto res = std::make_unique<Literal>(node.value, node.kind);
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(Identifier& node) {
    auto res = std::make_unique<Identifier>(node.name);
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(FunctionCall& node) {
    auto res = std::make_unique<FunctionCall>(
        node.name,
        cloneVector(node.args, *this)
    );
    res->generic_args = cloneVector(node.generic_args, *this);
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(MethodCall& node) {
    auto res = std::make_unique<MethodCall>(
        clone(node.object.get()),
        node.method_name,
        cloneVector(node.args, *this),
        cloneVector(node.generic_args, *this)
    );
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(CastExpression& node) {
    auto res = std::make_unique<CastExpression>(
        clone(node.target_type.get()),
        clone(node.expr.get())
    );
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(NewExpression& node) {
    // Handle both constructors
    if (!node.init_fields.empty()) {
        std::vector<std::pair<std::string, std::unique_ptr<Expression>>> fields;
        for(auto& f : node.init_fields) {
            fields.push_back({f.first, clone(f.second.get())});
        }
        auto res = std::make_unique<NewExpression>(clone(node.type.get()), std::move(fields));
        res->setLoc(node.loc);
        result = std::move(res);
    } else {
        auto res = std::make_unique<NewExpression>(clone(node.type.get()), cloneVector(node.args, *this));
        res->setLoc(node.loc);
        result = std::move(res);
    }
}

void CloneVisitor::visit(MemberAccess& node) {
    auto res = std::make_unique<MemberAccess>(
        clone(node.object.get()),
        node.member
    );
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(StructInstantiation& node) {
    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> fields;
    for(auto& f : node.fields) {
        fields.push_back({f.first, clone(f.second.get())});
    }
    auto res = std::make_unique<StructInstantiation>(
        node.struct_name,
        std::move(fields),
        cloneVector(node.generic_args, *this)
    );
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(ArrayLiteral& node) {
    auto res = std::make_unique<ArrayLiteral>(cloneVector(node.elements, *this));
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(ArrayAccess& node) {
    auto res = std::make_unique<ArrayAccess>(
        clone(node.array.get()),
        clone(node.index.get())
    );
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(TypeNode& node) {
    auto res = std::make_unique<TypeNode>(node.name);
    res->generics = cloneVector(node.generics, *this);
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
        cloneVector(node.param_types, *this),
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

void CloneVisitor::visit(LambdaExpression& node) {
    // Handle both constructors
    if (node.body) {
        auto res = std::make_unique<LambdaExpression>(
            cloneVector(node.params, *this),
            clone(node.return_type.get()),
            clone(node.body.get())
        );
        res->setLoc(node.loc);
        result = std::move(res);
    } else {
        auto res = std::make_unique<LambdaExpression>(
            cloneVector(node.params, *this),
            clone(node.return_type.get()),
            clone(node.expression_body.get())
        );
        res->setLoc(node.loc);
        result = std::move(res);
    }
}

void CloneVisitor::visit(QuoteExpression& node) {
    auto res = std::make_unique<QuoteExpression>(clone(node.block.get()));
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(MacroInvocation& node) {
    auto res = std::make_unique<MacroInvocation>(
        node.name,
        cloneVector(node.args, *this)
    );
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(TernaryOp& node) {
    auto res = std::make_unique<TernaryOp>(
        clone(node.condition.get()),
        clone(node.true_expr.get()),
        clone(node.false_expr.get())
    );
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(SizeofExpression& node) {
    if (node.type_target) {
        auto res = std::make_unique<SizeofExpression>(clone(node.type_target.get()));
        res->setLoc(node.loc);
        result = std::move(res);
    } else {
        auto res = std::make_unique<SizeofExpression>(clone(node.expr_target.get()));
        res->setLoc(node.loc);
        result = std::move(res);
    }
}

// --- Stubs / Simple Clones ---
void CloneVisitor::visit(BreakStatement& node) { 
    auto res = std::make_unique<BreakStatement>(); 
    res->setLoc(node.loc); result = std::move(res); 
}
void CloneVisitor::visit(ContinueStatement& node) { 
    auto res = std::make_unique<ContinueStatement>(); 
    res->setLoc(node.loc); result = std::move(res); 
}
void CloneVisitor::visit(DeleteStatement& node) {
    auto res = std::make_unique<DeleteStatement>(clone(node.expr.get()));
    res->setLoc(node.loc); result = std::move(res);
}
void CloneVisitor::visit(TryCatch& node) {
    auto res = std::make_unique<TryCatch>(
        clone(node.try_block.get()),
        node.catch_var,
        clone(node.catch_type.get()),
        clone(node.catch_block.get())
    );
    res->setLoc(node.loc); result = std::move(res);
}
void CloneVisitor::visit(BlameStatement& node) {
    auto res = std::make_unique<BlameStatement>(clone(node.error_expr.get()));
    res->setLoc(node.loc); result = std::move(res);
}
void CloneVisitor::visit(ImportModule& node) {
    auto res = std::make_unique<ImportModule>(node.source, node.is_package, node.alias, node.targets);
    res->setLoc(node.loc); result = std::move(res);
}
void CloneVisitor::visit(StructDeclaration& node) {
    auto res = std::make_unique<StructDeclaration>(
        node.name,
        cloneVector(node.members, *this),
        node.is_public
    );
    res->methods = cloneVector(node.methods, *this);
    res->operators = cloneVector(node.operators, *this);
    res->constructors = cloneVector(node.constructors, *this); // Clone ctors
    if (node.destructor) res->destructor = clone(node.destructor.get()); // Clone dtor
    
    res->generic_params = cloneVector(node.generic_params, *this);
    res->attributes = cloneVector(node.attributes, *this);
    
    res->parents = cloneVector(node.parents, *this);
    
    res->setLoc(node.loc);
    result = std::move(res);
}
void CloneVisitor::visit(InterfaceDeclaration& node) {
    auto res = std::make_unique<InterfaceDeclaration>(
        node.name,
        cloneVector(node.members, *this),
        cloneVector(node.methods, *this),
        cloneVector(node.operators, *this),
        cloneVector(node.constructors, *this),
        node.destructor ? clone(node.destructor.get()) : nullptr,
        node.is_public
    );
    res->attributes = cloneVector(node.attributes, *this);
    res->generic_params = cloneVector(node.generic_params, *this);
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(EnumDeclaration& node) {
    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> vals;
    for(auto& v : node.values) vals.push_back({v.first, clone(v.second.get())});
    auto res = std::make_unique<EnumDeclaration>(node.name, std::move(vals), node.is_public);
    res->attributes = cloneVector(node.attributes, *this);
    res->setLoc(node.loc);
    result = std::move(res);
}
void CloneVisitor::visit(DefineDeclaration& node) {
    auto res = std::make_unique<DefineDeclaration>(
        node.name,
        cloneVector(node.params, *this),
        clone(node.return_type.get()),
        node.is_vararg
    );
    res->setLoc(node.loc);
    result = std::move(res);
}
void CloneVisitor::visit(MacroDeclaration& node) {
    // Vector copy is sufficient for MacroParam since it contains strings
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
        cloneVector(node.params, *this),
        clone(node.return_type.get()),
        clone(node.body.get()),
        node.is_public
    );
    res->generic_params = cloneVector(node.generic_params, *this);
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(SuperExpression& node) {
    if (!node.init_fields.empty()) {
        std::vector<std::pair<std::string, std::unique_ptr<Expression>>> fields;
        for(auto& f : node.init_fields) {
            fields.push_back({f.first, clone(f.second.get())});
        }
        // Use the new constructor that takes parent_name
        auto res = std::make_unique<SuperExpression>(node.parent_name, std::move(fields));
        res->setLoc(node.loc);
        result = std::move(res);
    } else {
        auto res = std::make_unique<SuperExpression>(node.parent_name, cloneVector(node.args, *this));
        res->setLoc(node.loc);
        result = std::move(res);
    }
}

void CloneVisitor::visit(MacroCall& node) {
    auto res = std::make_unique<MacroCall>(node.name, cloneVector(node.args, *this));
    res->setLoc(node.loc); result = std::move(res);
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

void CloneVisitor::visit(StaticMethodCall& node) {
    auto res = std::make_unique<StaticMethodCall>(
        clone(node.target_type.get()),
        node.method_name,
        cloneVector(node.args, *this),
        cloneVector(node.generic_args, *this)
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
    res->attributes = cloneVector(node.attributes, *this);
    res->default_value = clone(node.default_value.get());
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(ConstructorDeclaration& node) {
    auto res = std::make_unique<ConstructorDeclaration>(
        node.name,
        cloneVector(node.params, *this),
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

} // namespace fin
