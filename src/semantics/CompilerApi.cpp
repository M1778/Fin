#include "CompilerApi.hpp"

namespace fin::compilerapi {

namespace {

Member op(std::string name, std::string result, std::vector<std::string> params) {
    return Member{std::move(name), std::move(result), std::move(params), false, 0, false};
}
Member konst(std::string name, std::string type) {
    return Member{std::move(name), std::move(type), {}, true, 0, false};
}

std::vector<Component> build() {
    std::vector<Component> cs;

    // --- compiler.types: identity, resolution, comparison, kind (§2.4) ---
    {
        Component c{"types", 1, {}};
        // `cmp_types` returns -1 for unequal; stdlib/error.fin:28 compares against it.
        c.members.push_back(op("cmp_types", "int", {"$type", "$type"}));
        // The compile-time type of a value; stdlib/types.fin:91.
        c.members.push_back(op("ct_any", "$type", {"any"}));
        // Turbofish, no value argument, and the result is a `$struct` and not a
        // `$type`: it is the compiler's own TypeInfo-shaped handle, which is what
        // makes `select_field` applicable to it (stdlib/types.fin:25).
        c.members.push_back(Member{"gettype", "$struct", {}, false, 1, false});
        c.members.push_back(op("typefrom_typeid", "$type", {"uint"}));
        c.members.push_back(op("typeid_of", "uint", {"$type"}));
        c.members.push_back(op("name_of", "string", {"$type"}));
        c.members.push_back(op("kind_of", "int", {"$type"}));
        c.members.push_back(op("implements", "bool", {"$type", "$interface"}));
        c.members.push_back(op("is_comptime_type", "bool", {"$type"}));
        // §2.1b: a constant lives under the component whose operations consume it,
        // and `kind_of` is the consumer of these.
        for (const char* k : {"KindType", "KindStruct", "KindInterface", "KindEnum",
                              "KindPrimitive", "KindPointer", "KindArray",
                              "KindFunction", "KindPrototype"})
            c.members.push_back(konst(k, "int"));
        cs.push_back(std::move(c));
    }

    // --- compiler.structs: the structure of a struct, class or interface ---
    {
        Component c{"structs", 1, {}};
        // Generic in its *result*, and nullable: `select_field::<R>(s, name) <?R>`.
        // stdlib/types.fin:25 denullifies it with a postfix `?`, which is the only
        // reason the `int` it assigns to fits.
        c.members.push_back(Member{"select_field", "R", {"$struct", "string"}, false, 1, true});
        c.members.push_back(op("has_field", "bool", {"$struct", "string"}));
        c.members.push_back(op("field_count", "int", {"$struct"}));
        c.members.push_back(Member{"field_type", "$type", {"$struct", "string"}, false, 0, true});
        c.members.push_back(op("field_visibility", "int", {"$struct", "string"}));
        c.members.push_back(op("has_destructor", "bool", {"$struct"}));
        for (const char* k : {"VisPublic", "VisPrivate", "VisReadonly"})
            c.members.push_back(konst(k, "int"));
        cs.push_back(std::move(c));
    }

    // --- compiler.enums: enum reflection, plus one grandfathered constant ---
    {
        Component c{"enums", 1, {}};
        // `InBytes` is a unit constant whose only consumer is
        // `compiler.system.get_available_memory`, so §2.1b would file it under
        // `system`. The corpus puts it here (stdlib/memory.fin:32,33,41) and the
        // thirteen paths are the specification, so it stays -- a documented
        // exception, and the reason memory.fin must gain an `enums` grant.
        for (const char* k : {"InBytes", "InKilobytes", "InMegabytes", "InGigabytes"})
            c.members.push_back(konst(k, "uint"));
        // `EnumType` is `pub type EnumType = any implements <Enum>` -- a library
        // alias declared in stdlib/enums.fin, which this table cannot name. `any`
        // is its substrate and accepts what the corpus passes.
        c.members.push_back(op("resolve_id", "int", {"any"}));
        // Replaces `enum_member._keyid` at stdlib/enums.fin:23 and keeps
        // `$enum_member` opaque (§2.3).
        c.members.push_back(op("keyid_of", "int", {"$enum_member"}));
        c.members.push_back(Member{"payload_type", "$type", {"$enum_member"}, false, 0, true});
        cs.push_back(std::move(c));
    }

    // --- compiler.system: host and target facts ---
    {
        Component c{"system", 1, {}};
        c.members.push_back(op("get_total_memory", "uint", {"uint"}));
        c.members.push_back(op("get_available_memory", "uint", {"uint"}));
        c.members.push_back(op("get_memorycard_model", "string", {}));
        c.members.push_back(op("pointer_size", "uint", {}));
        c.members.push_back(op("target_triple", "string", {}));
        cs.push_back(std::move(c));
    }

    return cs;
}

} // namespace

const std::vector<Component>& components() {
    static const std::vector<Component> table = build();
    return table;
}

const Component* findComponent(const std::string& name) {
    for (const auto& c : components())
        if (c.name == name) return &c;
    return nullptr;
}

const Member* findMember(const Component& c, const std::string& member) {
    for (const auto& m : c.members)
        if (m.name == member) return &m;
    return nullptr;
}

const std::vector<Member>& referenceOps() {
    // ADR 0012: "Any third segment under `compiler.components` is an operation on a
    // reference, never a component name." These four are that set. `present()` is
    // the one with a hard requirement attached -- it must answer for a component
    // this compiler does not have (§2.1a).
    static const std::vector<Member> ops = {
        op("present", "bool", {}),
        op("version", "int", {}),
        op("granted", "bool", {}),
        op("name", "string", {}),
    };
    return ops;
}

} // namespace fin::compilerapi
