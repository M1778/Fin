#include "ErrorHandling.hpp"
#include "../Visitor.hpp"

namespace fin {

TryCatch::TryCatch(std::unique_ptr<Block> t, std::string cv, std::unique_ptr<TypeNode> ct, std::unique_ptr<Block> cb)
    : try_block(std::move(t)), catch_var(std::move(cv)), catch_type(std::move(ct)), catch_block(std::move(cb)) {}
void TryCatch::accept(Visitor& v) { v.visit(*this); }

BlameStatement::BlameStatement(std::unique_ptr<Expression> c, std::unique_ptr<Expression> m) 
    : condition(std::move(c)), message(std::move(m)) {}
void BlameStatement::accept(Visitor& v) { v.visit(*this); }

}
