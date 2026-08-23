#include "StructType.hpp"
#include "TypeImpl.hpp"
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
    if (fields.count(n)) return fields.at(n).type;
    for (const auto& parent : parents) {
        if (auto p = std::dynamic_pointer_cast<StructType>(parent)) {
            if (auto t = p->getFieldType(n)) return t;
        }
    }
    return nullptr;
}

bool StructType::isFieldPublic(const std::string& n) {
    if (fields.count(n)) return fields.at(n).is_public;
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
    for(auto& kv : fields) s->defineField(kv.first, kv.second.type->clone(), kv.second.is_public);
    for(auto& kv : methods) s->defineMethod(kv.first, kv.second->clone());
    for(auto& kv : operators) s->defineOperator(kv.first, kv.second->clone());
    for(const auto& p : parents) s->parents.push_back(p->clone());
    
    s->constructors = constructors;
    s->has_destructor = has_destructor;
    s->is_interface = is_interface;
    
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

    for(auto& kv : fields) newStruct->defineField(kv.first, kv.second.type->substitute(mapping, nextSelf), kv.second.is_public);
    for(auto& kv : methods) newStruct->defineMethod(kv.first, kv.second->substitute(mapping, nextSelf));
    for(auto& kv : operators) newStruct->defineOperator(kv.first, kv.second->substitute(mapping, nextSelf));
    for(const auto& p : parents) newStruct->parents.push_back(p->substitute(mapping, nextSelf));

    newStruct->is_interface = is_interface;
    newStruct->has_destructor = has_destructor;
    
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

bool StructType::implements(const StructType* interface) const {
    for (const auto& [methodName, retType] : interface->methods) {
        if (methods.find(methodName) == methods.end()) return false;
    }
    for (const auto& [op, retType] : interface->operators) {
        if (operators.find(op) == operators.end()) return false;
    }
    if (interface->has_destructor && !this->has_destructor) return false;
    for (const auto& ifaceCtor : interface->constructors) {
        bool found = false;
        for (const auto& myCtor : this->constructors) {
            if (myCtor->equals(*ifaceCtor)) { found = true; break; }
        }
        if (!found) return false;
    }
    return true;
}

}
