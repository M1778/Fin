#include "TypeImpl.hpp"

namespace fin {

const TypePtr& errorType() {
    static const TypePtr instance = std::make_shared<ErrorType>();
    return instance;
}

bool isErrorType(const TypePtr& t) {
    if (!t) return false;
    if (t->as<ErrorType>()) return true;
    if (auto* p = t->as<PointerType>()) return isErrorType(p->pointee);
    if (auto* a = t->as<ArrayType>()) return isErrorType(a->element_type);
    if (auto* n = t->as<NullableType>()) return isErrorType(n->inner);
    // A function whose return type did not resolve is registered as
    // `fn(int) -> <error>` (Analyzer_Decl.cpp step 6 puts the sentinel there rather
    // than dropping the whole declaration). Naming that function as a value then
    // reached checkType wrapped, and `let g <int> = f;` printed
    // `expected 'int', got 'fn(int) -> <error>'`.
    if (auto* f = t->as<FunctionType>()) {
        if (isErrorType(f->return_type)) return true;
        for (const auto& p : f->param_types) if (isErrorType(p)) return true;
    }
    return false;
}

} // namespace fin
