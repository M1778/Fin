#include "ErrorHandling.hpp"
#include "../Visitor.hpp"

namespace fin {

TryCatch::TryCatch(std::unique_ptr<Block> t, std::string cv, std::unique_ptr<TypeNode> ct, std::unique_ptr<Block> cb)
    : try_block(std::move(t)), catch_var(std::move(cv)), catch_type(std::move(ct)), catch_block(std::move(cb)) {}
void TryCatch::accept(Visitor& v) { v.visit(*this); }

BlameStatement::BlameStatement(std::unique_ptr<Expression> e) : error_expr(std::move(e)) {}
void BlameStatement::accept(Visitor& v) { v.visit(*this); }

}
