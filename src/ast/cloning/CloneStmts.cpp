#include "../CloneVisitor.hpp"

namespace fin {

void CloneVisitor::visit(Block& node) {
    auto res = std::make_unique<Block>(cloneVector(node.statements));
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
    auto res = std::make_unique<BlameStatement>(
        clone(node.condition.get()),
        clone(node.message.get())
    );
    res->setLoc(node.loc); result = std::move(res);
}

}
