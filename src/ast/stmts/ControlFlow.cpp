#include "ControlFlow.hpp"
#include "../Visitor.hpp"

namespace fin {

ReturnStatement::ReturnStatement(std::unique_ptr<Expression> v) : value(std::move(v)) {}
void ReturnStatement::accept(Visitor& v) { v.visit(*this); }

IfStatement::IfStatement(std::unique_ptr<Expression> c, std::unique_ptr<Block> t, std::unique_ptr<Statement> e)
    : condition(std::move(c)), then_block(std::move(t)), else_stmt(std::move(e)) {}
void IfStatement::accept(Visitor& v) { v.visit(*this); }

WhileLoop::WhileLoop(std::unique_ptr<Expression> c, std::unique_ptr<Block> b)
    : condition(std::move(c)), body(std::move(b)) {}
void WhileLoop::accept(Visitor& v) { v.visit(*this); }

ForLoop::ForLoop(std::unique_ptr<Statement> i, std::unique_ptr<Expression> c, std::unique_ptr<Expression> inc, std::unique_ptr<Block> b)
    : init(std::move(i)), condition(std::move(c)), increment(std::move(inc)), body(std::move(b)) {}
void ForLoop::accept(Visitor& v) { v.visit(*this); }

ForeachLoop::ForeachLoop(std::string n, std::unique_ptr<TypeNode> t, std::unique_ptr<Expression> i, std::unique_ptr<Block> b)
    : var_name(std::move(n)), var_type(std::move(t)), iterable(std::move(i)), body(std::move(b)) {}
void ForeachLoop::accept(Visitor& v) { v.visit(*this); }

void BreakStatement::accept(Visitor& v) { v.visit(*this); }
void ContinueStatement::accept(Visitor& v) { v.visit(*this); }

}
