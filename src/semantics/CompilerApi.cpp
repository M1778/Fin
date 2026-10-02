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

    // --- compiler.layout: sizes, alignments, offsets, pointer maps (§2.5) ---
    //
    // The layout pass (src/types/Layout.hpp) is the floor these stand on, and
    // the two moments are its phases: every read below is legal in observe and
    // refused by the engine in decide (ADR 0015), while the two effects are
    // legal only in decide. The analyzer resolves these signatures -- including
    // inside a provider body, which is what makes `pointer_map_quote` callable
    // there -- and never evaluates them; phase legality is the engine's, driven
    // by the events that open each moment.
    {
        Component c{"layout", 1, {}};
        c.members.push_back(op("size_of", "uint", {"$type"}));
        c.members.push_back(op("align_of", "uint", {"$type"}));
        c.members.push_back(Member{"offset_of", "uint", {"$struct", "string"}, false, 0, true});
        c.members.push_back(op("pointer_count", "int", {"$type"}));
        c.members.push_back(op("pointer_offset_at", "uint", {"$type", "int"}));
        c.members.push_back(op("pointee_type_at", "$type", {"$type", "int"}));
        c.members.push_back(op("pointer_map_quote", "quote", {"$type"}));
        c.members.push_back(op("is_layout_final", "bool", {"$type"}));
        c.members.push_back(op("request_header_words", "noret", {"$struct", "uint"}));
        c.members.push_back(op("request_min_align", "noret", {"$struct", "uint"}));
        cs.push_back(std::move(c));
    }

    // --- compiler.events: arming (wave-4 step 15) ---
    //
    // One operation (`enable`) plus the payload constants (§2.4).
    // `disable`, `is_enabled`, `armed_count` and `handler_name_at` (§2.5) are
    // the firing slice's; naming one now reports "has no member", which is
    // the honest answer until then. The parameter is
    // `any` rather than a `handler` type there is no such type yet: every type
    // fits `any`, so the call type-checks and the Arm phase (which reads the
    // handler reference off the tree) owns the validation. `enable` at top
    // level never reaches this table at all -- the Arm phase consumes it first.
    {
        Component c{"events", 1, {}};
        c.members.push_back(op("enable", "void", {"any"}));
        // docs/compiler-api.md §2.4: a constant lives under the component
        // whose operations consume it, and the variable_scope_exit payload
        // (exit_kind, moved) is consumed under `events`. Values are pinned
        // beside the payload (MovedAnalysis.hpp): ExitNormal 0, ExitBlamed
        // 1; MovedNo 0, MovedYes 1, MovedMaybe 2.
        for (const char* k : {"ExitNormal", "ExitBlamed", "MovedYes", "MovedNo", "MovedMaybe"})
            c.members.push_back(konst(k, "int"));
        cs.push_back(std::move(c));
    }

    // --- compiler.diag: a handler's own diagnostics (§2.5, step 19) ---
    //
    // Effect operations taking a message. `error` records, suppresses that
    // handler's injection at that point, and fails the build; `warning` and
    // `note` report and the quote still splices (§3.8: every one names the
    // handler, the event and the event point). `error_at` stays absent: it
    // needs a `source_loc` value Tier 4 never built, and an invented spelling
    // would be a ruling rather than a transcription.
    {
        Component c{"diag", 1, {}};
        c.members.push_back(op("error", "noret", {"string"}));
        c.members.push_back(op("warning", "noret", {"string"}));
        c.members.push_back(op("note", "noret", {"string"}));
        cs.push_back(std::move(c));
    }

    // --- compiler.scopes: what is live where (§2.5, step 18) ---
    //
    // Wave-4 step 18 (W9): the projection only. `live_pointers_quote()`
    // <quote> folds the live pointer set at the current event point into an
    // array literal of addresses, so a handler needs no loop (Q4) and the
    // interpretability line holds. The value forms (`live_count`,
    // `live_name_at`, `live_type_at`, `enclosing_function`, `depth`,
    // `is_function_scope`) need arrays and loops to consume and stay
    // deferred; inventing their signatures now would be a ruling rather than
    // a transcription. Pointer interiors (offsets, pointee types) are W4's
    // LayoutEngine answers, referenced by LivePointers, never duplicated.
    {
        Component c{"scopes", 1, {}};
        c.members.push_back(op("live_pointers_quote", "quote", {}));
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

const std::vector<ProviderSlot>& providerSlots() {
    // §3.9's slot table, first row only. `type_metadata` answers the per-type
    // pointer map for ADR 0003's collector: the subject is a `$struct`, the
    // answer the quote-form projection of its whole map.
    static const std::vector<ProviderSlot> slots = {
        {"type_metadata", "$struct", "quote"},
    };
    return slots;
}

const ProviderSlot* findProviderSlot(const std::string& slot) {
    for (const auto& s : providerSlots())
        if (s.slot == slot) return &s;
    return nullptr;
}

const std::vector<ProtocolSlot>& protocolSlots() {
    // ADR 0014's protocol slots, all four and no others. `move_or_copy` is the
    // move-vs-copy rewrite (ADR 0007's gap); `deallocate` what `delete` lowers
    // to; `lifetime` the `#[slaveof]` relation; `destructor` the overwrite
    // `deeptest2.fin:46` asks for ("basic destructors are generated by
    // compiler by default but you can overwrite it" -- overwrite, not add to).
    static const std::vector<ProtocolSlot> slots = {
        {"move_or_copy"},
        {"deallocate"},
        {"lifetime"},
        {"destructor"},
    };
    return slots;
}

const ProtocolSlot* findProtocolSlot(const std::string& slot) {
    for (const auto& s : protocolSlots())
        if (s.slot == slot) return &s;
    return nullptr;
}

} // namespace fin::compilerapi
