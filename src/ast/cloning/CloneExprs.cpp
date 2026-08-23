#include "../CloneVisitor.hpp"
#include "../exprs/PrototypeExpr.hpp"

namespace fin {

void CloneVisitor::visit(PrototypeLiteral& node) {
    std::vector<std::pair<std::unique_ptr<Expression>, std::unique_ptr<Expression>>> elements;
    for (auto& e : node.elements) {
        elements.push_back({clone(e.first.get()), clone(e.second.get())});
    }
    auto res = std::make_unique<PrototypeLiteral>(std::move(elements));
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
        cloneVector(node.args)
    );
    res->generic_args = cloneVector(node.generic_args);
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(MethodCall& node) {
    auto res = std::make_unique<MethodCall>(
        clone(node.object.get()),
        node.method_name,
        cloneVector(node.args),
        cloneVector(node.generic_args)
    );
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(TypeLiteralExpression& node) {
    // The declaration is cloned through the same visit the named form uses, so a
    // literal inside a macro body is copied as completely as a named type is. The
    // generated name is copied with it: a macro expanded twice therefore yields two
    // types with one name, which is a duplicate-definition diagnostic rather than
    // silent aliasing. Nothing in the corpus does it.
    auto res = std::make_unique<TypeLiteralExpression>(clone(node.decl.get()), node.is_interface);
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
    if (!node.init_fields.empty()) {
        std::vector<std::pair<std::string, std::unique_ptr<Expression>>> fields;
        for(auto& f : node.init_fields) {
            fields.push_back({f.first, clone(f.second.get())});
        }
        auto res = std::make_unique<NewExpression>(clone(node.type.get()), std::move(fields));
        res->setLoc(node.loc);
        result = std::move(res);
    } else {
        auto res = std::make_unique<NewExpression>(clone(node.type.get()), cloneVector(node.args));
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
        cloneVector(node.generic_args)
    );
    res->setLoc(node.loc);
    result = std::move(res);
}

void CloneVisitor::visit(ArrayLiteral& node) {
    auto res = std::make_unique<ArrayLiteral>(cloneVector(node.elements));
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

void CloneVisitor::visit(LambdaExpression& node) {
    if (node.body) {
        auto res = std::make_unique<LambdaExpression>(
            cloneVector(node.params),
            clone(node.return_type.get()),
            clone(node.body.get())
        );
        res->setLoc(node.loc);
        result = std::move(res);
    } else {
        auto res = std::make_unique<LambdaExpression>(
            cloneVector(node.params),
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
        cloneVector(node.args)
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

void CloneVisitor::visit(SuperExpression& node) {
    if (!node.init_fields.empty()) {
        std::vector<std::pair<std::string, std::unique_ptr<Expression>>> fields;
        for(auto& f : node.init_fields) {
            fields.push_back({f.first, clone(f.second.get())});
        }
        auto res = std::make_unique<SuperExpression>(node.parent_name, std::move(fields));
        res->setLoc(node.loc);
        result = std::move(res);
    } else {
        auto res = std::make_unique<SuperExpression>(node.parent_name, cloneVector(node.args));
        res->setLoc(node.loc);
        result = std::move(res);
    }
}

void CloneVisitor::visit(MacroCall& node) {
    auto res = std::make_unique<MacroCall>(node.name, cloneVector(node.args));
    res->setLoc(node.loc); result = std::move(res);
}

void CloneVisitor::visit(StaticMethodCall& node) {
    auto res = std::make_unique<StaticMethodCall>(
        clone(node.target_type.get()),
        node.method_name,
        cloneVector(node.args),
        cloneVector(node.generic_args)
    );
    res->setLoc(node.loc);
    result = std::move(res);
}

}
