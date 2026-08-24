#include "StructType.hpp"
#include "TypeImpl.hpp"
// `operators` is keyed by a plain `int` so that this layer need not know what an
// operator token is. One rule below does need to know two of them by name, so the
// header comes in here and not into StructType.hpp.
#include "../ast/nodes/ASTNode.hpp"
#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace fin {

namespace {

// Instantiating a struct that mentions `Self` makes that struct self-referential:
// `pub fun me(self: &Self) <Self>` on `C<T>` becomes `fn() -> C<int>` on the very
// object being built. That is what `Self` means and the cycle has to stay -- but it
// means the *next* substitution of that object walks the member, arrives back at the
// object, and starts over. `struct H<T> { keys <&C<T>>, }` is enough to reach it, and
// lib/std reaches it for real: `HashMap<T, U>` holds two `Collection`s and
// `Collection<T>` declares `from_prototype() <Self>`.
//
// So a substitution publishes its result before it walks its members, and a recursive
// visit for the same (struct, mapping, Self) is handed that result instead of starting
// a second descent. Types are immutable once built, so sharing the object is not just
// safe, it is the answer: the recursive occurrence *is* the type being built.
//
// Soundness_MachineContract.ASelfReferentialInstantiationIsNotFatal is the record.
std::unordered_map<std::string, TypePtr>& substitutionsInProgress() {
    static thread_local std::unordered_map<std::string, TypePtr> inProgress;
    return inProgress;
}

// Keyed on the mapping's contents rather than its address: `instantiate` builds its
// TypeMap on the stack, and two unrelated instantiations can be handed the same
// address once the first has returned.
std::string substitutionKey(const void* subject, const TypeMap& mapping,
                            const TypePtr& selfReplacement) {
    std::vector<std::string> entries;
    entries.reserve(mapping.size());
    for (const auto& kv : mapping) {
        entries.push_back(kv.first + "=" + (kv.second ? kv.second->toString() : std::string("<null>")));
    }
    std::sort(entries.begin(), entries.end());
    std::ostringstream os;
    os << subject << '|' << static_cast<const void*>(selfReplacement.get()) << '|';
    for (const auto& e : entries) os << e << ';';
    return os.str();
}

struct InProgressGuard {
    std::string key;
    InProgressGuard(std::string k, const TypePtr& value) : key(std::move(k)) {
        substitutionsInProgress()[key] = value;
    }
    ~InProgressGuard() { substitutionsInProgress().erase(key); }
    InProgressGuard(const InProgressGuard&) = delete;
    InProgressGuard& operator=(const InProgressGuard&) = delete;
};

} // namespace

std::string StructType::toString() const {
    if (generic_args.empty()) return name;
    std::string s = name + "<";
    for (size_t i = 0; i < generic_args.size(); ++i) {
        s += generic_args[i]->toString();
        if (i < generic_args.size() - 1) s += ", ";
    }
    s += ">";
    return s;
}

TypePtr StructType::getFieldType(const std::string& n) {
    if (auto* own = findField(n)) return own->type;
    for (const auto& parent : parents) {
        if (auto p = std::dynamic_pointer_cast<StructType>(parent)) {
            if (auto t = p->getFieldType(n)) return t;
        }
    }
    return nullptr;
}

bool StructType::isFieldPublic(const std::string& n) {
    if (auto* own = findField(n)) return own->is_public;
    for (const auto& parent : parents) {
        if (auto p = std::dynamic_pointer_cast<StructType>(parent)) {
            if (p->getFieldType(n)) return p->isFieldPublic(n);
        }
    }
    return false;
}

TypePtr StructType::getMethodType(const std::string& n) {
    if (methods.count(n)) return methods.at(n);
    for (const auto& parent : parents) {
        if (auto p = std::dynamic_pointer_cast<StructType>(parent)) {
            if (auto t = p->getMethodType(n)) return t;
        }
    }
    return nullptr;
}

TypePtr StructType::getOperatorType(int op) const {
    auto it = operators.find(op);
    if (it != operators.end()) return it->second;
    for (const auto& parent : parents) {
        if (auto p = std::dynamic_pointer_cast<StructType>(parent)) {
            if (auto t = p->getOperatorType(op)) return t;
        }
    }
    return nullptr;
}

TypePtr StructType::getOperatorReturnType(int op) const {
    auto t = getOperatorType(op);
    if (!t) return nullptr;
    if (auto* f = t->as<FunctionType>()) return f->return_type;
    return t;
}

TypePtr StructType::getMethodReturnType(const std::string& n) {
    auto t = getMethodType(n);
    if (!t) return nullptr;
    if (auto* f = t->as<FunctionType>()) return f->return_type;
    return t;
}

bool StructType::equals(const Type& other) const {
    auto* o = other.as<StructType>();
    if (!o) return false;
    if (name != o->name) return false;
    if (generic_args.size() != o->generic_args.size()) return false;
    for (size_t i = 0; i < generic_args.size(); ++i) {
        if (!typesEqual(generic_args[i], o->generic_args[i])) return false;
    }
    return true;
}

TypePtr StructType::clone() const {
    std::vector<TypePtr> newArgs;
    for (auto& arg : generic_args) newArgs.push_back(arg->clone());
    
    auto s = std::make_shared<StructType>(name, newArgs);
    // In order, so the copy lays out the way the original does. defineField rebuilds
    // field_index as it goes, which is why the index is never copied directly.
    for(const auto& f : fields) s->defineField(f.name, f.type->clone(), f.is_public);
    for(auto& kv : methods) s->defineMethod(kv.first, kv.second->clone());
    for(auto& kv : operators) s->defineOperator(kv.first, kv.second->clone());
    for(const auto& p : parents) s->parents.push_back(p->clone());
    
    s->constructors = constructors;
    s->has_destructor = has_destructor;
    s->is_interface = is_interface;
    s->is_enum = is_enum;
    for (auto& kv : enumerators) s->defineEnumerator(kv.first, kv.second->clone());
    
    return s;
}

TypePtr StructType::substitute(const TypeMap& mapping, TypePtr selfReplacement) {
    const std::string key = substitutionKey(this, mapping, selfReplacement);
    auto& inProgress = substitutionsInProgress();
    auto found = inProgress.find(key);
    if (found != inProgress.end()) return found->second;

    std::vector<TypePtr> newArgs;
    for(auto& arg : generic_args) newArgs.push_back(arg->substitute(mapping, selfReplacement));
    
    auto newStruct = std::make_shared<StructType>(name, newArgs);
    const InProgressGuard guard(key, newStruct);
    
    // Pass selfReplacement (or newStruct if we are the struct being instantiated)
    TypePtr nextSelf = selfReplacement ? selfReplacement : newStruct;

    // In order: `Pair<int, string>` laid out differently from `Pair<T, U>` would be
    // an ABI split between a generic function and its caller.
    for(const auto& f : fields) newStruct->defineField(f.name, f.type->substitute(mapping, nextSelf), f.is_public);
    for(auto& kv : methods) newStruct->defineMethod(kv.first, kv.second->substitute(mapping, nextSelf));
    for(auto& kv : operators) newStruct->defineOperator(kv.first, kv.second->substitute(mapping, nextSelf));
    for(const auto& p : parents) newStruct->parents.push_back(p->substitute(mapping, nextSelf));

    newStruct->is_interface = is_interface;
    newStruct->is_enum = is_enum;
    newStruct->has_destructor = has_destructor;
    // `Result<int, string>::Ok` is `fn(int) -> Result<int, string>`, so an enumerator
    // is substituted like a method: without this, instantiating a generic enum kept
    // every payload at the template's type parameter.
    for (auto& kv : enumerators) newStruct->defineEnumerator(kv.first, kv.second->substitute(mapping, nextSelf));
    
    for(auto& c : constructors) {
        if (auto* func = c->as<FunctionType>()) {
            std::vector<TypePtr> newParams;
            for(auto& p : func->param_types) newParams.push_back(p->substitute(mapping, nextSelf));
            
            auto newCtor = std::make_shared<FunctionType>(newParams, nextSelf, func->is_vararg);
            newStruct->addConstructor(newCtor);
        }
    }

    return newStruct;
}

TypePtr StructType::instantiate(const std::vector<TypePtr>& concreteArgs) {
    if (concreteArgs.size() != generic_args.size()) return nullptr;
    TypeMap mapping;
    for(size_t i=0; i<generic_args.size(); ++i) {
        mapping[generic_args[i]->toString()] = concreteArgs[i];
    }
    return substitute(mapping);
}

TypePtr StructType::constructorFor(const std::shared_ptr<StructType>& type) {
    if (!type) return nullptr;
    if (!type->constructors.empty()) return type->constructors[0];
    for (const auto& parent : type->parents) {
        auto p = std::dynamic_pointer_cast<StructType>(parent);
        if (!p || p->is_interface) continue;
        if (auto inherited = constructorFor(p)) {
            // Rebound to `type`. The parameters are the parent's, the thing
            // constructed is not -- see the header for why that matters. The recursive
            // call has already rebound it to `p`; only the outermost binding survives,
            // which is the one the caller asked about.
            if (auto* sig = inherited->as<FunctionType>()) {
                return std::make_shared<FunctionType>(sig->param_types, type, sig->is_vararg);
            }
            // Not a signature, so there is nothing to rebind and nothing this can
            // usefully say about it. Handed back as found rather than dropped: a
            // caller that checks arguments against it will refuse the call, which is
            // a better failure than reporting a constructor that does exist as absent.
            return inherited;
        }
    }
    return nullptr;
}

// Does this constructor satisfy that requirement? Parameters only, deliberately.
//
// A constructor's return type is the type being constructed, so an interface's
// requirement is registered `fn(params) -> TheInterface` and the implementor's own
// constructor `fn(params) -> TheImplementor` (Analyzer_Decl.cpp registers both).
// FunctionType::equals compares return types -- correctly, for every other kind of
// function -- and the pair therefore never matched, whatever the parameters said. No
// struct in the language could satisfy `Self(data: [char]);`, which is the whole of
// `Struct 'Stream' does not implement interface 'IStream'` at stdlib/stdio.fin:93.
//
// Not a looser check than equals but a differently aimed one: the parameters are still
// compared exactly, and Soundness_InterfaceConstructors has a guard on each way of
// getting them wrong -- absent, wrong arity, wrong type.
static bool constructorSatisfies(const TypePtr& mine, const TypePtr& required) {
    auto* m = mine ? mine->as<FunctionType>() : nullptr;
    auto* r = required ? required->as<FunctionType>() : nullptr;
    // A requirement that is not a FunctionType is not something this can match. It
    // should not happen -- both registration sites build one -- and returning false
    // rather than asserting leaves a diagnostic rather than a crash if it ever does.
    if (!m || !r) return false;
    if (m->is_vararg != r->is_vararg) return false;
    if (m->param_types.size() != r->param_types.size()) return false;
    for (size_t i = 0; i < m->param_types.size(); ++i) {
        if (!typesEqual(m->param_types[i], r->param_types[i])) return false;
    }
    return true;
}

bool StructType::implements(const StructType* interface) const {
    for (const auto& [methodName, retType] : interface->methods) {
        if (methods.find(methodName) == methods.end()) return false;
    }
    for (const auto& [op, retType] : interface->operators) {
        if (operators.find(op) != operators.end()) continue;
        // `operator=` is the other spelling of `operator []=`. stdlib/operators.fin:126
        // requires `operator []=` for `IndexAssign`, and the two corpus structs that
        // implement it write `operator=` -- stdlib/hashmap.fin:51 live, next to its
        // `operator[]`, and stdlib/collection.fin:80 commented out beside the same
        // pairing. Neither file writes the bracket form anywhere, and none of the thirty
        // interfaces asks for a plain assignment operator, so nothing else claims the
        // spelling.
        //
        // Only the requirement is lenient: a declaration keeps whichever kind it was
        // written as, `operator []=` still satisfies this on its own, and the leniency is
        // one-directional -- an `operator =` requirement is not satisfied by
        // `operator []=` (Soundness_IndexAssignInterface).
        if (op == static_cast<int>(ASTTokenKind::INDEX_ASSIGN) &&
            operators.count(static_cast<int>(ASTTokenKind::EQUAL))) {
            continue;
        }
        return false;
    }
    if (interface->has_destructor && !this->has_destructor) return false;
    for (const auto& ifaceCtor : interface->constructors) {
        bool found = false;
        for (const auto& myCtor : this->constructors) {
            if (constructorSatisfies(myCtor, ifaceCtor)) { found = true; break; }
        }
        if (!found) return false;
    }
    return true;
}

}
