#pragma once
#include "Type.hpp"

namespace fin {

class FunctionType : public Type {
public:
    std::vector<TypePtr> param_types;
    TypePtr return_type;
    bool is_vararg;

    // Which parameters were written with a default value, positionally parallel to
    // `param_types`. Empty means none was, which is what every construction site that
    // cannot know says by omitting the argument -- an `fn(int)` type annotation, an
    // enum payload constructor, a compiler-API method. A shorter-than-parameters vector
    // is legal for the same reason and reads as "false" past its end, so a site that
    // learns about only its leading parameters is not obliged to pad.
    //
    // Parallel to `param_types` rather than a count of required parameters, because
    // *which* parameters are optional decides arity and a count cannot say it:
    // arguments bind positionally, so `(a: int = 1, b: int)` still needs both written
    // while `(a: int, b: int = 1)` needs one. checkCallArity folds this together with
    // nullability, which is the other source of optionality and the only one there was.
    //
    // A vector of flags and not a vector of the default *expressions*: nothing checks a
    // default at a call site -- the declaration checked it where it was written
    // (visitParameterDefaults) -- and the backend does not lower a call to an imported
    // function at all yet, so there is no consumer for the expression. Storing one would
    // put an AST pointer inside a Type, which is the aliasing `substitute` and `clone`
    // exist to avoid.
    std::vector<bool> param_defaults;

    FunctionType(std::vector<TypePtr> params, TypePtr ret, bool vararg = false,
                 std::vector<bool> defaults = {})
        : param_types(std::move(params)), return_type(std::move(ret)), is_vararg(vararg),
          param_defaults(std::move(defaults)) {}

    // Reads past the end as false, so a caller need not ask how long the vector is.
    bool hasDefault(size_t i) const {
        return i < param_defaults.size() && param_defaults[i];
    }

    std::string toString() const override;
    bool equals(const Type& other) const override;
    TypePtr substitute(const TypeMap& mapping, TypePtr selfReplacement = nullptr) override;
    TypePtr clone() const override;
};

}
