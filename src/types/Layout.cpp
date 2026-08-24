#include "Layout.hpp"

#include <algorithm>

#include "TypeImpl.hpp"

namespace fin {

namespace {

// "'<type>' has no layout: <reason>". One shape for every refusal, so that a
// diagnostic built from one always names the type the caller asked about -- the
// caller may be three levels of nesting away from the type that is actually
// undecided, and "no layout" on its own sends the reader to this file rather than
// to the decision that is missing.
std::string refuse(const Type& type, const std::string& reason) {
    return "'" + type.toString() + "' has no layout: " + reason;
}

}  // namespace

uint64_t alignUp(uint64_t value, uint64_t align) {
    if (align <= 1) return value;
    return (value + align - 1) / align * align;
}

// --- scalars ----------------------------------------------------------------

std::optional<ScalarInfo> scalarByName(const std::string& name) {
    // `void` and `noret` are here rather than absent because the backend asks this
    // table for them and gets a void type back; the layout pass then refuses,
    // which is a different question from "is this name a scalar".
    if (name == "void" || name == "noret") return ScalarInfo{ScalarKind::Void, 0, false};
    if (name == "bool") return ScalarInfo{ScalarKind::Bool, 1, false};

    // The widths. See the header: this is the compiler's one table, and the
    // aliases are here so that `lib/std`'s `i64`/`u64`/`size_t` resolve to a width
    // rather than to a second table somewhere else.
    if (name == "char" || name == "int8") return ScalarInfo{ScalarKind::Int, 8, true};
    if (name == "byte" || name == "uint8") return ScalarInfo{ScalarKind::Int, 8, false};
    if (name == "short" || name == "int16") return ScalarInfo{ScalarKind::Int, 16, true};
    if (name == "ushort" || name == "uint16") return ScalarInfo{ScalarKind::Int, 16, false};
    if (name == "int" || name == "int32") return ScalarInfo{ScalarKind::Int, 32, true};
    if (name == "uint" || name == "uint32") return ScalarInfo{ScalarKind::Int, 32, false};
    if (name == "long" || name == "int64") return ScalarInfo{ScalarKind::Int, 64, true};
    if (name == "ulong" || name == "uint64") return ScalarInfo{ScalarKind::Int, 64, false};
    if (name == "float") return ScalarInfo{ScalarKind::Float, 32, true};
    if (name == "double") return ScalarInfo{ScalarKind::Float, 64, true};
    // A Fin `string` is a pointer to NUL-terminated bytes, which is what makes the
    // corpus's `printf("%s", s)` work. It is a Pointer for size purposes and is
    // deliberately *not* a traced slot -- see layoutOf.
    if (name == "string") return ScalarInfo{ScalarKind::Pointer, 0, false};
    return std::nullopt;
}

uint64_t sizeOfScalar(const ScalarInfo& info, const TargetLayout& target) {
    switch (info.kind) {
        case ScalarKind::Void: return 0;
        case ScalarKind::Pointer: return target.pointerSize;
        // A bool is one bit of value and one byte of storage. Rounding up here is
        // the only place the two are reconciled, so no caller can use the bit
        // count as a byte count by accident.
        case ScalarKind::Bool:
        case ScalarKind::Int:
        case ScalarKind::Float: return (info.bits + 7) / 8;
    }
    return 0;
}

uint64_t alignOfScalar(const ScalarInfo& info, const TargetLayout& target) {
    if (info.kind == ScalarKind::Pointer) {
        return std::min<uint64_t>(target.pointerSize, target.maxScalarAlign);
    }
    const uint64_t size = sizeOfScalar(info, target);
    if (size == 0) return 1;
    return std::min<uint64_t>(size, std::max<uint64_t>(1, target.maxScalarAlign));
}

// --- TypeLayout -------------------------------------------------------------

const FieldLayout* TypeLayout::field(const std::string& name) const {
    for (auto it = fields.rbegin(); it != fields.rend(); ++it) {
        if (it->name == name) return &*it;
    }
    return nullptr;
}

// --- the engine -------------------------------------------------------------

void LayoutEngine::beginDeciding(const std::shared_ptr<StructType>& type) {
    if (!type) return;
    keepAlive_[type.get()] = type;
    deciding_[type.get()] = true;
    // Entering the decide phase discards any layout already computed. A type whose
    // layout is being decided has no layout, and a stale one left in the cache is
    // exactly the confidently-wrong answer the phases exist to prevent.
    cache_.erase(type.get());
}

LayoutResult LayoutEngine::finalise(const std::shared_ptr<StructType>& type) {
    if (!type) return {{}, "no type to finalise"};
    deciding_.erase(type.get());
    return layoutOf(type);
}

bool LayoutEngine::isLayoutFinal(const std::shared_ptr<StructType>& type) const {
    if (!type) return false;
    if (deciding_.count(type.get())) return false;
    auto found = cache_.find(type.get());
    return found != cache_.end() && found->second.ok();
}

std::string LayoutEngine::requestHeaderWords(const std::shared_ptr<StructType>& type,
                                             uint64_t words) {
    if (!type) return "no type to reserve a header on";
    if (!deciding_.count(type.get())) {
        return "request_header_words on '" + type->toString() +
               "' is legal only while its layout is being decided "
               "(struct_layout_deciding)";
    }
    requests_[type.get()].headerWords += words;
    return "";
}

std::string LayoutEngine::requestMinAlign(const std::shared_ptr<StructType>& type,
                                          uint64_t align) {
    if (!type) return "no type to align";
    if (!deciding_.count(type.get())) {
        return "request_min_align on '" + type->toString() +
               "' is legal only while its layout is being decided "
               "(struct_layout_deciding)";
    }
    if (align == 0 || (align & (align - 1)) != 0) {
        return "an alignment must be a power of two, not " + std::to_string(align);
    }
    auto& r = requests_[type.get()];
    r.minAlign = std::max(r.minAlign, align);
    return "";
}

void LayoutEngine::reset() {
    cache_.clear();
    requests_.clear();
    deciding_.clear();
    inProgress_.clear();
    keepAlive_.clear();
}

LayoutResult LayoutEngine::layoutOf(const TypePtr& type) {
    if (!type) return {{}, "no type"};

    if (auto st = std::dynamic_pointer_cast<StructType>(type)) return layoutOfStruct(st);

    // Everything else is structural and cheap, so it is computed rather than
    // memoised: `&int` is built fresh at every use, so a cache keyed by identity
    // would never hit and a cache keyed by spelling would have to prove that two
    // types spelled alike are alike.
    return compute(type);
}

LayoutResult LayoutEngine::layoutOfStruct(const std::shared_ptr<StructType>& type) {
    if (deciding_.count(type.get())) {
        return {{}, refuse(*type,
                           "its layout is still being decided (struct_layout_deciding), so "
                           "its size, alignment and offsets do not exist yet")};
    }
    auto cached = cache_.find(type.get());
    if (cached != cache_.end()) return cached->second;

    // Re-entry means the type reached itself by value. The field that closed the
    // cycle is named by the caller, which is the frame that knows it.
    if (std::find(inProgress_.begin(), inProgress_.end(), type.get()) != inProgress_.end()) {
        return {{}, refuse(*type, "it is already being laid out, so it contains itself by "
                                  "value and has no finite size")};
    }

    inProgress_.push_back(type.get());
    LayoutResult result = compute(type);
    inProgress_.pop_back();

    // Only successes are memoised. A refusal can depend on the frame it was
    // produced in -- a cycle refusal is true of A-while-laying-out-B and says so --
    // so caching one would hand a later caller a sentence about a computation it
    // was not part of.
    if (result.ok()) {
        keepAlive_[type.get()] = type;
        cache_[type.get()] = result;
    }
    return result;
}

LayoutResult LayoutEngine::compute(const TypePtr& type) {
    const Type& t = *type;

    if (auto* prim = t.as<PrimitiveType>()) {
        auto info = scalarByName(prim->name);
        if (!info) {
            // `auto`, and the four `$` meta-types. Not scalars of unknown size:
            // names with no run-time value at all.
            if (prim->name == "auto") {
                return {{}, refuse(t, "an inferred type that was never inferred has no "
                                      "representation")};
            }
            if (!prim->name.empty() && prim->name[0] == '$') {
                return {{}, refuse(t, "a compile-time meta-type is a value of the compiler, "
                                      "not of the program; whether it has a run-time "
                                      "representation at all is unruled")};
            }
            return {{}, refuse(t, "it is not a type this compiler knows the width of")};
        }
        if (info->kind == ScalarKind::Void) {
            return {{}, refuse(t, "void has no size")};
        }
        TypeLayout out;
        out.size = sizeOfScalar(*info, target_);
        out.align = alignOfScalar(*info, target_);
        // `string` is pointer-sized and is not a traced slot. It points at a
        // NUL-terminated blob in .rodata that no allocator owns, so a precise
        // collector following it would read an object header that is not there.
        // Owed ruling: when a string becomes a library value with a heap buffer
        // (ADR 0003), this becomes a slot.
        return {out, ""};
    }

    if (auto* ptr = t.as<PointerType>()) {
        TypeLayout out;
        ScalarInfo info{ScalarKind::Pointer, 0, false};
        out.size = sizeOfScalar(info, target_);
        out.align = alignOfScalar(info, target_);
        // The pointee is recorded, never laid out. That independence is what makes
        // `struct Node { next <&Node> }` finite, and it is also what lets a
        // pointer to a type with no decided layout still have one of its own.
        if (ptr->pointee) out.pointers.push_back({0, ptr->pointee});
        return {out, ""};
    }

    if (auto* nullable = t.as<NullableType>()) {
        if (!nullable->inner) return {{}, refuse(t, "it wraps nothing")};
        // `null` is the null pointer, so a nullable pointer needs no discriminant
        // and is the same eight bytes. Anything else needs somewhere to put
        // "absent" -- a flag byte, a reserved bit pattern, a separate word -- and
        // which of those Fin picks is not decided.
        auto inner = layoutOf(nullable->inner);
        if (!inner.ok()) return inner;
        const bool pointerShaped = nullable->inner->as<PointerType>() != nullptr;
        if (!pointerShaped) {
            return {{}, refuse(t, "where a nullable value keeps its 'absent' bit is unruled; "
                                  "only a nullable pointer has an answer today, because "
                                  "`null` is the null pointer")};
        }
        return inner;
    }

    if (auto* arr = t.as<ArrayType>()) {
        // A fixed array is N of its element, laid end to end. A dynamic one is not
        // laid out at all: how a `[T]` is *represented* -- a pointer and a length
        // side by side, a header word ahead of the elements, something else -- is
        // undecided, and the extent does not decide it.
        //
        // This used to refuse both, and the reason was the extent rather than the
        // arithmetic: `[int, 4]` and `[int, 8]` were one semantic type, so any size
        // reported here would have been a guess, and a guessed size is a struct that
        // silently has the wrong shape. The retired
        // KnownDefect_Layout.AFixedArrayHasNoExtentToLayOut is what held that open.
        if (!arr->extent) {
            return {{}, refuse(t, "a dynamic array's representation -- a pointer and a "
                                  "length side by side, a header ahead of the elements, "
                                  "something else -- is undecided")};
        }
        if (!arr->element_type) return {{}, refuse(t, "it has no element type")};
        auto element = layoutOf(arr->element_type);
        if (!element.ok()) {
            return {{}, refuse(t, "its element '" + arr->element_type->toString() +
                                      "' has none -- " + element.refusal)};
        }

        TypeLayout out;
        // The element's alignment, not the whole array's size. Sixteen bytes of ints
        // is 4-aligned, and demanding 16 would over-align every array field in every
        // struct that has one.
        out.align = std::max<uint64_t>(1, element.layout.align);
        // The stride is the element's size rounded up to its own alignment, which is
        // what makes the *second* element land aligned as well as the first. Those
        // two numbers are always equal for every layout this engine produces today
        // -- a struct's size is already rounded up to its own alignment and a
        // scalar's size is a multiple of its own -- so the round-up is here for the
        // first type where that stops being true, rather than to fix a case that
        // exists. Packing instead would misalign every element after the first.
        const uint64_t stride = alignUp(element.layout.size, out.align);
        const uint64_t count = *arr->extent;
        if (stride != 0 && count > UINT64_MAX / stride) {
            return {{}, refuse(t, "its size does not fit in 64 bits")};
        }
        out.size = stride * count;

        // The element's pointer map, repeated once per element. This is the reason
        // this file exists (ADR 0003): a collector handed a struct with a
        // `[&Node, 3]` field must be told about three pointers, not one and not
        // none, and an array is the first type whose map is a repeat rather than a
        // walk of named fields.
        //
        // Emitted flat, like every other map here, which for a large array of
        // pointers is a large vector. That is a size problem rather than a wrong
        // number, and the eventual answer is a repeat encoding -- an offset, a
        // stride and a count -- in TypeLayout::pointers itself, so that a
        // nested array does not multiply out either. Owed, not guessed at here: the
        // shape a collector wants to walk is the collector's ruling.
        if (!element.layout.pointers.empty()) {
            for (uint64_t i = 0; i < count; ++i) {
                const uint64_t base = i * stride;
                for (const auto& slot : element.layout.pointers) {
                    out.pointers.push_back({base + slot.offset, slot.pointee});
                }
            }
        }
        // No `fields`. An element has an index, and an index is not a name that
        // TypeLayout::field() could answer.
        return {out, ""};
    }

    if (t.as<GenericType>()) {
        return {{}, refuse(t, "a generic parameter has no layout until monomorphisation "
                              "substitutes it")};
    }

    if (auto* self = t.as<SelfType>()) {
        if (self->originalStruct) return layoutOf(self->originalStruct);
        return {{}, refuse(t, "`Self` is not bound to the type it stands for")};
    }

    if (t.as<DynamicType>()) {
        // docs/plan.md fixes `any` as `{i8*, i64}` -- payload plus typeid -- and
        // says in the same paragraph to emit that layout from a declaration in
        // lib/std rather than hardcoding it. Hardcoding 16 here would be the
        // hardcoding the plan refuses, one file earlier.
        return {{}, refuse(t, "its representation is to come from a declaration in lib/std "
                              "rather than from the compiler (ADR 0003), and that "
                              "declaration does not exist yet")};
    }

    if (t.as<FunctionType>()) {
        return {{}, refuse(t, "whether a function value is a bare code pointer or a closure "
                              "pair is undecided, and no lambda is lowered yet")};
    }

    if (t.as<PrototypeType>()) {
        return {{}, refuse(t, "a prototype's keys are decided at run time, so it has no "
                              "static field list to lay out")};
    }

    if (t.as<ErrorType>()) {
        return {{}, refuse(t, "it is the sentinel for a type that failed to resolve, so "
                              "asking for its size would report a second error for the "
                              "first one")};
    }

    if (t.as<NullType>()) {
        return {{}, refuse(t, "`null`'s own type is not a type a value is stored as")};
    }

    if (t.as<NamespaceType>()) {
        return {{}, refuse(t, "a namespace is a scope, not a value")};
    }

    auto st = std::dynamic_pointer_cast<StructType>(type);
    if (!st) {
        return {{}, refuse(t, "this compiler has no layout rule for it")};
    }

    if (st->is_interface) {
        // The D lesson in one line: an interface-typed value's representation --
        // a fat pointer, a vtable slot, something else -- is undecided, and
        // §1.3's all-zero bitmap is what happens when a compiler answers anyway.
        return {{}, refuse(t, "an interface-typed value's representation (fat pointer, "
                              "vtable pointer, something else) is undecided; a struct that "
                              "implements it has a layout, the interface does not")};
    }
    if (st->is_enum) {
        return {{}, refuse(t, "an enum's tag width and the union of its payloads are "
                              "undecided")};
    }

    // A generic struct that was never instantiated. Caught by its fields
    // refusing, but named here so the refusal says which of the two it is: a
    // layout for `Pair<T, U>` would be a *different* layout from
    // `Pair<int, long>`, and two layouts for one name is the ABI split ADR 0002's
    // erasure rule exists to prevent.
    for (const auto& arg : st->generic_args) {
        if (arg && arg->as<GenericType>()) {
            return {{}, refuse(t, "its generic arguments are still parameters, so this is "
                                  "the uninstantiated template rather than a type a value "
                                  "has")};
        }
    }

    const Requests requests = [&] {
        auto found = requests_.find(st.get());
        return found == requests_.end() ? Requests{} : found->second;
    }();

    TypeLayout out;
    out.align = std::max<uint64_t>(1, requests.minAlign);
    uint64_t offset = 0;

    // A base struct's fields come first, whole, at offset zero. That is single
    // inheritance's ABI trick -- a pointer to the derived type already is a
    // pointer to the base, so an upcast emits no instruction -- and any other
    // order would make it emit an addition.
    const StructType* base = nullptr;
    for (const auto& parent : st->parents) {
        if (!parent) continue;
        auto parentStruct = std::dynamic_pointer_cast<StructType>(parent);
        // An interface parent contributes no bytes: `struct S : <I>` puts it in
        // the same `parents` vector as a base struct, and a slot for it would move
        // every field after it for something that has no run-time existence.
        if (parentStruct && parentStruct->is_interface) continue;
        if (!parentStruct) {
            return {{}, refuse(t, "its parent '" + parent->toString() +
                                      "' is not a struct, so there is nothing to inherit a "
                                      "layout from")};
        }
        if (base) {
            return {{}, refuse(t, "it has more than one base struct, and where a second "
                                  "base's fields go -- and whether an upcast to it stays "
                                  "free -- is undecided")};
        }
        base = parentStruct.get();
        auto baseLayout = layoutOf(parent);
        if (!baseLayout.ok()) {
            return {{}, refuse(t, "its base '" + parent->toString() + "' has none -- " +
                                      baseLayout.refusal)};
        }
        for (const auto& f : baseLayout.layout.fields) {
            FieldLayout inheritedField = f;
            inheritedField.inherited = true;
            out.fields.push_back(inheritedField);
        }
        for (const auto& p : baseLayout.layout.pointers) out.pointers.push_back(p);
        offset = baseLayout.layout.size;
        out.align = std::max(out.align, baseLayout.layout.align);
    }

    for (const auto& field : st->fields) {
        if (!field.type) {
            return {{}, refuse(t, "field '" + field.name + "' has no type")};
        }
        auto fieldLayout = layoutOf(field.type);
        if (!fieldLayout.ok()) {
            return {{}, refuse(t, "field '" + field.name + "' has none -- " +
                                      fieldLayout.refusal)};
        }
        const uint64_t fieldAlign = std::max<uint64_t>(1, fieldLayout.layout.align);
        offset = alignUp(offset, fieldAlign);
        out.fields.push_back({field.name, field.type, offset, fieldLayout.layout.size,
                              fieldAlign, false});
        // The pointer map is uniform: whatever the field's own layout traces,
        // shifted by where the field sits. That one line covers a pointer (one
        // slot at zero), a nested struct (its whole map), a nullable pointer, and
        // a scalar (nothing) -- so nesting is resolved here, at compile time,
        // exactly once per type, and a collector gets a flat list to walk.
        for (const auto& p : fieldLayout.layout.pointers) {
            out.pointers.push_back({offset + p.offset, p.pointee});
        }
        offset += fieldLayout.layout.size;
        out.align = std::max(out.align, fieldAlign);
    }

    out.size = alignUp(offset, out.align);
    // Rounded up to the type's own alignment so that `block + headerBytes` is
    // correctly aligned for the object -- three pointer words ahead of a
    // 16-aligned type is 24 bytes, and 24 is not a multiple of 16.
    out.headerBytes = alignUp(requests.headerWords * target_.pointerSize, out.align);
    return {out, ""};
}

}  // namespace fin
