// SPDX-License-Identifier: GPL-3.0-or-later
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

    // Issue #101 (see TypeSweep.hpp): the two edges this node owns. A signature is
    // the half of every struct/enum cycle this file was silent about: the struct's
    // method, constructor and enumerator tables point here, and these edges point
    // back at the struct. `param_defaults` is positionally parallel flags, not an
    // edge, and stays.
    //
    // The edges move into stack locals before anything is dropped: a signature
    // naming its struct twice (`f(self: S) -> S`) holds two of the struct's
    // references, so dropping the edges in place can free this node itself
    // mid-function, and a destructor destroying the containers the sweep is
    // still walking is use-after-free by construction. The locals carry the
    // graph on this stack frame while the members sit empty for their own
    // destructor.
    void releaseTypeEdges() override {
        std::vector<TypePtr> params = std::move(param_types);
        TypePtr ret = std::move(return_type);
    }
};

}
