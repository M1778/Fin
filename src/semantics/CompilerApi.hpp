#pragma once
#include <string>
#include <vector>

namespace fin {

// The compiler API's component inventory, as data.
//
// docs/compiler-api.md §2.4 is the specification and this is a transcription of
// it: the four Tier-1 components the corpus names, with the thirteen paths the
// standard library writes, unchanged. It is a table rather than a set of cases in
// the analyzer because the next component is a table row -- Tier 2 and Tier 3 are
// already argued for in §2.5 and §2.6 and nothing about adding them should touch
// the walk.
//
// Two rules from ADR 0012 shape the shape:
//
//   * `compiler.components.<name>` is a component **reference** -- capability
//     negotiation. Its members are the four in `referenceOps()` and no others,
//     and it answers them for a component this compiler does not have
//     (§2.1a: `present()` must evaluate to `false`, not fail to resolve).
//
//   * `compiler.<name>.<member>` is the *use* of a component: its operations and
//     its constants. Reaching it needs the matching
//     `#[use(compiler.components.<name>)]` grant.
//
// Only members whose signature §2.4 states outright appear here. The doc lists
// several more by name with the signature left open -- `compiler.structs.method_*`,
// `compiler.enums.member_*`, `compiler.system.endianness`/`max_align` -- and those
// are absent rather than guessed at: an invented signature is a ruling, and this
// file is a transcription. The `layout` component (§2.5, steps 8+16) is transcribed
// the same way, including `pointer_map_quote`: `quote` is registered as an opaque
// analyzer type (Analyzer_Core.cpp) so the signature is nameable before wave-4
// steps 11-13 build quote values. A `quote` names no value the compiler can hold
// yet; resolving the signature is not evaluating it.
namespace compilerapi {

// A type is named by its spelling and resolved through the analyzer's scope, so
// `$type` and `uint` here are the same types the program can write. `"R"` is the
// turbofish argument of a generic member -- `select_field::<R>(...) <?R>`.
struct Member {
    std::string name;
    std::string result;
    std::vector<std::string> params;
    // A constant is read, never called: `compiler.enums.InBytes`
    // (stdlib/memory.fin:32,33,41). `params` is empty and `result` is its type.
    bool is_constant = false;
    // Turbofish arity. 1 for `gettype::<T>()` and `select_field::<R>(...)`, which
    // are the only two the corpus writes; both bind the single name `R`.
    int generics = 0;
    bool result_nullable = false;
};

struct Component {
    std::string name;
    int version;
    std::vector<Member> members;
};

const std::vector<Component>& components();
const Component* findComponent(const std::string& name);
const Member* findMember(const Component& c, const std::string& member);

// What every component reference answers, present or absent (§2.1, ADR 0012).
const std::vector<Member>& referenceOps();

// The provider slots (§3.9, ADR 0014): a `@special` the compiler calls once per
// subject, whose returned value the compiler stores and emits, declared
// `#[provides(<slot>)]`. Exclusive per slot, memoised per subject, so a
// provider is a pure function of its subject.
//
// One row per slot: the slot's name, the subject's spelling, the answer's
// spelling. Wave 4 ships `type_metadata` only; further slots (`type_name`,
// `allocator`) are argued in §3.9 and absent rather than guessed at, exactly
// as for component members above.
struct ProviderSlot {
    std::string slot;
    std::string subject;
    std::string result;
};

const std::vector<ProviderSlot>& providerSlots();
const ProviderSlot* findProviderSlot(const std::string& slot);

// The protocol slots (ADR 0014, wave-5 slice 0): a `@special` the library
// writes to *replace* one compiler operation, declared `#[protocol(<slot>)]`.
// Exactly these four; anything else is "unknown protocol slot". Exclusive per
// slot like providers (two claimants name both), armed by default (no opt-in:
// collecting the attribute IS the claim).
//
// Slice 0 pins only the slot set and the claim contract's shape (one subject
// in, one `quote` out -- the replacement node per §3.7's table). Per-slot
// subject types are later slices: the four operations take different subjects
// and a uniform meta-type pin now would be a wrong contract to unwind.
struct ProtocolSlot {
    std::string slot;
};

const std::vector<ProtocolSlot>& protocolSlots();
const ProtocolSlot* findProtocolSlot(const std::string& slot);

} // namespace compilerapi
} // namespace fin
