#include "CodeGen.hpp"

#include "../ast/ASTNode.hpp"   // the master AST include
#include "../ast/Visitor.hpp"
#include "../diagnostics/DiagnosticEngine.hpp"
#include "../types/Layout.hpp"
#include "../utils/IntegerConstant.hpp"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>

#include <fmt/color.h>
#include <fmt/core.h>

#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

// The LLVM 18 backend (ADR 0002, ADR 0010).
//
// This is the first slice, and what it covers was chosen by one question: what is
// the smallest set of constructs that turns `finc hello.fin -o hello && ./hello`
// -- the exit criterion docs/plan.md sets for wave 5 -- from a plan into a fact?
// The answer is external declarations, top-level functions, the scalar types, one
// kind of local, and the four control-flow shapes. Everything else refused. Units
// since have added to that set one construct at a time -- structs are the first --
// and the list below is what has not been added yet, kept honest by the rule that
// anything absent from it must refuse.
//
// Refusing is the load-bearing half. `runCodeGen` used to `return true` without
// emitting anything, which meant `finc x.fin -o x` printed "Build Successful." and
// produced no file; the same shape of mistake inside the emitter -- a statement
// silently skipped because its node type was not handled -- produces something
// worse, a binary that runs and computes the wrong answer. So the emitter
// implements the exhaustive `Visitor` rather than StructuralWalk: a node type
// nobody has written a case for does not compile *this* compiler, and the ones
// that are written but unlowerable all route through `unsupported()`. There is no
// path from an unhandled node to a successful build.
//
// WHAT IS DELIBERATELY NOT HERE, and is a unit of its own rather than an omission:
//
//   * The rest of the aggregates. Enums, arrays and indexing, prototypes, and
//     interfaces. Structs *are* here as of this unit -- what unblocked them was
//     giving StructType an ordered field list, because a struct with no field
//     order has no offsets, and a field read at the wrong offset is still a
//     well-typed int. Layout is settled in two named moments (ADR 0015) and the
//     second of those, the byte offsets, is src/types/Layout.hpp's; this file
//     hands the field list to LLVM in declaration order and lets LLVM place it,
//     and Soundness_Codegen.AStructsLayoutMatchesWhatLLVMWouldChoose is the test
//     that the two agree. Two consequences worth naming, because both are
//     refusals rather than gaps in the lowering:
//       - A struct crosses a Fin-to-Fin boundary as an LLVM aggregate, by value.
//         It does not cross an `@define` boundary at all. The platform ABI decides
//         per struct whether it arrives in registers, split across two, or behind
//         a hidden pointer, and clang implements that classification itself rather
//         than leaving it to LLVM -- so emitting the aggregate on an extern would
//         link cleanly and pass garbage. The classifier is its own unit.
//       - Methods, operators and constructors on a struct are not emitted, and
//         every way of reaching one refuses at the call site. Nothing implicit
//         calls them: a `P { a: 1 }` literal does not run a constructor. A
//         destructor would run implicitly, which is why a struct that has one is
//         refused whole rather than lowered as plain data with no call emitted.
//   * Generics. Monomorphisation, and the erasure rule ADR 0002 carries forward
//     from pyprototype (an erasure-marker constraint on any one parameter selects
//     erasure; an erased generic is a raw pointer).
//   * `blame`, `try`/`catch`, and raising. The runtime shape of a raised value is
//     not settled and there is no runtime to put it in.
//   * Lambdas and function values. `functions.fin` passes a function by name into
//     a `fn(int, int) => int` parameter, which needs a decision about whether a
//     Fin function value is a bare pointer or a closure pair.
//   * The compiler API and `@special`. Wave 4 executes those at compile time; a
//     `@special` reaching codegen means the interpreter did not consume it.
//
// TYPE WIDTHS are a choice this file makes and does not own, and as of wave 4
// step 6 it does not even hold. `int` is lowered as i32 and `long` as i64,
// matching what a C `printf("%d")` reads, because the corpus declares printf
// variadically (`functions.fin:3`) and every observable in the sample set goes
// through it -- but the table itself now lives in src/types/Layout.hpp, because
// the layout pass has to answer `size_of` in a build with no backend and cannot
// read a table that is inside one. The owner ruling on integer widths and on
// conversions between integer types is still open -- see KnownDefect_Integer-
// Constants and stdlib/stdio.fin's eleven mismatches -- and if it lands
// differently, `scalarByName` is the one place that changes.
//
// NAMES ARE NOT MANGLED. A Fin `add` is an LLVM `add`. That has to be true for
// `@define printf` -- an external C symbol has exactly one spelling -- and nothing
// in the corpus yet needs two Fin functions of one name, so the second half of the
// rule costs nothing today. Overloads, methods and namespaces all need a scheme,
// and the scheme needs `finn`'s ABI story, so it is not invented here.

namespace fin {

namespace {

// What the backend needs to know about a Fin type, which is less than the
// analyzer's Type: the LLVM representation, plus the two bits that change which
// *instruction* is selected rather than which type is stored.
//
// Signedness is here because `/`, `%` and `<` are three different instructions
// depending on it, and a backend that guessed would compile `a / b` on unsigned
// operands into a signed divide -- correct for every value the tests happen to
// use and wrong at the top of the range. `isFloat` is here for the same reason.
struct StructInfo;

struct CgType {
    enum class Kind { Void, Int, Float, Ptr, Struct, Array };
    llvm::Type* llvmType = nullptr;
    Kind kind = Kind::Void;
    bool isSigned = true;
    // Width in bits, for Int. Kept because promoting a vararg argument needs the
    // width and llvm::Type::getIntegerBitWidth() asserts on non-integers.
    unsigned bits = 0;
    // `bool` is an i1 in registers and must be zero-extended before it reaches a C
    // variadic, where the callee reads a full int.
    bool isBool = false;

    // Set for Kind::Struct and null otherwise: the field names, their order, and
    // their types. A struct's identity in this file is this pointer, not its LLVM
    // type -- two Fin structs with the same field types are the same
    // llvm::StructType only by accident of LLVM's uniquing of *literal* structs,
    // and these are named, so they are distinct. It points into Emitter::structs_,
    // which is never rehashed after declareStructs finishes.
    const StructInfo* structInfo = nullptr;

    // Set for Kind::Array and null otherwise: what one element is. Held by value
    // through a shared_ptr rather than inline, because a CgType cannot contain
    // itself and `[[int, 2], 2]` needs it to contain one.
    //
    // The extent is a separate field and not read off the llvm::ArrayType, because
    // `.length` is answered from it and an ArrayType's element count is the same
    // number only as long as nothing here starts lowering an array as anything but
    // an [N x T]. A dynamic `[T]` never becomes a CgType at all -- its
    // representation is undecided (ADR 0003's neighbourhood) and it refuses at the
    // mapper.
    std::shared_ptr<CgType> element;
    uint64_t extent = 0;

    // Set for Kind::Ptr when this file knows what is at the other end, and null
    // when it does not.
    //
    // LLVM has had one `ptr` since 15, so the pointee is not recoverable from the
    // IR -- it is a fact this table carries or a fact nobody has. Everything a
    // pointer can do needs it: the load `*p` emits, the GEP `p.field` emits, and
    // the stride `p[i]` would emit are all the pointee's, and a pointer that had
    // lost it would compile a one-byte `&char` read as a four-byte one.
    //
    // Null for the two pointers with nothing to say: a `string`, whose bytes are a
    // library question (ADR 0003), and a bare `null`, which has no type until it
    // reaches somewhere that has one. Both are still pointers -- they can be
    // stored, passed and compared -- and neither may be dereferenced, so the null
    // is what refuses rather than a rule invented here.
    //
    // A shared_ptr for the same reason `element` is one: `&&int` needs a CgType to
    // contain one.
    std::shared_ptr<CgType> pointee;

    bool isVoid() const { return kind == Kind::Void; }
    bool isStruct() const { return kind == Kind::Struct; }
    bool isArray() const { return kind == Kind::Array; }
    bool isPointer() const { return kind == Kind::Ptr; }
    // What may not cross an `@define` boundary or a C variadic: the platform ABI
    // decides how each is passed and clang implements that classification, so
    // emitting the LLVM aggregate would link cleanly and pass garbage.
    bool isAggregate() const { return isStruct() || isArray(); }
};

// One field, in declaration order. The order is the whole point: the layout pass
// and LLVM both index by position, and the *written* order of a struct literal's
// initialisers is the author's convenience and nothing else
// (Soundness_Codegen.AStructLiteralFollowsDeclarationOrderNotWrittenOrder).
struct StructField {
    std::string name;
    CgType type;
    // The declaration's `= expr`, or null. Borrowed from the AST, which outlives
    // the emitter -- kept as an expression rather than folded to a constant here
    // because it may be a call, and a call has to happen where the literal is.
    Expression* defaultValue = nullptr;
};

// What one type parameter became, for as long as a template's body is being
// mapped: the representation, and the name to print.
//
// The display name is carried beside the representation and not derived from it,
// because the representation is not unique -- a fieldless enum and an `int` are one
// CgType here (enumByName returns byName("int")), and `Box<Colour>` and `Box<int>`
// would otherwise be one instantiation under one name. They are still one *layout*,
// which is why sharing would have been sound; they are two names because a
// diagnostic that says `Box<int>` about a program that wrote `Box<Colour>` is a
// diagnostic about a different program.
struct TypeBinding {
    CgType type;
    std::string display;
};

// A template's parameters bound to one instantiation's arguments. A vector because
// there are one or two of them and the order is the written order -- `Pair<A, B>`
// binds by position, and a map keyed by name would still need the position to fill
// it, so this holds both without a second structure.
using Substitution = std::vector<std::pair<std::string, TypeBinding>>;

struct StructInfo {
    std::string finName;
    llvm::StructType* llvmType = nullptr;
    std::vector<StructField> fields;
    std::unordered_map<std::string, size_t> indexByName;
    // False between the two passes of declareStructs: the name exists and the body
    // does not. A field of an incomplete struct type has no size, so it refuses --
    // which is also what stops a mutually recursive pair from reaching LLVM, where
    // it would be an infinite size computation rather than an error.
    bool complete = false;

    // Non-empty for an instantiation of a generic template, and what it was
    // instantiated at. Kept because a field's *default* may mention the parameter --
    // `x <T> = cast<T>(0)` -- and the default runs at each literal that omits the
    // field, which is somewhere else entirely and has no other way to know what T
    // was there (buildStructValue pushes it back).
    Substitution substitution;

    // The declaration this was built from, borrowed from the AST -- the template's
    // for an instantiation. Kept for the methods: a call site that does not find
    // `Point<int>.set_x` has to say *why*, and "the generic method 'set_x'" is only
    // readable off the declaration. Also what the third pass of declareStructs and
    // instantiateGeneric walk to declare the methods in the first place.
    const StructDeclaration* decl = nullptr;

    // What the mapper is handed while one of this struct's method bodies is emitted:
    // this struct's own substitution, plus `Self`, plus the template's bare name.
    //
    // Three spellings of one type, because struct_methods.fin writes all three --
    // `self: &Self` at :10, `<&Self>` at :18, `<&Point>` at :21 with the comment
    // "using &Point instead of &Self is correct too" -- and a binding is the only way
    // they can be one type rather than one type and two refusals. Bindings and not a
    // name lookup, because for an instantiation the answer is `Box<int>` and the
    // written name is `Box`, which resolves to nothing at all (a template is not a
    // type).
    //
    // Built once, when the struct is complete, and pointed at from there on: the
    // mapper holds a pointer to it, and a method body may instantiate further
    // templates, so it cannot be a local.
    Substitution methodBindings;

    // The index is looked up per struct, never in one table shared across struct
    // types: `v` is field 1 of `A` and field 0 of `B`, and one table gives the same
    // answer for both (Soundness_Codegen.TwoStructsWithTheSameFieldNameUseTheir-
    // OwnOffsets).
    bool find(const std::string& name, size_t& indexOut) const {
        auto it = indexByName.find(name);
        if (it == indexByName.end()) return false;
        indexOut = it->second;
        return true;
    }
};

// A fieldless enum: its name, and what number each member is.
//
// The numbers are computed once, here, rather than at each use. A member with no
// written value is the one before it plus one, starting at 0, which is C's rule and
// the only rule the corpus is consistent with -- `State { Alive = 1, Dead }`
// (operators.fin:6) makes Dead 2, so the value is not the member's position.
//
// `members` keeps declaration order because the numbering depends on it;
// `valueByName` is what a read consults.
struct EnumInfo {
    std::string finName;
    std::vector<std::pair<std::string, int64_t>> members;
    std::unordered_map<std::string, int64_t> valueByName;
};

struct CgVal {
    llvm::Value* value = nullptr;
    CgType type;
    bool ok() const { return value != nullptr; }
};

struct Local {
    llvm::AllocaInst* slot = nullptr;
    CgType type;
};

// A module-scope variable. The same pair as a Local, with the home in the object
// file instead of a frame -- which is why everything downstream of an address
// treats the two alike (see emitAddress).
struct GlobalVar {
    llvm::GlobalVariable* var = nullptr;
    CgType type;
};

struct FnInfo {
    llvm::Function* fn = nullptr;
    CgType returnType;
    std::vector<CgType> paramTypes;
    bool isVarArg = false;
    // Whether this is the program's entry point, which is a property of the Fin
    // name and not of the emitted symbol: `return` inside it means something
    // different (a process status) and its LLVM signature is C's rather than
    // what Fin wrote.
    bool isMain = false;
    // Parameter 0 is a receiver the source never wrote. Read by emitCallArgs, to know
    // which parameter the first written argument lands on, and by emitBody, to know
    // which argument is `self`.
    bool hasReceiver = false;
};

// The one place that maps a written type name to a representation. Returns
// nullopt for a name this slice does not lower, which the caller turns into a
// refusal naming the line -- never into a default.
class TypeMapper {
public:
    explicit TypeMapper(llvm::LLVMContext& ctx) : ctx_(ctx) {}

    // The struct table is bound after construction because it lives in the Emitter
    // and the Emitter constructs this. Until it is bound, and for a name that is
    // not in it, a struct name maps to nothing and the caller refuses.
    void bindStructs(const std::unordered_map<std::string, StructInfo>* structs) {
        structs_ = structs;
    }

    void bindEnums(const std::unordered_map<std::string, EnumInfo>* enums) {
        enums_ = enums;
    }

    // How a `Box<int>` becomes a struct that exists.
    //
    // The mapper is the one place that turns a written type into a representation,
    // so it is where the demand for an instantiation appears -- and it cannot answer
    // it, because building one means mapping fields, reporting refusals and writing
    // to the struct table, all of which are the Emitter's. So the Emitter installs a
    // callback: the mapper asks for the mangled name and gets it back registered, or
    // gets nothing and refuses as it would for any name it does not know.
    //
    // A std::function rather than a back-pointer because this header order has the
    // mapper defined before the Emitter, and the alternative is a forward
    // declaration and an out-of-line definition for one call.
    void bindInstantiator(std::function<bool(const TypeNode&, std::string&)> fn) {
        instantiate_ = std::move(fn);
    }

    // The parameters currently in scope, or empty. Set for exactly as long as one
    // template's body is being mapped, and restored after -- see
    // Emitter::instantiateGeneric, which is the only caller, and the ScopedBinding
    // it uses to guarantee the restore on the refusal paths too.
    //
    // Not a stack of substitutions, because it does not nest: mapping `Box<Box<int>>`
    // maps the inner argument *before* the outer body is entered (the arguments are
    // mapped to name the instantiation), so at any moment exactly one body is being
    // mapped. `Node<T> { next <&Node<T>> }` is the case that looks like nesting and
    // is not: the inner `Node<T>` resolves T from the same binding and finds its own
    // name already registered.
    void setBindings(const Substitution* bindings) { bindings_ = bindings; }
    const Substitution* bindings() const { return bindings_; }

    // The binding for a written type, when the node is an undecorated type-parameter
    // name and a substitution is active -- and nullptr otherwise.
    //
    // Public because a *name* needs the same answer a representation does. Inside a
    // template's body a written `T` reads back as "T", and an instantiation keyed on
    // that is `Box<T>`: a struct keyed on the parameter's name rather than on what it
    // was bound to, with a layout identical to the `Box<int>` the caller has and a
    // different name, so the two are not assignable. See Emitter::spell.
    const TypeBinding* boundBinding(const TypeNode* node) const {
        if (!node || !bindings_) return nullptr;
        // Only when the node carries nothing else. `T` decorated -- `&T`, `[T, 3]` --
        // arrives as a Pointer- or ArrayTypeNode and reaches this same lookup through
        // its pointee or element, so the decoration is applied to what T became rather
        // than lost.
        if (!node->generics.empty() || node->pointer_depth != 0 || node->is_array ||
            node->is_nullable || node->is_prototype || !node->implements_list.empty() ||
            node->array_size || dynamic_cast<const FunctionTypeNode*>(node) ||
            dynamic_cast<const PointerTypeNode*>(node) ||
            dynamic_cast<const ArrayTypeNode*>(node)) {
            return nullptr;
        }
        for (const auto& binding : *bindings_) {
            if (binding.first == node->name) return &binding.second;
        }
        return nullptr;
    }

    // `allowIncomplete` admits a struct whose body is not set yet, and is passed
    // by exactly one caller: a pointer, for its immediate pointee. See mapPointer.
    std::optional<CgType> map(const TypeNode* node, bool allowIncomplete = false) const {
        if (!node) return voidType();

        // A bare type parameter -- the `T` in `val <T>` -- becomes whatever this
        // instantiation bound it to. First, because a template may name its parameter
        // anything, including a name that is also a struct's: substituting has to beat
        // resolving, or `struct Wrapper<Box> { v <Box> }` would silently use the
        // struct called Box for a program that meant the parameter.
        //
        // Only when the node carries nothing else. `T` decorated -- `&T`, `[T, 3]` --
        // arrives here as a Pointer- or ArrayTypeNode and reaches this same lookup
        // through its pointee or element, so the decoration is applied to what T
        // became rather than lost.
        if (const TypeBinding* bound = boundBinding(node)) return bound->type;

        // A nullable, a function type, a prototype or a generic argument list all
        // mean "not this slice" rather than "the base name" -- silently dropping the
        // decoration is how `[int]` would become `int` and start being copied by
        // value.
        // An array is one of the two decorations this slice lowers, and only when
        // its extent is written and constant. See mapArray.
        if (auto* arr = dynamic_cast<const ArrayTypeNode*>(node)) {
            if (node->pointer_depth != 0 || node->is_nullable) return std::nullopt;
            return mapArray(*arr);
        }
        // A pointer is the other. `pointer_depth` is checked and never set: the
        // parser builds a PointerTypeNode for every spelling of a pointer type
        // (`&int`, `*int`, `&&int`) and leaves the counter at 0, so a non-zero one
        // would be a second encoding of the same fact and this file would be
        // reading the wrong one.
        if (auto* ptr = dynamic_cast<const PointerTypeNode*>(node)) {
            if (node->pointer_depth != 0 || node->is_array || node->is_nullable ||
                node->array_size) {
                return std::nullopt;
            }
            return mapPointer(*ptr);
        }
        if (node->pointer_depth != 0 || node->is_array || node->is_nullable ||
            node->is_prototype || !node->implements_list.empty() || node->array_size ||
            dynamic_cast<const FunctionTypeNode*>(node)) {
            return std::nullopt;
        }
        // `Box<int>` -- a generic argument list, which is the third decoration this
        // slice lowers. It is not the base name with the arguments dropped: `Box<int>`
        // and `Box<char>` are different types and `Box` alone is not a type at all,
        // so the arguments are what is being asked about and the instantiation is
        // named by all of them together.
        if (!node->generics.empty()) {
            if (!instantiate_) return std::nullopt;
            std::string mangled;
            if (!instantiate_(*node, mangled)) return std::nullopt;
            return structByName(mangled, allowIncomplete);
        }
        if (auto scalar = byName(node->name)) return scalar;
        if (auto e = enumByName(node->name)) return e;
        return structByName(node->name, allowIncomplete);
    }

    // `&T` becomes one machine word, whatever T is, plus the pointee recorded
    // beside it (CgType::pointee).
    //
    // The pointee is mapped with `allowIncomplete`, and that is the whole reason the
    // flag exists: `struct Node { pub next <&Node> = null }` (deeptest3.fin:78) maps
    // its own field during declareStructs' second pass, when `Node`'s llvm::StructType
    // exists and its body does not. Pointing at a type whose size nobody knows yet is
    // exactly what a pointer is for -- it is one word either way -- and the body is
    // set before any function body is emitted, so every load through it happens after.
    //
    // A struct *field* of incomplete type still refuses, and so does an array of one,
    // because those need the size. The flag stops here and is not passed on to
    // anything but another pointer.
    std::optional<CgType> mapPointer(const PointerTypeNode& node) const {
        if (!node.pointee) return std::nullopt;
        auto pointee = map(node.pointee.get(), /*allowIncomplete=*/true);
        if (!pointee) return std::nullopt;
        CgType t = pointerType();
        t.pointee = std::make_shared<CgType>(*pointee);
        return t;
    }

    // An address with nothing recorded about what is at it.
    CgType pointerType() const {
        CgType t;
        t.kind = CgType::Kind::Ptr;
        t.llvmType = llvm::PointerType::getUnqual(ctx_);
        return t;
    }

    CgType pointerTo(const CgType& pointee) const {
        CgType t = pointerType();
        t.pointee = std::make_shared<CgType>(pointee);
        return t;
    }

    // `[T, N]` becomes an LLVM [N x T]: N of the element, laid end to end, with LLVM
    // placing them. The stride is the element's size rounded up to its alignment and
    // LLVM computes it, which is the same division of labour a struct gets here --
    // this file hands over the shape and does not do the arithmetic.
    // Soundness_Codegen.TheLayoutTableAgreesWithLLVM is what keeps that agreeing with
    // what src/types/Layout.cpp computes for a collector.
    //
    // A *dynamic* `[T]` returns nullopt, and it is not a fixed array with a number
    // missing. How a `[T]` is represented -- a pointer and a length side by side, a
    // header word ahead of the elements, something else -- is undecided, and it decides
    // what `array.length` compiles to inside a callee that was handed one
    // (arrays.fin's `sort(array: &[T])`). The caller turns the nullopt into a refusal
    // naming the line, which is the same answer Layout.cpp gives at the same fork.
    //
    // A non-constant extent returns nullopt too. The analyzer has already refused it
    // -- `new [T, n]` is Fin's spelling for a run-time count -- so this is a
    // disagreement between the two passes rather than a program error, and reading it
    // as anything (least of all as 1) would be a stack slot the wrong size.
    std::optional<CgType> mapArray(const ArrayTypeNode& node) const {
        if (!node.element_type || !node.size) return std::nullopt;

        uint64_t extent = 0;
        if (readConstant(*node.size, extent) != ConstantRead::Ok) return std::nullopt;

        auto element = map(node.element_type.get());
        if (!element) return std::nullopt;
        // An array of void or of an incomplete struct has no size, so it is not an
        // array of anything. LLVM would assert rather than refuse.
        if (element->isVoid() || !element->llvmType || !element->llvmType->isSized()) {
            return std::nullopt;
        }

        CgType t;
        t.kind = CgType::Kind::Array;
        t.llvmType = llvm::ArrayType::get(element->llvmType, extent);
        t.element = std::make_shared<CgType>(*element);
        t.extent = extent;
        return t;
    }

    // The widths come from src/types/Layout.hpp and are not repeated here.
    //
    // They used to be a table in this function, which was one table too many: the
    // layout pass has to answer `size_of` in a build with no backend at all
    // (FIN_WITH_LLVM=OFF), so it needs the widths whether or not this file is
    // compiled -- and two tables that agree today are two tables that disagree
    // after one edit, with the disagreement showing up as a struct whose field
    // offsets the backend and the collector compute differently. Which is an ABI
    // split, and the worst kind: every program still compiles and runs.
    //
    // Soundness_Codegen.TheLayoutTableAgreesWithLLVM is the check that the widths
    // this maps to are the widths LLVM's own DataLayout computes for the types
    // built here.
    std::optional<CgType> byName(const std::string& name) const {
        auto info = scalarByName(name);
        if (!info) return std::nullopt;
        switch (info->kind) {
            case ScalarKind::Void:
                return voidType();
            case ScalarKind::Bool: {
                CgType t = intType(info->bits, false);
                t.isBool = true;
                return t;
            }
            case ScalarKind::Int:
                return intType(info->bits, info->isSigned);
            case ScalarKind::Float:
                return floatType(info->bits == 32 ? llvm::Type::getFloatTy(ctx_)
                                                  : llvm::Type::getDoubleTy(ctx_));
            case ScalarKind::Pointer: {
                // The only Pointer-kinded scalar is `string`: a pointer to
                // NUL-terminated bytes, which is what makes `printf("%s", s)`
                // work. A length-carrying string is a library decision (ADR 0003)
                // and a different representation.
                //
                // No pointee, deliberately. `*s` on a string would be a char if the
                // pointee were `char`, and whether a Fin string dereferences to its
                // first byte is a question the corpus does not ask -- the analyzer
                // refuses it ("Cannot dereference non-pointer type 'string'") and
                // this agrees by having nothing to load.
                return pointerType();
            }
        }
        return std::nullopt;
    }

    // A name that is not a scalar may still be a struct. Checked second, so a
    // struct called `int` could not shadow the scalar -- the analyzer rejects that
    // name anyway, and the ordering means this file does not depend on it doing so.
    // A fieldless enum is an `int`: the same width and signedness, from the same
    // table, so an enum and an int are one representation and not two that agree.
    //
    // `int` because that is what the analyzer checks a written member value against
    // (Analyzer_Decl.cpp, visit(EnumDeclaration&)), and because a C enum is an int at
    // an `@define` boundary. Narrowing it to the smallest width that holds the members
    // would be a size win and an ABI decision, and the ABI decision belongs to `finn`.
    //
    // The CgType is a plain Int and does not record which enum it came from. Nothing
    // downstream needs to know: the analyzer has already decided what may be assigned
    // to what, and this file's job at that point is to store a number. What it costs
    // is that a backend-level check like "these two enums are different types" cannot
    // be written here -- which is the analyzer's check, and it has it.
    std::optional<CgType> enumByName(const std::string& name) const {
        if (!enums_ || !enums_->count(name)) return std::nullopt;
        return byName("int");
    }

    std::optional<CgType> structByName(const std::string& name,
                                       bool allowIncomplete = false) const {
        if (!structs_) return std::nullopt;
        auto it = structs_->find(name);
        if (it == structs_->end()) return std::nullopt;
        if (!it->second.complete && !allowIncomplete) return std::nullopt;
        CgType t;
        t.kind = CgType::Kind::Struct;
        t.llvmType = it->second.llvmType;
        t.structInfo = &it->second;
        return t;
    }

    CgType voidType() const {
        CgType t;
        t.kind = CgType::Kind::Void;
        t.llvmType = llvm::Type::getVoidTy(ctx_);
        return t;
    }

    CgType intType(unsigned bits, bool isSigned) const {
        CgType t;
        t.kind = CgType::Kind::Int;
        t.llvmType = llvm::Type::getIntNTy(ctx_, bits);
        t.isSigned = isSigned;
        t.bits = bits;
        return t;
    }

    CgType floatType(llvm::Type* ty) const {
        CgType t;
        t.kind = CgType::Kind::Float;
        t.llvmType = ty;
        return t;
    }

private:
    llvm::LLVMContext& ctx_;
    const std::unordered_map<std::string, StructInfo>* structs_ = nullptr;
    const std::unordered_map<std::string, EnumInfo>* enums_ = nullptr;
    std::function<bool(const TypeNode&, std::string&)> instantiate_;
    const Substitution* bindings_ = nullptr;
};

// Decodes one Fin string or character literal into the bytes it denotes.
//
// `Literal::value` is the lexeme, quotes and backslashes and all: lexer.l:308
// hands `yytext` straight through and the parser wraps it unchanged. So the
// decoding happens here, exactly once, and `"a\tb\n"` is five bytes rather than
// seven (Soundness_Codegen.AnEscapeIsLoweredOnce).
std::string decodeLiteral(const std::string& lexeme) {
    std::string body = lexeme;
    if (body.size() >= 2 && (body.front() == '"' || body.front() == '\'') &&
        body.back() == body.front()) {
        body = body.substr(1, body.size() - 2);
    }
    std::string out;
    for (size_t i = 0; i < body.size(); ++i) {
        if (body[i] != '\\' || i + 1 >= body.size()) {
            out += body[i];
            continue;
        }
        char c = body[++i];
        switch (c) {
            case 'n':  out += '\n'; break;
            case 't':  out += '\t'; break;
            case 'r':  out += '\r'; break;
            case '0':  out += '\0'; break;
            case '\\': out += '\\'; break;
            case '\'': out += '\''; break;
            case '"':  out += '"';  break;
            // An unknown escape keeps the character it introduced rather than the
            // backslash. Nothing in the corpus writes one; guessing the other way
            // would put a stray backslash in the output.
            default:   out += c;    break;
        }
    }
    return out;
}

// Binds a substitution into the mapper for one scope and restores what was there.
//
// An RAII holder and not a pair of calls, because instantiateGeneric has eight
// early returns on refusal paths and every one of them has to restore -- a
// substitution left installed would apply to the *next* type mapped, which is
// somewhere else in the program entirely, and it would resolve rather than refuse.
class ScopedBindings {
public:
    ScopedBindings(TypeMapper& types, const Substitution* bindings)
        : types_(types), saved_(types.bindings()) {
        types_.setBindings(bindings);
    }
    ~ScopedBindings() { types_.setBindings(saved_); }
    ScopedBindings(const ScopedBindings&) = delete;
    ScopedBindings& operator=(const ScopedBindings&) = delete;

private:
    TypeMapper& types_;
    const Substitution* saved_;
};

class Emitter : public Visitor {
public:
    Emitter(DiagnosticEngine& diag, bool debug)
        : diag_(diag), debug_(debug), ctx_(), module_("fin", ctx_), builder_(ctx_),
          types_(ctx_) {
        types_.bindStructs(&structs_);
        types_.bindEnums(&enums_);
        types_.bindInstantiator([this](const TypeNode& node, std::string& out) {
            return instantiateGeneric(node, out);
        });
    }

    bool run(Program& program) {
        // Before the structs, because a field may be of enum type -- and before
        // anything else for the same reason declareStructs runs early: a name has to
        // have a representation before a signature that mentions it is built.
        declareEnums(program);
        if (failed_) return false;
        // Before the functions, because a function's signature may name a struct.
        declareStructs(program);
        if (failed_) return false;
        declareTopLevel(program);
        if (failed_) return false;
        // After the signatures, because a global of struct type needs the struct and
        // nothing else here needs a function -- an initialiser that called one is
        // refused. Before the bodies, because every body may read every global.
        declareGlobals(program);
        if (failed_) return false;
        // The methods of the structs written at module scope. After the globals, so a
        // method body may read one; before the statements, so the emitted order matches
        // the written order for a reader of `--emit-llvm`.
        drainPendingBodies();
        if (failed_) return false;
        for (auto& stmt : program.statements) {
            if (failed_) break;
            stmt->accept(*this);
        }
        if (failed_) return false;
        // The methods of the instantiations the statements asked for. A call site
        // reaches `Box<int>.get` as a declaration and this is what puts a body in it,
        // which is why it is after the loop and not inside it: emitting a body from the
        // middle of another body would work (ScopedEmission exists for exactly that)
        // and this way the queue is drained once, at a point with no live insert point.
        drainPendingBodies();
        return !failed_;
    }

    llvm::Module& module() { return module_; }

private:
    // ---- refusal ----------------------------------------------------------

    // The single exit from "this cannot be lowered". One wording, one place, so a
    // refusal always names the construct and always carries a location.
    void unsupported(ASTNode& node, const std::string& what) {
        if (failed_) return;  // the first refusal is the useful one
        failed_ = true;
        diag_.reportError(node.loc, fmt::format("codegen: {} is not lowered yet", what));
    }

    void unsupportedType(ASTNode& node, const TypeNode* type, const std::string& role) {
        unsupported(node, fmt::format("{} of type '{}'", role, typeName(type)));
    }

    // How a written type reads back in a diagnostic.
    //
    // Recursive, because the decorations nest and the node's own `name` is only the
    // innermost part of one: an ArrayTypeNode leaves it empty, so `[int]` used to
    // print as `[]`, and a PointerTypeNode sets it to the literal word "ptr", so
    // `&[T]` used to print as `&ptr`. Neither names anything the program wrote.
    //
    // The extent is included when there is one, because "a variable of type '[int]'"
    // and "of type '[int, 3]'" are refused for different reasons and only one of
    // them is a decision waiting on a ruling.
    std::string typeName(const TypeNode* type) const { return spell(type, false); }

    // The same spelling with the active bindings applied, which is what an
    // instantiation's *key* is built from.
    //
    // Two names for one written type, because the two readers want different things. A
    // diagnostic wants what the program wrote -- "a parameter of type '&T'", under a
    // caret pointing at the `&T` -- and a key wants what it became, or `Box<T>` inside
    // `unbox<int>` would be a second struct with `Box<int>`'s layout and a different
    // name. Both walk the same decorations, so they are one function with a flag rather
    // than two that can drift.
    std::string displayName(const TypeNode* type) const { return spell(type, true); }

    std::string spell(const TypeNode* type, bool substituted) const {
        if (!type) return "<none>";
        if (substituted) {
            if (const TypeBinding* bound = types_.boundBinding(type)) return bound->display;
        }
        if (auto* ptr = dynamic_cast<const PointerTypeNode*>(type))
            return "&" + spell(ptr->pointee.get(), substituted);
        if (auto* arr = dynamic_cast<const ArrayTypeNode*>(type)) {
            const std::string inner = spell(arr->element_type.get(), substituted);
            uint64_t extent = 0;
            const bool fixed = arr->size &&
                               readConstant(*arr->size, extent) == ConstantRead::Ok;
            return fixed ? fmt::format("[{}, {}]", inner, extent) : "[" + inner + "]";
        }
        std::string name = type->name.empty() ? std::string("?") : type->name;
        // `Box<int>`, and recursively, so `Result<Result<int>>` reads back as itself.
        // This is also what an instantiation's *key* is built from (mangledName), so
        // the rendering being faithful is not only a diagnostic concern: two argument
        // lists that rendered the same would share one layout.
        if (!type->generics.empty()) {
            name += "<";
            for (size_t i = 0; i < type->generics.size(); ++i) {
                if (i) name += ", ";
                name += spell(type->generics[i].get(), substituted);
            }
            name += ">";
        }
        if (type->is_array) name = "[" + name + "]";
        if (type->pointer_depth > 0) name = std::string(type->pointer_depth, '&') + name;
        return name;
    }

    void debugLog(const std::string& text) {
        if (debug_) diag_.note("[codegen] " + text);
    }

    // What an array literal is being built as, for the moment it is being built.
    //
    // An array literal has no type of its own here: `[1, 2, 3]` could be an
    // `[int, 3]` or a `[uint, 3]` and the front end has already decided which, from
    // an annotation this file does not carry on the literal node. So the type comes
    // from where the literal is going -- a declaration's annotation, a parameter, a
    // return type -- and it is set for exactly one `emit` and cleared.
    //
    // Deliberately not a general expression hint. It reaches one node, it is
    // consumed by the one visitor that cannot work without it, and an array literal
    // nested inside one gets the element type from its parent rather than from here
    // (visit(ArrayLiteral&) saves and restores it around each element).
    const CgType* arrayHint_ = nullptr;

    // Emits `expr` with `type` offered to it, when it is a literal that needs one.
    CgVal emitAs(Expression& expr, const CgType& type) {
        auto* saved = arrayHint_;
        arrayHint_ = type.isArray() ? &type : nullptr;
        CgVal v = emit(expr);
        arrayHint_ = saved;
        return v;
    }

    // A field default, emitted where the literal is but resolved as if the literal
    // were not there. The locals are put aside for the duration, because the default
    // was written in the struct's declaration and the analyzer resolved its names in
    // that scope: `x <int> = q` is "Undefined variable 'q'" even with a `q` in scope
    // at every use. Leaving the caller's locals visible would let one of them capture
    // a name the declaration had already bound to something else -- an enumerator, in
    // the case the corpus can reach (ADefaultResolvesInTheStructsScopeAndNotTheUseSite)
    // -- and that is a miscompile rather than a missing feature.
    //
    // One empty scope rather than none, so that anything reaching for `scopes_.back()`
    // still finds a scope to put a name in.
    CgVal emitDefault(Expression& expr, const CgType& target) {
        std::vector<std::unordered_map<std::string, Local>> saved;
        saved.swap(scopes_);
        scopes_.emplace_back();
        CgVal v = emitAs(expr, target);
        scopes_.swap(saved);
        return v;
    }

    // A member's number as a value of the enum's representation, which is `int`'s.
    // Signed, so that `Neg = -1` is -1 and not 4294967295.
    CgVal enumConstant(int64_t value) {
        CgType type = *types_.byName("int");
        return CgVal{llvm::ConstantInt::getSigned(type.llvmType, value), type};
    }

    // ---- scopes -----------------------------------------------------------

    void pushScope() { scopes_.emplace_back(); }
    void popScope() { scopes_.pop_back(); }

    Local* findLocal(const std::string& name) {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) return &found->second;
        }
        return nullptr;
    }

    // ---- prototypes -------------------------------------------------------

    // Every top-level signature is declared before any body is emitted, for the
    // same reason the analyzer hoists them (6f48a89): a call may sit above its
    // declaration, and mutual recursion sits above both.
    // Every fieldless enum the module declares, with its members numbered.
    //
    // One pass and not two: an enum's members are numbers, so nothing here can name
    // a type that does not exist yet -- which is the whole reason a struct needs two.
    //
    // The refusals are eager, as declareStructs's are and for the reason given there:
    // an enum declaration that is quietly skipped is a type name that later resolves
    // to nothing, and "resolves to nothing" is how a variable gets a size this file
    // invented.
    void declareEnums(Program& program) {
        for (auto& stmt : program.statements) {
            auto* e = dynamic_cast<EnumDeclaration*>(stmt.get());
            if (!e) continue;

            if (!e->generic_params.empty()) {
                unsupported(*e, fmt::format("a generic enum '{}'", e->name));
                return;
            }
            for (auto& attr : e->attributes) {
                // Nothing here reads one, and one of them asks for something this
                // file cannot give: `#[llvm_name="Result"]` (stdlib/typing.fin:24)
                // renames a type, and an enum lowers to an integer -- an integer type
                // has no name. `#[export]` (stdlib/stdio.fin:50) is about linkage,
                // which an enum does not have either. Refused rather than dropped:
                // accepting an attribute is claiming to have done what it asked.
                unsupported(*e, fmt::format("the attribute '{}' on enum '{}'",
                                            attr->name, e->name));
                return;
            }

            EnumInfo info;
            info.finName = e->name;
            // The next number, which is 0 until a member says otherwise.
            int64_t next = 0;
            for (size_t i = 0; i < e->values.size(); ++i) {
                const std::string& name = e->values[i].first;

                // A payload makes this a tagged union rather than an integer. Checked
                // per member and against the payload's own copy of the name, the way
                // visit(EnumDeclaration&) in the analyzer checks it: nothing enforces
                // that `values` and `member_payloads` stay parallel.
                if (i < e->member_payloads.size() && e->member_payloads[i].name == name &&
                    !e->member_payloads[i].types.empty()) {
                    unsupported(*e, fmt::format("a payload on enum member '{}::{}'",
                                                e->name, name));
                    return;
                }

                if (Expression* written = e->values[i].second.get()) {
                    int64_t value = 0;
                    if (readSignedConstant(*written, value) != ConstantRead::Ok) {
                        unsupported(*written,
                                    fmt::format("the value of enum member '{}::{}' (it is "
                                                "not an integer constant)",
                                                e->name, name));
                        return;
                    }
                    next = value;
                }
                info.members.emplace_back(name, next);
                info.valueByName[name] = next;
                ++next;
            }

            debugLog(fmt::format("enum {} with {} member(s)", info.finName,
                                 info.members.size()));
            const EnumInfo& stored = (enums_[e->name] = std::move(info));

            // The bare name too: the analyzer defines every enumerator in the scope the
            // enum was declared in (arrays_enums.fin:17 reads `OK` with no `Status::`),
            // so a bare name has to reach the same member. Later declarations win, which
            // is what a scope that redefines a symbol does; nothing in the corpus
            // declares one name in two enums.
            for (const auto& member : stored.members)
                enumMembers_[member.first] = {&stored, member.second};

            registeredEnums_.insert(e);
        }
    }

    // Every struct the module declares, as a named llvm::StructType, in two passes
    // so that a struct may name one declared above it.
    //
    // Two passes rather than one because a field of struct type needs that struct's
    // LLVM type to exist, and one pass in source order would work only for the
    // orders the analyzer happens to accept today. The names are created first and
    // the bodies second, and a body that names a struct whose body is not set yet
    // refuses -- see StructInfo::complete for why that refusal is the useful one.
    //
    // The refusals are eager: a struct this file cannot lower fails the build even
    // if nothing uses it. That is the same rule the rest of the file follows and it
    // is the safe direction -- a struct declaration that is quietly skipped is a
    // type that later resolves to nothing, and "resolves to nothing" is how a field
    // read turns into a read of some other field.
    void declareStructs(Program& program) {
        std::vector<StructDeclaration*> decls;
        for (auto& stmt : program.statements) {
            auto* s = dynamic_cast<StructDeclaration*>(stmt.get());
            if (!s) continue;
            if (!s->generic_params.empty()) {
                // A template, not a type. Recorded so that an instantiation can find
                // it later and nothing is emitted for it now: it has no layout, no
                // size and no LLVM type, and `struct M <T> {}` (blame_assert.fin:19)
                // is a whole sample's worth of evidence that one nobody instantiates
                // is not an error either.
                //
                // Deliberately *not* checked here, beyond the shapes below that are
                // wrong however it is used. Whether its fields are lowerable depends
                // on what it is instantiated at, so the check belongs where the
                // arguments are known -- which is also why `struct M <T> {}` may be
                // empty while `M<int>` may not.
                if (!lowerableTemplate(*s)) return;
                if (templates_.count(s->name)) {
                    unsupported(*s, fmt::format("a second declaration of struct '{}'",
                                                s->name));
                    return;
                }
                templates_[s->name] = s;
                registered_.insert(s);
                continue;
            }
            if (!lowerableStruct(*s)) return;
            if (s->is_forward_declaration && s->members.empty()) {
                // `struct Stream;` (stdlib/stdio.fin:42) declares a name whose size
                // nothing knows yet. Left unregistered, so a variable of it refuses
                // rather than being given a size this file invented.
                continue;
            }
            if (structs_.count(s->name)) {
                unsupported(*s, fmt::format("a second declaration of struct '{}'", s->name));
                return;
            }
            StructInfo info;
            info.finName = s->name;
            info.llvmType = llvm::StructType::create(ctx_, llvmNameOf(*s, s->name));
            info.decl = s;
            structs_[s->name] = info;
            registered_.insert(s);
            decls.push_back(s);
        }

        for (StructDeclaration* s : decls) {
            StructInfo& info = structs_[s->name];
            std::vector<llvm::Type*> members;
            for (auto& m : s->members) {
                auto t = types_.map(m->type.get());
                if (!t) { unsupportedType(*m, m->type.get(), "a struct field"); return; }
                if (t->isVoid()) {
                    unsupported(*m, fmt::format("a field of type 'void' in struct '{}'",
                                                s->name));
                    return;
                }
                if (info.indexByName.count(m->name)) {
                    unsupported(*m, fmt::format("a second field '{}' in struct '{}'",
                                                m->name, s->name));
                    return;
                }
                info.indexByName[m->name] = info.fields.size();
                // The default is recorded and not evaluated: it is an expression,
                // and where it runs (each literal that omits the field) is not
                // here. Nothing is checked about it at the declaration either --
                // a struct nobody instantiates never runs its defaults, so a
                // default this file could not lower is not a reason to refuse the
                // type. The refusal lands at the literal that needs it.
                info.fields.push_back(StructField{m->name, *t, m->default_value.get()});
                members.push_back(t->llvmType);
            }
            if (members.empty()) {
                // A struct with no fields has no size to speak of and nothing in
                // the corpus writes one. LLVM would give it size 0, C gives it 1,
                // and picking either here would be inventing a rule.
                unsupported(*s, fmt::format("an empty struct '{}'", s->name));
                return;
            }
            // isPacked=false, which is the same choice src/types/Layout.hpp makes
            // and what Soundness_Codegen.AStructsLayoutMatchesWhatLLVMWouldChoose
            // compares against. A packed body here would agree with a padded layout
            // pass on every field at offset 0 and on nothing else.
            info.llvmType->setBody(members, /*isPacked=*/false);
            info.complete = true;
            debugLog("declared struct " + s->name + llvmNameNote(*info.llvmType, s->name));
        }

        // A third pass, for the methods, after every body above is set.
        //
        // Third and not folded into the second because a method's signature may name
        // any struct in the program -- `fun neighbour() <&Other>` where `Other` is
        // declared below this one -- and a signature is mapped without
        // `allowIncomplete`. Declaring the fields first is what makes the order the
        // source wrote its structs in stop mattering, which is the same reason the
        // second pass is separate from the first.
        for (StructDeclaration* s : decls) {
            StructInfo& info = structs_[s->name];
            bindMethodTypes(info);
            ScopedBindings bound(types_, &info.methodBindings);
            if (!declareStructMethods(info)) return;
        }
    }

    // `Struct.method`, and `Box<int>.method` for an instantiation.
    //
    // The least mangling that keeps two structs' `get` apart, and `.` is a legal
    // character in an ELF symbol. It is also already this file's convention: a generic
    // function instance publishes `ident<int>`, brackets and all, on the grounds that
    // the only reader of a Fin symbol name is a person reading `nm` output. A method
    // has the same two readers and the same answer.
    static std::string methodKey(const std::string& structName, const std::string& method) {
        return structName + "." + method;
    }

    // `V.operator+` -- an operator's method name, and the reason this file needs a
    // speller at all.
    //
    // An OperatorDeclaration carries an ASTTokenKind, not the text the writer typed, so
    // there is nothing to concatenate until a token is turned back into characters.
    // Every use of one wants that: the symbol, the trace line, and the diagnostic --
    // which before this could only say "an operator on struct 'V'" and leave the reader
    // to find which of two operators it meant.
    //
    // Spelled the way the source spells it, which is why ``-` and `-` are two
    // strings rather than one: UNARY_MINUS and MINUS are different declarations on one
    // struct (`operator `-` is negation, `operator -` is subtraction) and one
    // spelling for both would make them one symbol and silently keep the first.
    static const char* spellOperator(ASTTokenKind op) {
        switch (op) {
            case ASTTokenKind::PLUS:            return "+";
            case ASTTokenKind::MINUS:           return "-";
            case ASTTokenKind::MULT:            return "*";
            case ASTTokenKind::DIV:             return "/";
            case ASTTokenKind::MOD:             return "%";
            case ASTTokenKind::EQEQ:            return "==";
            case ASTTokenKind::NOTEQ:           return "!=";
            case ASTTokenKind::LT:              return "<";
            case ASTTokenKind::GT:              return ">";
            case ASTTokenKind::LTEQ:            return "<=";
            case ASTTokenKind::GTEQ:            return ">=";
            case ASTTokenKind::AMPERSAND:       return "&";
            case ASTTokenKind::AND:             return "&&";
            case ASTTokenKind::PIPE:            return "|";
            case ASTTokenKind::OR:              return "||";
            case ASTTokenKind::CARET:           return "^";
            case ASTTokenKind::SHIFTLEFT:       return "<<";
            case ASTTokenKind::SHIFTRIGHT:      return ">>";
            case ASTTokenKind::NOT:             return "!";
            case ASTTokenKind::TILDE:           return "~";
            case ASTTokenKind::EQUAL:           return "=";
            case ASTTokenKind::PLUSEQUAL:       return "+=";
            case ASTTokenKind::MINUSEQUAL:      return "-=";
            case ASTTokenKind::MULTEQUAL:       return "*=";
            case ASTTokenKind::DIVEQUAL:        return "/=";
            case ASTTokenKind::MODEQUAL:        return "%=";
            case ASTTokenKind::AMPERSANDEQUAL:  return "&=";
            case ASTTokenKind::PIPEEQUAL:       return "|=";
            case ASTTokenKind::SHIFTLEFTEQUAL:  return "<<=";
            case ASTTokenKind::SHIFTRIGHTEQUAL: return ">>=";
            case ASTTokenKind::INCREMENT:       return "++";
            case ASTTokenKind::DECREMENT:       return "--";
            case ASTTokenKind::INDEX:           return "[]";
            case ASTTokenKind::INDEX_ASSIGN:    return "[]=";
            case ASTTokenKind::DEREF:           return "`*";
            case ASTTokenKind::UNARY_MINUS:     return "`-";
            // VARIADIC_CALL is `operator (...args)` (stdlib/operators.fin), which is a
            // call and not a token anyone writes between two operands. Left unspelled
            // so that declaring one refuses by name instead of publishing `V.operator?`.
            default:                            return "";
        }
    }

    // The name an operator is declared and looked up under. `operator` is a keyword, so
    // no Fin method can collide with one of these.
    static std::string operatorKey(const std::string& structName, ASTTokenKind op) {
        return methodKey(structName, std::string("operator") + spellOperator(op));
    }

    // The declaration behind an operator token, or null for one this struct does not
    // declare. The counterpart of findMethod, and read for the same two reasons: to
    // know whether a struct has one at all, and to say why the one it has was not
    // declared.
    static const OperatorDeclaration* findOperator(const StructInfo& info,
                                                  ASTTokenKind op) {
        if (!info.decl) return nullptr;
        for (auto& o : info.decl->operators)
            if (o->op == op) return o.get();
        return nullptr;
    }

    // What the mapper is handed while one of this struct's methods is declared or
    // emitted. See StructInfo::methodBindings for why it is a binding and not a lookup.
    void bindMethodTypes(StructInfo& info) {
        info.methodBindings = info.substitution;
        // allowIncomplete, because this runs for an instantiation from inside
        // instantiateGeneric -- where the body has just been set but nothing has
        // published it yet. For the non-generic path the struct is complete either way.
        auto self = types_.structByName(info.finName, /*allowIncomplete=*/true);
        if (!self) return;  // an unregistered struct has no methods to declare
        info.methodBindings.push_back({"Self", TypeBinding{*self, info.finName}});
        // `Box` written inside `Box<T>`'s own method means this instantiation.
        // struct_methods.fin:21 writes the non-generic form of exactly this (`<&Point>`
        // where `<&Self>` would do) and calls it correct, and for a template the bare
        // name is not merely equivalent -- it resolves to nothing at all, because a
        // template is not a type. Skipped when the two spellings are the same word, so
        // a concrete struct keeps going through ordinary name resolution.
        if (info.decl && info.decl->name != info.finName) {
            info.methodBindings.push_back(
                {info.decl->name, TypeBinding{*self, info.finName}});
        }
    }

    // The prototypes for one struct's methods, and a queued job per body.
    //
    // Call with `methodBindings` active. Returns false having already reported.
    bool declareStructMethods(StructInfo& info) {
        if (!info.decl) return true;
        auto self = types_.structByName(info.finName, /*allowIncomplete=*/true);
        if (!self) return true;
        CgType receiver = types_.pointerTo(*self);

        for (auto& m : info.decl->methods) {
            // A method generic is one layer further than this unit goes: two
            // substitutions at once, the struct's and the call's. Not declared, so a
            // call to it refuses at the call site with a name to blame rather than
            // linking against a symbol that was never defined -- and *not* refused
            // here, because struct_methods.fin declares `set_x<T>` and never calls it,
            // and a declaration nobody instantiates has no signature to lower.
            if (!m->generic_params.empty()) continue;
            // Likewise a method with no body. `@define` writes prototypes at module
            // scope, not inside a struct, so this is a shape the corpus does not have;
            // declaring one would publish a symbol that nothing defines, and the
            // failure would land on the linker rather than on the line.
            if (!m->body) continue;

            // A written `self` is the receiver, so it has to *be* the receiver: `&Self`,
            // `&Point`, `&Point<T>` -- three spellings of one pointer. `self: Point` is
            // a copy, and this file passes a pointer, so accepting it would mean a
            // method whose signature says by-value and whose body assigns through a
            // pointer into the caller's object. `self: int` is not the struct at all.
            // Checked here and not in lowerableMethods because the receiver's type is
            // only known once the struct is complete -- and for a template, only once
            // it is instantiated.
            if (!m->is_static && !m->params.empty() && m->params[0]->name == "self" &&
                m->params[0]->type) {
                auto written = types_.map(m->params[0]->type.get(), /*allowIncomplete=*/true);
                const bool matches = written && written->isPointer() && written->pointee &&
                                     written->pointee->llvmType == self->llvmType;
                if (!matches) {
                    if (failed_) return false;
                    unsupported(*m->params[0],
                                fmt::format("a 'self' of type '{}' on struct '{}', which "
                                            "is not a pointer to it",
                                            typeName(m->params[0]->type.get()), info.finName));
                    return false;
                }
            }

            const std::string key = methodKey(info.finName, m->name);
            // A static method takes no receiver -- struct_methods.fin:8 calls
            // `Point::make(1, 2)` with nobody to be `self`. Everything else does, and
            // it is a *pointer*: `set_x` at :16 assigns to `self.x`, and a by-value
            // receiver would make that a store into a copy that is discarded at the
            // return. That is not an unimplemented feature, it is a program that
            // silently does not assign.
            declareFunction(*m, key, key, m->params, m->return_type.get(),
                            /*isVarArg=*/false, /*isExtern=*/false,
                            m->is_static ? nullptr : &receiver);
            auto declared = functions_.find(key);
            if (declared == functions_.end()) return false;  // declareFunction reported

            // Weak, for the reason a generic function instance is weak: two objects
            // that each declare this struct both publish this symbol and neither knows
            // the other exists, so identical definitions and let the linker keep one.
            // A method is not a template, but a struct declaration reaches an object
            // file the same way a template does -- through a header everyone includes.
            declared->second.fn->setLinkage(llvm::Function::LinkOnceODRLinkage);

            // The body is deferred, not emitted here. Two reasons, and either alone
            // would be enough: a method may call a free function whose prototype
            // declareTopLevel has not created yet (declareStructs runs first), and a
            // method of an *instantiation* is declared from the middle of another
            // function's body, where emitting straight away would mean nesting two
            // insert points for no reason.
            pendingBodies_.push_back(PendingBody{m.get(), &m->params, m->body.get(), key,
                                                 &info.methodBindings});
        }

        // The operators, on the same terms. An operator is a method with a spelled name:
        // the receiver is the same pointer, the body is deferred to the same queue, the
        // linkage is weak for the same reason, and an instantiation gets its own copy
        // because this runs once per instantiation. What differs is where it is *reached*
        // from -- visit(BinaryOp&) rather than a written name -- and nothing about that
        // is decided here.
        for (auto& o : info.decl->operators) {
            // A generic operator: two substitutions at once, the struct's and the
            // operator's, which is one layer further than this unit goes. Not declared
            // and not refused, because operators.fin:15 declares `operator + : <T>` and
            // applies it nowhere -- the sample is `//@ ok` with it in.
            if (!o->generic_params.empty()) continue;
            // An operator with no body is one bound by `implements`
            // (hashmap.fin:50-51): the function it forwards to is written in the cast,
            // and reading that cast is a feature of its own. Declaring the operator
            // anyway would publish a symbol nothing defines and move the failure to the
            // linker.
            if (!o->body) continue;

            const std::string key = operatorKey(info.finName, o->op);
            declareFunction(*o, key, key, o->params, o->return_type.get(),
                            /*isVarArg=*/false, /*isExtern=*/false, &receiver);
            auto declared = functions_.find(key);
            if (declared == functions_.end()) return false;  // declareFunction reported
            declared->second.fn->setLinkage(llvm::Function::LinkOnceODRLinkage);
            pendingBodies_.push_back(PendingBody{o.get(), &o->params, o->body.get(), key,
                                                &info.methodBindings});
        }
        return true;
    }

    // A method or operator body waiting for the point in run() where everything it can
    // name exists. `bindings` points into a StructInfo, which outlives this.
    //
    // The parts of a declaration rather than the declaration, because a
    // FunctionDeclaration and an OperatorDeclaration are two unrelated classes with the
    // same three members -- params, body, loc -- and emitting a body needs exactly
    // those. A common base class for the two would be the tidier answer and is a change
    // to the AST, which this unit is not.
    struct PendingBody {
        ASTNode* node = nullptr;
        const std::vector<std::unique_ptr<Parameter>>* params = nullptr;
        Block* body = nullptr;
        std::string key;
        const Substitution* bindings = nullptr;
    };

    // Emits every queued body, including the ones queued while emitting them.
    void drainPendingBodies() {
        // By index and re-reading size(), because emitting a body may instantiate a
        // template -- which declares that instantiation's own methods onto the end of
        // this same queue. By value, because that push may reallocate.
        for (size_t i = 0; i < pendingBodies_.size(); ++i) {
            PendingBody job = pendingBodies_[i];
            ScopedBindings bound(types_, job.bindings);
            emitBody(*job.node, *job.params, *job.body, job.key);
        }
        // Cleared, because run() drains more than once and a second entry block on a
        // function that already has one is invalid IR rather than a duplicate.
        pendingBodies_.clear();
    }

    // The struct shapes this file will not lower, each with the reason it cannot be
    // guessed at. Returns false having already reported.
    bool lowerableStruct(StructDeclaration& s) {
        if (s.is_class) {
            // Whether a `class` is a value like a struct or a reference is not
            // settled, and the two lower differently at every assignment.
            unsupported(s, fmt::format("a class '{}'", s.name));
            return false;
        }
        if (s.destructor) {
            // A destructor runs implicitly at the end of a scope. Lowering the
            // struct as plain data and emitting no call is not an unimplemented
            // feature, it is a program that silently does not free.
            unsupported(*s.destructor, fmt::format("a destructor on struct '{}'", s.name));
            return false;
        }
        if (!lowerableMethods(s)) return false;
        if (!lowerableOperators(s)) return false;
        for (auto& c : s.constructors) {
            unsupported(*c, fmt::format("a constructor on struct '{}'", s.name));
            return false;
        }
        for (auto& m : s.members) {
            for (auto& attr : m->attributes) {
                // readonly.fin:19 writes `#[debug]` on a field. Nothing here reads a
                // field attribute, and a field attribute is one edit away from being
                // one that moves the field -- which is the failure this file refuses
                // an unread attribute to avoid everywhere else.
                unsupported(*m, fmt::format("the attribute '{}' on field '{}' of "
                                            "struct '{}'", attr->name, m->name, s.name));
                return false;
            }
        }
        if (!s.parents.empty()) {
            unsupported(s, fmt::format("struct '{}' inheriting another type", s.name));
            return false;
        }
        for (auto& attr : s.attributes) {
            if (attr->name == "llvm_name" && !attr->is_flag) continue;  // read below
            // An attribute this file does not read may be one that changes the
            // layout. Ignoring it is the failure mode that produces a working
            // program with the wrong offsets. `#[llvm_name]` in its flag form lands
            // here too: with no value it names nothing, and treating it as absent
            // would be a guess at what the writer meant.
            unsupported(s, fmt::format("the attribute '{}' on struct '{}'",
                                       attr->name, s.name));
            return false;
        }
        return true;
    }

    // The method shapes that are wrong however the struct is used, checked where the
    // struct is written.
    //
    // Only those. A method's *body* is checked where it is emitted, which for a
    // template is once per instantiation -- the same split a generic free function
    // has, and for the same reason: `cast<T>(x)` may be lowerable at one binding and
    // not at another, so refusing it at the declaration would refuse a program that
    // works.
    bool lowerableMethods(StructDeclaration& s) {
        std::set<std::string> seen;
        for (auto& m : s.methods) {
            // An attribute this file does not read may be the one that decides
            // linkage (`#[export]`) or which of two definitions wins
            // (`#[overwrite]`), and a method is a symbol like any other.
            if (!attributesAreJustLlvmName(*m, m->attributes, "method")) return false;
            for (auto& attr : m->attributes) {
                // The valued form is what attributesAreJustLlvmName lets through, and
                // a method may not have it: an instantiation's method is emitted once
                // per binding, and one name over two of them is either a duplicate
                // definition or a silent `general_point.1` that nobody can call. This
                // is the generic-function rule (declareTopLevel) applied one level in.
                unsupported(*m, fmt::format("the attribute '{}' on the method '{}' of "
                                            "struct '{}'", attr->name, m->name, s.name));
                return false;
            }
            if (!seen.insert(m->name).second) {
                // Two methods of one name would be two definitions of one symbol, and
                // declareFunction keeps the first -- so the second body would silently
                // not be the one that runs. Overload resolution is the analyzer's
                // (`constructors[0]` is the state of it), and until it exists there is
                // no way to tell which the call meant.
                unsupported(*m, fmt::format("a second method '{}' on struct '{}'",
                                            m->name, s.name));
                return false;
            }
            for (auto& param : m->params) {
                if (!param->is_vararg) continue;
                // `...` on a Fin definition needs va_start, which is a library
                // question (ADR 0003), and on a method it is not written anywhere in
                // the corpus.
                unsupported(*param, fmt::format("'...' on the method '{}' of struct '{}'",
                                                m->name, s.name));
                return false;
            }
            if (m->is_static && !m->params.empty() && m->params[0]->name == "self") {
                // A static method has no receiver, and the analyzer drops a parameter
                // called `self` wherever it appears (buildMethodSignature). So this
                // parameter exists for the caller and not for the callee, or the other
                // way round, depending on which pass you ask -- and either way the
                // arguments after it land one place out.
                unsupported(*m->params[0],
                            fmt::format("a 'self' parameter on the static method '{}' of "
                                        "struct '{}'", m->name, s.name));
                return false;
            }
            for (size_t i = 0; i < m->params.size(); ++i) {
                if (m->params[i]->name != "self" || i == 0) continue;
                // The receiver is parameter 0 or it is injected. A `self` written
                // second is not a receiver the analyzer dropped from the signature
                // (buildMethodSignature drops it wherever it is), so lowering it as
                // one would shift every argument by a place.
                unsupported(*m->params[i],
                            fmt::format("a 'self' parameter in position {} of method "
                                        "'{}' on struct '{}'", i + 1, m->name, s.name));
                return false;
            }
        }
        return true;
    }

    // The operator shapes that are wrong however the struct is used, checked where the
    // struct is written. lowerableMethods' counterpart, and the same split: an
    // operator's *body* is checked where it is emitted, once per instantiation.
    bool lowerableOperators(StructDeclaration& s) {
        std::set<std::string> seen;
        for (auto& o : s.operators) {
            const std::string spelling = spellOperator(o->op);
            if (spelling.empty()) {
                // A token this file cannot turn back into characters has no symbol to
                // be declared under and no name to appear in a diagnostic, and picking
                // one would put a symbol in the object file that no reader can trace to
                // a line. `operator (...args)` is the one the corpus has.
                unsupported(*o, fmt::format("an operator on struct '{}' whose token has "
                                            "no spelling", s.name));
                return false;
            }
            if (!seen.insert(spelling).second) {
                // Two operators of one token are two definitions of one symbol, and
                // declareFunction keeps the first -- so the second body would silently
                // not be the one that runs. Which of the two a use meant is overload
                // resolution, and the analyzer has none for an operator (it does not
                // even check the arity), so there is nothing to pick with.
                unsupported(*o, fmt::format("a second operator '{}' on struct '{}'",
                                            spelling, s.name));
                return false;
            }
            if (!o->params.empty() && o->params[0]->name == "self") {
                // A method's written `self` *is* the receiver, because
                // buildMethodSignature drops a parameter of that name wherever it
                // appears. Nothing drops this one: the analyzer's
                // visit(OperatorDeclaration&) defines `self` as the struct
                // unconditionally and then defines every written parameter too, so a
                // written `self` here is an ordinary operand hidden behind the injected
                // receiver -- two things of one name that disagree about the arity.
                unsupported(*o->params[0],
                            fmt::format("a 'self' parameter on the operator '{}' of "
                                        "struct '{}'", spelling, s.name));
                return false;
            }
            for (auto& param : o->params) {
                if (!param->is_vararg) continue;
                // `...` needs va_start, which is a library question (ADR 0003), and an
                // operator with one is not written anywhere in the corpus.
                unsupported(*param, fmt::format("'...' on the operator '{}' of struct "
                                                "'{}'", spelling, s.name));
                return false;
            }
        }
        return true;
    }

    // What this struct is called in the IR.
    //
    // `#[llvm_name="general_point"]` (struct_methods.fin:5, letssee.fin:8,
    // stdlib/error.fin:2, stdlib/types.fin:6), which the corpus glosses as "a rust
    // like attribute for compile time codegen manipulation (for specific statements
    // like struct declarations)". On an `@define` the same attribute binds a C symbol
    // and is load-bearing; here it is not, and the difference is worth being explicit
    // about. An llvm::StructType's name reaches no object file: LLVM compares struct
    // types structurally, nothing refers to a type by name at link time, and two types
    // asking for one name are uniqued (`vec2_f32`, `vec2_f32.0`) rather than merged.
    // So honouring this cannot change what a program computes -- it changes what a
    // person reading the IR sees, which is what the writer asked for.
    //
    // The default keeps the `struct.` prefix, which is only this file's convention for
    // telling its own types apart in a dump. An `#[llvm_name]` replaces the whole name
    // rather than being prefixed: the writer spelled the name they want to read.
    //
    // OWNER RULING NEEDED: on a *template*, one name has to cover every instantiation,
    // so `#[llvm_name="vec2_f32"]` on `struct Vec2<T>` is either a name for the
    // template (LLVM uniques the second `Vec2<char>` to `vec2_f32.0`) or a name for the
    // one instantiation the writer had in mind. This file reads it as the template's,
    // because that is the declaration it is written on. Nothing observable rides on it.
    static std::string llvmNameOf(const StructDeclaration& s, const std::string& finName) {
        for (auto& attr : s.attributes) {
            if (attr->name == "llvm_name" && !attr->is_flag) return attr->value_str;
        }
        return "struct." + finName;
    }

    // The trace's account of a rename. Reads the name back off the type rather than
    // off the attribute, because LLVM is the one that decides: the second
    // instantiation of a renamed template gets `vec2_f32.0`, and a trace that printed
    // the attribute would claim both were called the same thing.
    static std::string llvmNameNote(const llvm::StructType& type,
                                    const std::string& finName) {
        const std::string actual = type.getName().str();
        if (actual == "struct." + finName) return {};
        return " as " + actual;
    }

    // The template shapes that are wrong however they are instantiated: the same
    // list as lowerableStruct's, less the ones that depend on the arguments (its
    // fields, and whether it is empty). Checked at the declaration because a
    // `class Box<T>` is not going to become lowerable at `Box<int>`, and a
    // diagnostic at the declaration is where the reader can act on it.
    bool lowerableTemplate(StructDeclaration& s) {
        if (!lowerableStruct(s)) return false;
        return !hasErasureMarker(s, s.generic_params, "struct");
    }

    // `Castable` -- the erasure marker, and the reason it is refused rather than
    // monomorphised. ADR 0002 carries two of pyprototype's lowering decisions forward
    // deliberately: erasure is selected by the presence of an erasure-marker
    // constraint on any one parameter, and an erased generic is represented as a raw
    // pointer. That is a different representation and not a different spelling, so a
    // monomorphised body for one is a *different program* -- it happens to agree
    // wherever the argument is a scalar and disagrees wherever the erased pointer is
    // what the code is about.
    //
    // One name, because one name is what the corpus writes:
    // generics_interfaces.fin:8 (`<T: Castable, U: Castable>`), nullifier.fin:10,
    // deeptest2.fin:13, lambdas.fin:69. The analyzer registers it as a type
    // (Analyzer_Core.cpp:142, "Mock Castable") rather than the corpus declaring it,
    // so there is nothing to read the marker-ness off except the name.
    static bool isErasureMarker(const std::string& name) { return name == "Castable"; }

    // Refused at the declaration and not at the instantiation, because a marker is a
    // property of the template: `maybe<int>` is not going to stop being erased.
    bool hasErasureMarker(ASTNode& node,
                          const std::vector<std::unique_ptr<GenericParam>>& params,
                          const char* what) {
        for (auto& p : params) {
            if (!p->constraint || !isErasureMarker(p->constraint->name)) continue;
            unsupported(node,
                        fmt::format("the erasure marker '{}' on '{}' of a generic {}",
                                    p->constraint->name, p->name, what));
            return true;
        }
        return false;
    }

    // `Box<int>` -- one instantiation of one template, built the first time it is
    // asked for and then found.
    //
    // Monomorphisation, which is the strategy the corpus names: struct_methods.fin:6
    // says "T is a generic and it will be a Monomorphization Generic type because
    // its the default generic type we use", and ADR 0002 carries the same rule
    // forward from pyprototype -- erasure is what an erasure-*marker* constraint
    // selects, and a bare `<T>` has none. So each distinct argument list gets its own
    // llvm::StructType, laid out as if the argument had been written in place of the
    // parameter, and `Box<char>` is one byte where `Box<long>` is eight.
    //
    // Writes the mangled name to `out` and returns whether it registered (or found)
    // a complete struct under it. Returns false having already reported.
    //
    // The three steps are ordered by what depends on what: the arguments have to be
    // mapped before the name can be spelled, and the name has to be registered
    // before the body is mapped -- `struct Node<T> { next <&Node<T>> }` asks for its
    // own instantiation while its own fields are being mapped, and finds the
    // incomplete name that step 2 put there.
    bool instantiateGeneric(const TypeNode& node, std::string& out) {
        auto found = templates_.find(node.name);
        if (found == templates_.end()) {
            // Not a template. Either a plain struct with arguments written on it,
            // which the analyzer has already refused, or a generic the front end
            // knows and this file does not -- an alias, an interface, an enum. Silent,
            // because the mapper's caller reports it at the line, and it reports what
            // the *use* was ("a variable of type 'Result<int>'") rather than guessing
            // which of those it is.
            return false;
        }
        StructDeclaration& tmpl = *found->second;

        if (node.generics.size() != tmpl.generic_params.size()) {
            // The analyzer says "Generic count mismatch" before this, so reaching here
            // is the two passes disagreeing. Refused rather than padded with defaults:
            // a missing argument has no representation to guess at.
            unsupported(const_cast<TypeNode&>(node),
                        fmt::format("'{}' with {} type argument(s) where it declares {}",
                                    node.name, node.generics.size(),
                                    tmpl.generic_params.size()));
            return false;
        }

        // 1. The arguments, mapped in the *enclosing* scope. Cleared first, because an
        //    argument is written at the use site and not inside the template -- when
        //    `Node<T>`'s own body asks for `Node<T>`, the T in the argument list is
        //    the enclosing template's T and has to resolve through the binding that is
        //    already active. Which is exactly what leaving it in place does, so the
        //    bindings are *not* cleared here; the comment records that this was
        //    considered, because clearing them is the obvious move and it breaks the
        //    self-referential case.
        Substitution substitution;
        for (size_t i = 0; i < node.generics.size(); ++i) {
            const TypeNode* arg = node.generics[i].get();
            auto mapped = arg ? types_.map(arg) : std::nullopt;
            if (!mapped || mapped->isVoid() || !mapped->llvmType ||
                !mapped->llvmType->isSized()) {
                if (failed_) return false;  // a nested instantiation already reported
                // Named as the argument and not as the template: `Box` is fine and
                // `[int]` is the thing with no representation yet, and a reader who is
                // told "a generic struct 'Box'" goes looking in the wrong place.
                unsupportedType(const_cast<TypeNode&>(node), arg,
                                fmt::format("'{}' at a type argument", node.name));
                return false;
            }
            substitution.push_back(
                {tmpl.generic_params[i]->name, TypeBinding{*mapped, displayName(arg)}});
        }

        // 2. The name. One name per distinct argument list, so asking twice finds the
        //    first one -- which is what makes `Box<int>` assignable to `Box<int>`
        //    (two named llvm::StructTypes with identical bodies are still two types).
        out = mangledName(tmpl.name, substitution);
        auto existing = structs_.find(out);
        if (existing != structs_.end()) {
            // Complete, or in the middle of being built (the self-referential case).
            // Either way the name is registered and a pointer to it is legal; a
            // *field* of an incomplete one refuses at the field, as it does for a
            // non-generic struct.
            return true;
        }

        StructInfo info;
        info.finName = out;
        info.llvmType = llvm::StructType::create(ctx_, llvmNameOf(tmpl, out));
        info.substitution = substitution;
        structs_[out] = info;

        // 3. The body, with the parameters bound. Everything about this is the
        //    non-generic path in declareStructs' second pass, with `types_.map` seeing
        //    the substitution -- so a field of `T` is a field of what T became, and a
        //    field of `&T` or `[T, 3]` is the decoration applied to it.
        ScopedBindings bound(types_, &structs_[out].substitution);
        std::vector<llvm::Type*> members;
        StructInfo& live = structs_[out];
        for (auto& m : tmpl.members) {
            auto t = types_.map(m->type.get());
            if (!t) {
                if (!failed_) unsupportedType(*m, m->type.get(),
                                              fmt::format("a field of '{}'", out));
                return false;
            }
            if (t->isVoid()) {
                unsupported(*m, fmt::format("a field of type 'void' in struct '{}'", out));
                return false;
            }
            if (live.indexByName.count(m->name)) {
                unsupported(*m, fmt::format("a second field '{}' in struct '{}'",
                                            m->name, out));
                return false;
            }
            live.indexByName[m->name] = live.fields.size();
            live.fields.push_back(StructField{m->name, *t, m->default_value.get()});
            members.push_back(t->llvmType);
        }
        if (members.empty()) {
            // `M<int>` where `struct M <T> {}` (blame_assert.fin:19). The template was
            // allowed to be empty and the instantiation is not, and that is the same
            // rule the non-generic path has: LLVM says size 0, C says 1, and the
            // corpus writes no empty struct that anyone instantiates.
            unsupported(const_cast<TypeNode&>(node),
                        fmt::format("an empty struct '{}'", out));
            return false;
        }
        live.llvmType->setBody(members, /*isPacked=*/false);
        live.complete = true;
        debugLog("instantiated struct " + out + llvmNameNote(*live.llvmType, out));

        // 4. The methods, once per instantiation and only for the instantiations the
        //    program asks for. This is what makes struct_methods.fin compile with a
        //    `Point<T>` in it and nothing in the object file for it: a method on a
        //    template is a template, and a template with no arguments has no
        //    signature to lower. It is also why `Box<int>.get` and `Box<char>.get` are
        //    two functions -- they are two bodies over two representations, the same
        //    as a generic free function's instances.
        live.decl = &tmpl;
        bindMethodTypes(live);
        // Nested inside `bound` above, and replacing it for the duration: a method
        // signature needs `Self` and the template's bare name as well as `T`, and
        // methodBindings is the substitution plus those two.
        ScopedBindings methodScope(types_, &live.methodBindings);
        return declareStructMethods(live);
    }

    // How an instantiation is spelled, in diagnostics and as the LLVM type's name.
    //
    // `Box<int>`, which is what the program wrote -- not a scheme with lengths and
    // sigils. Nothing links against these names (a struct type name is debug
    // information, and Fin does not mangle its functions either), so the only reader
    // is a person reading a diagnostic or `--emit-llvm`, and the name they wrote is
    // the one they can find.
    //
    // It is still a key, so it has to be injective in the arguments: two different
    // argument lists must not spell the same name, or two instantiations would share
    // a layout. The comma-separated display names give that as long as a display name
    // is itself unambiguous, and they nest -- `Box<Box<int>>` contains the inner
    // instantiation's own mangled name.
    // A representation spelled the way Fin would have written it.
    //
    // Needed because a binding inferred from an argument has no written spelling to
    // carry: `ident(5)` names no type anywhere, and the instance still has to be keyed
    // on something. Fin-style rather than LLVM-style, so that a `Box<T>` inside the
    // instance's body spells the *same* instantiation a written `Box<int>` does -- key
    // it as `i32` and the body would build a second `Box<i32>` with an identical layout
    // and a different name, and two names for one layout are two types that are not
    // assignable to each other.
    //
    // It is a key, so it has to stay injective in everything codegen distinguishes:
    // width, signedness (`int` and `uint` are one i32 and must not be one instance),
    // bool-ness, and a float against a double.
    //
    // A pointer with no pointee reads as `string`, which is what one nearly always is
    // (the other is a bare `null`, which has no type until it reaches somewhere that
    // has one). The two share a representation exactly, so sharing one instance is
    // sound and only the name would be a small lie -- and the name of an instance is
    // read by the trace and by nothing else.
    static std::string cgDisplay(const CgType& t) {
        switch (t.kind) {
            case CgType::Kind::Void:
                return "void";
            case CgType::Kind::Int:
                if (t.isBool) return "bool";
                switch (t.bits) {
                    case 8: return t.isSigned ? "char" : "byte";
                    case 16: return t.isSigned ? "short" : "ushort";
                    case 32: return t.isSigned ? "int" : "uint";
                    case 64: return t.isSigned ? "long" : "ulong";
                    default: break;
                }
                // No name in scalarByName's table has this width, so this cannot be
                // spelled as a Fin type -- it is still a distinct key, which is what
                // matters, and `int{7}` (bit-width annotations, a unit of its own) is
                // where it would come from.
                return fmt::format("{}{{{}}}", t.isSigned ? "int" : "uint", t.bits);
            case CgType::Kind::Float:
                return t.llvmType && t.llvmType->isFloatTy() ? "float" : "double";
            case CgType::Kind::Ptr:
                return t.pointee ? "&" + cgDisplay(*t.pointee) : "string";
            case CgType::Kind::Struct:
                // The instantiation's own mangled name for a generic one, so
                // `Box<Colour>` stays `Box<Colour>`.
                return t.structInfo ? t.structInfo->finName : "struct";
            case CgType::Kind::Array:
                return t.element ? fmt::format("[{}, {}]", cgDisplay(*t.element), t.extent)
                                 : "[]";
        }
        return "?";
    }

    static std::string mangledName(const std::string& templateName,
                                   const Substitution& substitution) {
        std::string out = templateName + "<";
        for (size_t i = 0; i < substitution.size(); ++i) {
            if (i) out += ", ";
            out += substitution[i].second.display;
        }
        return out + ">";
    }

    // Every module-scope variable, as an llvm::GlobalVariable with a constant
    // initialiser.
    //
    // External linkage, and the name in the object is the name in the source: Fin
    // does not mangle, and tests/samples/variables.fin:9 says a module-scope `let`
    // "can be changed from outside of program" -- which internal linkage would make
    // false. Whether `pub` should narrow that is an open question and is not decided
    // by omission here: `is_public` is not read, so nothing depends on a rule that
    // does not exist yet.
    //
    // Eager, like the structs and enums: a global this file cannot lower fails the
    // build even if nothing reads it. A global quietly skipped is a name that later
    // resolves to nothing, and a read of nothing is not a diagnostic, it is a load
    // from wherever the linker put the next symbol.
    void declareGlobals(Program& program) {
        for (auto& stmt : program.statements) {
            auto* var = dynamic_cast<VariableDeclaration*>(stmt.get());
            if (!var) continue;

            if (!var->attributes.empty()) {
                // `#[slaveof($Fin)]` (variables.fin:35) says "live until the program
                // exits", which a global already does -- but an attribute this file
                // does not read may be one that changes where the variable lives, and
                // ignoring that is how a working program ends up in the wrong section.
                unsupported(*var, fmt::format("an attribute on global '{}'", var->name));
                return;
            }
            if (globals_.count(var->name)) {
                unsupported(*var, fmt::format("a second declaration of global '{}'",
                                              var->name));
                return;
            }

            // `<auto>` takes the initialiser's type, as it does for a local.
            const bool isAuto = var->type && var->type->name == "auto" &&
                                var->type->generics.empty() && !var->type->is_array &&
                                var->type->pointer_depth == 0;
            std::optional<CgType> declared;
            if (!isAuto) {
                declared = types_.map(var->type.get());
                if (!declared) {
                    unsupportedType(*var, var->type.get(), "a global");
                    return;
                }
                if (declared->isVoid()) {
                    unsupported(*var, fmt::format("a global '{}' of type 'void'",
                                                  var->name));
                    return;
                }
            } else if (!var->initializer) {
                unsupported(*var, fmt::format("an '<auto>' global '{}' with no initialiser",
                                              var->name));
                return;
            }

            llvm::Constant* init = nullptr;
            CgType type;
            if (var->initializer) {
                if (!constantInitializer(*var->initializer, declared, init, type)) return;
            } else {
                type = *declared;
                // No initialiser: zero, which is the answer a local with no
                // initialiser gets and also just where the object file puts it (.bss).
                init = llvm::Constant::getNullValue(type.llvmType);
            }

            // isConstant for a `const`, which is what lets a read of one fold into
            // the code that reads it. Sound because the analyzer already refuses
            // assigning one ("Cannot assign to immutable variable"), so the two
            // passes are saying the same thing rather than two things that agree.
            auto* global = new llvm::GlobalVariable(
                module_, type.llvmType, /*isConstant=*/!var->is_mutable,
                llvm::GlobalValue::ExternalLinkage, init, var->name);

            globals_[var->name] = GlobalVar{global, type};
            registeredGlobals_.insert(var);
            debugLog(fmt::format("declared global {}{}", var->is_mutable ? "" : "const ",
                                 var->name));
        }
    }

    // A global's initialiser, folded to a constant.
    //
    // Emitted into a throwaway function so that the ordinary expression path does the
    // work -- literals, an array or struct literal, an enumerator, `sizeof`, a cast,
    // and any arithmetic over those all fold through the IRBuilder's own folder, and
    // this pass does not need a second, smaller constant evaluator that would
    // disagree with the first one somewhere.
    //
    // The block has to come out *empty*. That is the test, and it is stricter than
    // asking whether the result happens to be an llvm::Constant: an instruction left
    // behind is a computation the program asked for, and this function is about to be
    // deleted. `let G <int> = one();` lands there and is refused, because when code
    // before `main` runs -- and in what order against every other module's -- is the
    // initialisation-order rule and not something a lowering pass may decide. C
    // refuses it too.
    bool constantInitializer(Expression& expr, const std::optional<CgType>& declared,
                             llvm::Constant*& out, CgType& outType) {
        auto* holder = llvm::Function::Create(
            llvm::FunctionType::get(llvm::Type::getVoidTy(ctx_), false),
            llvm::GlobalValue::InternalLinkage, "fin.global.init", module_);
        auto* block = llvm::BasicBlock::Create(ctx_, "entry", holder);

        // The emitter state a global initialiser must not see: no frame and no
        // locals. The holder stands in for the current function, so an expression
        // that wants a basic block gets a real one rather than a null dereference --
        // and if it puts anything in it, the emptiness check below refuses.
        FnInfo holderInfo;
        holderInfo.fn = holder;
        holderInfo.returnType = *types_.byName("int");
        FnInfo* savedFn = currentFn_;
        auto savedBlock = builder_.saveIP();
        std::vector<std::unordered_map<std::string, Local>> savedScopes;
        savedScopes.swap(scopes_);
        scopes_.emplace_back();
        currentFn_ = &holderInfo;
        builder_.SetInsertPoint(block);

        CgVal v = declared ? emitAs(expr, *declared) : emit(expr);

        // Any block, not just the entry one: a short-circuit or a ternary would have
        // made more of them, and they are all code this global cannot have.
        const bool emittedCode = holder->size() != 1 || !block->empty();
        builder_.restoreIP(savedBlock);
        scopes_.swap(savedScopes);
        currentFn_ = savedFn;

        llvm::Constant* folded = v.ok() ? llvm::dyn_cast<llvm::Constant>(v.value) : nullptr;
        CgType type = declared ? *declared : v.type;
        llvm::Value* converted = nullptr;
        if (folded && !emittedCode) {
            // Through the same conversion an assignment uses, so an `int` literal
            // reaching a `<double>` global widens here exactly as it would there.
            converted = convert(expr, CgVal{folded, v.type}, type);
        }
        holder->eraseFromParent();

        if (failed_) return false;
        if (!v.ok() || !folded || emittedCode || !converted ||
            !llvm::isa<llvm::Constant>(converted)) {
            unsupported(expr, "a global initialiser that is not a constant");
            return false;
        }
        out = llvm::cast<llvm::Constant>(converted);
        outType = type;
        return true;
    }

    void declareTopLevel(Program& program) {
        for (auto& stmt : program.statements) {
            if (auto* fn = dynamic_cast<FunctionDeclaration*>(stmt.get())) {
                if (!fn->generic_params.empty()) {
                    // A template, which has no signature to declare: `fun ident<T>(a: T)
                    // <T>` names no LLVM type until a call says what T is. Registered
                    // instead, and instantiated per distinct binding at the call.
                    //
                    // `#[llvm_name]` is refused here rather than honoured, and this is
                    // the one place it differs from a struct template. A struct type's
                    // name reaches no object file, so two instantiations asking for one
                    // name are uniqued by LLVM and nothing observable changes. A
                    // function's name *is* its symbol: one name over two instances is
                    // either a duplicate definition or a silent `fin_ident.1` that
                    // nobody can call.
                    if (!fn->attributes.empty()) {
                        unsupported(*fn,
                                    fmt::format("the attribute '{}' on a generic function",
                                                fn->attributes.front()->name));
                        return;
                    }
                    if (hasErasureMarker(*fn, fn->generic_params, "function")) return;
                    if (fn->body != nullptr) fnTemplates_[fn->name] = fn;
                    continue;
                }
                // `#[llvm_name="dealloc"]` on a definition (stdlib/memory.fin:8), read
                // the same way as on an `@define` -- and it matters more here. An
                // extern's name is a promise about someone else's object file; a
                // definition's name is the symbol this object publishes, so ignoring
                // the attribute emits a function nobody can find under the name they
                // were told to call. Every other attribute is refused below.
                if (!attributesAreJustLlvmName(*fn, fn->attributes, "function")) return;
                declareFunction(*fn, fn->name, symbolNameOf(fn->attributes, fn->name),
                                fn->params, fn->return_type.get(),
                                false, /*isExtern=*/false);
            } else if (auto* def = dynamic_cast<DefineDeclaration*>(stmt.get())) {
                // `#[llvm_name="c_printf"]` renames the *symbol* and not the Fin
                // name: stdlib/stdio.fin:11 declares `@define printf` under it, and
                // that is the only mechanism the corpus has for binding an extern to
                // a C symbol whose spelling differs. The Fin name is still what a
                // call site writes, so the two are tracked separately.
                if (!attributesAreJustLlvmName(*def, def->attributes, "'@define'")) return;
                declareFunction(*def, def->name,
                                symbolNameOf(def->attributes, def->name), def->params,
                                def->return_type.get(), def->is_vararg,
                                /*isExtern=*/true);
            }
            if (failed_) return;
        }
    }

    // The symbol a declaration publishes or calls: its `#[llvm_name]` if it has one
    // in the valued form, otherwise its Fin name. Shared by `@define` and `fun`, which
    // is the point -- the two sides of a rename have to agree on what the rename is.
    static std::string symbolNameOf(
            const std::vector<std::unique_ptr<Attribute>>& attributes,
            const std::string& finName) {
        for (auto& attr : attributes) {
            if (attr->name == "llvm_name" && !attr->is_flag) return attr->value_str;
        }
        return finName;
    }

    // Every attribute on `node` is a valued `#[llvm_name]`, which is the one this file
    // reads. Anything else is refused by name: an attribute this file cannot read may
    // be the one that decides linkage (`#[export]`, stdlib/stdio.fin:23) or which of
    // two definitions wins (`#[overwrite(printf)]`, :35), and dropping either produces
    // a program that builds and is wrong. The flag form of `llvm_name` lands here too,
    // because with no value it names nothing.
    bool attributesAreJustLlvmName(ASTNode& node,
            const std::vector<std::unique_ptr<Attribute>>& attributes,
            const char* what) {
        for (auto& attr : attributes) {
            if (attr->name == "llvm_name" && !attr->is_flag) continue;
            unsupported(node, fmt::format("the attribute '{}' on a {}", attr->name, what));
            return false;
        }
        return true;
    }

    void declareFunction(ASTNode& node, const std::string& name,
                         const std::string& symbol,
                         const std::vector<std::unique_ptr<Parameter>>& params,
                         const TypeNode* returnType, bool isVarArg, bool isExtern,
                         const CgType* receiver = nullptr) {
        if (functions_.count(name)) return;  // first declaration wins, as the analyzer's does

        FnInfo info;
        info.isVarArg = isVarArg;
        info.hasReceiver = (receiver != nullptr);

        auto ret = types_.map(returnType);
        if (!ret) { unsupportedType(node, returnType, "a return"); return; }
        info.returnType = *ret;
        // A struct crossing an `@define` boundary is refused in both directions.
        //
        // A Fin-to-Fin call passes a struct as an LLVM aggregate, and caller and
        // callee agree because both are emitted here. A C function does not read it
        // that way: the platform ABI decides per struct whether it arrives in
        // registers, split across two, or as a hidden pointer, and clang implements
        // that classification itself rather than leaving it to LLVM. Emitting the
        // aggregate and calling it C-compatible would link cleanly and pass garbage.
        // Settling it needs the ABI classifier, which is a unit of its own.
        if (isExtern && info.returnType.isAggregate()) {
            unsupported(node, fmt::format("an extern '{}' returning {}", name,
                                          info.returnType.isArray() ? "an array"
                                                                    : "a struct"));
            return;
        }

        std::vector<llvm::Type*> llvmParams;
        // The receiver goes in front of what the source wrote, and the source does not
        // write it at the call either -- see emitCallArgs, which starts the caller's
        // arguments at parameter 1 for exactly this reason.
        if (receiver) {
            info.paramTypes.push_back(*receiver);
            llvmParams.push_back(receiver->llvmType);
        }
        for (auto& p : params) {
            // `...` in `@define printf(fmt: string, ...)` is a Parameter with the
            // vararg flag and no type of its own.
            if (p->is_vararg) { info.isVarArg = true; continue; }
            // A *written* `self` is the receiver that was already pushed, not a second
            // parameter. The analyzer drops it from the signature for the same reason
            // (buildMethodSignature: "The receiver is not a parameter of the call"), so
            // keeping it here would make this file and the analyzer disagree about
            // arity -- and struct_methods.fin writes it both ways, `self: &Self` at :10
            // and nothing at :16, and calls both the same.
            if (receiver && p->name == "self") continue;
            auto t = types_.map(p->type.get());
            if (!t) { unsupportedType(*p, p->type.get(), "a parameter"); return; }
            if (t->isVoid()) { unsupported(*p, "a parameter of type 'void'"); return; }
            if (isExtern && t->isAggregate()) {
                // An array parameter on a C function is a pointer by C's own decay
                // rule, so passing an LLVM [N x T] by value would link cleanly and
                // pass garbage -- the same trap as the struct, one type further along.
                unsupported(*p, fmt::format("{} parameter on extern '{}'",
                                            t->isArray() ? "an array" : "a struct", name));
                return;
            }
            info.paramTypes.push_back(*t);
            llvmParams.push_back(t->llvmType);
        }

        // `main` is the process entry point, so its LLVM signature is the C one
        // whatever Fin wrote. A Fin `main` returning nothing still has to hand the
        // shell a status, and 0 is the only answer a program that reached the end
        // of main can be said to have given.
        llvm::Type* llvmRet = info.returnType.llvmType;
        info.isMain = (name == "main");
        if (info.isMain) {
            if (!info.returnType.isVoid() &&
                !(info.returnType.kind == CgType::Kind::Int && !info.returnType.isBool)) {
                unsupported(node, "a 'main' returning a non-integer");
                return;
            }
            llvmRet = llvm::Type::getInt32Ty(ctx_);
            if (!llvmParams.empty()) {
                unsupported(node, "a 'main' with parameters");
                return;
            }
        }

        auto* fnType = llvm::FunctionType::get(llvmRet, llvmParams, info.isVarArg);
        info.fn = llvm::Function::Create(fnType, llvm::Function::ExternalLinkage, symbol,
                                         &module_);
        functions_[name] = info;
        debugLog("declared " + name);
    }

    // ---- expression helpers ----------------------------------------------

    CgVal emit(Expression& expr) {
        value_ = CgVal{};
        expr.accept(*this);
        return value_;
    }

    // Widens or narrows a value to `target` when the analyzer has already ruled the
    // program well-typed. It converts and never checks: `int` into `long`, an
    // integer constant into a float parameter. A pair it cannot convert is a gap in
    // this slice, not a type error, so it refuses rather than emitting a bitcast.
    llvm::Value* convert(ASTNode& node, const CgVal& from, const CgType& to) {
        if (!from.ok()) return nullptr;
        if (from.type.llvmType == to.llvmType) return from.value;

        if (from.type.kind == CgType::Kind::Int && to.kind == CgType::Kind::Int) {
            if (from.type.bits == to.bits) return from.value;
            if (from.type.bits < to.bits) {
                // A bool is 0 or 1 and widens unsigned whatever the target's
                // signedness; everything else follows its own.
                return from.type.isSigned && !from.type.isBool
                           ? builder_.CreateSExt(from.value, to.llvmType)
                           : builder_.CreateZExt(from.value, to.llvmType);
            }
            return builder_.CreateTrunc(from.value, to.llvmType);
        }
        if (from.type.kind == CgType::Kind::Int && to.kind == CgType::Kind::Float) {
            return from.type.isSigned && !from.type.isBool
                       ? builder_.CreateSIToFP(from.value, to.llvmType)
                       : builder_.CreateUIToFP(from.value, to.llvmType);
        }
        if (from.type.kind == CgType::Kind::Float && to.kind == CgType::Kind::Float) {
            return from.type.llvmType->getPrimitiveSizeInBits() <
                           to.llvmType->getPrimitiveSizeInBits()
                       ? builder_.CreateFPExt(from.value, to.llvmType)
                       : builder_.CreateFPTrunc(from.value, to.llvmType);
        }
        if (from.type.kind == CgType::Kind::Ptr && to.kind == CgType::Kind::Ptr) {
            return from.value;  // opaque pointers: one type, no cast
        }
        unsupported(node, "this conversion");
        return nullptr;
    }

    // The common type of an arithmetic pair. Float wins over int, wider wins over
    // narrower, and a signed/unsigned pair of one width keeps the left's
    // signedness -- provisional, and flagged rather than hidden: the corpus asks
    // for no mixed-signedness arithmetic and the owner ruling on integer
    // conversions will decide it.
    CgType commonType(const CgType& a, const CgType& b) {
        if (a.kind == CgType::Kind::Float || b.kind == CgType::Kind::Float) {
            if (a.kind != CgType::Kind::Float) return b;
            if (b.kind != CgType::Kind::Float) return a;
            return a.llvmType->getPrimitiveSizeInBits() >=
                           b.llvmType->getPrimitiveSizeInBits()
                       ? a
                       : b;
        }
        if (a.isBool && !b.isBool) return b;
        if (b.isBool && !a.isBool) return a;
        return a.bits >= b.bits ? a : b;
    }

    // ---- lvalues ----------------------------------------------------------

    // Where a value lives, for the expressions that have a where.
    struct Addr {
        llvm::Value* ptr = nullptr;
        CgType type;
    };

    // The address of an expression, or nullopt for one that has none.
    //
    // This is the half of struct support that everything else is built on: a field
    // read is a load from here, a field write is a store to here, and `p.a += 1` is
    // both against *one* address computed once. Computing it twice is the same
    // answer for every expression this handles -- a local and a chain of field
    // names, neither of which can have a side effect -- and that is exactly why the
    // recursion stops where it does rather than reaching for a general lvalue.
    //
    // A refusal is not reported here. Not having an address is a normal answer:
    // `make(5).a` is a field of a value that never had one, and reads through
    // extractvalue instead.
    std::optional<Addr> emitAddress(Expression& expr) {
        if (auto* id = dynamic_cast<Identifier*>(&expr)) {
            if (Local* local = findLocal(id->name)) return Addr{local->slot, local->type};
            // A global has an address for the same reasons a local does, and being one
            // is the whole of what makes `Counter = Counter + 1` and `Cells[1] = 42`
            // work at module scope: everything past this point is the same code.
            auto global = globals_.find(id->name);
            if (global != globals_.end())
                return Addr{global->second.var, global->second.type};
            return std::nullopt;
        }
        if (auto* unary = dynamic_cast<UnaryOp*>(&expr)) {
            // The address of `*p` is the value of `p`. That one line is what makes
            // `*a = *b` (deeptest3.fin:10), `*p += 5`, `**pp = 500` (:134),
            // `(*p).field` and `*h.p = 42` all work: each of them is an existing path
            // -- assignment, compound assignment, increment, a field GEP -- against an
            // address, and this is the address they were missing.
            if (unary->op != ASTTokenKind::MULT || unary->is_postfix ||
                !unary->operand) {
                return std::nullopt;
            }
            CgVal p = emit(*unary->operand);
            if (failed_ || !p.ok()) return std::nullopt;
            if (!p.type.isPointer() || !p.type.pointee) return std::nullopt;
            if (p.type.pointee->isVoid() || !p.type.pointee->llvmType ||
                !p.type.pointee->llvmType->isSized()) {
                return std::nullopt;
            }
            return Addr{p.value, *p.type.pointee};
        }
        if (auto* member = dynamic_cast<MemberAccess*>(&expr)) {
            // `Type::name` is an enum member or a static, not a field of an object.
            if (member->is_static) return std::nullopt;
            auto base = baseAddress(*member->object, CgType::Kind::Struct);
            if (!base) return std::nullopt;
            if (!base->type.isStruct() || !base->type.structInfo) return std::nullopt;
            size_t index = 0;
            if (!base->type.structInfo->find(member->member, index)) return std::nullopt;
            // CreateStructGEP indexes by field position, which is why the field
            // order in StructInfo has to be the declaration's.
            llvm::Value* ptr = builder_.CreateStructGEP(base->type.llvmType, base->ptr,
                                                        (unsigned)index,
                                                        member->member);
            return Addr{ptr, base->type.structInfo->fields[index].type};
        }
        if (auto* access = dynamic_cast<ArrayAccess*>(&expr)) {
            // Only through an address. An array that is a *value* -- `make()[0]` --
            // has no home to index into, and unlike a struct field there is no
            // extractvalue with a run-time index: LLVM's extractvalue takes constants
            // only. Returning nullopt sends it to visit(ArrayAccess&), which refuses
            // rather than materialising a temporary the program did not ask for.
            // Through the array's address, or through a pointer to the array --
            // deeptest3.fin:111 says `ptr_to_arr[0]` on a `<&[int, 3]>` is element 0
            // of the array ("The compiler knows to dereference the base first"), and
            // that is the same rule `.` follows one line up.
            //
            // Indexing a pointer to a *non-array* is not this, and baseAddress does not
            // find a base for it: `p[i]` on an `&int` is pointer arithmetic, and whether
            // a Fin pointer strides by an element or a byte is the ruling that also
            // refuses `p++` (see emitIncrement).
            auto base = baseAddress(*access->array, CgType::Kind::Array);
            if (!base) return std::nullopt;
            if (!base->type.isArray() || !base->type.element) return std::nullopt;

            CgVal idx = emit(*access->index);
            if (failed_ || !idx.ok()) return std::nullopt;
            if (idx.type.kind != CgType::Kind::Int) return std::nullopt;

            // Two indices, and the first is the constant 0: the pointer is to the
            // array, so it is stepped over zero whole arrays and then to element i.
            // A single-index GEP on an array pointer strides by the *array*, which
            // is the classic way to land one full array past where the program meant.
            //
            // The index is widened to i64 first. A GEP index narrower than the
            // pointer is sign-extended by LLVM anyway, but doing it here keeps the
            // signedness this file's own -- a Fin `int` is signed, and a `uint`
            // index must not be read as a negative offset.
            llvm::Value* i = idx.type.bits == 64
                                 ? idx.value
                                 : (idx.type.isSigned
                                        ? builder_.CreateSExt(idx.value, builder_.getInt64Ty())
                                        : builder_.CreateZExt(idx.value, builder_.getInt64Ty()));
            llvm::Value* ptr = builder_.CreateInBoundsGEP(
                base->type.llvmType, base->ptr,
                {builder_.getInt64(0), i}, "elem");
            return Addr{ptr, *base->type.element};
        }
        return std::nullopt;
    }

    // The address of what `object` denotes, following one pointer if that is what it
    // takes to get something of kind `want`.
    //
    // deeptest3.fin:39 -- "Access members via pointer (Fin automatically handles ->
    // logic with .)" -- and :111 for the index. There is no `->` in Fin, so a `.` and
    // a `[]` each have two bases to cope with, and the difference between them is one
    // load: a struct's address is the struct, a pointer's address is where the pointer
    // is *kept*, and the struct is at the pointer's value.
    //
    // Only through an address, never by emitting the object as a value. A struct that
    // is a value has no home (`make(5).a` reads through extractvalue instead) and
    // emitting it here would be emitting it twice, once for the address that failed
    // and once for the value that worked.
    std::optional<Addr> baseAddress(Expression& object, CgType::Kind want) {
        auto direct = emitAddress(object);
        if (!direct) return std::nullopt;
        if (direct->type.kind == want) return direct;
        if (direct->type.isPointer() && direct->type.pointee &&
            direct->type.pointee->kind == want) {
            llvm::Value* p = builder_.CreateLoad(direct->type.llvmType, direct->ptr,
                                                 "deref");
            return Addr{p, *direct->type.pointee};
        }
        return std::nullopt;
    }

    // A condition is a truth value whatever it was written as.
    llvm::Value* asCondition(ASTNode& node, const CgVal& v) {
        if (!v.ok()) return nullptr;
        if (v.type.isBool) return v.value;
        if (v.type.kind == CgType::Kind::Int)
            return builder_.CreateICmpNE(v.value,
                                         llvm::ConstantInt::get(v.type.llvmType, 0));
        if (v.type.kind == CgType::Kind::Float)
            return builder_.CreateFCmpONE(v.value,
                                          llvm::ConstantFP::get(v.type.llvmType, 0.0));
        if (v.type.isPointer()) {
            // `if (p)` needs "a pointer is true when it is not null" to be a rule of
            // Fin, and Fin's nullability rules are the open ones. The corpus asks the
            // question the other way every time -- `if (val_ptr == null)`
            // (deeptest3.fin:64) -- which needs no rule and is lowered.
            unsupported(node, "a pointer used as a condition");
            return nullptr;
        }
        unsupported(node, "this condition");
        return nullptr;
    }

    bool terminated() const {
        auto* block = builder_.GetInsertBlock();
        return !block || block->getTerminator() != nullptr;
    }

    // ---- declarations -----------------------------------------------------

    void visit(Program& node) override {
        for (auto& stmt : node.statements) {
            if (failed_) return;
            stmt->accept(*this);
        }
    }

    void visit(FunctionDeclaration& node) override {
        if (node.body == nullptr) return;  // a declaration only; the prototype is enough
        if (!node.generic_params.empty()) {
            // A template, and monomorphisation means a template is not code: one body
            // per distinct binding, emitted where the binding is known. A template
            // nobody calls emits nothing, which is why this is silent rather than a
            // refusal -- it was a refusal, and it fired on generics_interfaces.fin's
            // uncalled `normal_generics`.
            return;
        }
        emitBody(node, node.name);
    }

    // The emitter state one function body owns, saved and put back.
    //
    // An instantiation is emitted from the middle of a call, so the caller is mid-body:
    // `builder_` points into a block that is still being filled, `currentFn_` is the
    // caller's, and `scopes_` holds the caller's locals. The instance's body must see
    // none of those -- a local called `temp` in the caller is not in scope inside the
    // instance -- and every one of them has to be exactly as it was when the call
    // resumes. A destructor rather than three assignments at the end, so that a refusal
    // partway through the body cannot leave the caller's insert point in someone else's
    // function.
    class ScopedEmission {
    public:
        explicit ScopedEmission(Emitter& e)
            : e_(e), block_(e.builder_.GetInsertBlock()),
              point_(block_ ? e.builder_.GetInsertPoint() : llvm::BasicBlock::iterator()),
              fn_(e.currentFn_), scopes_(std::move(e.scopes_)) {
            e_.scopes_.clear();
            e_.currentFn_ = nullptr;
        }
        ~ScopedEmission() {
            e_.scopes_ = std::move(scopes_);
            e_.currentFn_ = fn_;
            if (block_) e_.builder_.SetInsertPoint(block_, point_);
            else e_.builder_.ClearInsertionPoint();
        }
        ScopedEmission(const ScopedEmission&) = delete;
        ScopedEmission& operator=(const ScopedEmission&) = delete;

    private:
        Emitter& e_;
        llvm::BasicBlock* block_;
        llvm::BasicBlock::iterator point_;
        FnInfo* fn_;
        std::vector<std::unordered_map<std::string, Local>> scopes_;
    };

    // One function's body, into the llvm::Function that `name` was declared under.
    //
    // Shared by the ordinary path and by an instantiation, which is the point: an
    // instance is not a special kind of function, it is this function with the type
    // parameters bound, so anything the ordinary path does for a body has to happen
    // for an instance too or the two drift.
    void emitBody(FunctionDeclaration& node, const std::string& name) {
        emitBody(node, node.params, *node.body, name);
    }

    // The same, for a declaration that is not a FunctionDeclaration. See PendingBody
    // for why an operator arrives as three pieces rather than as a node.
    void emitBody(ASTNode& node, const std::vector<std::unique_ptr<Parameter>>& params,
                  Block& body, const std::string& name) {
        auto found = functions_.find(name);
        if (found == functions_.end()) return;  // the refusal was already reported
        const FnInfo info = found->second;

        // An instantiation is emitted from the middle of a call, so the caller's
        // half-built block, its FnInfo and its scopes are all live and have to come
        // back. The ordinary path enters with all three empty, where this is a no-op.
        ScopedEmission resume(*this);

        auto* entry = llvm::BasicBlock::Create(ctx_, "entry", info.fn);
        builder_.SetInsertPoint(entry);

        currentFn_ = &found->second;
        pushScope();

        // The receiver, which the source may not have written and which is a
        // parameter all the same. It gets a slot like any other, so `self.x = v` is the
        // ordinary store-through-a-pointer that emitAddress already knows how to do,
        // and so a method may rebind `self` -- a pointer parameter is assignable.
        size_t index = 0;
        if (info.hasReceiver && !info.paramTypes.empty()) {
            auto* slot = builder_.CreateAlloca(info.paramTypes[0].llvmType, nullptr, "self");
            builder_.CreateStore(info.fn->getArg(0), slot);
            scopes_.back()["self"] = Local{slot, info.paramTypes[0]};
            index = 1;
        }

        // Each parameter gets a stack slot, because a parameter is assignable in
        // Fin and an argument register is not.
        for (auto& p : params) {
            if (p->is_vararg) continue;
            // Written or injected, `self` is the slot above and not a second one.
            if (info.hasReceiver && p->name == "self") continue;
            if (index >= info.paramTypes.size()) break;
            auto* slot = builder_.CreateAlloca(info.paramTypes[index].llvmType, nullptr,
                                               p->name);
            builder_.CreateStore(info.fn->getArg((unsigned)index), slot);
            scopes_.back()[p->name] = Local{slot, info.paramTypes[index]};
            ++index;
        }

        body.accept(*this);

        // The implicit tail. A Fin function that falls off the end returns nothing,
        // except `main`, which owes the shell a status.
        if (!terminated()) {
            if (info.isMain) {
                builder_.CreateRet(builder_.getInt32(0));
            } else if (info.returnType.isVoid()) {
                builder_.CreateRetVoid();
            } else {
                // The analyzer's missing-return check is what makes this
                // unreachable for a well-typed program; `fun?` is its documented
                // exemption and a nullable return is not lowered by this slice, so
                // there is nothing to fall through with. Emitting `unreachable`
                // rather than a zero keeps a hole from looking like a value.
                builder_.CreateUnreachable();
            }
        }

        popScope();
        currentFn_ = nullptr;

        if (llvm::verifyFunction(*info.fn, &llvm::errs())) {
            failed_ = true;
            diag_.reportError(node.loc,
                              fmt::format("codegen: emitted invalid IR for '{}'", name));
        }
    }

    // `ident<int>` -- one instantiation of one function template, built the first time
    // it is asked for and then found.
    //
    // The same three steps as instantiateGeneric's, ordered for the same reason: the
    // bindings have to exist before the signature can be built, and the signature has
    // to be registered before the body is emitted -- `return down(n - 1)` asks for the
    // instantiation it is inside, and finds the name step 2 put there rather than
    // starting a second one that never ends.
    bool instantiateFunction(FunctionDeclaration& tmpl, const Substitution& substitution,
                             const std::string& key) {
        // 1. The bindings, stored before anything is emitted. The TypeMapper holds a
        //    pointer to them for the whole of the signature and the body, and a body
        //    may instantiate further templates into this same map -- which is why the
        //    storage is a member and not a local, and why a node-based map.
        fnInstances_[key] = substitution;
        ScopedBindings bound(types_, &fnInstances_[key]);

        // 2. The signature, under the mangled name. `T` in a parameter or return
        //    position resolves through the bindings, so this is the ordinary path with
        //    the parameters substituted -- including every refusal it has, which is how
        //    an instance whose signature cannot be lowered says so at the call.
        declareFunction(tmpl, key, key, tmpl.params, tmpl.return_type.get(),
                        /*isVarArg=*/false, /*isExtern=*/false);
        auto found = functions_.find(key);
        if (found == functions_.end()) return false;  // declareFunction reported

        // Weak, not external. Two objects that each wrote this template and each
        // instantiated it at the same arguments both publish this symbol, and there is
        // no third place to put it -- neither object knows the other exists. So the C++
        // template bargain: identical bodies, one copy kept, the linker picks. An
        // external definition in each would make the second a duplicate-symbol error,
        // which is a link failure for a program that is correct.
        found->second.fn->setLinkage(llvm::GlobalValue::LinkOnceODRLinkage);

        // 3. The body, with the parameters bound.
        emitBody(tmpl, key);
        return !failed_;
    }

    // One parameter's written type against the argument's actual one, binding whatever
    // type parameters the written type mentions.
    //
    // Structural and one-directional: the written type is the pattern, the CgType is the
    // fact. A bare `T` binds. `&T` requires a pointer and recurses on what it points at,
    // which is what simple_pointers.fin:18 needs -- `swap(&a, &b)` passes a `&int` and
    // `T` is the `int` inside it. `[T, 3]` recurses on the element. `Box<T>` requires
    // that struct and reads `T` off the *instantiation's own* substitution, so
    // `Box<Colour>` binds T to Colour rather than to the `int` the two share.
    //
    // Nothing is checked here. A written `int` against an argument that is a double is
    // the analyzer's business, and the conversion at the call does the widening it
    // permits. First binding wins where a parameter appears twice, which is what the
    // non-generic path does with a declared type: `same<T>(a: T, b: T)` called at
    // (int, double) instantiates at int and converts the second argument.
    void unifyBinding(const TypeNode* pattern, const TypeBinding& actual,
                      const std::vector<std::unique_ptr<GenericParam>>& params,
                      Substitution& out) {
        if (!pattern) return;
        if (auto* ptr = dynamic_cast<const PointerTypeNode*>(pattern)) {
            if (!actual.type.isPointer() || !actual.type.pointee) return;
            const CgType& inner = *actual.type.pointee;
            unifyBinding(ptr->pointee.get(), TypeBinding{inner, cgDisplay(inner)}, params,
                         out);
            return;
        }
        if (auto* arr = dynamic_cast<const ArrayTypeNode*>(pattern)) {
            if (!actual.type.isArray() || !actual.type.element) return;
            const CgType& inner = *actual.type.element;
            unifyBinding(arr->element_type.get(), TypeBinding{inner, cgDisplay(inner)},
                         params, out);
            return;
        }
        if (!pattern->generics.empty()) {
            // The argument is an instantiation and already knows what its own parameters
            // became, so the inner binding is read off it rather than re-derived -- and
            // that carries the display the struct was instantiated under, which is the
            // one a diagnostic and the instance's key both want.
            const StructInfo* info = actual.type.structInfo;
            if (!info || info->substitution.size() != pattern->generics.size()) return;
            for (size_t i = 0; i < pattern->generics.size(); ++i) {
                unifyBinding(pattern->generics[i].get(), info->substitution[i].second,
                             params, out);
            }
            return;
        }
        // An undecorated name: a type parameter's, or a concrete type's. Decorated with
        // anything this file does not lower (`T?`, a prototype, an `implements` list) it
        // binds nothing, and the caller then refuses for the parameter that stayed
        // unbound -- which names the thing that is actually missing.
        if (pattern->pointer_depth != 0 || pattern->is_array || pattern->is_nullable ||
            pattern->is_prototype || !pattern->implements_list.empty() ||
            pattern->array_size || dynamic_cast<const FunctionTypeNode*>(pattern)) {
            return;
        }
        for (auto& p : params) {
            if (p->name != pattern->name) continue;
            for (auto& already : out) {
                if (already.first == p->name) return;
            }
            out.push_back({p->name, actual});
            return;
        }
    }

    // A call to a generic function: resolve the bindings, instantiate, call.
    //
    // The bindings are resolved *here* rather than read off the analyzer, and that is
    // what makes the turbofish work at all -- a free function's turbofish binds nothing
    // in Analyzer_Expr (booked), so a backend that trusted the analyzer's answer would
    // instantiate `ident::<long>(5)` at int.
    void emitGenericCall(FunctionCall& node, FunctionDeclaration& tmpl) {
        for (auto& p : tmpl.params) {
            if (!p->is_vararg) continue;
            // No corpus site, and nothing to infer from: a `...` position has no
            // declared type for a binding to unify against.
            unsupported(*p, fmt::format("'...' on the generic function '{}'", tmpl.name));
            return;
        }
        if (node.args.size() != tmpl.params.size()) {
            // The analyzer already checked arity; reaching here is the two passes
            // disagreeing, so it says so rather than padding.
            unsupported(node,
                        fmt::format("a call to '{}' with {} argument(s) where it declares {}",
                                    tmpl.name, node.args.size(), tmpl.params.size()));
            return;
        }

        Substitution bindings;
        if (!node.generic_args.empty()) {
            // Written. Mapped in the caller's scope, exactly as a struct's type
            // arguments are -- so a `T` written inside another instance resolves through
            // the binding that is already active.
            if (node.generic_args.size() != tmpl.generic_params.size()) {
                unsupported(node,
                            fmt::format("a call to '{}' with {} type argument(s) where it "
                                        "declares {}", tmpl.name, node.generic_args.size(),
                                        tmpl.generic_params.size()));
                return;
            }
            for (size_t i = 0; i < node.generic_args.size(); ++i) {
                const TypeNode* arg = node.generic_args[i].get();
                auto mapped = arg ? types_.map(arg) : std::nullopt;
                if (!mapped || mapped->isVoid() || !mapped->llvmType ||
                    !mapped->llvmType->isSized()) {
                    if (failed_) return;  // a nested instantiation already reported
                    unsupportedType(node, arg,
                                    fmt::format("'{}' at a type argument", tmpl.name));
                    return;
                }
                bindings.push_back({tmpl.generic_params[i]->name,
                                    TypeBinding{*mapped, displayName(arg)}});
            }
        }

        // The arguments, emitted before the instantiation exists. They have no
        // parameter type to be offered, because the parameter's type is what is being
        // inferred *from* them -- so an argument that cannot be typed on its own (an
        // array literal written at a call site) refuses here, and would need the
        // turbofish to fix the binding first. No corpus site writes one.
        //
        // Emitted into the caller's block, which the instantiation below leaves exactly
        // as it found it -- see ScopedEmission.
        std::vector<CgVal> values;
        values.reserve(node.args.size());
        for (auto& arg : node.args) {
            CgVal a = emit(*arg);
            if (failed_) return;
            if (!a.ok()) { unsupported(node, "this argument"); return; }
            values.push_back(a);
        }

        if (node.generic_args.empty()) {
            for (size_t i = 0; i < values.size(); ++i) {
                unifyBinding(tmpl.params[i]->type.get(),
                             TypeBinding{values[i].type, cgDisplay(values[i].type)},
                             tmpl.generic_params, bindings);
            }
        }

        // In declaration order, whatever order inference found them in: the key is built
        // from this list, and `f<A, B>(b: B, a: A)` would otherwise be two names for one
        // instantiation depending on which call site reached it first.
        Substitution ordered;
        for (auto& p : tmpl.generic_params) {
            bool found = false;
            for (auto& b : bindings) {
                if (b.first != p->name) continue;
                ordered.push_back(b);
                found = true;
                break;
            }
            if (found) continue;
            // Nothing to infer it from -- `fun nothing<T>() <int>` mentions T in no
            // parameter. Refused naming the parameter, because the alternative is
            // picking a type, and a function instantiated at a type the program never
            // named is a function the program did not write. `nothing::<int>()` is how
            // this one is called.
            unsupported(node, fmt::format("a call to '{}' whose type argument '{}' no "
                                          "argument mentions", tmpl.name, p->name));
            return;
        }

        const std::string key = mangledName(tmpl.name, ordered);
        if (!functions_.count(key) && !instantiateFunction(tmpl, ordered, key)) return;
        auto instance = functions_.find(key);
        if (instance == functions_.end()) return;  // already reported
        const FnInfo& info = instance->second;
        if (info.paramTypes.size() != values.size()) {
            unsupported(node, fmt::format("a call to '{}' with too few arguments", tmpl.name));
            return;
        }

        std::vector<llvm::Value*> args;
        args.reserve(values.size());
        for (size_t i = 0; i < values.size(); ++i) {
            llvm::Value* converted = convert(node, values[i], info.paramTypes[i]);
            if (!converted) return;
            args.push_back(converted);
        }
        auto* call = builder_.CreateCall(info.fn, args);
        // A void call is a statement, not a value: `value_` staying empty is what makes
        // `let x <int> = voidcall();` refuse rather than store a token.
        value_ = info.returnType.isVoid() ? CgVal{} : CgVal{call, info.returnType};
    }

    void visit(DefineDeclaration& node) override { (void)node; }  // prototype only

    void visit(VariableDeclaration& node) override {
        if (registeredGlobals_.count(&node)) return;  // declareGlobals did it
        // A local's attributes, which a global's have always been refused and a local's
        // were not. `#[slaveof(z)]` (variables.fin:27) says the storage lives as long
        // as another variable does, and the sample's own comment is explicit that this
        // is about lifetime -- so it is a rule about generated code, and dropping it
        // produces a program that frees too early and runs anyway.
        for (auto& attr : node.attributes) {
            unsupported(node, fmt::format("the attribute '{}' on the variable '{}'",
                                          attr->name, node.name));
            return;
        }
        if (!currentFn_) {
            unsupported(node, fmt::format("the variable '{}' declared here", node.name));
            return;
        }

        std::optional<CgType> declared;
        // `<auto>` takes the initialiser's type, which is the only inference this
        // slice does and is why it is done here rather than in TypeMapper.
        const bool isAuto = node.type && node.type->name == "auto" &&
                            node.type->generics.empty() && !node.type->is_array &&
                            node.type->pointer_depth == 0;
        if (!isAuto) {
            declared = types_.map(node.type.get());
            if (!declared) { unsupportedType(node, node.type.get(), "a variable"); return; }
            if (declared->isVoid()) { unsupported(node, "a variable of type 'void'"); return; }
        }

        CgVal init;
        if (node.initializer) {
            init = declared ? emitAs(*node.initializer, *declared) : emit(*node.initializer);
            if (failed_) return;
            if (!init.ok()) { unsupported(node, "this initialiser"); return; }
        } else if (isAuto) {
            unsupported(node, "an '<auto>' variable with no initialiser");
            return;
        }

        CgType type = declared ? *declared : init.type;
        if (type.isVoid()) { unsupported(node, "a variable of type 'void'"); return; }

        auto* slot = builder_.CreateAlloca(type.llvmType, nullptr, node.name);
        if (init.ok()) {
            llvm::Value* stored = convert(node, init, type);
            if (!stored) return;
            builder_.CreateStore(stored, slot);
        } else {
            // No initialiser: zeroed rather than left as whatever the stack held.
            // Fin has not ruled on whether an uninitialised local is readable, and
            // undefined stack contents is the one answer that cannot be tested.
            builder_.CreateStore(llvm::Constant::getNullValue(type.llvmType), slot);
        }
        scopes_.back()[node.name] = Local{slot, type};
    }

    // ---- statements -------------------------------------------------------

    void visit(Block& node) override {
        pushScope();
        for (auto& stmt : node.statements) {
            if (failed_) break;
            // Everything after a `return` in the same block is dead. Emitting into
            // a terminated block is an LLVM error, and inventing a fresh block for
            // code the program cannot reach would only hide it.
            if (terminated()) break;
            stmt->accept(*this);
        }
        popScope();
    }

    void visit(ReturnStatement& node) override {
        if (!currentFn_) { unsupported(node, "a return outside a function"); return; }
        const bool isMain = currentFn_->isMain;

        if (!node.value) {
            if (isMain) builder_.CreateRet(builder_.getInt32(0));
            else if (currentFn_->returnType.isVoid()) builder_.CreateRetVoid();
            else unsupported(node, "a bare 'return' from a function with a return type");
            return;
        }

        CgVal v = emitAs(*node.value, currentFn_->returnType);
        if (failed_) return;
        if (!v.ok()) { unsupported(node, "this returned expression"); return; }

        CgType target = currentFn_->returnType;
        if (isMain) {
            CgType i32 = types_.intType(32, true);
            llvm::Value* status = convert(node, v, i32);
            if (status) builder_.CreateRet(status);
            return;
        }
        if (target.isVoid()) { unsupported(node, "a 'return <value>' from a void function"); return; }
        llvm::Value* out = convert(node, v, target);
        if (out) builder_.CreateRet(out);
    }

    void visit(ExpressionStatement& node) override {
        if (!node.expr) return;
        emit(*node.expr);
    }

    void visit(IfStatement& node) override {
        if (!currentFn_) { unsupported(node, "an 'if' outside a function"); return; }
        CgVal cond = emit(*node.condition);
        if (failed_) return;
        llvm::Value* test = asCondition(node, cond);
        if (!test) return;

        auto* thenBB = llvm::BasicBlock::Create(ctx_, "if.then", currentFn_->fn);
        auto* elseBB = llvm::BasicBlock::Create(ctx_, "if.else", currentFn_->fn);
        auto* mergeBB = llvm::BasicBlock::Create(ctx_, "if.end", currentFn_->fn);
        builder_.CreateCondBr(test, thenBB, elseBB);

        builder_.SetInsertPoint(thenBB);
        if (node.then_block) node.then_block->accept(*this);
        if (!terminated()) builder_.CreateBr(mergeBB);

        builder_.SetInsertPoint(elseBB);
        if (node.else_stmt) node.else_stmt->accept(*this);
        if (!terminated()) builder_.CreateBr(mergeBB);

        builder_.SetInsertPoint(mergeBB);
        // Both arms returned, so nothing reaches here. The block still has to be
        // terminated or the function is invalid IR.
        if (mergeBB->hasNPredecessors(0)) builder_.CreateUnreachable();
    }

    void visit(WhileLoop& node) override {
        if (!currentFn_) { unsupported(node, "a 'while' outside a function"); return; }

        auto* condBB = llvm::BasicBlock::Create(ctx_, "while.cond", currentFn_->fn);
        auto* bodyBB = llvm::BasicBlock::Create(ctx_, "while.body", currentFn_->fn);
        auto* endBB = llvm::BasicBlock::Create(ctx_, "while.end", currentFn_->fn);

        // `do { } while (c)` runs the body first, which is the whole difference
        // between the two spellings and the only thing this flag means.
        builder_.CreateBr(node.is_do_while ? bodyBB : condBB);

        builder_.SetInsertPoint(condBB);
        CgVal cond = emit(*node.condition);
        if (failed_) return;
        llvm::Value* test = asCondition(node, cond);
        if (!test) return;
        builder_.CreateCondBr(test, bodyBB, endBB);

        loops_.push_back({condBB, endBB});
        builder_.SetInsertPoint(bodyBB);
        if (node.body) node.body->accept(*this);
        if (!terminated()) builder_.CreateBr(condBB);
        loops_.pop_back();

        builder_.SetInsertPoint(endBB);
    }

    void visit(ForLoop& node) override {
        if (!currentFn_) { unsupported(node, "a 'for' outside a function"); return; }

        pushScope();
        if (node.init) node.init->accept(*this);
        if (failed_) { popScope(); return; }

        auto* condBB = llvm::BasicBlock::Create(ctx_, "for.cond", currentFn_->fn);
        auto* bodyBB = llvm::BasicBlock::Create(ctx_, "for.body", currentFn_->fn);
        auto* stepBB = llvm::BasicBlock::Create(ctx_, "for.step", currentFn_->fn);
        auto* endBB = llvm::BasicBlock::Create(ctx_, "for.end", currentFn_->fn);

        builder_.CreateBr(condBB);
        builder_.SetInsertPoint(condBB);
        if (node.condition) {
            CgVal cond = emit(*node.condition);
            if (failed_) { popScope(); return; }
            llvm::Value* test = asCondition(node, cond);
            if (!test) { popScope(); return; }
            builder_.CreateCondBr(test, bodyBB, endBB);
        } else {
            builder_.CreateBr(bodyBB);
        }

        // `continue` goes to the step and not to the condition: skipping the
        // increment is an infinite loop, which is the classic way to get this
        // wrong.
        loops_.push_back({stepBB, endBB});
        builder_.SetInsertPoint(bodyBB);
        if (node.body) node.body->accept(*this);
        if (!terminated()) builder_.CreateBr(stepBB);
        loops_.pop_back();

        builder_.SetInsertPoint(stepBB);
        if (node.increment) emit(*node.increment);
        if (!terminated()) builder_.CreateBr(condBB);

        builder_.SetInsertPoint(endBB);
        popScope();
    }

    void visit(BreakStatement& node) override {
        if (loops_.empty()) { unsupported(node, "a 'break' outside a loop"); return; }
        builder_.CreateBr(loops_.back().breakTo);
    }

    void visit(ContinueStatement& node) override {
        if (loops_.empty()) { unsupported(node, "a 'continue' outside a loop"); return; }
        builder_.CreateBr(loops_.back().continueTo);
    }

    // ---- expressions ------------------------------------------------------

    void visit(Literal& node) override {
        switch (node.kind) {
            case ASTTokenKind::INTEGER: {
                // `int` unless the value does not fit, which is the same reading the
                // analyzer gives a bare literal.
                long long n = 0;
                try {
                    n = std::stoll(node.value);
                } catch (const std::exception&) {
                    unsupported(node, fmt::format("the integer literal '{}'", node.value));
                    return;
                }
                CgType t = types_.intType(
                    (n > 2147483647LL || n < -2147483648LL) ? 64 : 32, true);
                value_ = CgVal{llvm::ConstantInt::getSigned(t.llvmType, n), t};
                return;
            }
            case ASTTokenKind::FLOAT: {
                CgType t = types_.floatType(llvm::Type::getDoubleTy(ctx_));
                value_ = CgVal{llvm::ConstantFP::get(t.llvmType, std::stod(node.value)), t};
                return;
            }
            case ASTTokenKind::BOOL: {
                CgType t = *types_.byName("bool");
                value_ = CgVal{llvm::ConstantInt::get(t.llvmType, node.value == "true" ? 1 : 0),
                               t};
                return;
            }
            case ASTTokenKind::CHAR_LITERAL: {
                std::string decoded = decodeLiteral(node.value);
                CgType t = types_.intType(8, true);
                value_ = CgVal{
                    llvm::ConstantInt::get(t.llvmType, decoded.empty() ? 0 : (unsigned char)decoded[0]),
                    t};
                return;
            }
            case ASTTokenKind::STRING_LITERAL: {
                CgType t = *types_.byName("string");
                value_ = CgVal{builder_.CreateGlobalStringPtr(decodeLiteral(node.value)), t};
                return;
            }
            case ASTTokenKind::KW_NULL:
                // One word of zeroes, and no pointee: `print_if_exists(null)`
                // (deeptest3.fin:75) is a `null` with no declared type anywhere near
                // it, so there is nothing here to be a pointer *to*. It does not need
                // one -- convert() makes a pointer-to-pointer conversion a no-op
                // because there is only one pointer type in the IR -- and it must not
                // invent one, because `*null` would then have a width.
                //
                // A constant, so a global initialiser folds to it and emits no code
                // (`let GP <&int> = null;`).
                value_ = CgVal{llvm::ConstantPointerNull::get(
                                   llvm::PointerType::getUnqual(ctx_)),
                               types_.pointerType()};
                return;
            case ASTTokenKind::M1778:
                // ADR 0001: the word means "not implemented", so a build that
                // reaches one is a build of an unfinished program.
                unsupported(node, "'m1778'");
                return;
            default:
                unsupported(node, "this literal");
                return;
        }
    }

    void visit(Identifier& node) override {
        if (Local* local = findLocal(node.name)) {
            value_ = CgVal{builder_.CreateLoad(local->type.llvmType, local->slot, node.name),
                           local->type};
            return;
        }
        // Then the globals, which are the same kind of thing as a local with a
        // different home -- and after them for the same reason: a local of the name
        // shadows one (ALocalOutranksAGlobalOfTheSameName).
        auto global = globals_.find(node.name);
        if (global != globals_.end()) {
            value_ = CgVal{builder_.CreateLoad(global->second.type.llvmType,
                                               global->second.var, node.name),
                           global->second.type};
            return;
        }
        // After the locals and not before: a local of the same name shadows the
        // enumerator, because that is the scope the analyzer resolved it in
        // (Soundness_Codegen.ALocalOutranksAnEnumMemberOfTheSameName).
        auto member = enumMembers_.find(node.name);
        if (member != enumMembers_.end()) {
            value_ = enumConstant(member->second.value);
            return;
        }
        // A function named as a value needs a decision about what a Fin function
        // value *is* (a bare pointer, or a closure pair), so it refuses rather
        // than picking one.
        if (functions_.count(node.name)) {
            unsupported(node, fmt::format("the function '{}' used as a value", node.name));
            return;
        }
        unsupported(node, fmt::format("the name '{}'", node.name));
    }

    void visit(BinaryOp& node) override {
        switch (node.op) {
            case ASTTokenKind::EQUAL:
            case ASTTokenKind::PLUSEQUAL:
            case ASTTokenKind::MINUSEQUAL:
            case ASTTokenKind::MULTEQUAL:
            case ASTTokenKind::DIVEQUAL:
            case ASTTokenKind::MODEQUAL:
            case ASTTokenKind::AMPERSANDEQUAL:
            case ASTTokenKind::PIPEEQUAL:
            case ASTTokenKind::SHIFTLEFTEQUAL:
            case ASTTokenKind::SHIFTRIGHTEQUAL:
                emitAssignment(node);
                return;
            case ASTTokenKind::AND:
            case ASTTokenKind::OR:
                emitShortCircuit(node);
                return;
            default:
                break;
        }

        CgVal lhs = emit(*node.left);
        if (failed_) return;

        // A declared operator, if the left operand is a struct.
        //
        // The left operand and not either one: `v + 1` looks on V, and `1 + v` does not
        // look at all -- the analyzer refuses that outright ("Type mismatch: expected
        // 'int', got 'V'"), so there is no second rule to write here.
        //
        // Gated on the operand's type rather than on the program having declared an
        // operator with this token, which is the stronger of the two guarantees and the
        // cheaper: a scalar `1 + 2` reaches the same code it reached before this
        // existed, in a program that declares operators as much as in one that does not.
        // (It is also why the operand is emitted *before* the lookup -- the ordinary
        // path needs that value, and computing an address first for every `+` in the
        // program would emit a dead one for each.)
        if (lhs.type.isStruct()) { emitStructOperator(node, lhs); return; }

        CgVal rhs = emit(*node.right);
        if (failed_) return;
        if (!lhs.ok() || !rhs.ok()) { unsupported(node, "this operand"); return; }
        value_ = emitArithmetic(node, node.op, lhs, rhs);
    }

    // `v1 + v2` -- a call to `V.operator+` with the left operand as the receiver.
    //
    // Never falls back: a struct on the left of an operator is either a declared
    // operator or a refusal, because the built-in path has nothing to do with a struct
    // (commonType compares bit widths and a struct has none) and inventing a field-wise
    // meaning for `==` is a ruling nobody has made.
    void emitStructOperator(BinaryOp& node, const CgVal& lhs) {
        const StructInfo* owner = lhs.type.structInfo;
        const std::string spelling = spellOperator(node.op);
        if (!owner || spelling.empty()) {
            // An anonymous struct type, or a token with no spelling. Neither can be
            // looked up, and the old wording is still the right one.
            unsupported(node, "an operator on a struct");
            return;
        }
        if (!findOperator(*owner, node.op)) {
            unsupported(node, fmt::format("an undeclared operator '{}' on struct '{}'",
                                          spelling, owner->finName));
            return;
        }
        const std::string key = operatorKey(owner->finName, node.op);
        auto found = functions_.find(key);
        if (found == functions_.end()) {
            reportMissingOperator(node, *owner, node.op);
            return;
        }
        const FnInfo& info = found->second;

        // The receiver is the left operand's *address*, and not the value emitted a
        // moment ago. `operator +` may assign through `self` -- and one in the corpus
        // reads `self.val`, which is a load through the same pointer -- so a receiver
        // spilled to a temporary would be a program whose operator silently writes into
        // a copy. That is also why a left operand with no address refuses instead:
        // `make() + v` has nothing to be `self`, and materialising one would be
        // inventing the object.
        auto receiver = baseAddress(*node.left, CgType::Kind::Struct);
        if (failed_) return;
        if (!receiver) {
            unsupported(node, fmt::format("an operator '{}' on a left operand with no "
                                          "address", spelling));
            return;
        }

        std::vector<llvm::Value*> args{receiver->ptr};
        if (!emitCallArgs(node, info, key, {node.right.get()}, args)) return;
        emitCall(info, args);
    }

    CgVal emitArithmetic(ASTNode& node, ASTTokenKind op, CgVal lhs, CgVal rhs) {
        // An aggregate operand is refused before anything else looks at it. Not for
        // tidiness: commonType compares bit widths, a struct has none, so it would
        // return one of the two and hand a struct to CreateAdd -- which is an
        // assertion inside LLVM, reported as a compiler crash rather than as the
        // unlowered operator it is. Whether `a == b` on two structs compares
        // field-wise is a ruling nobody has made.
        if (lhs.type.isStruct() || rhs.type.isStruct()) {
            unsupported(node, "an operator on a struct");
            return CgVal{};
        }

        // A pointer operand, for the same reason and with a narrower exit: equality
        // against another pointer is two words compared, which needs no rule and is
        // what deeptest3.fin:64 writes. Everything else does need one.
        //
        // `p + 1` never arrives -- the analyzer refuses it ("Type mismatch: expected
        // '&int', got 'int'") -- but `p < q` does, and an ordering is a claim about
        // which of two objects the allocator put first. commonType would hand both to
        // CreateICmpSLT after picking one of the two zero-width types, which is an
        // answer; refusing is not.
        if (lhs.type.isPointer() || rhs.type.isPointer()) {
            const bool comparison = op == ASTTokenKind::EQEQ || op == ASTTokenKind::NOTEQ;
            if (!comparison || !lhs.type.isPointer() || !rhs.type.isPointer()) {
                unsupported(node, "an operator on a pointer");
                return CgVal{};
            }
            // No convert: there is one pointer type in the IR, so a `&int` and a bare
            // `null` are already the same operand type.
            CgType boolType = *types_.byName("bool");
            llvm::Value* out = op == ASTTokenKind::EQEQ
                                   ? builder_.CreateICmpEQ(lhs.value, rhs.value)
                                   : builder_.CreateICmpNE(lhs.value, rhs.value);
            return CgVal{out, boolType};
        }

        // A shift's operands are not a pair: the count is not widened to the value's
        // type, it is truncated or extended to it, and mixing them through
        // commonType would silently widen the value.
        if (op == ASTTokenKind::SHIFTLEFT || op == ASTTokenKind::SHIFTRIGHT) {
            if (lhs.type.kind != CgType::Kind::Int || rhs.type.kind != CgType::Kind::Int) {
                unsupported(node, "a shift of a non-integer");
                return CgVal{};
            }
            llvm::Value* count = convert(node, rhs, lhs.type);
            if (!count) return CgVal{};
            llvm::Value* out = op == ASTTokenKind::SHIFTLEFT
                                   ? builder_.CreateShl(lhs.value, count)
                                   : (lhs.type.isSigned
                                          ? builder_.CreateAShr(lhs.value, count)
                                          : builder_.CreateLShr(lhs.value, count));
            return CgVal{out, lhs.type};
        }

        CgType common = commonType(lhs.type, rhs.type);
        llvm::Value* l = convert(node, lhs, common);
        llvm::Value* r = convert(node, rhs, common);
        if (!l || !r) return CgVal{};

        const bool fp = common.kind == CgType::Kind::Float;
        CgType boolType = *types_.byName("bool");

        switch (op) {
            case ASTTokenKind::PLUS:
                return CgVal{fp ? builder_.CreateFAdd(l, r) : builder_.CreateAdd(l, r), common};
            case ASTTokenKind::MINUS:
                return CgVal{fp ? builder_.CreateFSub(l, r) : builder_.CreateSub(l, r), common};
            case ASTTokenKind::MULT:
                return CgVal{fp ? builder_.CreateFMul(l, r) : builder_.CreateMul(l, r), common};
            case ASTTokenKind::DIV:
                return CgVal{fp ? builder_.CreateFDiv(l, r)
                                : (common.isSigned ? builder_.CreateSDiv(l, r)
                                                   : builder_.CreateUDiv(l, r)),
                             common};
            case ASTTokenKind::MOD:
                return CgVal{fp ? builder_.CreateFRem(l, r)
                                : (common.isSigned ? builder_.CreateSRem(l, r)
                                                   : builder_.CreateURem(l, r)),
                             common};
            case ASTTokenKind::AMPERSAND:
                if (fp) break;
                return CgVal{builder_.CreateAnd(l, r), common};
            case ASTTokenKind::PIPE:
                if (fp) break;
                return CgVal{builder_.CreateOr(l, r), common};
            case ASTTokenKind::CARET:
                if (fp) break;
                return CgVal{builder_.CreateXor(l, r), common};
            case ASTTokenKind::EQEQ:
                return CgVal{fp ? builder_.CreateFCmpOEQ(l, r) : builder_.CreateICmpEQ(l, r),
                             boolType};
            case ASTTokenKind::NOTEQ:
                return CgVal{fp ? builder_.CreateFCmpONE(l, r) : builder_.CreateICmpNE(l, r),
                             boolType};
            case ASTTokenKind::LT:
                return CgVal{fp ? builder_.CreateFCmpOLT(l, r)
                                : (common.isSigned ? builder_.CreateICmpSLT(l, r)
                                                   : builder_.CreateICmpULT(l, r)),
                             boolType};
            case ASTTokenKind::LTEQ:
                return CgVal{fp ? builder_.CreateFCmpOLE(l, r)
                                : (common.isSigned ? builder_.CreateICmpSLE(l, r)
                                                   : builder_.CreateICmpULE(l, r)),
                             boolType};
            case ASTTokenKind::GT:
                return CgVal{fp ? builder_.CreateFCmpOGT(l, r)
                                : (common.isSigned ? builder_.CreateICmpSGT(l, r)
                                                   : builder_.CreateICmpUGT(l, r)),
                             boolType};
            case ASTTokenKind::GTEQ:
                return CgVal{fp ? builder_.CreateFCmpOGE(l, r)
                                : (common.isSigned ? builder_.CreateICmpSGE(l, r)
                                                   : builder_.CreateICmpUGE(l, r)),
                             boolType};
            default:
                break;
        }
        unsupported(node, "this operator");
        return CgVal{};
    }

    // `&&` and `||` are the two operators whose right side may not run. Lowering
    // them as a plain `and` of both operands compiles, links, and is a different
    // program (Soundness_Codegen.LogicalAndShortCircuits).
    void emitShortCircuit(BinaryOp& node) {
        if (!currentFn_) { unsupported(node, "this operator outside a function"); return; }
        const bool isAnd = node.op == ASTTokenKind::AND;

        CgVal lhs = emit(*node.left);
        if (failed_) return;
        llvm::Value* l = asCondition(node, lhs);
        if (!l) return;

        auto* rhsBB = llvm::BasicBlock::Create(ctx_, isAnd ? "and.rhs" : "or.rhs",
                                               currentFn_->fn);
        auto* endBB = llvm::BasicBlock::Create(ctx_, isAnd ? "and.end" : "or.end",
                                               currentFn_->fn);
        auto* entryBB = builder_.GetInsertBlock();
        if (isAnd) builder_.CreateCondBr(l, rhsBB, endBB);
        else       builder_.CreateCondBr(l, endBB, rhsBB);

        builder_.SetInsertPoint(rhsBB);
        CgVal rhs = emit(*node.right);
        if (failed_) return;
        llvm::Value* r = asCondition(node, rhs);
        if (!r) return;
        auto* rhsExit = builder_.GetInsertBlock();
        builder_.CreateBr(endBB);

        builder_.SetInsertPoint(endBB);
        CgType boolType = *types_.byName("bool");
        auto* phi = builder_.CreatePHI(boolType.llvmType, 2);
        phi->addIncoming(builder_.getInt1(!isAnd), entryBB);
        phi->addIncoming(r, rhsExit);
        value_ = CgVal{phi, boolType};
    }

    void emitAssignment(BinaryOp& node) {
        // One address, used by both halves of a compound assignment. An index or a
        // dereference on the left still has none -- those are their own units -- but
        // a local and any chain of field names off one now do.
        auto target = emitAddress(*node.left);
        if (failed_) return;
        if (!target) {
            unsupported(node, "an assignment to this target");
            return;
        }

        CgVal rhs = emit(*node.right);
        if (failed_) return;
        if (!rhs.ok()) { unsupported(node, "this assigned expression"); return; }

        if (node.op != ASTTokenKind::EQUAL) {
            // `x += e` is `x = x + e`, read once and written once.
            static const std::unordered_map<int, ASTTokenKind> kUnderlying = {
                {(int)ASTTokenKind::PLUSEQUAL, ASTTokenKind::PLUS},
                {(int)ASTTokenKind::MINUSEQUAL, ASTTokenKind::MINUS},
                {(int)ASTTokenKind::MULTEQUAL, ASTTokenKind::MULT},
                {(int)ASTTokenKind::DIVEQUAL, ASTTokenKind::DIV},
                {(int)ASTTokenKind::MODEQUAL, ASTTokenKind::MOD},
                {(int)ASTTokenKind::AMPERSANDEQUAL, ASTTokenKind::AMPERSAND},
                {(int)ASTTokenKind::PIPEEQUAL, ASTTokenKind::PIPE},
                {(int)ASTTokenKind::SHIFTLEFTEQUAL, ASTTokenKind::SHIFTLEFT},
                {(int)ASTTokenKind::SHIFTRIGHTEQUAL, ASTTokenKind::SHIFTRIGHT},
            };
            auto found = kUnderlying.find((int)node.op);
            if (found == kUnderlying.end()) { unsupported(node, "this assignment"); return; }
            CgVal current{builder_.CreateLoad(target->type.llvmType, target->ptr),
                          target->type};
            rhs = emitArithmetic(node, found->second, current, rhs);
            if (!rhs.ok()) return;
        }

        llvm::Value* stored = convert(node, rhs, target->type);
        if (!stored) return;
        builder_.CreateStore(stored, target->ptr);
        // The assignment's value is the value stored, so `let a <int> = (b = 1);`
        // would work if the grammar admitted it.
        value_ = CgVal{stored, target->type};
    }

    // `i++`, `++i`, `i--`, `--i`. An assignment that reads its own target, which is
    // why it lives beside emitAssignment and shares its address path: one address,
    // computed once, loaded and stored through.
    //
    // The only difference between the four spellings is which value the *expression*
    // has -- the old one for postfix, the new one for prefix -- and until the AST
    // recorded is_postfix there was no way to tell, which is why this used to refuse
    // outright. In statement position they are the same instruction sequence, and
    // every increment in the corpus is a statement or a `for` step, so nothing would
    // have caught a guess.
    void emitIncrement(UnaryOp& node) {
        auto target = emitAddress(*node.operand);
        if (failed_) return;
        if (!target) { unsupported(node, "'++' on a target with no address"); return; }

        const CgType& t = target->type;
        if (t.isStruct()) { unsupported(node, "'++' on a struct"); return; }
        if (t.isBool) { unsupported(node, "'++' on a bool"); return; }
        if (t.kind == CgType::Kind::Ptr) {
            // Whether this advances by one element or one byte is an owner ruling.
            // Emitting either one would be an out-of-bounds access in the program that
            // wanted the other, with nothing to report it.
            unsupported(node, "'++' on a pointer");
            return;
        }
        if (t.kind != CgType::Kind::Int && t.kind != CgType::Kind::Float) {
            unsupported(node, "'++' on this type");
            return;
        }

        llvm::Value* before = builder_.CreateLoad(t.llvmType, target->ptr);
        const bool up = node.op == ASTTokenKind::INCREMENT;
        llvm::Value* after = nullptr;
        if (t.kind == CgType::Kind::Float) {
            llvm::Value* one = llvm::ConstantFP::get(t.llvmType, 1.0);
            after = up ? builder_.CreateFAdd(before, one) : builder_.CreateFSub(before, one);
        } else {
            llvm::Value* one = llvm::ConstantInt::get(t.llvmType, 1);
            // nsw/nuw are deliberately not set. Whether signed overflow here is
            // undefined is an owner ruling, and marking it nsw would let LLVM assume
            // a loop counter cannot wrap -- a real transformation on a real program,
            // decided by an omission rather than by anyone.
            after = up ? builder_.CreateAdd(before, one) : builder_.CreateSub(before, one);
        }
        builder_.CreateStore(after, target->ptr);
        value_ = CgVal{node.is_postfix ? before : after, t};
    }

    // `&x`. The address of a thing that has one, which is emitAddress' whole job --
    // so this is four lines and every form the corpus writes (`&x`, `&numbers[1]`,
    // `&my_array`, `&p`, `&G`, `&self.field`) comes from the one place that already
    // knew how.
    void emitAddressOf(UnaryOp& node) {
        auto addr = emitAddress(*node.operand);
        if (failed_) return;
        if (!addr) {
            // `&make()` and `&"Hello world"` (variables.fin:11). The value is real and
            // has no home, so taking its address means putting it in a fresh slot --
            // which answers "how long does that slot live, and what does the pointer
            // mean afterwards" by picking one. Nothing in the corpus reads such a
            // pointer, so nothing would catch the wrong pick.
            unsupported(node, "the address of a value with no home");
            return;
        }
        value_ = CgVal{addr->ptr, types_.pointerTo(addr->type)};
    }

    void visit(UnaryOp& node) override {
        if (node.op == ASTTokenKind::INCREMENT || node.op == ASTTokenKind::DECREMENT) {
            emitIncrement(node);
            return;
        }
        // Before the operand is emitted, because this one does not want its value.
        if (node.op == ASTTokenKind::AMPERSAND) { emitAddressOf(node); return; }
        CgVal v = emit(*node.operand);
        if (failed_) return;
        if (!v.ok()) { unsupported(node, "this operand"); return; }

        switch (node.op) {
            case ASTTokenKind::MINUS:
            case ASTTokenKind::UNARY_MINUS:
                if (v.type.kind == CgType::Kind::Float) {
                    value_ = CgVal{builder_.CreateFNeg(v.value), v.type};
                    return;
                }
                if (v.type.kind == CgType::Kind::Int && !v.type.isBool) {
                    value_ = CgVal{builder_.CreateNeg(v.value), v.type};
                    return;
                }
                break;
            case ASTTokenKind::NOT: {
                llvm::Value* test = asCondition(node, v);
                if (!test) return;
                CgType boolType = *types_.byName("bool");
                value_ = CgVal{builder_.CreateNot(test), boolType};
                return;
            }
            case ASTTokenKind::TILDE:
                if (v.type.kind == CgType::Kind::Int) {
                    value_ = CgVal{builder_.CreateNot(v.value), v.type};
                    return;
                }
                break;
            case ASTTokenKind::PLUS:
                value_ = v;
                return;
            case ASTTokenKind::MULT: {
                // `*p` as a value: the load. `*p` as a *target* never reaches here --
                // emitAddress has its own branch for it, so `*p = 99` stores through
                // the same pointer this would read through, and `*p += 5` does both
                // against one address.
                if (!v.type.isPointer()) break;
                if (!v.type.pointee || v.type.pointee->isVoid() ||
                    !v.type.pointee->llvmType || !v.type.pointee->llvmType->isSized()) {
                    // A `string`, a bare `null`, or an `&void`. The width of the load
                    // is the pointee's and there is no pointee, so there is no load to
                    // emit -- and reading a byte because a byte is the smallest thing
                    // it could be would be a guess with a result.
                    unsupported(node, "a dereference of a pointer to no particular type");
                    return;
                }
                value_ = CgVal{builder_.CreateLoad(v.type.pointee->llvmType, v.value,
                                                   "deref"),
                               *v.type.pointee};
                return;
            }
            default:
                break;
        }
        unsupported(node, "this unary operator");
    }

    void visit(FunctionCall& node) override {
        if (node.is_special) {
            // A `@special` runs at compile time (wave 4). Reaching codegen means
            // nothing consumed it, and lowering it as an ordinary call would emit a
            // call to a symbol no object file contains.
            unsupported(node, fmt::format("the compile-time call '@{}'", node.name));
            return;
        }
        // A local of function type shadows nothing today -- a function value is not
        // lowered -- but checking locals first is the order the analyzer uses and
        // the order that stays correct when they are.
        if (findLocal(node.name)) {
            unsupported(node, fmt::format("a call through the variable '{}'", node.name));
            return;
        }
        // Before the ordinary lookup, because a template is deliberately not in
        // functions_: it has no signature until this call says what its parameters are.
        auto tmpl = fnTemplates_.find(node.name);
        if (tmpl != fnTemplates_.end()) {
            emitGenericCall(node, *tmpl->second);
            return;
        }
        if (!node.generic_args.empty()) {
            // A turbofish on something that declares no type parameters. Read by
            // emitGenericCall for a template and by nobody here, so it is refused rather
            // than dropped -- a written type argument that changed nothing would be a
            // silent disagreement with whatever the writer expected it to change.
            unsupported(node, "a call with explicit generic arguments");
            return;
        }
        auto found = functions_.find(node.name);
        if (found == functions_.end()) {
            unsupported(node, fmt::format("a call to '{}'", node.name));
            return;
        }
        const FnInfo& info = found->second;

        std::vector<llvm::Value*> args;
        if (!emitCallArgs(node, info, node.name, argList(node.args), args)) return;
        emitCall(info, args);
    }

    // The arguments of a call, each offered the type of the parameter it lands on.
    //
    // `args` is in/out and may arrive non-empty: a method's receiver is parameter 0 and
    // the call site does not write it, so written argument i lands on parameter i+1.
    // That offset is the whole difference between a method call and a free call here,
    // which is why they share this rather than each keeping a copy of the conversion
    // rules -- a vararg promotion that existed in one and not the other would be a
    // silent ABI difference between two spellings of a call.
    //
    // Returns false having already reported.
    bool emitCallArgs(ASTNode& node, const FnInfo& info, const std::string& name,
                      const std::vector<Expression*>& argNodes,
                      std::vector<llvm::Value*>& args) {
        const size_t offset = args.size();
        for (size_t i = 0; i < argNodes.size(); ++i) {
            const size_t p = offset + i;
            // The parameter's type is offered to the argument, which is how an array
            // literal written at a call site knows what it is. A vararg position has
            // no declared type to offer, and an array literal there refuses.
            CgVal a = p < info.paramTypes.size() ? emitAs(*argNodes[i], info.paramTypes[p])
                                                : emit(*argNodes[i]);
            if (failed_) return false;
            if (!a.ok()) { unsupported(node, "this argument"); return false; }

            if (p < info.paramTypes.size()) {
                llvm::Value* converted = convert(node, a, info.paramTypes[p]);
                if (!converted) return false;
                args.push_back(converted);
                continue;
            }
            if (!info.isVarArg) {
                // The analyzer already checked arity; this is a backend
                // inconsistency rather than a program error, so it says so.
                unsupported(node, fmt::format("a call to '{}' with too many arguments",
                                              name));
                return false;
            }
            llvm::Value* promoted = promoteVararg(node, a);
            if (!promoted) return false;
            args.push_back(promoted);
        }
        if (args.size() < info.paramTypes.size()) {
            unsupported(node, fmt::format("a call to '{}' with too few arguments", name));
            return false;
        }
        return true;
    }

    // A written argument list as plain pointers, so that emitCallArgs can be shared by
    // a call (whose arguments are a vector) and by an operator (whose one operand is a
    // member of the BinaryOp). The alternative was a second copy of the conversion and
    // vararg rules, which is the kind of duplication that becomes an ABI difference
    // between two spellings of a call.
    static std::vector<Expression*> argList(
        const std::vector<std::unique_ptr<Expression>>& args) {
        std::vector<Expression*> out;
        out.reserve(args.size());
        for (auto& a : args) out.push_back(a.get());
        return out;
    }

    // The call itself, and what it leaves in `value_`.
    void emitCall(const FnInfo& info, const std::vector<llvm::Value*>& args) {
        auto* call = builder_.CreateCall(info.fn, args);
        // A void call is a statement, not a value. `value_` staying empty is what
        // makes `let x <int> = voidcall();` refuse rather than store a token.
        value_ = info.returnType.isVoid() ? CgVal{} : CgVal{call, info.returnType};
    }

    // The declaration behind a method name, for the sole purpose of saying why a call
    // to it did not find a function. Null for a name this struct does not declare.
    static const FunctionDeclaration* findMethod(const StructInfo& info,
                                                 const std::string& name) {
        if (!info.decl) return nullptr;
        for (auto& m : info.decl->methods)
            if (m->name == name) return m.get();
        return nullptr;
    }

    // Why `Struct.method` is not in functions_. Always reports.
    //
    // Separate from the lookup because the answer is never "it cannot be lowered" on
    // its own: declareStructMethods deliberately declares nothing for a generic or a
    // bodiless method, so a reader who is only told "not lowered yet" would go looking
    // for a missing feature instead of at the declaration two lines up.
    void reportMissingMethod(ASTNode& node, const StructInfo& info,
                             const std::string& method) {
        const FunctionDeclaration* decl = findMethod(info, method);
        if (decl && !decl->generic_params.empty()) {
            unsupported(node, fmt::format("a call to the generic method '{}' on struct '{}'",
                                          method, info.finName));
            return;
        }
        if (decl && !decl->body) {
            unsupported(node, fmt::format("a call to the bodiless method '{}' on struct '{}'",
                                          method, info.finName));
            return;
        }
        unsupported(node, fmt::format("a call to the method '{}' on struct '{}'", method,
                                      info.finName));
    }

    // Why `Struct.operator+` is not in functions_, given that the struct declares one.
    // Always reports. reportMissingMethod's counterpart, for the same reason: the two
    // shapes declareStructMethods deliberately skips are shapes a reader has to be sent
    // back to the declaration for, not told about as a missing feature.
    void reportMissingOperator(ASTNode& node, const StructInfo& info, ASTTokenKind op) {
        const OperatorDeclaration* decl = findOperator(info, op);
        const std::string spelling = spellOperator(op);
        if (decl && !decl->generic_params.empty()) {
            unsupported(node, fmt::format("the generic operator '{}' on struct '{}'",
                                          spelling, info.finName));
            return;
        }
        if (decl && !decl->body) {
            unsupported(node, fmt::format("an operator '{}' bound by 'implements' on "
                                          "struct '{}'", spelling, info.finName));
            return;
        }
        unsupported(node, fmt::format("the operator '{}' on struct '{}'", spelling,
                                      info.finName));
    }

    // The C variadic convention, which is not the Fin one: a float is passed as a
    // double and anything narrower than an int is passed as an int. A backend that
    // skipped this compiles and prints garbage, which is why
    // FloatsAreDoublesAtTheVarargBoundary is a run test and not an IR test.
    llvm::Value* promoteVararg(ASTNode& node, const CgVal& v) {
        if (v.type.isAggregate()) {
            // `printf("%d", p)` for a struct or an array `p` type-checks -- printf's
            // parameter is `...` and the analyzer does not read the format string.
            // Passing the aggregate would emit a call whose ABI is not the one C's
            // va_arg reads, so the value printed would be arbitrary. Refused, because
            // the default in this function is to pass the value through unchanged and
            // that is the wrong default for an aggregate.
            unsupported(node, v.type.isArray() ? "an array passed to a C variadic"
                                               : "a struct passed to a C variadic");
            return nullptr;
        }
        if (v.type.kind == CgType::Kind::Float &&
            v.type.llvmType->isFloatTy()) {
            return builder_.CreateFPExt(v.value, llvm::Type::getDoubleTy(ctx_));
        }
        if (v.type.kind == CgType::Kind::Int && v.type.bits < 32) {
            CgType i32 = types_.intType(32, v.type.isSigned);
            return convert(node, v, i32);
        }
        return v.value;
    }

    void visit(CastExpression& node) override {
        auto target = types_.map(node.target_type.get());
        if (!target) { unsupportedType(node, node.target_type.get(), "a cast"); return; }
        CgVal v = emit(*node.expr);
        if (failed_) return;
        if (!v.ok()) { unsupported(node, "this cast operand"); return; }
        llvm::Value* out = convert(node, v, *target);
        if (out) value_ = CgVal{out, *target};
    }

    void visit(TernaryOp& node) override {
        if (!currentFn_) { unsupported(node, "a ternary outside a function"); return; }
        CgVal cond = emit(*node.condition);
        if (failed_) return;
        llvm::Value* test = asCondition(node, cond);
        if (!test) return;

        auto* thenBB = llvm::BasicBlock::Create(ctx_, "tern.then", currentFn_->fn);
        auto* elseBB = llvm::BasicBlock::Create(ctx_, "tern.else", currentFn_->fn);
        auto* endBB = llvm::BasicBlock::Create(ctx_, "tern.end", currentFn_->fn);
        builder_.CreateCondBr(test, thenBB, elseBB);

        builder_.SetInsertPoint(thenBB);
        CgVal a = emit(*node.true_expr);
        if (failed_) return;
        auto* thenExit = builder_.GetInsertBlock();

        builder_.SetInsertPoint(elseBB);
        CgVal b = emit(*node.false_expr);
        if (failed_) return;
        auto* elseExit = builder_.GetInsertBlock();

        if (!a.ok() || !b.ok()) { unsupported(node, "a ternary arm with no value"); return; }
        CgType common = commonType(a.type, b.type);

        builder_.SetInsertPoint(thenExit);
        llvm::Value* av = convert(node, a, common);
        builder_.CreateBr(endBB);
        builder_.SetInsertPoint(elseExit);
        llvm::Value* bv = convert(node, b, common);
        builder_.CreateBr(endBB);
        if (!av || !bv) return;

        builder_.SetInsertPoint(endBB);
        auto* phi = builder_.CreatePHI(common.llvmType, 2);
        phi->addIncoming(av, thenExit);
        phi->addIncoming(bv, elseExit);
        value_ = CgVal{phi, common};
    }

    // ---- everything this slice refuses -----------------------------------
    //
    // One line each, and each one names what it is. `Visitor` being exhaustive is
    // what guarantees the list is complete: a node type added to FIN_NODE_LIST
    // without a case here does not compile.

    void visit(StructDeclaration& node) override {
        // declareStructs already lowered it, or already refused it. Reaching here
        // for a declaration it never saw means the declaration is somewhere it does
        // not scan -- inside a function body -- and a struct type whose name the
        // backend does not know is not a struct this file can lower.
        //
        // Its *methods* were declared and queued by declareStructs (a concrete
        // struct's) or by instantiateGeneric (an instantiation's), and nothing is left
        // to do for them here. Its operators and constructors are still refused at the
        // declaration, by lowerableStruct: an operator is reached by writing `a + b`,
        // which does not name it, so there is no call site to refuse at -- and a
        // constructor runs implicitly, which is the same objection a destructor gets.
        if (registered_.count(&node)) return;
        unsupported(node, fmt::format("a declaration of struct '{}' here", node.name));
    }
    void visit(InterfaceDeclaration& node) override { unsupported(node, "an interface declaration"); }
    void visit(EnumDeclaration& node) override {
        // Registered above and emits nothing: an enum's members are constants folded
        // into their uses, so there is no symbol and no storage. One declareEnums never
        // saw is refused rather than assumed handled -- an enum nested inside a
        // function body would land here.
        if (registeredEnums_.count(&node)) return;
        unsupported(node, fmt::format("a declaration of enum '{}' here", node.name));
    }
    void visit(ClassDeclaration& node) override { unsupported(node, "a class declaration"); }
    void visit(ImplementsBlock& node) override { unsupported(node, "an implements block"); }
    void visit(OperatorDeclaration& node) override { unsupported(node, "an operator declaration"); }
    void visit(ConstructorDeclaration& node) override { unsupported(node, "a constructor"); }
    void visit(DestructorDeclaration& node) override { unsupported(node, "a destructor"); }
    void visit(SpecialDeclaration& node) override { unsupported(node, "a '@special' declaration"); }
    void visit(TypeDefinition& node) override { unsupported(node, "a type alias"); }
    void visit(StructMember& node) override { unsupported(node, "a struct member"); }
    void visit(Parameter& node) override { unsupported(node, "a parameter"); }

    // A macro or an import that survives to codegen is a pass that did not run:
    // MacroExpander consumes the first, ModuleLoader the second. Saying so names
    // the pipeline stage rather than the syntax.
    void visit(MacroDeclaration& node) override { unsupported(node, "a macro declaration (macro expansion did not consume it)"); }
    void visit(MacroCall& node) override { unsupported(node, "a macro call (macro expansion did not consume it)"); }
    void visit(MacroInvocation& node) override { unsupported(node, "a macro invocation (macro expansion did not consume it)"); }
    void visit(QuoteExpression& node) override { unsupported(node, "a quote"); }
    void visit(ImportModule& node) override { unsupported(node, "an import (the module loader did not consume it)"); }

    void visit(ForeachLoop& node) override { unsupported(node, "a 'foreach' loop"); }
    // `delete p` returns the allocation. deeptest3.fin:44 says what it is:
    // "(Calls destructor if defined, then frees memory)".
    //
    // No destructor call is emitted, and that is sound rather than pending: a struct
    // with a destructor is refused outright at its declaration (lowerableStruct), so a
    // `delete` reaching here provably has nothing to run. The day destructors lower is
    // the day this line has to grow one, and ADR 0016 (destructors compose) is where
    // the order comes from.
    void visit(DeleteStatement& node) override {
        if (!currentFn_) { unsupported(node, "'delete' outside a function"); return; }
        if (!node.expr) { unsupported(node, "'delete' with no operand"); return; }
        CgVal v = emit(*node.expr);
        if (failed_) return;
        if (!v.ok() || !v.type.isPointer()) {
            // The analyzer already refuses `delete x` on a non-pointer ("Cannot delete
            // non-pointer type 'int'"), so this is the two passes disagreeing rather
            // than a program error -- and freeing a value read as an address is the one
            // outcome worse than refusing.
            unsupported(node, "'delete' of a non-pointer");
            return;
        }
        llvm::FunctionCallee release = runtimeFn(
            node, "free",
            llvm::FunctionType::get(llvm::Type::getVoidTy(ctx_),
                                    {llvm::PointerType::getUnqual(ctx_)}, false));
        if (!release) return;
        builder_.CreateCall(release, {v.value});
    }

    // A libc entry point, declared on demand.
    //
    // A Fin program may have declared the same name itself -- deeptest2.fin:4 writes
    // `@define free(ptr: &void) <noret>;`, which is this exact signature -- and one of
    // *those* is the same symbol, so the declaration is shared rather than duplicated.
    // A name already declared with a different signature refuses: calling through a
    // FunctionCallee whose type disagrees with the callee's would link and pass its
    // arguments in the wrong places.
    llvm::FunctionCallee runtimeFn(ASTNode& node, const char* name,
                                   llvm::FunctionType* type) {
        if (auto* existing = module_.getFunction(name)) {
            if (existing->getFunctionType() != type) {
                unsupported(node, fmt::format("an allocation, because '{}' is declared "
                                              "here with a different signature", name));
                return llvm::FunctionCallee();
            }
            return llvm::FunctionCallee(type, existing);
        }
        return module_.getOrInsertFunction(name, type);
    }
    void visit(TryCatch& node) override { unsupported(node, "'try'/'catch'"); }
    void visit(BlameStatement& node) override { unsupported(node, "'blame'"); }

    // `p.get()`, and `q.get()` where q is a `&Point`.
    void visit(MethodCall& node) override {
        if (!node.generic_args.empty()) {
            // A turbofish on the *method* rather than on the struct. Read by nobody
            // here, because a generic method is not declared at all, so it is refused
            // rather than dropped.
            unsupported(node, fmt::format("a call to the method '{}' with explicit "
                                          "generic arguments", node.method_name));
            return;
        }
        // The receiver is an address, and the same address a field access would take:
        // `p.get()` on a value, `q.get()` on a `&Point` with one load in between,
        // `o.inner.get()` on a field. One primitive for all three, which is what keeps
        // "Fin automatically handles -> logic with ." true of a call as well as of a
        // field (deeptest3.fin:39).
        auto receiver = baseAddress(*node.object, CgType::Kind::Struct);
        if (failed_) return;
        if (!receiver) {
            // `Point::make(1).get()`. The struct is a value with no home, so there is
            // no pointer to pass -- and a method takes a pointer because it may assign
            // through it. Copying to a temporary would work for a method that only
            // reads, and would silently discard the assignment of one that does not,
            // and this file cannot tell the two apart (whether a read-only method
            // should accept a temporary is an owner ruling). Refused the same way
            // `make()[0]` is refused: consistently, and at the receiver.
            unsupported(node, fmt::format("the receiver of a call to the method '{}' on "
                                          "a value with no address", node.method_name));
            return;
        }
        if (!receiver->type.structInfo) {
            unsupported(node, fmt::format("a call to the method '{}' on this receiver",
                                          node.method_name));
            return;
        }
        const StructInfo& owner = *receiver->type.structInfo;

        auto found = functions_.find(methodKey(owner.finName, node.method_name));
        if (found == functions_.end()) {
            reportMissingMethod(node, owner, node.method_name);
            return;
        }
        const FnInfo& info = found->second;
        if (!info.hasReceiver) {
            // A static method reached through a value. It has no `self` to be given
            // and the analyzer decides whether the spelling is legal at all; lowering
            // it here would mean silently dropping the receiver the source wrote.
            unsupported(node, fmt::format("a call to the static method '{}' through a "
                                          "value", node.method_name));
            return;
        }

        std::vector<llvm::Value*> args{receiver->ptr};
        if (!emitCallArgs(node, info, node.method_name, argList(node.args), args)) return;
        emitCall(info, args);
    }

    // `Point::make(1, 2)` (struct_methods.fin:8), and `Box::<int>::zero()` where the
    // type arguments are on the *type* and not on the method.
    void visit(StaticMethodCall& node) override {
        if (!node.generic_args.empty()) {
            unsupported(node, fmt::format("a '::' call to '{}' with explicit generic "
                                          "arguments", node.method_name));
            return;
        }
        // Through the mapper, so `Box::<int>::zero()` instantiates `Box<int>` on the way
        // -- including its methods, which is what puts `Box<int>.zero` in functions_ for
        // the lookup below to find. A bare `Box` written inside `Box<T>`'s own method
        // reaches its own instantiation through the same call, by the binding.
        auto target = types_.map(node.target_type.get());
        if (!target) {
            if (failed_) return;
            // A template written with no arguments -- `Vec2::make(1, 2)` on a
            // `struct Vec2<T>` (letssee.fin:26). The mapper cannot map it because there
            // is nothing to lay out until T is known, and inferring T from the arguments
            // is the same inference a free call needs and does not have. Named
            // specifically because "of type 'Vec2'" on its own reads as an unknown type
            // rather than as a template missing its arguments.
            if (node.target_type && templates_.count(node.target_type->name) &&
                node.target_type->generics.empty()) {
                unsupported(node, fmt::format("a '::' call to '{}' on the generic struct "
                                              "'{}' with no type arguments",
                                              node.method_name,
                                              node.target_type->name));
                return;
            }
            unsupportedType(node, node.target_type.get(), "a '::' call on a target");
            return;
        }
        if (!target->isStruct() || !target->structInfo) {
            // An enum, or a scalar. `Colour::Red` is a member access and not this, and
            // a `::` call on anything but a struct is a shape the corpus does not have.
            unsupported(node, fmt::format("a '::' call to '{}' on type '{}'",
                                          node.method_name, typeName(node.target_type.get())));
            return;
        }
        const StructInfo& owner = *target->structInfo;

        auto found = functions_.find(methodKey(owner.finName, node.method_name));
        if (found == functions_.end()) {
            reportMissingMethod(node, owner, node.method_name);
            return;
        }
        const FnInfo& info = found->second;
        if (info.hasReceiver) {
            // An instance method reached through the type. There is no receiver to
            // pass and inventing one would be inventing an object.
            unsupported(node, fmt::format("a '::' call to the instance method '{}' on "
                                          "struct '{}'", node.method_name, owner.finName));
            return;
        }

        std::vector<llvm::Value*> args;
        if (!emitCallArgs(node, info, node.method_name, argList(node.args), args)) return;
        emitCall(info, args);
    }
    void visit(MemberAccess& node) override {
        if (node.is_static) {
            // `MyEnum::B` -- the same member the bare `B` names and the same constant,
            // which is what extern_as.fin:44-45 writes two lines apart. The object is
            // an Identifier naming the *type* (the analyzer's visit(MemberAccess&) says
            // so), so it is read as a name here and never emitted as a value.
            if (auto* id = dynamic_cast<Identifier*>(node.object.get())) {
                auto e = enums_.find(id->name);
                if (e != enums_.end()) {
                    auto value = e->second.valueByName.find(node.member);
                    if (value != e->second.valueByName.end()) {
                        value_ = enumConstant(value->second);
                        return;
                    }
                }
            }
            // A `::` that is not an enum member: a static method, a namespaced symbol,
            // an associated constant. Each needs a mangling scheme.
            unsupported(node, fmt::format("the static member '{}'", node.member));
            return;
        }
        // `a.length` on a fixed array is the extent, and the extent is a number this
        // file already holds -- so it folds to a constant rather than loading
        // anything. There is no length field to load: an [N x T] carries its count in
        // its type and nowhere in its bytes.
        //
        // Typed as a signed i32 because the analyzer types `.length` as `int`
        // (Soundness_Members.ALengthIsAnIntAndNotAnotherIntegerWidth), and all five
        // corpus sites compare one against an int. A dynamic `[T]` has no extent here
        // and never reaches this -- it refuses at the mapper, because its
        // representation is what decides where a run-time length lives.
        // The object as a value, emitted at most once across everything below. Both
        // `.length` and the ordinary field path may need one, and `get().length` on a
        // struct would otherwise call `get()` to ask whether it is an array and again
        // to read the field.
        std::optional<CgVal> objectValue;

        if (node.member == "length" && !node.is_static && node.object) {
            // Through the address when there is one, so that reading the length does
            // not emit a load of the whole array. Either way the answer is the type's.
            // A pointer to an array answers with the array's, which is the same rule
            // `ptr_to_arr[0]` follows (deeptest3.fin:111).
            std::optional<CgType> arrayType;
            if (auto addr = baseAddress(*node.object, CgType::Kind::Array)) {
                arrayType = addr->type;
            }
            if (failed_) return;
            if (!arrayType) {
                objectValue = emit(*node.object);
                if (failed_) return;
                if (objectValue->ok()) {
                    if (objectValue->type.isArray()) {
                        arrayType = objectValue->type;
                    } else if (objectValue->type.isPointer() && objectValue->type.pointee &&
                               objectValue->type.pointee->isArray()) {
                        arrayType = *objectValue->type.pointee;
                    }
                }
            }
            if (arrayType) {
                CgType i32 = types_.intType(32, true);
                value_ = CgVal{llvm::ConstantInt::get(i32.llvmType, arrayType->extent, true),
                               i32};
                return;
            }
            // Not an array. A `string`'s length is a library question (ADR 0003) and a
            // struct's `length` field falls through to the ordinary field path below.
        }

        // The addressed path first: a GEP and a load of one field, rather than a
        // load of the whole struct followed by an extract. Both are correct; this
        // one does not copy the aggregate to read a byte of it.
        if (auto addr = emitAddress(node)) {
            value_ = CgVal{builder_.CreateLoad(addr->type.llvmType, addr->ptr, node.member),
                           addr->type};
            return;
        }
        if (failed_) return;

        // No address: the object is a value, so the field comes out of the value.
        // `make(5).a` is the shape -- a struct returned by a call is a real value
        // with no home, and materialising a temporary just to GEP into it would be
        // a copy for nothing.
        if (!objectValue) {
            objectValue = emit(*node.object);
            if (failed_) return;
        }
        CgVal object = *objectValue;
        if (!object.ok()) { unsupported(node, "this member's object"); return; }
        // A pointer that is a value rather than a variable: `make(3).hp` where `make`
        // returns a `&P`. The value *is* the address, so this is the addressed path's
        // GEP with one fewer load in front of it.
        if (object.type.isPointer() && object.type.pointee &&
            object.type.pointee->isStruct() && object.type.pointee->structInfo) {
            const StructInfo* info = object.type.pointee->structInfo;
            size_t index = 0;
            if (!info->find(node.member, index)) {
                unsupported(node, fmt::format("the member '{}', which struct '{}' does not have",
                                              node.member, info->finName));
                return;
            }
            llvm::Value* ptr = builder_.CreateStructGEP(object.type.pointee->llvmType,
                                                        object.value, (unsigned)index,
                                                        node.member);
            value_ = CgVal{builder_.CreateLoad(info->fields[index].type.llvmType, ptr,
                                               node.member),
                           info->fields[index].type};
            return;
        }
        if (!object.type.isStruct() || !object.type.structInfo) {
            unsupported(node, fmt::format("the member '{}' of a non-struct", node.member));
            return;
        }
        size_t index = 0;
        if (!object.type.structInfo->find(node.member, index)) {
            // The analyzer already rejects a field a struct does not have, so this
            // is a disagreement between the two rather than a program error. It
            // still refuses, because the alternative is reading field 0.
            unsupported(node, fmt::format("the member '{}', which struct '{}' does not have",
                                          node.member, object.type.structInfo->finName));
            return;
        }
        value_ = CgVal{builder_.CreateExtractValue(object.value, {(unsigned)index},
                                                   node.member),
                       object.type.structInfo->fields[index].type};
    }

    void visit(StructInstantiation& node) override {
        const std::string name = literalStructName(node, node.struct_name,
                                                   node.generic_args);
        if (name.empty()) return;  // already reported
        value_ = buildStructValue(node, name, node.fields);
    }

    // Which struct a literal is a literal *of*: the written name, or -- for
    // `Box::<int>{ val: 100 }` (complex.fin:12) -- the instantiation the turbofish
    // names. Returns empty having already reported.
    //
    // The arguments are mapped through a synthetic TypeNode rather than by a second
    // path into instantiateGeneric, so that `Box::<int>{...}` and `let b <Box<int>>`
    // are one code path and cannot drift: the same mangled name, the same layout, the
    // same refusals. It is synthetic because the parser hands the arguments over as a
    // bare list on the expression, with no type node of their own.
    //
    // The nodes are *borrowed* into it and released before it dies -- a TypeNode owns
    // its generics by unique_ptr and the AST owns these.
    std::string literalStructName(
        ASTNode& node, const std::string& writtenName,
        const std::vector<std::unique_ptr<TypeNode>>& args) {
        if (args.empty()) return writtenName;

        TypeNode probe(writtenName);
        probe.setLoc(node.loc);
        for (auto& arg : args) probe.generics.emplace_back(arg.get());
        std::string mangled;
        const bool ok = instantiateGeneric(probe, mangled);
        for (auto& borrowed : probe.generics) borrowed.release();
        if (!ok) {
            if (!failed_) {
                unsupported(node, fmt::format("a literal of generic struct '{}'",
                                              writtenName));
            }
            return {};
        }
        return mangled;
    }

    // The value a struct literal denotes: the written fields at their declared
    // positions, then the declared defaults for the fields the literal left out.
    //
    // Shared by `P{...}` and `new P{...}`, which differ in where the value ends up and
    // in nothing else. A `new` that built its own would be a second place for the
    // defaults to run, or to be forgotten -- and deeptest3.fin:85 (`new Node{value: 1}`,
    // with `next` left to its `= null`) is a `new` that depends on them running.
    //
    // Returns an invalid CgVal having already reported.
    CgVal buildStructValue(
        ASTNode& node, const std::string& structName,
        const std::vector<std::pair<std::string, std::unique_ptr<Expression>>>& literalFields) {
        auto found = structs_.find(structName);
        if (found == structs_.end() || !found->second.complete) {
            unsupported(node, fmt::format("a literal of struct '{}'", structName));
            return CgVal{};
        }
        const StructInfo& info = found->second;

        // Starts from all-zero, so a field the literal names in neither its text nor
        // a default is zero rather than whatever was in the slot. That is the same
        // answer a local with no initialiser gets, and undefined contents is the one
        // answer that cannot be tested.
        llvm::Value* aggregate = llvm::Constant::getNullValue(info.llvmType);
        std::vector<bool> written(info.fields.size(), false);

        // Walked in the order written, inserted at the index declared. The written
        // order is not the stored order and this is the only place that could
        // confuse them.
        for (auto& entry : literalFields) {
            size_t index = 0;
            if (!info.find(entry.first, index)) {
                unsupported(node, fmt::format("the field '{}', which struct '{}' does not have",
                                              entry.first, structName));
                return CgVal{};
            }
            if (!entry.second) { unsupported(node, "a field with no value"); return CgVal{}; }
            if (written[index]) {
                unsupported(node, fmt::format("field '{}' written twice in one literal",
                                              entry.first));
                return CgVal{};
            }
            // The field's type is offered to its value, which is what makes an array
            // field's literal know what it is: `Row { cells: [7, 8, 9] }` has no other
            // source for the element type.
            CgVal v = emitAs(*entry.second, info.fields[index].type);
            if (failed_) return CgVal{};
            if (!v.ok()) {
                unsupported(node, fmt::format("the value for field '{}'", entry.first));
                return CgVal{};
            }
            llvm::Value* stored = convert(node, v, info.fields[index].type);
            if (!stored) return CgVal{};
            aggregate = builder_.CreateInsertValue(aggregate, stored, {(unsigned)index},
                                                   entry.first);
            written[index] = true;
        }

        // Then the defaults, for the fields the literal did not write, in declared
        // order. After the written values and not interleaved with them: a default
        // is not in the literal's text, so no order puts it between two visible
        // lines, and running them last is the only arrangement a reader can predict
        // (Soundness_Codegen.TheWrittenValuesRunBeforeTheDefaults).
        //
        // Evaluated here, at the literal, and once per literal that omits the field
        // -- so `= tick()` ticks per instantiation. A default the literal *does*
        // write is not evaluated at all, which is why this loop skips it rather
        // than emitting and discarding.
        for (size_t index = 0; index < info.fields.size(); ++index) {
            const StructField& field = info.fields[index];
            if (written[index] || !field.defaultValue) continue;

            CgVal v = emitDefault(*field.defaultValue, field.type);
            if (failed_) return CgVal{};
            if (!v.ok()) {
                unsupported(*field.defaultValue,
                            fmt::format("the default value of field '{}' of struct '{}'",
                                        field.name, structName));
                return CgVal{};
            }
            llvm::Value* stored = convert(*field.defaultValue, v, field.type);
            if (!stored) return CgVal{};
            aggregate = builder_.CreateInsertValue(aggregate, stored, {(unsigned)index},
                                                   field.name);
        }

        CgType type;
        type.kind = CgType::Kind::Struct;
        type.llvmType = info.llvmType;
        type.structInfo = &found->second;
        return CgVal{aggregate, type};
    }
    void visit(PrototypeLiteral& node) override { unsupported(node, "a prototype literal"); }
    void visit(ArrayLiteral& node) override {
        // Built as a value, the way a struct literal is, and stored whole by whoever
        // asked for it. That is what makes `let c <[int, 3]> = a;` a copy: an LLVM
        // array value is a value.
        //
        // The element type comes from the hint the surrounding declaration set, and
        // there is exactly one thing that can set it -- a literal reaching here with
        // no hint has no element type to be an array *of*. `[1, 2, 3]` on its own is
        // not `[int, 3]` by inspection: the front end may have typed those constants
        // as `uint` against an annotation this file cannot see, and guessing from the
        // first element is how the two passes come to disagree about a stride.
        if (!arrayHint_ || !arrayHint_->element) {
            unsupported(node, "an array literal with no declared type");
            return;
        }
        const CgType type = *arrayHint_;
        if (node.elements.size() != type.extent) {
            // The front end refuses a literal whose length does not match its type,
            // which is what the extent being part of the type bought. A disagreement
            // here would be a store off the end of the slot.
            unsupported(node, fmt::format("an array literal of {} element{} for a type of {}",
                                          node.elements.size(),
                                          node.elements.size() == 1 ? "" : "s",
                                          type.extent));
            return;
        }

        // Zero first, so that a literal is never partly undefined even for the
        // moment between insertions.
        llvm::Value* aggregate = llvm::Constant::getNullValue(type.llvmType);
        for (size_t i = 0; i < node.elements.size(); ++i) {
            if (!node.elements[i]) { unsupported(node, "an array element with no value"); return; }
            // Each element gets the *element's* type as its own hint, which is what
            // makes `[[1, 2], [3, 4]]` lower: the inner literals are array literals
            // too and need to know what they are. Saved and restored rather than
            // cleared, because this literal's own hint has to survive its elements.
            auto* saved = arrayHint_;
            arrayHint_ = type.element->isArray() ? type.element.get() : nullptr;
            CgVal v = emit(*node.elements[i]);
            arrayHint_ = saved;
            if (failed_) return;
            if (!v.ok()) { unsupported(node, "this array element"); return; }
            llvm::Value* stored = convert(node, v, *type.element);
            if (!stored) return;
            aggregate = builder_.CreateInsertValue(aggregate, stored, {(unsigned)i});
        }
        value_ = CgVal{aggregate, type};
    }
    void visit(ArrayAccess& node) override {
        if (auto addr = emitAddress(node)) {
            value_ = CgVal{builder_.CreateLoad(addr->type.llvmType, addr->ptr, "load"),
                           addr->type};
            return;
        }
        if (failed_) return;
        // No address. An index into a struct's `operator []`, into a prototype, into
        // a pointer, or into an array that is a value with no home -- all of which
        // are their own units, and none of which may be read as "element 0".
        unsupported(node, "this index expression");
    }
    // `new T` allocates a T and yields an `&T`. Every spelling agrees on that --
    // `new Player{...}` (deeptest3.fin:37), `new int(5)` (variables.fin:28), and
    // `new int*`, whose parser comment spells the rule out: the stars describe the
    // result, so the type written here is one pointer shallower than it.
    //
    // `malloc` and `free` are the allocator, which is a decision and not an
    // implementation detail. ADR 0003: Fin has neither a garbage collector nor a
    // borrow checker, and an ownership model, a reference-counted pointer or a
    // collector is a *library* written against the compiler's components. A library
    // like that needs a raw substrate underneath it to hand out and take back, and
    // libc's allocator is the one every platform this targets already has -- the
    // linker driver is `cc` (Driver::runLinker), so it is already linked. Nothing
    // else in the corpus offers itself as one.
    //
    // The result is not null-checked. What a failed allocation does in Fin -- a raised
    // value, a null the program must test, an abort -- is a language decision, and
    // emitting a branch here would be making it; emitting none says "the pointer is
    // whatever malloc returned", which is the same thing C says and is at least a
    // rule someone else wrote down.
    void visit(NewExpression& node) override {
        if (!currentFn_) { unsupported(node, "'new' outside a function"); return; }

        auto allocated = types_.map(node.type.get());
        if (!allocated || allocated->isVoid() || !allocated->llvmType ||
            !allocated->llvmType->isSized()) {
            // Named as `new`'s own refusal: `new [char, n]` (stdlib/stdio.fin:112) asks
            // for a run-time number of elements, which is the dynamic-array
            // representation ruling and not a missing multiply.
            unsupportedType(node, node.type.get(), "'new'");
            return;
        }

        llvm::Value* initial = nullptr;
        if (allocated->isStruct()) {
            if (!node.args.empty()) {
                // `new P(1, 2)` -- a constructor call. Which constructor is a question
                // the analyzer does not answer yet either (it resolves `constructors[0]`
                // and no more), so there is nothing here to lower.
                unsupported(node, "'new' of a struct with constructor arguments");
                return;
            }
            CgVal v = buildStructValue(node, allocated->structInfo->finName,
                                       node.init_fields);
            if (!v.ok()) return;  // buildStructValue reported
            initial = v.value;
        } else {
            if (!node.init_fields.empty()) {
                unsupported(node, "'new' of a non-struct with field initialisers");
                return;
            }
            if (node.args.size() > 1) {
                unsupported(node, "'new' with more than one initial value");
                return;
            }
            if (node.args.empty() || !node.args.front()) {
                // `new int*` (simple_pointers.fin:23): an allocation with nothing to
                // put in it. Zeroed rather than left as whatever the allocator had,
                // which is the answer a local with no initialiser gets here too --
                // undefined contents is the one answer that cannot be tested.
                initial = llvm::Constant::getNullValue(allocated->llvmType);
            } else {
                CgVal v = emitAs(*node.args.front(), *allocated);
                if (failed_) return;
                if (!v.ok()) { unsupported(node, "the initial value of a 'new'"); return; }
                initial = convert(node, v, *allocated);
                if (!initial) return;
            }
        }

        // The size the module's own DataLayout gives, so the allocation, the GEPs into
        // it and `sizeof` all read one table (see visit(SizeofExpression&)).
        //
        // malloc's alignment is max_align_t, which covers every type this file builds --
        // an over-aligned one would need aligned_alloc, and Fin has no way to ask for
        // one yet.
        const uint64_t size = module_.getDataLayout().getTypeAllocSize(allocated->llvmType);
        llvm::FunctionCallee alloc = runtimeFn(
            node, "malloc",
            llvm::FunctionType::get(llvm::PointerType::getUnqual(ctx_),
                                    {llvm::Type::getInt64Ty(ctx_)}, false));
        if (!alloc) return;
        llvm::Value* raw = builder_.CreateCall(alloc, {builder_.getInt64(size)}, "new");
        builder_.CreateStore(initial, raw);
        value_ = CgVal{raw, types_.pointerTo(*allocated)};
    }
    void visit(SizeofExpression& node) override {
        // A type and only a type: `sizeof(1 + 1)` does not parse and `sizeof(a)`
        // parses as the type `a` (the grammar has no expression form), so there is
        // nothing here to evaluate -- and nothing that could be evaluated twice or
        // for its side effects. The expr form is refused rather than guessed at,
        // because the day it parses is the day it needs a rule about that.
        if (!node.type_target) {
            unsupported(node, "'sizeof' of an expression");
            return;
        }

        auto type = types_.map(node.type_target.get());
        if (!type || type->isVoid() || !type->llvmType || !type->llvmType->isSized()) {
            // Named as `sizeof`'s own refusal and not as "a variable of type X": the
            // program asked for a number, and what is missing is the representation
            // that would have one. `void` lands here too -- it is a type name the
            // front end accepts inside sizeof, and 0 would be an answer to a
            // question that does not have one.
            const std::string name = node.type_target->name.empty()
                                         ? std::string("that type")
                                         : "'" + node.type_target->name + "'";
            unsupported(node, fmt::format("'sizeof' of {}", name));
            return;
        }

        // The module's own DataLayout, which is set before any IR is emitted
        // (generateObject) precisely so that this is the same table the GEPs and
        // allocas around it use. A `sizeof` that disagreed with the code indexing
        // the thing it measured would be the worst kind of wrong: it would run.
        const uint64_t size = module_.getDataLayout().getTypeAllocSize(type->llvmType);
        CgType intType = *types_.byName("int");
        value_ = CgVal{llvm::ConstantInt::get(intType.llvmType, size, true), intType};
    }
    void visit(LambdaExpression& node) override { unsupported(node, "a lambda"); }
    void visit(SuperExpression& node) override { unsupported(node, "'super'"); }
    void visit(TypeLiteralExpression& node) override { unsupported(node, "a type literal"); }

    // A type node reached as an expression is a bug in whoever dispatched, not a
    // construct: TypeMapper is the only thing that should read one.
    void visit(TypeNode& node) override { unsupported(node, "a type in expression position"); }
    void visit(FunctionTypeNode& node) override { unsupported(node, "a function type in expression position"); }
    void visit(PointerTypeNode& node) override { unsupported(node, "a pointer type in expression position"); }
    void visit(ArrayTypeNode& node) override { unsupported(node, "an array type in expression position"); }

    // ---- state ------------------------------------------------------------

    struct LoopTargets {
        llvm::BasicBlock* continueTo = nullptr;
        llvm::BasicBlock* breakTo = nullptr;
    };

    DiagnosticEngine& diag_;
    bool debug_ = false;
    bool failed_ = false;

    llvm::LLVMContext ctx_;
    llvm::Module module_;
    llvm::IRBuilder<> builder_;
    TypeMapper types_;

    std::unordered_map<std::string, FnInfo> functions_;
    // Keyed by Fin name, and never erased from after declareEnums: enumMembers_
    // points into it.
    std::unordered_map<std::string, EnumInfo> enums_;
    // One entry per member of every enum, by the member's own name, for a member read
    // without its enum. The value is copied rather than looked up again because that
    // is all a read needs; the owner is kept for diagnostics and for debugLog.
    struct EnumMember {
        const EnumInfo* owner = nullptr;
        int64_t value = 0;
    };
    std::unordered_map<std::string, EnumMember> enumMembers_;
    std::set<const EnumDeclaration*> registeredEnums_;
    // Keyed by Fin name. Never erased from or rehashed after declareStructs, because
    // CgType::structInfo points into it.
    // A std::unordered_map because it does not move its values when it grows, and
    // this file depends on that in two ways that a vector-backed map would break:
    // CgType::structInfo points into it, and instantiateGeneric *inserts* into it
    // while a function body is being emitted (a `Box<int>` first seen at a literal
    // deep inside main). A rehash moves buckets and not elements, so every CgType
    // already handed out stays valid.
    std::unordered_map<std::string, StructInfo> structs_;
    // Which StructDeclaration nodes declareStructs actually took, so that one it
    // never saw is refused rather than assumed handled.
    std::set<const StructDeclaration*> registered_;

    // Every generic struct declaration, by name, borrowed from the AST -- which
    // outlives the emitter (run() takes the Program by reference). Not in structs_,
    // because a template is not a type: it has no llvm::StructType, no size, and a
    // name that no `let` may be declared at. instantiateGeneric is the only reader.
    std::unordered_map<std::string, StructDeclaration*> templates_;

    // Every generic function declaration, by name, borrowed from the AST for the same
    // reason the struct templates are. Not in functions_, because a template has no
    // signature: `fun ident<T>(a: T) <T>` names no LLVM type until something calls it,
    // and a call site is the only place the argument is known.
    std::unordered_map<std::string, FunctionDeclaration*> fnTemplates_;

    // One instantiation's bindings, by the instance's mangled name, kept alive for as
    // long as the emitter is. The TypeMapper holds a *pointer* to the substitution
    // while the instance's body is emitted, and a body may instantiate further
    // templates, so the storage cannot be a local. A node-based map, so a reference
    // handed to ScopedBindings survives every later insertion.
    std::unordered_map<std::string, Substitution> fnInstances_;

    // Method bodies whose prototypes exist and whose bodies have not been emitted yet.
    // Drained by run(), and appended to while being drained. See PendingBody.
    std::vector<PendingBody> pendingBodies_;

    std::vector<std::unordered_map<std::string, Local>> scopes_;

    // Module-scope variables, and the declarations declareGlobals has already
    // handled -- so that the top-level walk skips them instead of refusing.
    std::unordered_map<std::string, GlobalVar> globals_;
    std::set<const VariableDeclaration*> registeredGlobals_;
    std::vector<LoopTargets> loops_;
    FnInfo* currentFn_ = nullptr;
    CgVal value_;
};

}  // namespace

bool backendAvailable() { return true; }

bool generateObject(Program& ast, const std::string& objectPath, DiagnosticEngine& diag,
                    int optLevel, bool debugCodegen) {
    // The target comes first, before a single instruction is emitted, because the
    // module's DataLayout is an *input* to emission and not a stamp applied to the
    // result: `sizeof` folds to a number the layout decides, and a module laid out
    // after the fact would answer it from LLVM's default layout -- which agrees with
    // x86-64 by luck and with nothing else at all.
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    const std::string triple = llvm::sys::getDefaultTargetTriple();
    std::string lookupError;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, lookupError);
    if (!target) {
        diag.reportError("codegen: no LLVM target for " + triple, lookupError);
        return false;
    }

    llvm::TargetOptions options;
    // PIC because every mainstream Linux toolchain links PIE by default, and a
    // non-PIC object then fails at the link step with a relocation error that says
    // nothing about Fin.
    auto codeGenLevel = optLevel > 0 ? llvm::CodeGenOptLevel::Default
                                     : llvm::CodeGenOptLevel::None;
    std::unique_ptr<llvm::TargetMachine> machine(target->createTargetMachine(
        triple, "generic", "", options, llvm::Reloc::PIC_, std::nullopt, codeGenLevel));
    if (!machine) {
        diag.reportError("codegen: could not create a target machine for " + triple);
        return false;
    }

    Emitter emitter(diag, debugCodegen);
    llvm::Module& module = emitter.module();
    module.setTargetTriple(triple);
    module.setDataLayout(machine->createDataLayout());

    if (!emitter.run(ast)) return false;

    // Verified before anything is written. An invalid module that reaches the
    // object writer is an assertion failure deep in LLVM, which reads as a
    // compiler crash rather than as the compiler bug it is.
    std::string verifyError;
    llvm::raw_string_ostream verifyStream(verifyError);
    if (llvm::verifyModule(module, &verifyStream)) {
        diag.reportError("codegen: emitted invalid IR", verifyStream.str());
        return false;
    }

    if (optLevel > 0) {
        // The IR-level pipeline, which is the half a TargetMachine's opt level does
        // not cover. `finc -O2` reaches this (main.cpp), and what it runs is LLVM's
        // own default pipeline at that level -- which is the shape and not yet a
        // story: which passes a Fin build should run, and what it may assume about
        // aliasing and about the collector, is owed (ADR 0002 names it a
        // requirement).
        llvm::LoopAnalysisManager lam;
        llvm::FunctionAnalysisManager fam;
        llvm::CGSCCAnalysisManager cgam;
        llvm::ModuleAnalysisManager mam;
        llvm::PassBuilder pb(machine.get());
        pb.registerModuleAnalyses(mam);
        pb.registerCGSCCAnalyses(cgam);
        pb.registerFunctionAnalyses(fam);
        pb.registerLoopAnalyses(lam);
        pb.crossRegisterProxies(lam, fam, cgam, mam);
        auto level = optLevel >= 3 ? llvm::OptimizationLevel::O3
                                   : (optLevel == 2 ? llvm::OptimizationLevel::O2
                                                    : llvm::OptimizationLevel::O1);
        pb.buildPerModuleDefaultPipeline(level).run(module, mam);
    }

    std::error_code ec;
    llvm::raw_fd_ostream out(objectPath, ec, llvm::sys::fs::OF_None);
    if (ec) {
        diag.reportError("codegen: could not open '" + objectPath + "'", ec.message());
        return false;
    }

    llvm::legacy::PassManager pass;
    if (machine->addPassesToEmitFile(pass, out, nullptr,
                                     llvm::CodeGenFileType::ObjectFile)) {
        diag.reportError("codegen: this target cannot emit an object file");
        return false;
    }
    pass.run(module);
    out.flush();

    if (debugCodegen) diag.note("[codegen] wrote " + objectPath);
    return true;
}

}  // namespace fin
