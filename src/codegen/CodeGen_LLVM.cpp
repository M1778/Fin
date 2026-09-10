#include "CodeGen.hpp"

#include "../ast/ASTNode.hpp"   // the master AST include
#include "../ast/Visitor.hpp"
#include "../diagnostics/DiagnosticEngine.hpp"
// The list of macros the compiler implements, read here for the same reason the
// analyzer reads it: an invocation that survives expansion is either a builtin's
// -- lowered below -- or a pass that did not run, and one table is what keeps the
// two apart. A second copy of the names in this file would be a second place the
// truth lives, which is the failure ADR 0008 rejects by name.
#include "../semantics/BuiltinMacros.hpp"
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
#include <llvm/TargetParser/Triple.h>

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
//   * `try`/`catch`, and `blame`'s *raise* form. The runtime shape of a raised
//     value is not settled and there is no runtime to put it in. `blame`'s assert
//     form is lowered -- it prints where it failed and aborts -- and the split is
//     what let it land alone: the two forms are one keyword told apart by the
//     operand's type, and every `blame` the corpus reaches this file with is an
//     assert. Deliberately not catchable: `abort` unwinds nothing, so a `blame`
//     inside a `try` would leave the `catch` unreached, and nothing in the corpus
//     writes one there.
//   * A *generic* lambda and a generic function used as a value. Ordinary function
//     values and lambdas are lowered -- a Fin function value is a bare code pointer,
//     and a lambda that captures an enclosing local is refused rather than lowered,
//     which is what makes that representation sound. What is still missing for the
//     generic ones is not a representation but the code: a template has no address
//     until something says which instantiation is meant.
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
struct InterfaceInfo;

// `v.0`: is this member name a position rather than a name?
//
// The same test Analyzer_Expr.cpp's `positionalMember` makes, and deliberately a second
// copy of five lines rather than a header shared between the analyzer and the backend:
// what the two have to agree on is which *spellings* are positions, and that is fixed by
// the grammar (`expression DOT INTEGER`, parser.y) rather than by either of them. A name
// that is not all digits is not positional whatever else it is, because an identifier
// cannot begin with a digit and so no declared field can collide with one.
bool positionalMember(const std::string& name, size_t& index) {
    if (name.empty() || name.size() > 9) return false;
    for (char c : name) if (c < '0' || c > '9') return false;
    index = static_cast<size_t>(std::stoul(name));
    return true;
}

struct CgType {
    enum class Kind { Void, Int, Float, Ptr, Struct, Array, Fn, Prototype };
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
    const InterfaceInfo* interfaceInfo = nullptr;

    // Set for Kind::Array and null otherwise: what one element is. Held by value
    // through a shared_ptr rather than inline, because a CgType cannot contain
    // itself and `[[int, 2], 2]` needs it to contain one.
    //
    // The extent is a separate field and not read off the llvm::ArrayType, because
    // `.length` is answered from it. Fixed arrays use LLVM [N x T]; a dynamic `[T]`
    // is the two-field `{ptr, len}` struct described by ADR 0025.
    std::shared_ptr<CgType> element;
    uint64_t extent = 0;

    // Dynamic `[T]` only: the pair's fields are pointer-to-element and `int` length.
    // Keeping the element type here lets indexing and delete recover the pointee
    // without pretending the LLVM struct itself is an array.
    bool isDynamicArray = false;
    // Interface references are fat values: {data, vtable}.
    bool isInterface = false;
    std::string interfaceName;

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

    // Set for Kind::Fn and null otherwise: what the function value's signature is.
    //
    // A Fin function value is a bare code pointer -- see visit(LambdaExpression&) for
    // why the corpus settles that and not a closure pair -- so its llvmType is the same
    // `ptr` every other pointer is, and the signature is not recoverable from it. An
    // indirect call needs it: `CreateCall` on a pointer callee takes the
    // llvm::FunctionType explicitly, and getting it wrong reads argument registers the
    // caller never set. So it is carried here or it is nowhere.
    //
    // `result` and `params` are the Fin-level types and `llvmSignature` is what the
    // call instruction wants. Both, rather than one derived from the other: the LLVM
    // signature has lost which of two same-width integers was signed, and a conversion
    // at a call site through this pointer needs that.
    //
    // shared_ptr for the reason `element` and `pointee` are: `fn() -> fn() -> int`
    // needs a CgType to contain one.
    std::shared_ptr<CgType> result;
    std::vector<std::shared_ptr<CgType>> params;
    llvm::FunctionType* llvmSignature = nullptr;

    // Kind::Prototype only: the `[K]` and the `[V]`, each a dynamic-array CgType.
    //
    // A `prototype<K, V>` is `{ [K], [V] }` -- the keys beside the values, two
    // `{ptr, len}` pairs -- and that is a derivation from the corpus rather than a
    // choice. tests/samples/stdlib/prototypes.fin is the normative statement of what
    // a prototype is, and it says `prtp.0` is `[T]` (the keys) and `prtp.1` is the
    // values; the analyzer types both exactly that way already
    // (Analyzer_Expr.cpp, the positional-member path). Given that, storing anything
    // other than two arrays would mean `.0` had to *build* one, which is a copy the
    // program did not write, at a size only a run time knows.
    //
    // The two arrays are the whole representation and there is no third word: nothing
    // in the corpus asks a prototype for a count that is not one of the two arrays'
    // lengths, and the invariant that makes the pair meaningful -- key i belongs to
    // value i -- is the reason `.0.length` and `.1.length` are the same number rather
    // than a reason to store it twice.
    //
    // What this representation does *not* do is look a key up: `a[10]` needs an
    // equality over an arbitrary key type and a search, which is prototype access and
    // is a unit of its own (tests/samples/prototype_test.fin's note). Storage,
    // construction and the two projections are what is here, and the subscript is
    // refused rather than answered with element 0.
    //
    // shared_ptr for the reason `element`, `pointee` and `result` are: a CgType cannot
    // contain itself, and `prototype<int, prototype<int, int>>` needs it to contain one.
    std::shared_ptr<CgType> keys;
    std::shared_ptr<CgType> values;

    bool isVoid() const { return kind == Kind::Void; }
    bool isStruct() const { return kind == Kind::Struct; }
    bool isArray() const { return kind == Kind::Array; }
    bool isPointer() const { return kind == Kind::Ptr; }
    bool isFn() const { return kind == Kind::Fn; }
    bool isPrototype() const { return kind == Kind::Prototype; }
    // What may not cross an `@define` boundary or a C variadic: the platform ABI
    // decides how each is passed and clang implements that classification, so
    // emitting the LLVM aggregate would link cleanly and pass garbage. A prototype is
    // two structs in a struct, so it is one of them -- and it is the only kind here
    // that no C function could have been written to receive in the first place.
    bool isAggregate() const { return isStruct() || isArray() || isPrototype(); }
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
    // True for a field this struct got from a base rather than declaring itself.
    //
    // The offsets are the same either way -- base fields come first, so an inherited
    // field is at the index it had in the base -- which is what makes an upcast emit
    // nothing. The flag exists because a struct *literal* must not be allowed to name
    // one: `Derived { a: 1, c: 3 }` writes a field the derived struct did not declare,
    // and whether that is legal is a language question nobody has answered. Mirrors
    // FieldLayout::inherited in src/types/Layout.hpp, which carries it for the
    // collector's benefit for the same reason.
    bool inherited = false;
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

struct InterfaceInfo {
    std::string finName;
    std::vector<StructField> fields;
    std::vector<FunctionDeclaration*> methods;
    llvm::StructType* vtableType = nullptr;
};

// The members an `implements` block adds to a struct declared somewhere else.
//
// `MyStruct implements <GetVal<int>> { pub fun get_val() <int> {...} }`
// (implements_block.fin:13) writes a method of `MyStruct` outside `MyStruct`, so the
// declaration the third pass of declareStructs walks does not contain it. This is the
// rest of that walk: three vectors of borrowed AST nodes, collected before the structs
// are declared and read wherever a struct's own members are read.
//
// Borrowed pointers and not copies, for the reason StructInfo::decl is borrowed: the
// Program outlives the emitter, a body is emitted from the node, and a diagnostic
// blames the node's own line rather than the block's.
//
// Not merged into the StructDeclaration. Mutating the AST from the backend would make
// the analyzer's view of a program depend on whether it had been lowered, and the two
// passes reading one node is the property that keeps a diagnostic's line honest.
struct StructExtras {
    std::vector<FunctionDeclaration*> methods;
    std::vector<OperatorDeclaration*> operators;
    std::vector<ConstructorDeclaration*> constructors;
    // Every block that contributed, so the statement walk can tell one this file
    // consumed from one whose target it never lowered.
    std::vector<const ImplementsBlock*> blocks;
};

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

    // What an `implements` block added to this struct, or null for a struct no block
    // names. Points into Emitter::implementsExtras_, which is filled before
    // declareStructs and never touched again -- so an instantiation may point at the
    // template's entry and outlive the statement that instantiated it.
    const StructExtras* extras = nullptr;

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
    // Set when the value was loaded from an addressable source. Interface conversion
    // needs this home for its data pointer; an rvalue has no stable address to borrow.
    llvm::Value* address = nullptr;
    bool ok() const { return value != nullptr; }
};

struct Local {
    llvm::AllocaInst* slot = nullptr;
    CgType type;
};

// Where a `continue` and a `break` inside the innermost loop go. Declared out here
// beside Local and FnInfo rather than in the Emitter, because ScopedEmission holds one
// of these and its member declarations are read before the Emitter's own are.
struct LoopTargets {
    llvm::BasicBlock* continueTo = nullptr;
    llvm::BasicBlock* breakTo = nullptr;
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
    bool isConstructor = false;
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

    void bindInterfaces(const std::unordered_map<std::string, InterfaceInfo>* interfaces) { interfaces_ = interfaces; }

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
        if (!node->generics.empty() || !node->annotations.empty() || node->pointer_depth != 0 || node->is_array ||
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

        // A bit-width annotation -- the `{64}` in `int{64}` -- is refused rather than
        // dropped, and it is refused here so that every role gets the same answer from
        // one place: a variable, a parameter, a return, a struct field, a pointer's
        // pointee, an array's element and a `cast<>` target each reach this function
        // and each used to be lowered at the *base* type's width. `let x <int{64}>`
        // emitted an i32 and computed in 32 bits, silently, which is a miscompile of
        // exactly the kind this file's "refuse, never skip" rule exists to prevent --
        // and worse than the missing feature, because the program built and ran.
        //
        // Nothing in the compiler honours the annotation yet: `annotations` is written
        // by one grammar production (parser.y, `base_type LBRACE expression_list
        // RBRACE`) and read in four places -- StructuralWalk, ASTPrinter, CloneTypes,
        // and Analyzer_Core.cpp:393, which walks the expressions for their own side
        // effects and hands back the type *unannotated*. So the front end does not
        // narrow either: `uint{8}` accepts -1 and lays out as four bytes
        // (KnownDefect_IntegerWidths and KnownDefect_Layout.AWidthAnnotationDoesNot
        // ChangeTheSize). Honouring the width end to end is its own unit (HANDOFF §6
        // item 9); until then the honest answer is that this type is not lowered.
        //
        // `spell` renders the annotation, so the refusal names `int{64}` rather than
        // `int` -- a refusal that says "a variable of type 'int'" about a line that
        // lowers `int` fine would send the reader to the wrong half of the type.
        // Resolved semantic types may arrive with the width materialized in the
        // primitive name rather than as an AST annotation.
        const auto braceName = node->name.find('{');
        if (node->annotations.empty() && braceName != std::string::npos &&
            node->name.back() == '}') {
            const std::string base = node->name.substr(0, braceName);
            try {
                const unsigned bits = static_cast<unsigned>(std::stoull(
                    node->name.substr(braceName + 1, node->name.size() - braceName - 2)));
                const auto info = scalarByName(base);
                if (info && info->kind == ScalarKind::Int &&
                    isRepresentableIntegerWidth(bits))
                    return intType(bits, info->isSigned);
            } catch (...) { return std::nullopt; }
        }

        if (!node->annotations.empty()) {
            const auto info = scalarByName(node->name);
            if (!info) return std::nullopt;
            if (info->kind != ScalarKind::Int) {
                // The front end drops the width, but codegen must not silently
                // claim that the written annotation was honored.
                return std::nullopt;
            } else {
                if (node->annotations.size() != 1) {
                    return std::nullopt;
                }
                uint64_t bits = 0;
                if (readConstant(*node->annotations[0], bits) != ConstantRead::Ok) {
                    return std::nullopt;
                }
                if (!isRepresentableIntegerWidth(static_cast<unsigned>(bits))) {
                    return std::nullopt;
                }
                CgType mapped = intType(static_cast<unsigned>(bits), info->isSigned);
                return mapped;
            }
        }

        // A prototype is a decoration this slice lowers, and the node is an ordinary
        // TypeNode with the flag set rather than a subclass -- the parser builds
        // `TypeNode("prototype")` with `is_prototype` and the key and value in
        // `generics` (parser.y, `LBRACE type_list RBRACE`). Before the pointer and
        // array branches for the same reason those come before the catch-all: a
        // prototype that is also a pointer or an array is a decoration this does not
        // cover, and mapPrototype says so by refusing rather than by dropping half of
        // what was written.
        if (node->is_prototype) {
            if (node->pointer_depth != 0 || node->is_array || node->is_nullable ||
                !node->implements_list.empty() || node->array_size) {
                return std::nullopt;
            }
            return mapPrototype(*node);
        }

        // A nullable or an erasure constraint means "not this slice" rather than "the
        // base name" -- silently dropping the decoration is how `[int]` would become
        // `int` and start being copied by value.
        // An array is one of the decorations this slice lowers, and only when
        // its extent is written and constant. See mapArray.
        if (auto* arr = dynamic_cast<const ArrayTypeNode*>(node)) {
            if (node->pointer_depth != 0 || node->is_nullable) return std::nullopt;
            return mapArray(*arr);
        }
        // A pointer is the second. `pointer_depth` is checked and never set: the
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
        // A function type is the third decoration this slice lowers. Before the
        // catch-all below, which used to reject every FunctionTypeNode outright.
        if (auto* fn = dynamic_cast<const FunctionTypeNode*>(node)) {
            if (node->pointer_depth != 0 || node->is_array || node->is_nullable ||
                node->array_size) {
                return std::nullopt;
            }
            return mapFunction(*fn);
        }
        if (node->pointer_depth != 0 || node->is_array || node->is_nullable ||
            node->is_prototype || !node->implements_list.empty() || node->array_size) {
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
        // Semantic spelling may materialize a resolved width in the name while
        // preserving the source node's annotation elsewhere.
        const auto brace = node->name.find('{');
        if (brace != std::string::npos && node->name.back() == '}') {
            const std::string base = node->name.substr(0, brace);
            uint64_t bits = 0;
            try { bits = std::stoull(node->name.substr(brace + 1, node->name.size() - brace - 2)); }
            catch (...) { return std::nullopt; }
            const auto info = scalarByName(base);
            if (info && info->kind == ScalarKind::Int &&
                isRepresentableIntegerWidth(static_cast<unsigned>(bits)))
                return intType(static_cast<unsigned>(bits), info->isSigned);
        }
        if (auto e = enumByName(node->name)) return e;
        if (interfaces_ && interfaces_->count(node->name)) {
            CgType t;
            t.kind = CgType::Kind::Struct;
            t.isInterface = true;
            t.interfaceName = node->name;
            t.interfaceInfo = interfaces_ ? &interfaces_->at(node->name) : nullptr;
            t.llvmType = llvm::StructType::get(ctx_, {
                llvm::PointerType::get(ctx_, 0), llvm::PointerType::get(ctx_, 0)});
            return t;
        }
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

    // `fn(int, int) -> int` becomes one machine word -- a bare code pointer -- with the
    // signature recorded beside it (CgType::result, ::params, ::llvmSignature).
    //
    // A bare pointer and not a closure pair, and the corpus is what settles it rather
    // than a preference: every one of the thirteen lambdas the samples write reads
    // nothing but its own parameters (and, at lambdas.fin:58, the global `printf`, which
    // is a symbol and not a capture). Nothing in the corpus closes over a local, so the
    // second word of a pair would be a word every function value carried for no reader,
    // and it would have to be threaded through every `fn` field, parameter and return.
    // A lambda that does read an enclosing local is refused by name rather than
    // lowered -- see enclosingNames_ -- so the day one appears is the day the pair has
    // to be designed, and this shape does not quietly compile it wrong in the meantime.
    //
    // `fn<T: Castable>(m: T) -> T` (lambdas.fin:69) refuses here. A generic function
    // type has no signature until someone names the arguments, and a value of it is a
    // pointer to code that has not been emitted -- monomorphisation has nothing to key
    // on. That is a decision about generic function values, not a gap in this mapping.
    std::optional<CgType> mapFunction(const FunctionTypeNode& node) const {
        if (!node.generic_params.empty() || !node.generics.empty()) return std::nullopt;
        auto ret = map(node.return_type.get());
        if (!ret) return std::nullopt;

        CgType t;
        t.kind = CgType::Kind::Fn;
        // The same `ptr` every other pointer is: LLVM has had one since 15, and a
        // function pointer was never a distinct type in the IR anyway.
        t.llvmType = llvm::PointerType::getUnqual(ctx_);
        t.result = std::make_shared<CgType>(*ret);

        std::vector<llvm::Type*> llvmParams;
        for (auto& p : node.param_types) {
            auto mapped = map(p.get());
            if (!mapped) return std::nullopt;
            // `fn(void)` is not a nullary function, it is a parameter with no
            // representation. Refused rather than dropped, because dropping it would
            // make `fn(void) -> int` and `fn() -> int` the same type.
            if (mapped->isVoid()) return std::nullopt;
            t.params.push_back(std::make_shared<CgType>(*mapped));
            llvmParams.push_back(mapped->llvmType);
        }
        // No vararg form: a `fn` type has no syntax for `...`, so a variadic function
        // has no type here to be a value of. visit(Identifier&) refuses one by name.
        t.llvmSignature = llvm::FunctionType::get(ret->llvmType, llvmParams, false);
        return t;
    }

    CgType pointerTo(const CgType& pointee) const {
        CgType t = pointerType();
        t.pointee = std::make_shared<CgType>(pointee);
        return t;
    }

    // `[T, N]` becomes an LLVM [N x T]: N of the element, laid end to end, with LLVM
    // placing them. A dynamic `[T]` becomes `{ptr, len}`: the pointer to elements
    // followed by an i32 length, matching Layout.cpp's two-word ABI (ADR 0025).
    std::optional<CgType> mapArray(const ArrayTypeNode& node) const {
        if (!node.element_type) return std::nullopt;

        auto element = map(node.element_type.get());
        if (!element) return std::nullopt;

        if (!node.size) {
            CgType t;
            t.kind = CgType::Kind::Array;
            t.element = std::make_shared<CgType>(*element);
            t.isDynamicArray = true;
            // `get` and not `create`: a *literal* struct type, which LLVM uniques by
            // its element types, so every mapping of `[int]` is the same
            // llvm::StructType. `create` mints a fresh named type per call, and two
            // `[int]`s were then two different types -- which nothing noticed until
            // `convert`'s `from.type.llvmType == to.llvmType` fell through to the
            // bottom and refused with `this conversion is not lowered yet` on
            // `take(a)` and on `return a`. The interface pair above is built the same
            // way for the same reason. A name buys nothing here: nothing reads it, and
            // two `[T]`s of one element type must be assignable.
            t.llvmType = llvm::StructType::get(ctx_, {element->llvmType->getPointerTo(),
                                                     llvm::Type::getInt32Ty(ctx_)});
            return t;
        }

        uint64_t extent = 0;
        if (readConstant(*node.size, extent) != ConstantRead::Ok) return std::nullopt;
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

    // `prototype<K, V>` becomes `{ [K], [V] }`: the keys' dynamic array beside the
    // values', which is two `{ptr, len}` pairs and so four words. See CgType::keys for
    // why that is the representation and not a choice -- stdlib/prototypes.fin says
    // `.0` is `[K]` and `.1` is `[V]`, and the analyzer already types them so.
    //
    // Refused rather than guessed at wherever the written type does not name both
    // halves, and each refusal is a different question that is genuinely open:
    //
    //  - An arity other than two. The analyzer accepts `{int}` and `{int, float, char}`
    //    and defaults a missing half to `any`, so a one-element prototype arrives here
    //    as a real type with a made-up value type. Which of "the value is `any`", "it is
    //    a set" and "it is an error" Fin means is unruled, and lowering it as `any`
    //    would pick one silently.
    //  - `any` or `object` on either side. Neither has a representation at all
    //    (Layout.cpp refuses a DynamicType: `any` is to be `{i8*, i64}` from a
    //    declaration in lib/std that does not exist), so `<{object, object}>` --
    //    prototype_test.fin:40's own annotation -- is refused for the same reason a
    //    bare `let v <any>` is, and not for being inside a prototype.
    //  - A half whose own type does not lower, which is every other case and needs no
    //    rule of its own: `map` on the element answers, and the answer propagates.
    std::optional<CgType> mapPrototype(const TypeNode& node) const {
        if (node.generics.size() != 2) return std::nullopt;

        // Each half as a *dynamic* array of itself, built through mapArray so that
        // there is one place in this file that decides what a `{ptr, len}` is. An
        // ArrayTypeNode with no size is exactly what `[K]` is written as, so this is
        // the same door `let k <[int]>` goes through rather than a second encoding of
        // the pair -- and if ADR 0025's shape ever changes, it changes for both.
        auto half = [&](const TypeNode* written) -> std::optional<CgType> {
            if (!written) return std::nullopt;
            auto inner = map(written);
            if (!inner) return std::nullopt;
            // `any` and `object` map to nothing here, so they are already refused by
            // the line above; void is a name that maps to something and is still not a
            // value, which is why it is named separately.
            if (inner->isVoid() || !inner->llvmType || !inner->llvmType->isSized()) {
                return std::nullopt;
            }
            CgType arr;
            arr.kind = CgType::Kind::Array;
            arr.element = std::make_shared<CgType>(*inner);
            arr.isDynamicArray = true;
            // `get`, not `create`, for the reason mapArray gives: LLVM uniques literal
            // structs, so every `[int]` in the program is one llvm::StructType and two
            // prototypes of one key and value type are assignable to each other.
            arr.llvmType = llvm::StructType::get(ctx_,
                {inner->llvmType->getPointerTo(), llvm::Type::getInt32Ty(ctx_)});
            return arr;
        };

        auto keys = half(node.generics[0].get());
        if (!keys) return std::nullopt;
        auto values = half(node.generics[1].get());
        if (!values) return std::nullopt;

        CgType t;
        t.kind = CgType::Kind::Prototype;
        t.keys = std::make_shared<CgType>(*keys);
        t.values = std::make_shared<CgType>(*values);
        t.llvmType = llvm::StructType::get(ctx_, {keys->llvmType, values->llvmType});
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
    const std::unordered_map<std::string, InterfaceInfo>* interfaces_ = nullptr;
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
    Emitter(DiagnosticEngine& diag, bool debug, std::string sourceName)
        : diag_(diag), debug_(debug), sourceName_(std::move(sourceName)), ctx_(),
          module_("fin", ctx_), builder_(ctx_), types_(ctx_) {
        types_.bindStructs(&structs_);
        types_.bindEnums(&enums_);
        types_.bindInterfaces(&interfaces_);
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
        // Before the structs, because a struct's `parents` vector holds base structs
        // and implemented interfaces together and only a name can tell them apart.
        // Nothing is emitted: see interfaceNames_.
        declareInterfaces(program);
        if (failed_) return false;
        // Before the structs, because a block writes members *of* a struct: the
        // declaration declareStructs walks does not contain them, and both the
        // lowerability checks and the third pass have to see them at the same time
        // they see the struct's own. Nothing is emitted and nothing is refused here --
        // see collectImplementsBlocks.
        collectImplementsBlocks(program);
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
        // The resume point. One top-level statement is not an input to the next -- a
        // function's body is not built out of the body beside it -- so a refusal in one
        // is no reason to stop looking at the others, and stopping is what made a
        // per-sample refusal count a depth-1 probe. Clearing the transient flag here is
        // safe without any further reset because ScopedEmission already puts the
        // emitter back: its destructor restores the insert point, `currentFn_` and
        // `scopes_` however the body was left, which is the property its own comment
        // was written for.
        //
        // Between phases the walk still halts, and that is the other half of the
        // design: `declareStructs` reads what `declareEnums` built, so a struct refused
        // for want of an enum would be an invented refusal. Siblings inside a phase
        // consume nothing from each other, so they are exactly the boundary that can be
        // crossed.
        for (auto& stmt : program.statements) {
            stmt->accept(*this);
            failed_ = false;
        }
        if (everFailed_) return false;
        // The methods of the instantiations the statements asked for. A call site
        // reaches `Box<int>.get` as a declaration and this is what puts a body in it,
        // which is why it is after the loop and not inside it: emitting a body from the
        // middle of another body would work (ScopedEmission exists for exactly that)
        // and this way the queue is drained once, at a point with no live insert point.
        drainPendingBodies();
        return !everFailed_;
    }

    llvm::Module& module() { return module_; }

private:
    // ---- refusal ----------------------------------------------------------

    // The single exit from "this cannot be lowered". One wording, one place, so a
    // refusal always names the construct and always carries a location.
    // The guard stays, and it is what keeps a collected list of refusals honest: within
    // one unit the first refusal is the useful one, and every later call while `failed_`
    // is still set is a consequence of it rather than a finding of its own. Refusing
    // `[int]` leaves the variable with no storage, so the statement that reads it next
    // would refuse too -- and reporting that would send a reader after work that does
    // not exist. Suppression by construction, not by filtering afterwards: a second
    // refusal is only ever reported once the walk has reached a point where nothing it
    // sees can depend on the first.
    void unsupported(ASTNode& node, const std::string& what) {
        if (failed_) return;  // within this unit, the first refusal is the useful one
        failed_ = true;
        everFailed_ = true;
        diag_.reportError(node.loc, fmt::format("codegen: {} is not lowered yet", what));
    }

    void unsupportedType(ASTNode& node, const TypeNode* type, const std::string& role) {
        std::string what = fmt::format("{} of type '{}'", role, typeName(type));
        if (type) {
            uint64_t bits = 0;
            bool width = false;
            if (type->annotations.size() == 1 &&
                readConstant(*type->annotations.front(), bits) == ConstantRead::Ok) {
                width = true;
            }
            const std::string spelled = typeName(type);
            const auto open = spelled.find('{');
            const auto close = spelled.find('}', open);
            if (!width && open != std::string::npos && close != std::string::npos) {
                try { bits = std::stoull(spelled.substr(open + 1, close - open - 1)); width = true; }
                catch (...) {}
            }
            if (width && !isRepresentableIntegerWidth(static_cast<unsigned>(bits)))
                what += fmt::format(" (supported integer widths are {})",
                                    kRepresentableIntegerWidths);
        }
        unsupported(node, what);
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
        if (auto* ptr = dynamic_cast<const PointerTypeNode*>(type)) {
            std::string out = ptr->annotations.empty() ? "&" + spell(ptr->pointee.get(), substituted)
                : "(&" + spell(ptr->pointee.get(), substituted) + ")";
            if (!ptr->annotations.empty()) out += "{8}";
            return out;
        }
        // `fn(int, int) -> int`, and recursively. The node's own `name` is the bare word
        // "fn", so without this every function type in every refusal read as "of type
        // 'fn'" -- which does not distinguish the generic one this file refuses from the
        // plain one it lowers, and those refuse for entirely different reasons.
        if (auto* fn = dynamic_cast<const FunctionTypeNode*>(type)) {
            std::string out = "fn";
            if (!fn->generic_params.empty()) out += "<...>";
            out += "(";
            for (size_t i = 0; i < fn->param_types.size(); ++i) {
                if (i) out += ", ";
                out += spell(fn->param_types[i].get(), substituted);
            }
            return out + ") -> " + spell(fn->return_type.get(), substituted);
        }
        if (auto* arr = dynamic_cast<const ArrayTypeNode*>(type)) {
            const std::string inner = spell(arr->element_type.get(), substituted);
            uint64_t extent = 0;
            const bool fixed = arr->size &&
                               readConstant(*arr->size, extent) == ConstantRead::Ok;
            std::string out = fixed ? fmt::format("[{}, {}]", inner, extent) : "[" + inner + "]";
            if (!arr->annotations.empty()) out = "(" + out + "){8}";
            return out;
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
        // The bit-width annotation, `{64}` in `int{64}`. Included because the mapper
        // refuses an annotated type and lowers the bare one, so a refusal that dropped
        // the annotation would read "a variable of type 'int'" about a line whose
        // `int` half is not the problem.
        //
        // A constant is printed as its value and anything else as `...`. `int{8 * 8}`
        // (tests/samples/type_annotations.fin:8) is a BinaryOp, and this file has no
        // expression printer -- an arithmetic annotation is folded nowhere yet, so
        // there is no number to print. `...` is what ASTPrinter uses for the same
        // reason, and it still distinguishes an annotated type from a bare one, which
        // is the whole job here.
        if (!type->annotations.empty()) {
            name += "{";
            for (size_t i = 0; i < type->annotations.size(); ++i) {
                if (i) name += ", ";
                uint64_t width = 0;
                const ASTNode* ann = type->annotations[i].get();
                if (ann && readConstant(*ann, width) == ConstantRead::Ok)
                    name += std::to_string(width);
                else
                    name += "...";
            }
            name += "}";
        }
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

    // The same thing for a prototype literal, and separate from `arrayHint_` rather
    // than one hint of either kind, because `{ 10: 1.5 }` and `[1, 2, 3]` are different
    // nodes with different needs and a single slot would have each visitor checking
    // that the hint it found is the kind it wanted. Two slots, one predicate each.
    const CgType* prototypeHint_ = nullptr;

    // Emits `expr` with `type` offered to it, when it is a literal that needs one.
    CgVal emitAs(Expression& expr, const CgType& type) {
        auto* savedArray = arrayHint_;
        auto* savedProto = prototypeHint_;
        arrayHint_ = type.isArray() ? &type : nullptr;
        prototypeHint_ = type.isPrototype() ? &type : nullptr;
        CgVal v = emit(expr);
        arrayHint_ = savedArray;
        prototypeHint_ = savedProto;
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
        // The nested functions are put aside for the same reason the locals are: the
        // default was written in the struct's declaration, where a function declared
        // inside the use site's body is not a name at all.
        std::vector<std::unordered_map<std::string, std::string>> savedNested;
        savedNested.swap(nested_);
        nested_.emplace_back();
        CgVal v = emitAs(expr, target);
        nested_.swap(savedNested);
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

    // The nested functions a scope declares live on a stack of their own, pushed and
    // popped with the locals rather than stored beside them.
    //
    // Beside them would mean a Local, and a Local is a frame slot; what a nested
    // function has is a symbol. Keeping the two apart is what lets nestedFor decide
    // between a `let h` and a `fun h` by *depth* -- see it for why that is the only
    // answer that matches the scope the analyzer resolved the name in.
    void pushScope() {
        scopes_.emplace_back();
        nested_.emplace_back();
        lambdaTemplates_.emplace_back();
    }
    void popScope() {
        scopes_.pop_back();
        nested_.pop_back();
        lambdaTemplates_.pop_back();
    }

    // The names this body refused a declaration for, so that a later statement reading
    // one is not reported as a discovery of its own.
    //
    // Nothing here is a lookup table: a poisoned name has no storage, and that is the
    // whole of what it means. `let a <[int]> = ...` is refused before any slot is
    // registered -- visit(VariableDeclaration) writes into scopes_ on its last line --
    // so `a` is simply absent afterwards, and `a[0]` on the next line then refuses as
    // "this index expression". That message is false: indexing an array this file *can*
    // lower is lowered (Soundness_Codegen.SuppressingACascadeDoesNotSuppressAnUnrelated-
    // Refusal's fixed-extent sibling compiles), so the index expression was never the
    // problem -- the missing base was. Reporting it would send a reader to implement
    // work that does not exist.
    //
    // A vector rather than a set because a body refuses one or two names at most, and
    // because this file deliberately does not include <unordered_set>.
    std::vector<std::string> poisoned_;

    // The names that were in scope where the lambda now being emitted was written.
    // Empty except while a lambda body is being emitted, which is what makes the check
    // that reads it cost nothing for every other body.
    //
    // A list of names and not the scopes themselves, deliberately: a capture is refused,
    // so nothing here is ever used to *reach* the enclosing storage. If this held slots
    // it would be one edit away from loading one, and loading one is the miscompile --
    // the frame is gone by the time the pointer is called. See refuseIfCapture.
    //
    // A vector for the same two reasons poisoned_ is one.
    std::vector<std::string> enclosingNames_;

    // The counter behind `fin.lambda.<n>`. Monotonic, so no two lambdas in one module
    // can collide, and the spelling is one a Fin program cannot write -- the lexer has
    // no `.` in an identifier.
    unsigned lambdas_ = 0;

    // The same, for `fin.nested.<n>.<name>`. The written name is kept in the symbol
    // because the only reader of a Fin symbol name is a person reading `nm` output, and
    // the counter is what keeps two bodies' `helper` apart -- two functions may each
    // declare one and they are two functions.
    unsigned nestedFns_ = 0;

    // What a refused capture is a capture *by*. A lambda and a nested function are the
    // two bodies written inside another body, neither may reach the enclosing frame, so
    // the check is one function -- but the wording has to differ, because a reader told
    // "a lambda capturing 'n'" about a `fun` would go looking for a lambda.
    std::string captureKind_ = "a lambda";

    // Per scope, the nested functions it declares: the written name to the symbol the
    // body was emitted under. Pushed and popped with `scopes_`.
    std::vector<std::unordered_map<std::string, std::string>> nested_;

    // A generic lambda, held as the template it is rather than as a value.
    //
    // `let id <auto> = fun <T>(x: T) <T> { return x; };` binds a name to a *recipe*, and
    // ADR 0002's monomorphisation is what makes that the whole of the declaration's
    // meaning: `id<int>` and `id<double>` are two functions, a bare `id` names neither,
    // and there is no address for a slot to hold until a call says which is meant. So
    // the declaration allocates nothing and this is where the name goes; the call
    // instantiates, exactly as `fnTemplates_` does for a named `fun ident<T>`.
    //
    // What a named template does not need and this does: the two things a lambda body is
    // emitted *with*. An instance is built from the middle of a call, so the scopes the
    // lambda was written among are long gone by then -- `enclosing` is what
    // refuseIfCapture reads, and `nested` is what the body may call. Snapshotted at the
    // declaration because that is where they are true, and a body given the *call site's*
    // would refuse the wrong names as captures and admit the wrong nested functions.
    //
    // `id` is the `fin.lambda.<n>` the instances share, taken from the same counter a
    // non-generic lambda takes its symbol from: one template is one lambda however many
    // instances it has, and the mangled key is that name with the substitution appended.
    // Held by pointer to an incomplete type on purpose: the environment is a table of
    // LambdaTemplates and a LambdaTemplate names the environment it was written in, so
    // one of the two has to be indirect. A `shared_ptr` to a struct defined below is the
    // spelling that is legal for both directions, and shared rather than unique because
    // every instance of one template reads the same environment.
    struct LambdaEnv;

    struct LambdaTemplate {
        LambdaExpression* node = nullptr;
        unsigned id = 0;
        std::vector<std::string> enclosing;
        std::unordered_map<std::string, std::string> nested;
        std::shared_ptr<LambdaEnv> visible;
        // The type parameters that were bound where the lambda was *written*, which is a
        // thing a named template never has: a module-scope `fun ident<T>` is written
        // where nothing is bound, and a lambda inside `fun outer<S>` is not. `fun <U>(y:
        // U) <S> { ... }` mentions `S` in its signature, so an instance built with only
        // its own binding installed would refuse `S` as a type it does not know. See
        // instantiateTemplate, which installs these under the lambda's own.
        Substitution outer;
    };

    // The generic lambdas one body may call, which is what crosses a body boundary for
    // the reason `nestedCarry_` does: a template is a node and a snapshot, neither of
    // which is a frame, so it may be read from a body the writing scope no longer
    // encloses.
    struct LambdaEnv {
        std::unordered_map<std::string, LambdaTemplate> table;
    };

    // Per scope, the generic lambdas it declares -- pushed and popped with `scopes_` for
    // the reason `nested_` is: a template bound to a name in a block is out of scope
    // after it, and a flat table keyed by the written name would make an inner one and
    // an outer one the same entry. `emitNestedFunction` already records that as the
    // reason a *nested* template is refused, and the shape here is the answer to it.
    std::vector<std::unordered_map<std::string, LambdaTemplate>> lambdaTemplates_;

    // What the *next* body emitted should see as its enclosing nested functions, handed
    // over rather than inherited: emitBodyOf takes it and clears it, so a body nobody
    // filled it for starts with none, and only the two callers that emit a body written
    // inside another body -- emitNestedFunction and visit(LambdaExpression&) -- fill it.
    //
    // One flat map and not a stack, because what crosses the boundary is the *set* of
    // names the body may call and not which scope each came from: inside the body they
    // are all equally outer, and the body's own scopes are pushed on top of them.
    //
    // Names and symbols, never slots: a symbol is reachable from anywhere and a frame is
    // not, which is the whole reason these may cross a body boundary when a local may
    // not.
    std::unordered_map<std::string, std::string> nestedCarry_;

    // The same, for the generic lambdas. Taken and cleared by emitBodyOf exactly as
    // `nestedCarry_` is, so a body nobody filled it for -- a top-level function, a
    // queued method -- sees none rather than the last filler's.
    std::unordered_map<std::string, LambdaTemplate> lambdaCarry_;

    bool isPoisoned(const std::string& name) const {
        for (auto& p : poisoned_) if (p == name) return true;
        return false;
    }

    // Called at the resume point for a declaration that did not finish. Its name is
    // poisoned whether or not the refusal was reported, because a statement suppressed
    // here leaves exactly the same hole as the one that was reported.
    void poison(const std::string& name) {
        if (!isPoisoned(name)) poisoned_.push_back(name);
    }

    Local* findLocal(const std::string& name) {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) return &found->second;
        }
        // Checked only after the scopes miss, which is what keeps the suppression from
        // reaching further than the hole: a declaration that registered its slot and
        // then failed for some later reason is found above and behaves normally.
        //
        // `failed_` without a report. Every caller of this and of emitAddress already
        // unwinds on `failed_` before reaching its own categorical refusal, so one flag
        // set here stops the statement through the paths that already exist -- and the
        // statement is *stopped*, not accepted. It is not lowered, the compile still
        // fails, and what was not examined is said on the trace below.
        if (isPoisoned(name)) {
            if (!failed_) {
                debugLog(fmt::format("not examined: a statement reading '{}', whose "
                                     "declaration was refused above", name));
            }
            failed_ = true;
        }
        return nullptr;
    }

    // Every nested function a body written *here* could call, flattened into the one map
    // that crosses a body boundary: outermost scope first, so an inner declaration
    // overwrites an outer one of the same name, and any name a local in the same scope
    // takes is removed again. Exactly the set nestedFor would answer for, computed once
    // because the body it is handed to gets a scope stack of its own and cannot walk
    // this one.
    // Every generic lambda a body written *here* could call, flattened the way
    // visibleNested flattens the nested functions and for the same three reasons:
    // outermost first so an inner binding wins, a name a local takes removed again, and
    // computed once because the body it is handed to gets a scope stack of its own.
    std::unordered_map<std::string, LambdaTemplate> visibleLambdas() const {
        std::unordered_map<std::string, LambdaTemplate> visible;
        const size_t depth = std::min(scopes_.size(), lambdaTemplates_.size());
        for (size_t i = 0; i < depth; ++i) {
            for (const auto& entry : lambdaTemplates_[i]) visible[entry.first] = entry.second;
            for (const auto& entry : scopes_[i]) visible.erase(entry.first);
        }
        return visible;
    }

    std::unordered_map<std::string, std::string> visibleNested() const {
        std::unordered_map<std::string, std::string> visible;
        const size_t depth = std::min(scopes_.size(), nested_.size());
        for (size_t i = 0; i < depth; ++i) {
            for (const auto& entry : nested_[i]) visible[entry.first] = entry.second;
            for (const auto& entry : scopes_[i]) visible.erase(entry.first);
        }
        return visible;
    }

    // The symbol a nested function was emitted under, or nothing when the name is not a
    // nested function's -- or when a local of the same name is at least as inner as it
    // is.
    //
    // The locals and the nested functions are two tables over *one* scope stack, and
    // that is why this walks them in lockstep rather than reading one to exhaustion
    // first. Reading the nested ones first would let a `fun h` in a block shadow a name
    // from a scope it does not enclose; reading the locals to the bottom first would do
    // the reverse to `let h <int> = 3; { fun h() ... h() }`, where the analyzer resolves
    // `h` to the inner function. Neither is the scope the name was resolved in, and the
    // difference is a call to the wrong thing rather than a missing feature.
    //
    // Because it answers only for a nested declaration at least as inner as any local of
    // the name, the identifier and call paths may ask it *first* and the locals lose
    // nothing by being second.
    //
    // The locals are consulted first *within* one depth, which is arbitrary and never
    // reached: a name that is both in one scope is refused where the second of the two is
    // declared (see emitNestedFunction).
    const std::string* nestedFor(const std::string& name) const {
        const size_t depth = std::min(scopes_.size(), nested_.size());
        for (size_t i = 0; i < depth; ++i) {
            if (scopes_[scopes_.size() - 1 - i].count(name)) return nullptr;
            const auto& fns = nested_[nested_.size() - 1 - i];
            auto found = fns.find(name);
            if (found != fns.end()) return &found->second;
        }
        return nullptr;
    }

    // The generic lambda a name is bound to, or nothing.
    //
    // Walks the two tables in lockstep exactly as `nestedFor` does, and for the same
    // reason: the locals and the templates are two tables over one scope stack, so a
    // local of the name declared further in has to win. Nothing in the corpus writes
    // that -- the analyzer refuses a call after a same-name local shadows a template
    // (`let g <auto> = fun<T>...; let g <int> = 3; g(1)` is `Undefined function or type
    // 'g'`) -- so this is the order that agrees with the front end rather than a case.
    LambdaTemplate* lambdaTemplateFor(const std::string& name) {
        const size_t depth = std::min(scopes_.size(), lambdaTemplates_.size());
        for (size_t i = 0; i < depth; ++i) {
            if (scopes_[scopes_.size() - 1 - i].count(name)) return nullptr;
            auto& table = lambdaTemplates_[lambdaTemplates_.size() - 1 - i];
            auto found = table.find(name);
            if (found != table.end()) return &found->second;
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
    // Records every module-scope interface's name, and emits nothing.
    //
    // Deliberately not a check: visit(InterfaceDeclaration&) already refuses the
    // shapes an interface may not have (a method body, an implemented operator, a
    // constructor, a destructor, a member default), and it runs in the statement walk
    // where each refusal names its own line. Doing it twice would report one fault
    // twice; doing it *here* instead would report it before the structs, where a
    // reader would see an interface's fault blamed for a struct that never got read.
    void declareInterfaces(Program& program) {
        for (auto& stmt : program.statements) {
            if (auto* i = dynamic_cast<InterfaceDeclaration*>(stmt.get())) {
                interfaceNames_.insert(i->name);
                InterfaceInfo info;
                info.finName = i->name;
                for (const auto& m : i->members) {
                    if (!m || !m->type) continue;
                    auto mapped = types_.map(m->type.get(), true);
                    if (mapped) info.fields.push_back({m->name, *mapped, nullptr});
                }
                for (const auto& m : i->methods) if (m) info.methods.push_back(m.get());
                interfaces_[i->name] = std::move(info);
            }
        }
    }

    // Whether a parent in a struct's `parents` vector names an interface rather than a
    // base struct.
    //
    // By name, because that is the only witness the AST carries: `parents` is a vector
    // of TypeNode, and a TypeNode is a spelling. Generic arguments are ignored --
    // `rptr_iface<T>` (stdlib/stdptr.fin:37) names the interface declared as
    // `interface rptr_iface<T>` (:10), and an interface's arguments cannot change
    // whether it has bytes.
    bool parentIsInterface(const TypeNode& parent) const {
        return interfaceNames_.count(parent.name) > 0;
    }

    // Every module-scope `implements` block, filed under the name of the struct it
    // writes members for.
    //
    // A collection pass and not a checking one: a block whose target is not a struct
    // this file lowered -- an enum (stdlib/typing.fin:27), an interface, a name declared
    // nowhere -- is left in the map and consumed by nobody, and visit(ImplementsBlock&)
    // refuses it in the statement walk where it can blame the block's own line. Which
    // is the same division declareInterfaces uses and for the same reason: reporting
    // here would report an interface's or an enum's fault while the structs were being
    // read, blamed on a struct that never got looked at.
    //
    // The single-member overwrite form is deliberately not collected. `@implements
    // Result<T, E>::unwrap = fun(...) {...}` (enums.fin:25) supplies a *value* for one
    // named member -- a lambda, not a declaration -- and a value has no signature to
    // declare a function from, so it stays refused as the whole block rather than half
    // consumed.
    void collectImplementsBlocks(Program& program) {
        for (auto& stmt : program.statements) {
            auto* b = dynamic_cast<ImplementsBlock*>(stmt.get());
            if (!b) continue;
            if (b->target_type.empty()) continue;
            if (!b->overwrite_member.empty()) continue;  // refused whole, see above
            StructExtras& extras = implementsExtras_[b->target_type];
            extras.blocks.push_back(b);
            for (auto& m : b->methods) if (m) extras.methods.push_back(m.get());
            for (auto& o : b->operators) if (o) extras.operators.push_back(o.get());
            for (auto& c : b->constructors) if (c) extras.constructors.push_back(c.get());
        }
    }

    // What the blocks added to the struct written under this name, or null for a name
    // no block names. Keyed by the *written* name, so a template's key is `Result` and
    // an instantiation of it borrows the template's entry (instantiateGeneric).
    const StructExtras* extrasFor(const std::string& name) const {
        auto found = implementsExtras_.find(name);
        return found == implementsExtras_.end() ? nullptr : &found->second;
    }

    // Marks every block that contributed to a struct this file lowered, so the
    // statement walk can tell one that was consumed from one whose target it never saw.
    void registerImplementsBlocks(const std::string& name) {
        if (const StructExtras* extras = extrasFor(name))
            for (const ImplementsBlock* b : extras->blocks) registeredBlocks_.insert(b);
    }

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
                // A block on a template names the template: `Result<T, U> implements
                // <IResult>` (stdlib/typing.fin:27). Marked consumed here, and its
                // members are declared once per instantiation by instantiateGeneric --
                // which is where a method of a template is declared anyway.
                registerImplementsBlocks(s->name);
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
            // The blocks that wrote members for this struct, if any. Attached in the
            // first pass because the third one reads it through StructInfo, and marked
            // registered so the statement walk knows these blocks were consumed rather
            // than skipped.
            info.extras = extrasFor(s->name);
            registerImplementsBlocks(s->name);
            structs_[s->name] = info;
            registered_.insert(s);
            decls.push_back(s);
        }

        for (StructDeclaration* s : decls) {
            StructInfo& info = structs_[s->name];
            std::vector<llvm::Type*> members;
            // The base's fields first, at the indices they had in the base. That is
            // the owner's ruling -- "the parent's fields splice in at offset 0" -- and
            // it is what makes a pointer to the derived struct a valid pointer to the
            // base, so an upcast emits no instruction. src/types/Layout.cpp:428-451
            // already computes exactly this for the collector; this is the backend
            // catching up rather than deciding.
            //
            // The base is looked up in `structs_`, which the first pass has finished
            // filling, so a base declared anywhere at module scope is found. Its
            // *body* may not be set yet (this pass sets bodies in `decls` order), which
            // is why the fields are copied from `StructInfo::fields` rather than from
            // the llvm::StructType -- the CgTypes are complete after pass one even
            // when the LLVM bodies are not.
            //
            // A base declared *below* its derived struct cannot arrive here at all:
            // the analyzer reports `Undefined type 'Base'` first (measured), so the
            // ordering problem the third pass exists to solve does not apply.
            for (auto& parent : s->parents) {
                if (!parent || parentIsInterface(*parent)) continue;
                auto base = structs_.find(parent->name);
                if (base == structs_.end()) {
                    // lowerableStruct refuses a base it cannot classify, so reaching
                    // here means the base was registered and then dropped -- the two
                    // passes disagreeing, not a program error.
                    unsupported(*s, fmt::format("struct '{}' inheriting '{}', which "
                                                "this file did not lower",
                                                s->name, parent->name));
                    return;
                }
                for (const StructField& f : base->second.fields) {
                    if (info.indexByName.count(f.name)) {
                        // Two bases with a field of the same name, or a base and this
                        // struct. Which one `d.x` means is a language question, and
                        // answering it by declaration order would answer it silently.
                        unsupported(*s, fmt::format("struct '{}' inheriting a second "
                                                    "field '{}' from '{}'",
                                                    s->name, f.name, parent->name));
                        return;
                    }
                    StructField carried = f;
                    carried.inherited = true;
                    info.indexByName[f.name] = info.fields.size();
                    info.fields.push_back(carried);
                    members.push_back(f.type.llvmType);
                }
            }
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
                // One byte, which is C's answer and not LLVM's.
                //
                // The choice was open and is now settled, and the deciding argument is
                // not aesthetics: LLVM's `{}` is zero bytes, so two distinct values of
                // an empty struct can be given the same address, and `&a != &b` then
                // reads false for two variables the program declared separately. C
                // gives an empty struct one byte precisely so that cannot happen, C++
                // inherits it, and finc is written in C++ and interoperates with it --
                // an empty Fin struct crossing into a C++ translation unit has to have
                // the size that side already believes it has. A surprise about object
                // identity surfaces very far from its cause, so the byte is cheaper.
                //
                // The byte is padding and not a field: `fields` and `indexByName` stay
                // empty, so `m.anything` still refuses as an unknown member rather than
                // reaching a member the compiler invented. Nothing needs to be added to
                // the literal path either, because a literal starts from
                // `Constant::getNullValue` of the whole type and inserts one value per
                // *declared* field -- zero of them here -- so `M {}` is `{ i8 0 }`
                // without a special case.
                members.push_back(llvm::Type::getInt8Ty(ctx_));
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
        if (info.decl)
            for (auto& o : info.decl->operators)
                if (o->op == op) return o.get();
        // A block's operators are this struct's, so every reader of this answer -- the
        // `a + b` path, the generic-operator path, the diagnostic -- sees one written in
        // a block exactly as it sees one written in the body. Second, because the
        // struct's own declaration is the one a reader looks at first; lowerableOperators
        // has already refused the case where both declare the same token, so the order
        // decides nothing.
        if (info.extras)
            for (OperatorDeclaration* o : info.extras->operators)
                if (o->op == op) return o;
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

        for (auto& m : info.decl->methods)
            if (!declareStructMethod(info, *m, receiver)) return false;
        // The methods an `implements` block wrote for this struct. Declared here and not
        // in a pass of their own, because they are this struct's methods: they get
        // `Struct.name` as their key, the same receiver, the same weak linkage and the
        // same deferred body, so `s.get_val()` finds one through the lookup that already
        // exists rather than through a second one written for blocks.
        //
        // Read off StructInfo::extras rather than the map, so an instantiation follows
        // the template's entry -- see instantiateGeneric.
        if (info.extras)
            for (FunctionDeclaration* m : info.extras->methods)
                if (!declareStructMethod(info, *m, receiver)) return false;

        // At most one constructor, whichever declaration carries it: lowerableStruct
        // refuses a second one across the struct and its blocks together, so reaching
        // here with one in each is impossible and the first found is the only one.
        for (auto& c : info.decl->constructors) {
            if (!c->body) continue;
            if (!declareStructConstructor(info, *c, receiver)) return false;
            break; // constructorFor currently selects constructors[0]
        }
        if (info.extras && !functions_.count(methodKey(info.finName, "constructor"))) {
            for (ConstructorDeclaration* c : info.extras->constructors) {
                if (!c->body) continue;
                if (!declareStructConstructor(info, *c, receiver)) return false;
                break;
            }
        }

        if (info.decl->destructor) {
            const std::string key = methodKey(info.finName, "destructor");
            static const std::vector<std::unique_ptr<Parameter>> noParams;
            declareFunction(*info.decl->destructor, key, key, noParams, nullptr,
                            /*isVarArg=*/false, /*isExtern=*/false, &receiver);
            auto declared = functions_.find(key);
            if (declared == functions_.end()) return false;
            declared->second.fn->setLinkage(llvm::Function::LinkOnceODRLinkage);
            pendingBodies_.push_back(PendingBody{info.decl->destructor.get(), &noParams,
                                                 info.decl->destructor->body.get(), key,
                                                 &info.methodBindings});
        }

        // The operators, on the same terms. An operator is a method with a spelled name:
        // the receiver is the same pointer, the body is deferred to the same queue, the
        // linkage is weak for the same reason, and an instantiation gets its own copy
        // because this runs once per instantiation. What differs is where it is *reached*
        // from -- visit(BinaryOp&) rather than a written name -- and nothing about that
        // is decided here.
        for (auto& o : info.decl->operators)
            if (!declareStructOperator(info, *o, receiver)) return false;
        // And a block's, `Point implements <Addable<Point>> { pub operator + ... }`
        // (implements_block.fin:28), which is the same operator of the same struct.
        if (info.extras)
            for (OperatorDeclaration* o : info.extras->operators)
                if (!declareStructOperator(info, *o, receiver)) return false;
        return true;
    }

    // One method's prototype and one queued body. Split out of declareStructMethods so
    // that a method written in an `implements` block goes through this code rather than
    // a copy of it -- the block's method *is* the struct's method, and the only
    // difference is which node it was read from.
    bool declareStructMethod(StructInfo& info, FunctionDeclaration& m,
                             const CgType& receiver) {
        // A method generic is one layer further than this unit goes: two
        // substitutions at once, the struct's and the call's. Not declared, so a
        // call to it refuses at the call site with a name to blame rather than
        // linking against a symbol that was never defined -- and *not* refused
        // here, because struct_methods.fin declares `set_x<T>` and never calls it,
        // and a declaration nobody instantiates has no signature to lower.
        if (!m.generic_params.empty()) return true;
        // Likewise a method with no body. `@define` writes prototypes at module
        // scope, not inside a struct, so this is a shape the corpus does not have;
        // declaring one would publish a symbol that nothing defines, and the
        // failure would land on the linker rather than on the line.
        if (!m.body) return true;

        auto self = types_.structByName(info.finName, /*allowIncomplete=*/true);
        if (!self) return true;

        // A written `self` is the receiver, so it has to *be* the receiver: `&Self`,
        // `&Point`, `&Point<T>` -- three spellings of one pointer. `self: Point` is
        // a copy, and this file passes a pointer, so accepting it would mean a
        // method whose signature says by-value and whose body assigns through a
        // pointer into the caller's object. `self: int` is not the struct at all.
        // Checked here and not in lowerableMethods because the receiver's type is
        // only known once the struct is complete -- and for a template, only once
        // it is instantiated.
        if (!m.is_static && !m.params.empty() && m.params[0]->name == "self" &&
            m.params[0]->type) {
            auto written = types_.map(m.params[0]->type.get(), /*allowIncomplete=*/true);
            const bool matches = written && written->isPointer() && written->pointee &&
                                 written->pointee->llvmType == self->llvmType;
            if (!matches) {
                if (failed_) return false;
                unsupported(*m.params[0],
                            fmt::format("a 'self' of type '{}' on struct '{}', which "
                                        "is not a pointer to it",
                                        typeName(m.params[0]->type.get()), info.finName));
                return false;
            }
        }

        const std::string key = methodKey(info.finName, m.name);
        // A static method takes no receiver -- struct_methods.fin:8 calls
        // `Point::make(1, 2)` with nobody to be `self`. Everything else does, and
        // it is a *pointer*: `set_x` at :16 assigns to `self.x`, and a by-value
        // receiver would make that a store into a copy that is discarded at the
        // return. That is not an unimplemented feature, it is a program that
        // silently does not assign.
        declareFunction(m, key, key, m.params, m.return_type.get(),
                        /*isVarArg=*/false, /*isExtern=*/false,
                        m.is_static ? nullptr : &receiver);
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
        pendingBodies_.push_back(PendingBody{&m, &m.params, m.body.get(), key,
                                             &info.methodBindings});
        return true;
    }

    // One constructor's prototype and its queued body. declareStructMethod's
    // counterpart, and split out for the same reason.
    bool declareStructConstructor(StructInfo& info, ConstructorDeclaration& c,
                                  const CgType& receiver) {
        const std::string key = methodKey(info.finName, "constructor");
        // The object is the receiver and the result is nothing: the caller owns the
        // storage, passes its address as parameter 0, and reads the value back out
        // of its own slot afterwards. That is the same convention `set_x` already
        // uses -- `self.x = nx` is a store through a pointer into the caller's
        // object -- and choosing it here is what makes the three sites that must
        // agree (this declaration, emitBodyOf/visit(ReturnStatement&) and
        // visit(FunctionCall&)) describe one calling convention rather than three.
        //
        // Returning the aggregate by value instead would need the struct ABI
        // classifier for the day a constructor crosses an `@define` boundary, and
        // would still load: the corpus's dominant body is `return new S{...}`,
        // which produces a pointer whatever the signature says.
        declareFunction(c, key, key, c.params, /*returnType=*/nullptr,
                        /*isVarArg=*/false, /*isExtern=*/false, &receiver);
        auto declared = functions_.find(key);
        if (declared == functions_.end()) return false;
        declared->second.fn->setLinkage(llvm::Function::LinkOnceODRLinkage);
        declared->second.isConstructor = true;
        pendingBodies_.push_back(PendingBody{&c, &c.params, c.body.get(), key,
                                             &info.methodBindings});
        return true;
    }

    // One operator's prototype and its queued body.
    bool declareStructOperator(StructInfo& info, OperatorDeclaration& o,
                               const CgType& receiver) {
        // A generic operator: two substitutions at once, the struct's and the
        // operator's, which is one layer further than this unit goes. Not declared
        // and not refused, because operators.fin:15 declares `operator + : <T>` and
        // applies it nowhere -- the sample is `//@ ok` with it in.
        if (!o.generic_params.empty()) return true;
        // An operator with no body is one bound by `implements`
        // (hashmap.fin:50-51): the function it forwards to is written in the cast,
        // and reading that cast is a feature of its own. Declaring the operator
        // anyway would publish a symbol nothing defines and move the failure to the
        // linker.
        if (!o.body) return true;

        const std::string key = operatorKey(info.finName, o.op);
        declareFunction(o, key, key, o.params, o.return_type.get(),
                        /*isVarArg=*/false, /*isExtern=*/false, &receiver);
        auto declared = functions_.find(key);
        if (declared == functions_.end()) return false;  // declareFunction reported
        declared->second.fn->setLinkage(llvm::Function::LinkOnceODRLinkage);
        pendingBodies_.push_back(PendingBody{&o, &o.params, o.body.get(), key,
                                             &info.methodBindings});
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
        // A class is represented by the same StructDeclaration path as a struct (ADR
        // 0026), so it reaches this check here and receives the same layout rules.
        if (s.destructor) {
            // A destructor is lowered as an explicit function the program calls.
            // Fin has not ruled that one runs implicitly at scope exit -- memory
            // management is a library (ADR 0003) and nothing here makes storage
            // die -- so emitting the body without wiring it to any scope is honest:
            // the destructor exists, is callable, and nothing silently skips it.
            // The day an implicit scope-exit rule exists, the wire-up goes where the
            // rule is written and this stays the body.
            if (!lowerableDestructor(*s.destructor, s.name)) return false;
        }
        if (!lowerableMethods(s)) return false;
        if (!lowerableOperators(s)) return false;
        // Constructors use one symbol per struct, matching the analyzer's current
        // constructorFor rule: overload resolution is deliberately not invented here.
        // The first declaration wins, and every other overload is refused by name.
        //
        // The count spans the `implements` blocks: `Collection<T> implements
        // <NoLengthCollection> { Collection() {...} }` (stdlib/collection.fin:103)
        // writes a constructor of a struct that may already declare one, and two of them
        // are two definitions of `Collection.constructor` however they are spread over
        // the file. The analyzer registers both against the same StructType
        // (addConstructor) and selects `constructors[0]`, so the ambiguity is real on
        // both sides rather than an artefact of this table.
        const StructExtras* extras = extrasFor(s.name);
        const size_t ctors = s.constructors.size() +
                             (extras ? extras->constructors.size() : 0);
        if (ctors > 1) {
            ASTNode& second = s.constructors.size() > 1
                                  ? static_cast<ASTNode&>(*s.constructors[1])
                                  : static_cast<ASTNode&>(*extras->constructors[
                                        s.constructors.empty() ? 1 : 0]);
            unsupported(second, fmt::format("constructor overloads on struct '{}'",
                                            s.name));
            return false;
        }
        for (auto& m : s.members) {
            for (auto& attr : m->attributes) {
                // `debug` is observational only; it does not affect field layout.
                // Other field attributes remain refused until their layout and
                // runtime semantics are lowered explicitly.
                if (attr->name == "debug" && attr->is_flag) continue;
                unsupported(*m, fmt::format("the attribute '{}' on field '{}' of "
                                            "struct '{}'", attr->name, m->name, s.name));
                return false;
            }
        }
        for (auto& parent : s.parents) {
            if (!parent) continue;
            // An interface contributes no bytes and no fields, so implementing one
            // changes nothing about the struct's shape -- which is what makes
            // `struct HashMap<T, U> : <Index, IndexAssign>` (stdlib/hashmap.fin:15)
            // and `struct ChangableSomehow: <UnchanableString>` (readonly.fin:34)
            // lowerable as the plain structs they are. src/types/Layout.cpp:427 skips
            // an interface parent for the same reason and in the same words.
            //
            // What is *not* claimed here: that the struct satisfies the interface.
            // Nothing in this file checks that, and nothing needs to -- the analyzer
            // reports `X does not implement Y`, and an unimplemented method is a call
            // that finds no symbol rather than a wrong lowering. ADR 0019 rules that an
            // interface *reference* is two words; no corpus site takes one, so the day
            // one does is the day that needs its own unit rather than this one.
            if (parentIsInterface(*parent)) continue;
            // A base struct's fields splice into this struct at offset 0 (the owner's
            // ruling; src/types/Layout.cpp:428-451 already computes it, and the second
            // pass of declareStructs now copies them). What is still refused is a base
            // this file did not itself lower -- a template, a class, or a struct
            // refused for one of the shapes above -- because splicing in fields from a
            // shape this file declined to give a layout would be inventing one.
            if (!structs_.count(parent->name)) {
                unsupported(s, fmt::format("struct '{}' inheriting '{}', which is not a "
                                           "struct this file lowered",
                                           s.name, parent->name));
                return false;
            }
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
        for (auto& m : s.methods)
            if (!lowerableMethod(*m, s.name, seen)) return false;
        // The methods an `implements` block wrote for this struct, on exactly the terms
        // its own are checked on -- one `seen` set across both, so a block method of a
        // name the struct already declares is refused as the second definition of one
        // symbol rather than quietly losing to whichever was declared first.
        if (const StructExtras* extras = extrasFor(s.name))
            for (FunctionDeclaration* m : extras->methods)
                if (!lowerableMethod(*m, s.name, seen)) return false;
        return true;
    }

    // One method's share of lowerableMethods, so a method written in an `implements`
    // block is checked by the same code and not by a copy of it. `seen` is the caller's,
    // so a block's methods and the struct's own share one namespace.
    bool lowerableDestructor(DestructorDeclaration& d, const std::string& sname) {
        if (!d.body) {
            unsupported(d, fmt::format("a destructor declaration on struct '{}'", sname));
            return false;
        }
        return true;
    }

    bool lowerableMethod(FunctionDeclaration& m, const std::string& sname,
                         std::set<std::string>& seen) {
        // An attribute this file does not read may be the one that decides
        // linkage (`#[export]`) or which of two definitions wins
        // (`#[overwrite]`), and a method is a symbol like any other.
        if (!attributesAreJustLlvmName(m, m.attributes, "method")) return false;
        for (auto& attr : m.attributes) {
            // The valued form is what attributesAreJustLlvmName lets through, and
            // a method may not have it: an instantiation's method is emitted once
            // per binding, and one name over two of them is either a duplicate
            // definition or a silent `general_point.1` that nobody can call. This
            // is the generic-function rule (declareTopLevel) applied one level in.
            unsupported(m, fmt::format("the attribute '{}' on the method '{}' of "
                                       "struct '{}'", attr->name, m.name, sname));
            return false;
        }
        if (!seen.insert(m.name).second) {
            // Two methods of one name would be two definitions of one symbol, and
            // declareFunction keeps the first -- so the second body would silently
            // not be the one that runs. Overload resolution is the analyzer's
            // (`constructors[0]` is the state of it), and until it exists there is
            // no way to tell which the call meant.
            unsupported(m, fmt::format("a second method '{}' on struct '{}'",
                                       m.name, sname));
            return false;
        }
        for (auto& param : m.params) {
            if (!param->is_vararg) continue;
            // `...` on a Fin definition needs va_start, which is a library
            // question (ADR 0003), and on a method it is not written anywhere in
            // the corpus.
            unsupported(*param, fmt::format("'...' on the method '{}' of struct '{}'",
                                            m.name, sname));
            return false;
        }
        if (m.is_static && !m.params.empty() && m.params[0]->name == "self") {
            // A static method has no receiver, and the analyzer drops a parameter
            // called `self` wherever it appears (buildMethodSignature). So this
            // parameter exists for the caller and not for the callee, or the other
            // way round, depending on which pass you ask -- and either way the
            // arguments after it land one place out.
            unsupported(*m.params[0],
                        fmt::format("a 'self' parameter on the static method '{}' of "
                                    "struct '{}'", m.name, sname));
            return false;
        }
        for (size_t i = 0; i < m.params.size(); ++i) {
            if (m.params[i]->name != "self" || i == 0) continue;
            // The receiver is parameter 0 or it is injected. A `self` written
            // second is not a receiver the analyzer dropped from the signature
            // (buildMethodSignature drops it wherever it is), so lowering it as
            // one would shift every argument by a place.
            unsupported(*m.params[i],
                        fmt::format("a 'self' parameter in position {} of method "
                                    "'{}' on struct '{}'", i + 1, m.name, sname));
            return false;
        }
        return true;
    }

    // The operator shapes that are wrong however the struct is used, checked where the
    // struct is written. lowerableMethods' counterpart, and the same split: an
    // operator's *body* is checked where it is emitted, once per instantiation.
    bool lowerableOperators(StructDeclaration& s) {
        std::set<std::string> seen;
        for (auto& o : s.operators)
            if (!lowerableOperator(*o, s.name, seen)) return false;
        // The operators an `implements` block wrote for this struct, on the same terms
        // and against the same `seen` set -- so a block operator of a token the struct
        // already declares is the second definition of one symbol here too.
        if (const StructExtras* extras = extrasFor(s.name))
            for (OperatorDeclaration* o : extras->operators)
                if (!lowerableOperator(*o, s.name, seen)) return false;
        return true;
    }

    // One operator's share of lowerableOperators. lowerableMethod's counterpart, split
    // out for the same reason: a block's operators go through this code and not a copy.
    bool lowerableOperator(OperatorDeclaration& o, const std::string& sname,
                           std::set<std::string>& seen) {
        const std::string spelling = spellOperator(o.op);
        if (spelling.empty()) {
            // A token this file cannot turn back into characters has no symbol to
            // be declared under and no name to appear in a diagnostic, and picking
            // one would put a symbol in the object file that no reader can trace to
            // a line. `operator (...args)` is the one the corpus has.
            unsupported(o, fmt::format("an operator on struct '{}' whose token has "
                                       "no spelling", sname));
            return false;
        }
        if (!seen.insert(spelling).second) {
            // Two operators of one token are two definitions of one symbol, and
            // declareFunction keeps the first -- so the second body would silently
            // not be the one that runs. Which of the two a use meant is overload
            // resolution, and the analyzer has none for an operator (it does not
            // even check the arity), so there is nothing to pick with.
            unsupported(o, fmt::format("a second operator '{}' on struct '{}'",
                                       spelling, sname));
            return false;
        }
        for (auto& param : o.params) {
            if (!param->is_vararg) continue;
            // `...` needs va_start, which is a library question (ADR 0003), and an
            // operator with one is not written anywhere in the corpus.
            unsupported(*param, fmt::format("'...' on the operator '{}' of struct "
                                            "'{}'", spelling, sname));
            return false;
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
    //
    // The erasure marker is deliberately not in this list, though it too is a
    // property of the template rather than of one instantiation. It is checked at
    // the instantiation instead, for the reason a template is registered rather
    // than declared in the first place: a template nobody names is not code, so
    // there is nothing about it for this pass to get wrong. See refuseIfErased.
    bool lowerableTemplate(StructDeclaration& s) { return lowerableStruct(s); }

    // `Castable` -- the erasure marker, and the reason an erased generic is refused
    // rather than monomorphised. ADR 0002 carries two of pyprototype's lowering
    // decisions forward deliberately: erasure is selected by the presence of an
    // erasure-marker constraint on any one parameter, and an erased generic is
    // represented as a raw pointer. That is a different representation and not a
    // different spelling, so a monomorphised body for one is a *different program* --
    // it happens to agree wherever the argument is a scalar and disagrees wherever
    // the erased pointer is what the code is about.
    //
    // One name, because one name is what the corpus writes:
    // generics_interfaces.fin:8 (`<T: Castable, U: Castable>`), nullifier.fin:10,
    // deeptest2.fin:13, lambdas.fin:69. The analyzer registers it as a type
    // (Analyzer_Core.cpp:142, "Mock Castable") rather than the corpus declaring it,
    // so there is nothing to read the marker-ness off except the name.
    static bool isErasureMarker(const std::string& name) { return name == "Castable"; }

    // The marked parameter, or nothing. The *first* one, because the message names one
    // and the first is the one a reader's eye is already on: `<T: Castable, U:
    // Castable>` (generics_interfaces.fin:8) has two and fixing either alone fixes
    // nothing, so naming both would be two lines saying the same thing.
    static const GenericParam* erasureMarkerOf(
            const std::vector<std::unique_ptr<GenericParam>>& params) {
        for (auto& p : params) {
            if (p->constraint && isErasureMarker(p->constraint->name)) return p.get();
        }
        return nullptr;
    }

    // Refused at the *use* and not at the declaration, which is the ruling this
    // predicate exists to hold to.
    //
    // A marker is a property of the template -- `maybe<int>` is not going to stop
    // being erased -- and that argument once put the refusal on the declaration. It is
    // the wrong argument, because it answers a question nobody asked. Monomorphisation
    // means a template is a recipe and not code: `fun ident<T>(a: T)` that nothing
    // calls emits nothing and costs nothing (AGenericFunctionNobodyCallsLowersToNothing),
    // and `struct M <T> {}` that nothing instantiates is a whole sample's worth of
    // evidence (blame_assert.fin:19) that an uninstantiated template is not an error
    // either. An erasure marker on one of those is a representation decision for code
    // this object file does not contain. Refusing it withholds a correct object for a
    // program that never poses the question -- which is what generics_interfaces.fin
    // was: its only blocker was an uncalled `using_erasure_generics`, and everything
    // the file actually does is monomorphic.
    //
    // So the marker is checked where the representation is first needed: the call for
    // a function template, the instantiation for a struct template, the call for a
    // generic method. Each of those is a point at which this pass would otherwise have
    // to lay something out, and laying it out monomorphically is the different program
    // ADR 0002 warns about.
    //
    // `what` is the whole noun phrase and not a bare word, because the three sites
    // name different things ("the generic function 'erased'", "the generic method
    // 'peek' of struct 'Box'") and a shared format string that tried to build those
    // from parts would be the two callers' grammar living here.
    bool refuseIfErased(ASTNode& node,
                        const std::vector<std::unique_ptr<GenericParam>>& params,
                        const std::string& what) {
        const GenericParam* p = erasureMarkerOf(params);
        if (!p) return false;
        unsupported(node, fmt::format("the erasure marker '{}' on '{}' of {}",
                                      p->constraint->name, p->name, what));
        return true;
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

        // At the instantiation and not at the declaration, for the same reason the
        // function's is at the call: `struct maybe<T: Castable>` (nullifier.fin:10)
        // that nothing names has no layout to be wrong about, and `maybe<int>` is the
        // first point at which one is needed. Ahead of the argument count so that a
        // marked template with the wrong arity says which of the two it is by naming
        // the marker -- the arity is the analyzer's and this is the representation.
        if (refuseIfErased(const_cast<TypeNode&>(node), tmpl.generic_params,
                           fmt::format("the generic struct '{}'", tmpl.name))) {
            return false;
        }

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
            // `M<int>` where `struct M <T> {}` (blame_assert.fin:19), and the same one
            // byte the non-generic path above lays down for the same reason. The two
            // sites stay separate because the template's own emptiness is not a
            // decision -- a template has no layout at all until someone names an
            // argument -- but once `M<int>` is asked for it is a struct like any other
            // and has to answer with a size.
            members.push_back(llvm::Type::getInt8Ty(ctx_));
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
        // The blocks written on the template are this instantiation's: `Result<T, U>
        // implements <IResult>` (stdlib/typing.fin:27) is filed under `Result`, and
        // `Result<int, string>` is what a method of it is declared for. Keyed by the
        // written name for exactly this reason -- a block cannot be written on a
        // mangled name, because nobody writes one.
        live.extras = extrasFor(tmpl.name);
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
            case CgType::Kind::Prototype:
                // The Fin spelling, so an instantiation keyed on a prototype argument
                // reads back as the program wrote it. Its two halves are dynamic arrays
                // and a dynamic array's extent is 0, so `cgDisplay` of one would say
                // `[int, 0]` -- the key is built from the *element* types instead, which
                // is what actually distinguishes two prototypes.
                return fmt::format("prototype<{}, {}>",
                                   t.keys && t.keys->element ? cgDisplay(*t.keys->element)
                                                             : "?",
                                   t.values && t.values->element
                                       ? cgDisplay(*t.values->element)
                                       : "?");
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
            if (auto* lambda = dynamic_cast<LambdaExpression*>(var->initializer.get())) {
                if (!lambda->generic_params.empty()) {
                    // A generic lambda at module scope. Inside a body this declaration
                    // registers a template and emits nothing; here there is nowhere to
                    // register it -- `lambdaTemplates_` is pushed and popped with the
                    // scopes, and this runs before any body has one. Refused with the
                    // question named rather than left to the initialiser path, which
                    // would say "used as a value" and send a reader to a boundary that
                    // is not the one in the way.
                    unsupported(*var,
                                fmt::format("the generic lambda '{}' declared at module "
                                            "scope", var->name));
                    return;
                }
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
                    // The erasure marker is *not* checked here, and this is where the
                    // difference is easiest to see: registering a template emits
                    // nothing, so a marked one that nothing calls has asked for no
                    // representation. emitTemplateCall refuses at the call instead.
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
                if (!defineAttributesAreReadable(*def)) return;
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

    // The rule above, with `#[global]` added, for an `@define` and for nothing else.
    //
    // `#[global]` asks this file for nothing, and unlike every other attribute the
    // refusal above is written against, that is provable rather than assumed: the
    // attribute's entire meaning is that the *analyzer* publishes the name into an
    // ambient scope so a file resolves it with no import (ADR 0021), and by the time a
    // program reaches here the front end has either done that or reported why it could
    // not. It changes no signature, no symbol and no linkage -- an extern's linkage is
    // external whatever is written above it, because there is nothing to emit.
    //
    // Accepted here rather than in `attributesAreJustLlvmName`, because only a
    // `DefineDeclaration` publishes: `#[global]` on a `fun`, a `struct`, an `interface`,
    // an `enum` or a `type` parses and validates and then binds nothing anywhere, so
    // accepting it on one of those would be this file claiming to have honoured what
    // nothing honoured -- which is the failure the refusal exists to prevent.
    //
    // Two spellings reach this, and both must build. `printf` called with no import is
    // the bundled `lib/std/stdio.fin` declaration, whose prototype the driver splices in
    // with only `#[llvm_name]` kept (ModuleLoader::appendAmbientPrototypes); a file that
    // writes `namespace std { #[global] @define ... }` itself keeps its attribute all the
    // way here, because the tree the backend walks is the file's own. Refusing the second
    // while the first works would make where a declaration was written decide whether it
    // lowers.
    //
    // `#[export]` is deliberately not on this list. It is the same kind of front-end
    // fact -- what a module's scope hands to an import -- but nothing needs it accepted
    // here yet: the one declaration in the tree that carries it is published from a
    // module, and the splice strips it. Adding it would be widening the set with no case
    // asking, and the set is what keeps an unread attribute from being dropped.
    bool defineAttributesAreReadable(DefineDeclaration& node) {
        for (auto& attr : node.attributes) {
            if (attr->name == "llvm_name" && !attr->is_flag) continue;
            if (attr->name == kGlobalAttribute && attr->is_flag) continue;
            unsupported(node, fmt::format("the attribute '{}' on a '@define'", attr->name));
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

    // Whether two function values may stand in for each other, which with opaque
    // pointers nothing in the IR can answer.
    //
    // Structural and not by identity: a `fn(int) -> int` mapped at a parameter and one
    // mapped at a variable are two CgTypes built from two TypeNodes, and they are the
    // same type. Signedness is compared along with width because `int` and `uint` are
    // both i32 and a call that swapped them would pick the wrong extension for every
    // argument it widened.
    static bool sameSignature(const CgType& a, const CgType& b) {
        if (!a.isFn() || !b.isFn() || !a.result || !b.result) return false;
        if (a.params.size() != b.params.size()) return false;
        if (!sameType(*a.result, *b.result)) return false;
        for (size_t i = 0; i < a.params.size(); ++i) {
            if (!a.params[i] || !b.params[i]) return false;
            if (!sameType(*a.params[i], *b.params[i])) return false;
        }
        return true;
    }

    // Type equality for the one question sameSignature asks. Deliberately shallow
    // outside Fn: a struct's identity in this file is its StructInfo pointer and an
    // array's is its element and extent, both of which the llvm::Type already
    // distinguishes, so llvmType is the whole comparison for them. A pointer's pointee
    // is *not* compared, matching the rest of the file -- `&int` and `&char` are one
    // `ptr` everywhere else here, and making a function signature stricter than an
    // assignment would refuse programs the rest of this file accepts.
    static bool sameType(const CgType& a, const CgType& b) {
        if (a.kind != b.kind) return false;
        if (a.isFn()) return sameSignature(a, b);
        if (a.kind == CgType::Kind::Int)
            return a.bits == b.bits && a.isSigned == b.isSigned && a.isBool == b.isBool;
        return a.llvmType == b.llvmType;
    }

    // How a CgType reads in a diagnostic, for the refusals that have no TypeNode to
    // spell. Only the shapes that reach one: a conversion involving a function type is
    // the single caller, and everything else on either side of it is named by kind
    // rather than in full, because "a conversion from 'fn(int) -> int' to a struct" says
    // what went wrong and a full struct spelling would not say more.
    std::string describe(const CgType& t) const {
        switch (t.kind) {
            case CgType::Kind::Void:   return "void";
            case CgType::Kind::Struct: return "a struct";
            case CgType::Kind::Array:  return "an array";
            case CgType::Kind::Ptr:    return "a pointer";
            case CgType::Kind::Int:    return t.isBool ? "bool" : "an integer";
            case CgType::Kind::Float:  return "a float";
            case CgType::Kind::Prototype: return "a prototype";
            case CgType::Kind::Fn:     break;
        }
        std::string out = "fn(";
        for (size_t i = 0; i < t.params.size(); ++i) {
            if (i) out += ", ";
            out += t.params[i] ? describe(*t.params[i]) : "?";
        }
        return out + ") -> " + (t.result ? describe(*t.result) : "?");
    }

    // Widens or narrows a value to `target` when the analyzer has already ruled the
    // program well-typed. It converts and never checks: `int` into `long`, an
    // integer constant into a float parameter. A pair it cannot convert is a gap in
    // this slice, not a type error, so it refuses rather than emitting a bitcast.
    bool interfaceMethodSignature(const InterfaceInfo& iface, const std::string& name,
                                  llvm::FunctionType*& signature, CgType& result,
                                  std::vector<CgType>& params) {
        for (auto* method : iface.methods) {
            if (!method || method->name != name) continue;
            auto ret = types_.map(method->return_type.get());
            if (!ret) return false;
            result = *ret;
            std::vector<llvm::Type*> llvmParams{llvm::PointerType::get(ctx_, 0)};
            for (const auto& p : method->params) {
                if (!p || p->name == "self" || p->is_vararg) continue;
                auto mapped = types_.map(p->type.get());
                if (!mapped || mapped->isVoid()) return false;
                params.push_back(*mapped);
                llvmParams.push_back(mapped->llvmType);
            }
            signature = llvm::FunctionType::get(result.llvmType, llvmParams, false);
            return true;
        }
        return false;
    }

    bool emitInterfaceMethodCall(MethodCall& node, const CgVal& object,
                                 const InterfaceInfo& iface) {
        llvm::FunctionType* signature = nullptr;
        CgType result;
        std::vector<CgType> params;
        if (!interfaceMethodSignature(iface, node.method_name, signature, result, params)) {
            unsupported(node, fmt::format("the method '{}' of interface '{}'",
                                          node.method_name, iface.finName));
            return false;
        }
        auto value = object.value;
        auto* data = builder_.CreateExtractValue(value, {0}, "data");
        auto* table = builder_.CreateExtractValue(value, {1}, "vtable");
        size_t slot = iface.fields.size();
        for (size_t i = 0; i < iface.methods.size(); ++i) {
            if (iface.methods[i] && iface.methods[i]->name == node.method_name) {
                slot += i;
                break;
            }
        }
        auto* entryPtr = builder_.CreateInBoundsGEP(llvm::PointerType::get(ctx_, 0), table,
            llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx_), slot));
        auto* entry = builder_.CreateLoad(llvm::PointerType::get(ctx_, 0), entryPtr, "method");
        std::vector<llvm::Value*> args{data};
        if (node.args.size() != params.size()) {
            unsupported(node, fmt::format("a call to interface method '{}' with wrong arity",
                                          node.method_name));
            return false;
        }
        for (size_t i = 0; i < params.size(); ++i) {
            CgVal arg = emitAs(*node.args[i], params[i]);
            if (failed_ || !arg.ok()) return false;
            auto* converted = convert(node, arg, params[i]);
            if (!converted) return false;
            args.push_back(converted);
        }
        auto* call = builder_.CreateCall(signature, entry, args);
        if (!result.isVoid()) value_ = CgVal{call, result};
        return true;
    }

    llvm::Value* interfaceVtable(const CgType& source, const InterfaceInfo& iface) {
        const std::string key = source.structInfo->finName + "." + iface.finName;
        auto found = interfaceVtables_.find(key);
        if (found != interfaceVtables_.end()) return found->second;
        auto* ptrTy = llvm::PointerType::get(ctx_, 0);
        auto* tableTy = llvm::ArrayType::get(ptrTy, iface.fields.size() + iface.methods.size());
        std::vector<llvm::Constant*> entries;
        for (const auto& field : iface.fields) {
            size_t index = 0;
            if (!source.structInfo->find(field.name, index)) return llvm::ConstantPointerNull::get(ptrTy);
            const auto* layout = module_.getDataLayout().getStructLayout(
                llvm::cast<llvm::StructType>(source.llvmType));
            auto* offset = llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx_),
                                                   layout->getElementOffset(index));
            entries.push_back(llvm::ConstantExpr::getIntToPtr(offset, ptrTy));
        }
        for (const auto* method : iface.methods) {
            // The base's method satisfies the interface the derived struct declares.
            // Looked up through the hierarchy because the alternative already here was
            // a null slot, and a call through one is a jump to address zero rather than
            // a diagnostic.
            //
            // No corpus site reaches this yet: the analyzer reports `Struct 'Talker'
            // does not implement interface 'Speaker'` when the method that satisfies it
            // is the base's (measured), so today the shape stops before the backend.
            // The lookup is still the right one -- what a vtable slot holds is this
            // file's question whoever answers the analyzer's.
            //
            // Only a base whose fields sit where its own methods expect them, because
            // the entry is called with the *derived* object's data pointer and nothing
            // adjusts it. A second base's method would read the first base's fields, so
            // the slot stays null rather than becoming a wrong read; the interface's own
            // unit is where a thunk that adjusts the pointer belongs.
            const StructInfo* provider =
                findMethodProvider(*source.structInfo, method->name);
            if (provider && provider != source.structInfo &&
                !baseSharesLayout(*source.structInfo, *provider)) {
                provider = nullptr;
            }
            auto fn = provider ? functions_.find(methodKey(provider->finName, method->name))
                               : functions_.end();
            if (fn == functions_.end()) {
                entries.push_back(llvm::ConstantPointerNull::get(ptrTy));
            } else {
                entries.push_back(llvm::cast<llvm::Constant>(fn->second.fn));
            }
        }
        auto* init = llvm::ConstantArray::get(tableTy, entries);
        auto* global = new llvm::GlobalVariable(module_, tableTy, true,
            llvm::GlobalValue::LinkOnceODRLinkage, init, "fin.vtable." + key);
        interfaceVtables_[key] = global;
        return builder_.CreateBitCast(global, ptrTy);
    }

    llvm::Value* convert(ASTNode& node, const CgVal& from, const CgType& to) {
        if (!from.ok()) return nullptr;
        // Before the identity shortcut below, and that is the whole point of putting it
        // here. Every function value is a `ptr`, so `from.type.llvmType ==
        // to.llvmType` is true for *any* pair of them -- and for a `&int` against an
        // `fn` too. Left to that line, `fn(int) -> int` would be accepted where
        // `fn(int, int) -> int` was wanted, and the indirect call through it would read
        // an argument register the caller never set. Nothing later would notice,
        // because there is nothing later to notice with.
        if (from.type.isFn() || to.isFn()) {
            // `null` into a function slot is a null function pointer, which is a value
            // a function type has (stdlib/collection.fin:18 writes one as a field
            // default). Recognised by the constant rather than by the type, because a
            // bare `null` is a Ptr with no pointee and so is a `string`.
            if (to.isFn() && llvm::isa<llvm::ConstantPointerNull>(from.value))
                return from.value;
            if (!from.type.isFn() || !to.isFn() || !sameSignature(from.type, to)) {
                unsupported(node, fmt::format("a conversion from '{}' to '{}'",
                                              describe(from.type), describe(to)));
                return nullptr;
            }
            return from.value;
        }
        if (from.type.isInterface && to.isInterface) return from.value;
        if (to.isInterface && from.type.isStruct() && !from.type.isInterface) {
            if (!from.address || !to.interfaceInfo) {
                unsupported(node, "an interface conversion from a value without an address");
                return nullptr;
            }
            auto* data = builder_.CreateBitCast(from.address, llvm::PointerType::get(ctx_, 0));
            auto* vtable = interfaceVtable(from.type, *to.interfaceInfo);
            llvm::Value* pair = llvm::UndefValue::get(to.llvmType);
            pair = builder_.CreateInsertValue(pair, data, {0});
            pair = builder_.CreateInsertValue(pair, vtable, {1});
            return pair;
        }
        if (to.isStruct() && from.type.isPointer() && from.type.pointee &&
            from.type.pointee->isStruct()) {
            return builder_.CreateLoad(to.llvmType, from.value, "constructed");
        }
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
            // Before the globals, because a poisoned local shadows one exactly as a
            // live local does: answering with the global's address would lower a read
            // of the wrong variable.
            if (failed_) return std::nullopt;
            // A global has an address for the same reasons a local does, and being one
            // is the whole of what makes `Counter = Counter + 1` and `Cells[1] = 42`
            // work at module scope: everything past this point is the same code.
            auto global = globals_.find(id->name);
            if (global != globals_.end())
                return Addr{global->second.var, global->second.type};
            // A name from the enclosing body, reached for its *address* -- which is what
            // `x = 1` inside a lambda is. Refused here as well as in visit(Identifier&)
            // because the two paths never meet: a read goes through the visitor and an
            // assignment target comes here, and only reporting the read would leave a
            // write to a captured local falling through to "this assignment target",
            // which names the wrong thing.
            // `&id` and `id = ...` on a generic lambda. Refused here as well as in
            // visit(Identifier&) for the reason a capture is: a read goes through the
            // visitor and an address comes here, so reporting only the read would leave
            // `&id` falling through to "the address of a value with no home" -- which
            // names a lifetime question, and the answer here is that there is no value.
            if (LambdaTemplate* tmpl = lambdaTemplateFor(id->name)) {
                (void)tmpl;
                unsupported(*id, fmt::format("the address of the generic lambda '{}'",
                                             id->name));
                return std::nullopt;
            }
            if (refuseIfCapture(*id, id->name)) return std::nullopt;
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

            // `p.0` and `p.1` are the prototype's two halves, and they have addresses
            // for the same reason a struct field does: the prototype is a struct here,
            // and the half is the field at that index. Addressed rather than extracted
            // wherever there is an address, because `p.0[1]` and `p.0.length` both
            // arrive through here -- the first as an ArrayAccess whose array is this,
            // the second as visit(MemberAccess&)'s `.length` path asking baseAddress
            // for an array -- and an extractvalue would give them a copy of the pair
            // with no home to index into.
            //
            // The position is checked against 2 rather than against the LLVM struct's
            // arity: they are the same number today, and saying it in terms of the
            // representation is what makes `p.2` refuse instead of GEPping past the end
            // if a third word is ever added for a reason this comment does not know.
            if (size_t position = 0; positionalMember(member->member, position)) {
                auto proto = baseAddress(*member->object, CgType::Kind::Prototype);
                if (failed_) return std::nullopt;
                if (proto && proto->type.isPrototype() && position < 2 &&
                    proto->type.keys && proto->type.values) {
                    const CgType& half = position == 0 ? *proto->type.keys
                                                       : *proto->type.values;
                    llvm::Value* ptr = builder_.CreateStructGEP(
                        proto->type.llvmType, proto->ptr, (unsigned)position,
                        position == 0 ? "keys" : "values");
                    return Addr{ptr, half};
                }
                // Not a prototype, or a position it does not have. Falls through: an
                // enum payload's `.0` is a different question with its own answer, and
                // a struct's is a refusal that visit(MemberAccess&) words.
            }

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
            llvm::Value* ptr = nullptr;
            if (base->type.isDynamicArray) {
                llvm::Value* pair = builder_.CreateLoad(base->type.llvmType, base->ptr, "array");
                llvm::Value* data = builder_.CreateExtractValue(pair, {0}, "data");
                ptr = builder_.CreateInBoundsGEP(base->type.element->llvmType, data, i, "elem");
            } else {
                ptr = builder_.CreateInBoundsGEP(
                    base->type.llvmType, base->ptr,
                    {builder_.getInt64(0), i}, "elem");
            }
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
        return baseOf(emitAddress(object), want);
    }

    // The pointer hop on its own, for a caller that already holds the address.
    //
    // Split out for exactly one reason: emitAddress *emits*. `p[i++].get()`'s index runs
    // when its address is taken, so a caller that asked for an address, did not like the
    // kind, and asked again would run the increment twice. visit(MethodCall&) asks twice
    // -- once for a prototype and once for a struct -- and asks through this.
    std::optional<Addr> baseOf(std::optional<Addr> direct, CgType::Kind want) {
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
        // A `fun` written inside another body is a different declaration from a
        // module-scope one and is emitted by its own path: it publishes no module
        // symbol, it is reached through the enclosing body's scopes, and the module
        // pre-pass never saw it. Checked first, because everything below assumes the
        // name was already declared by declareTopLevel.
        if (currentFn_) { emitNestedFunction(node); return; }
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

    // `fun recursive(a: int) <int> { ... }` written inside another body --
    // tests/samples/loops.fin:40, the corpus's only nested function declaration.
    //
    // It lowers to an ordinary function with a name nobody outside can write. That is
    // derived rather than chosen: the analyzer registers a nested `fun` in the enclosing
    // *body's* scope (Analyzer_Decl.cpp step 6 defines it in `currentScope->parent`,
    // which for a nested declaration is the enclosing body and not the module), so the
    // name is visible from the declaration to the end of that scope and nowhere else --
    // a sibling function cannot call it and a call written above it is "Undefined
    // function or type". A body that is reached by name from one scope and by nothing
    // else is a function with internal linkage; there is nothing else it could be.
    //
    // Its body is emitted here, at the declaration, rather than queued. A queue would
    // work and is what a method uses, but a nested function may name the enclosing
    // body's other nested functions and those are a property of *where* it was written;
    // emitting it here is what makes that set be the set at this point in the walk
    // rather than the set at the end of the body.
    //
    // What it may *not* do is read the enclosing frame. `loops.fin`'s one instance
    // captures nothing -- `recursive` reads its own parameter and calls itself -- so the
    // corpus does not say what a capture means, and a body that quietly loaded a slot
    // from a frame that is about to be gone is the miscompile this refuses instead. It
    // is the same rule a lambda already has and it is the same code: `enclosingNames_`
    // and refuseIfCapture, with `captureKind_` saying which of the two a reader is
    // looking at.
    void emitNestedFunction(FunctionDeclaration& node) {
        if (node.body == nullptr) {
            // `fun h() <int>;` inside a body. At module scope this is a prototype for a
            // definition elsewhere, and "elsewhere" is a scope that ends with this one:
            // nothing outside can define it and nothing inside is required to. Refused
            // rather than treated as an extern, which would emit a call to a symbol no
            // object file contains.
            unsupported(node, fmt::format("a nested function '{}' with no body", node.name));
            return;
        }
        if (!node.generic_params.empty()) {
            // A template, and a template is not code until a call says what its type
            // parameters are. The module-scope path registers one in `fnTemplates_` and
            // instantiates it at the call; that table is keyed by the written name with
            // no scope in it, so a nested template of a name the module also uses would
            // silently be one or the other. Refused until there is a case asking.
            unsupported(node, fmt::format("a nested generic function '{}'", node.name));
            return;
        }
        if (!node.attributes.empty()) {
            // Not even `#[llvm_name]`, which the module-scope path does read. That
            // attribute names the symbol this declaration publishes, and a nested
            // function publishes none -- honouring it would put an externally visible
            // name on a function only one scope can call, and ignoring it would drop an
            // attribute the writer expected to change the object.
            unsupported(node, fmt::format("the attribute '{}' on the nested function '{}'",
                                          node.attributes.front()->name, node.name));
            return;
        }
        if (node.is_public) {
            // `pub` says what a *module's* scope hands to an import. A nested function is
            // not in one, so there is nothing for the keyword to make public, and
            // accepting it would be this file claiming to have honoured what nothing
            // honoured.
            unsupported(node, fmt::format("'pub' on the nested function '{}'", node.name));
            return;
        }
        if (scopes_.empty() || nested_.empty()) {
            // currentFn_ is set, so emitBodyOf has pushed a scope. Arriving here without
            // one is this file disagreeing with itself.
            unsupported(node, fmt::format("a nested function '{}' outside a scope", node.name));
            return;
        }
        if (scopes_.back().count(node.name)) {
            // `let h <int> = 3; fun h() <int> { ... }` in one scope. The analyzer's
            // step 6 *overwrites* the symbol, so every read of `h` after this line means
            // the function and the variable becomes unnameable while its storage is still
            // live. One name over a slot and a symbol in the same scope is a state this
            // file's two tables cannot both hold, and picking either one silently gets a
            // program wrong: refused, and the corpus writes nothing like it.
            unsupported(node, fmt::format("a nested function '{}' whose name a variable in "
                                          "the same scope already has", node.name));
            return;
        }
        if (nested_.back().count(node.name)) {
            // Two nested functions of one name in one scope. `declareFunction` keeps the
            // first, so the second body would silently not be the one that runs -- the
            // same reason a second method on a struct is refused, one level in.
            unsupported(node, fmt::format("a second nested function '{}' in one scope",
                                          node.name));
            return;
        }

        // `fin.nested.<n>.<name>`: internal, uniqued by a counter that only goes up, and
        // spelled with dots so that no Fin program can write it. The written name is kept
        // because the only reader of a Fin symbol name is a person reading `nm` output --
        // the same bargain `fin.lambda.<n>` and `Box<int>.get` strike.
        const std::string symbol = fmt::format("fin.nested.{}.{}", nestedFns_++, node.name);
        declareFunction(node, symbol, symbol, node.params, node.return_type.get(),
                        /*isVarArg=*/false, /*isExtern=*/false);
        auto declared = functions_.find(symbol);
        if (declared == functions_.end()) return;  // declareFunction already reported
        declared->second.fn->setLinkage(llvm::Function::InternalLinkage);

        // Registered *before* the body is emitted, which is what makes `return
        // recursive(a - 1)` find the function it is inside rather than start a second
        // one. loops.fin:44 is that call and it is the whole of why this order matters.
        nested_.back()[node.name] = symbol;

        // What the body may call: every nested function visible at this point, which
        // now includes this one -- `return recursive(a - 1)` is loops.fin:44 and it is
        // the reason the registration above happens before this line.
        nestedCarry_ = visibleNested();
        // And the generic lambdas visible here, for the reason they cross into a lambda's
        // body: a template is instantiated, not called through a frame.
        lambdaCarry_ = visibleLambdas();

        // The locals in scope here, so that a body reading one is refused as a capture
        // instead of loading a frame that is about to be gone. Saved and put back around
        // the body.
        //
        // Added to what is already there rather than replacing it: a nested function two
        // bodies deep is as unable to read `main`'s frame as it is to read the frame of
        // the function it sits directly inside, so both sets of names are captures and
        // dropping the outer one only changes which *message* the read gets -- "the name
        // 'n'", which reads as a front-end bug, in place of the boundary this actually
        // is.
        std::vector<std::string> enclosing = enclosingNames_;
        for (const auto& scope : scopes_)
            for (const auto& entry : scope) enclosing.push_back(entry.first);
        enclosing.swap(enclosingNames_);
        std::string kind = "a nested function";
        kind.swap(captureKind_);
        emitBodyOf(node, node.params, node.body.get(), nullptr, symbol);
        kind.swap(captureKind_);
        enclosing.swap(enclosingNames_);

        if (failed_) {
            // The body was refused, so the name has no code behind it. Taken back out of
            // the table and poisoned, so that a call written below it is suppressed
            // rather than reported as "a call to 'h'" -- which would send a reader to
            // implement a call that is already implemented.
            nested_.back().erase(node.name);
            poison(node.name);
        }
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
              fn_(e.currentFn_), scopes_(std::move(e.scopes_)),
              nested_(std::move(e.nested_)), lambdas_(std::move(e.lambdaTemplates_)),
              loops_(std::move(e.loops_)), poisoned_(std::move(e.poisoned_)) {
            e_.scopes_.clear();
            e_.nested_.clear();
            e_.lambdaTemplates_.clear();
            // No loop, whatever the caller was in the middle of. A `break` written in
            // a body emitted from inside a loop used to branch to that loop's exit
            // block, which is a block in another function -- `Referring to a basic
            // block in another function!`, an invalid-IR refusal in place of the honest
            // one. The analyzer permits the spelling (it sees the enclosing loop), so
            // this is the pass that has to say no, and with the loop stack empty
            // visit(BreakStatement&) already says exactly that.
            e_.loops_.clear();
            e_.poisoned_.clear();
            e_.currentFn_ = nullptr;
        }
        ~ScopedEmission() {
            e_.scopes_ = std::move(scopes_);
            e_.nested_ = std::move(nested_);
            e_.lambdaTemplates_ = std::move(lambdas_);
            e_.loops_ = std::move(loops_);
            e_.poisoned_ = std::move(poisoned_);
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
        std::vector<std::unordered_map<std::string, std::string>> nested_;
        std::vector<std::unordered_map<std::string, LambdaTemplate>> lambdas_;
        std::vector<LoopTargets> loops_;
        std::vector<std::string> poisoned_;
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
        emitBodyOf(node, params, &body, nullptr, name);
    }

    // A lambda's body, which is either a Block like every other body or a single
    // expression whose value is the return. Exactly one of `block` and `value` is set.
    //
    // The expression form goes through the same prologue rather than a second copy of
    // it, because everything before the body -- the ScopedEmission, the entry block, a
    // stack slot per parameter, the implicit tail -- is what makes a body a body, and a
    // lambda whose parameters were not given slots would be a lambda that could not
    // assign to one. `(x: int) <int> => x - 3` differs from `fun (x: int) <int> { return
    // x - 3; }` in exactly one place and this is it.
    void emitBodyOf(ASTNode& node, const std::vector<std::unique_ptr<Parameter>>& params,
                    Block* block, Expression* value, const std::string& name) {
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

        // The nested functions this body may call, if it is one of the two kinds that
        // may call any: the outermost scope of the body holds them, so the body's own
        // declarations shadow them by sitting in a scope further in. Taken and cleared,
        // so a body whose caller filled nothing -- a template instantiation, a queued
        // method -- gets none rather than the last filler's.
        nested_.back() = std::move(nestedCarry_);
        nestedCarry_.clear();

        // The generic lambdas this body may call, by the same rule and for the same
        // reason: a template is a node and a snapshot, so it survives the scope it was
        // written in, and a lambda written beside `let id <auto> = fun <T>...` calling
        // `id` is the same instantiation the enclosing body would build.
        lambdaTemplates_.back() = std::move(lambdaCarry_);
        lambdaCarry_.clear();

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

        if (block) {
            block->accept(*this);
        } else if (value) {
            emitValueBody(*value, info);
        }

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

        // Not verified once anything has been refused. A body that resumed past a
        // refusal is deliberately incomplete -- a statement that produced no value
        // emitted no instructions -- so the verifier would report that incompleteness
        // as invalid IR, which is the invented follow-on refusal in its loudest form.
        // Nothing is lost by not asking: no object is written when everFailed_ is set.
        if (everFailed_) return;
        if (llvm::verifyFunction(*info.fn, &llvm::errs())) {
            failed_ = true;
            everFailed_ = true;
            diag_.reportError(node.loc,
                              fmt::format("codegen: emitted invalid IR for '{}'", name));
        }
    }

    // The single expression that is an arrow lambda's whole body.
    //
    // `(x: int) <int> => x - 3` returns its expression and `(msg: string) <void> =>
    // printf(...)` evaluates it and returns nothing, and the return type is what tells
    // them apart -- there is no second syntax. The void case is why this cannot simply
    // always return: `printf` here is declared `<noret>`, so there is no value to hand
    // back, and CreateRet of a void call is invalid IR.
    //
    // Deliberately not routed through visit(ReturnStatement&): that one has `main`'s
    // status-code rewrite in it, and a lambda is never main.
    void emitValueBody(Expression& value, const FnInfo& info) {
        if (info.returnType.isVoid()) {
            emit(value);
            if (failed_) return;
            builder_.CreateRetVoid();
            return;
        }
        CgVal v = emitAs(value, info.returnType);
        if (failed_) return;
        if (!v.ok()) {
            unsupported(value, "this lambda's body expression");
            return;
        }
        llvm::Value* out = convert(value, v, info.returnType);
        if (!out) return;
        builder_.CreateRet(out);
    }

    // One function template, in the form the instantiation path reads it.
    //
    // Two things are templates a call may instantiate -- a named `fun ident<T>` and a
    // generic lambda bound to a name -- and they differ in four fields and in nothing
    // else. Passing those four rather than the node is what lets emitTemplateCall and
    // instantiateTemplate be one copy each: a second copy would be a second inference
    // rule and a second mangling for what is one spelling of one thing, and the two
    // would drift at the first refusal added to either.
    //
    // `keyBase` is separate from `display` because they answer different questions. The
    // key is a symbol -- `ident<int>`, `fin.lambda.3<int>` -- and a lambda's has to be
    // one no Fin program can write, since two lambdas bound to the same name in two
    // scopes are two templates. The display is what a diagnostic says, which is the name
    // the program wrote.
    struct TemplateCallee {
        ASTNode* node = nullptr;
        std::string display;
        std::string keyBase;
        const std::vector<std::unique_ptr<GenericParam>>* generics = nullptr;
        const std::vector<std::unique_ptr<Parameter>>* params = nullptr;
        const TypeNode* returnType = nullptr;
        // Exactly one of these is set, and which one is the whole of the difference
        // between a `fun` body and `=> expr`. See emitBodyOf.
        Block* block = nullptr;
        Expression* value = nullptr;
        // The environment a lambda instance's body is emitted with, and null for a named
        // function -- which has none to have, being written at module scope.
        const LambdaTemplate* lambda = nullptr;
        // "the generic function" or "the generic lambda". The whole noun, because the
        // messages read `... of the generic lambda 'id'` and building that from parts
        // would put the caller's grammar here.
        std::string kind;
    };

    static TemplateCallee calleeOf(FunctionDeclaration& tmpl) {
        TemplateCallee c;
        c.node = &tmpl;
        c.display = tmpl.name;
        c.keyBase = tmpl.name;
        c.generics = &tmpl.generic_params;
        c.params = &tmpl.params;
        c.returnType = tmpl.return_type.get();
        c.block = tmpl.body.get();
        c.kind = "the generic function";
        return c;
    }

    static TemplateCallee calleeOf(const std::string& name, const LambdaTemplate& tmpl) {
        TemplateCallee c;
        c.node = tmpl.node;
        c.display = name;
        // The `fin.lambda.<n>` the template was given at its declaration, which every
        // instance shares: one lambda is one template however many types it is called
        // at, and `<int>` is appended to this by mangledName.
        //
        // The counter alone is enough to separate two enclosing instantiations, and that
        // is a fact about *when* a template is registered rather than a property of the
        // name: registration happens while a body is emitted, so the lambda inside
        // `outer<int>` and the one inside `outer<double>` are two registrations and take
        // two numbers. Folding `tmpl.outer` in as well would spell the distinction twice.
        c.keyBase = fmt::format("fin.lambda.{}", tmpl.id);
        c.generics = &tmpl.node->generic_params;
        c.params = &tmpl.node->params;
        c.returnType = tmpl.node->return_type.get();
        c.block = tmpl.node->body.get();
        c.value = tmpl.node->expression_body.get();
        c.lambda = &tmpl;
        c.kind = "the generic lambda";
        return c;
    }

    // `ident<int>` -- one instantiation of one function template, built the first time
    // it is asked for and then found.
    //
    // The same three steps as instantiateGeneric's, ordered for the same reason: the
    // bindings have to exist before the signature can be built, and the signature has
    // to be registered before the body is emitted -- `return down(n - 1)` asks for the
    // instantiation it is inside, and finds the name step 2 put there rather than
    // starting a second one that never ends.
    bool instantiateTemplate(const TemplateCallee& tmpl, const Substitution& substitution,
                             const std::string& key) {
        // 1. The bindings, stored before anything is emitted. The TypeMapper holds a
        //    pointer to them for the whole of the signature and the body, and a body
        //    may instantiate further templates into this same map -- which is why the
        //    storage is a member and not a local, and why a node-based map.
        // The lambda's own bindings first and the enclosing instance's after, so a lambda
        // that reuses the name -- `fun <T>` written inside `fun outer<T>` -- resolves T to
        // its own parameter. `boundBinding` answers with the first match, which is what
        // makes the order the shadowing rule.
        Substitution merged = substitution;
        if (tmpl.lambda) {
            for (const auto& binding : tmpl.lambda->outer) {
                bool shadowed = false;
                for (const auto& own : substitution) {
                    if (own.first == binding.first) { shadowed = true; break; }
                }
                if (!shadowed) merged.push_back(binding);
            }
        }
        fnInstances_[key] = std::move(merged);
        ScopedBindings bound(types_, &fnInstances_[key]);

        // 2. The signature, under the mangled name. `T` in a parameter or return
        //    position resolves through the bindings, so this is the ordinary path with
        //    the parameters substituted -- including every refusal it has, which is how
        //    an instance whose signature cannot be lowered says so at the call.
        declareFunction(*tmpl.node, key, key, *tmpl.params, tmpl.returnType,
                        /*isVarArg=*/false, /*isExtern=*/false);
        auto found = functions_.find(key);
        if (found == functions_.end()) return false;  // declareFunction reported

        // Weak, not external. Two objects that each wrote this template and each
        // instantiated it at the same arguments both publish this symbol, and there is
        // no third place to put it -- neither object knows the other exists. So the C++
        // template bargain: identical bodies, one copy kept, the linker picks. An
        // external definition in each would make the second a duplicate-symbol error,
        // which is a link failure for a program that is correct.
        //
        // A lambda instance is *internal* instead, which is not a smaller version of the
        // same bargain but the absence of it. Weak exists because two objects can each
        // instantiate one named template and the linker has to be told the copies are
        // interchangeable. A lambda template is bound to a local name inside one body, so
        // no other object can name it and there is no second copy to reconcile -- and the
        // symbol is not one weak could be trusted with anyway: `fin.lambda.<n>` is a
        // counter over emission order, so two objects whose instantiations ran in
        // different orders would publish one name for two different bodies, and weak
        // would silently keep either. Internal makes the question not arise, which is the
        // same linkage a non-generic lambda already gets for the same reason.
        found->second.fn->setLinkage(tmpl.lambda ? llvm::GlobalValue::InternalLinkage
                                                 : llvm::GlobalValue::LinkOnceODRLinkage);

        // 3. The body, with the parameters bound -- and, for a lambda, with the
        //    environment its *declaration* was written in rather than the call's. The
        //    two are different scopes entirely: an instance is emitted from the middle
        //    of whatever call asked for it, so reading the live tables here would refuse
        //    the caller's locals as captures and let the body call nested functions the
        //    lambda was never written among.
        if (tmpl.lambda) {
            std::vector<std::string> enclosing = tmpl.lambda->enclosing;
            enclosing.swap(enclosingNames_);
            nestedCarry_ = tmpl.lambda->nested;
            if (tmpl.lambda->visible) lambdaCarry_ = tmpl.lambda->visible->table;
            emitBodyOf(*tmpl.node, *tmpl.params, tmpl.block, tmpl.value, key);
            enclosing.swap(enclosingNames_);
        } else {
            emitBodyOf(*tmpl.node, *tmpl.params, tmpl.block, tmpl.value, key);
        }
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
    void emitTemplateCall(FunctionCall& node, const TemplateCallee& tmpl) {
        // First, and at the call rather than at the declaration: this is the point at
        // which the template stops being a recipe, and the representation ADR 0002
        // reserves for an erased parameter is what would have to be laid out. Before
        // the arguments are emitted, so a refused call emits no instructions for
        // operands nothing will consume.
        if (refuseIfErased(node, *tmpl.generics,
                           fmt::format("{} '{}'", tmpl.kind, tmpl.display))) return;
        for (auto& p : *tmpl.params) {
            if (!p->is_vararg) continue;
            // No corpus site, and nothing to infer from: a `...` position has no
            // declared type for a binding to unify against.
            unsupported(*p, fmt::format("'...' on {} '{}'", tmpl.kind, tmpl.display));
            return;
        }
        if (node.args.size() != tmpl.params->size()) {
            // The analyzer already checked arity; reaching here is the two passes
            // disagreeing, so it says so rather than padding.
            unsupported(node,
                        fmt::format("a call to '{}' with {} argument(s) where it declares {}",
                                    tmpl.display, node.args.size(), tmpl.params->size()));
            return;
        }

        Substitution bindings;
        if (!node.generic_args.empty()) {
            // Written. Mapped in the caller's scope, exactly as a struct's type
            // arguments are -- so a `T` written inside another instance resolves through
            // the binding that is already active.
            if (node.generic_args.size() != tmpl.generics->size()) {
                unsupported(node,
                            fmt::format("a call to '{}' with {} type argument(s) where it "
                                        "declares {}", tmpl.display, node.generic_args.size(),
                                        tmpl.generics->size()));
                return;
            }
            for (size_t i = 0; i < node.generic_args.size(); ++i) {
                const TypeNode* arg = node.generic_args[i].get();
                auto mapped = arg ? types_.map(arg) : std::nullopt;
                if (!mapped || mapped->isVoid() || !mapped->llvmType ||
                    !mapped->llvmType->isSized()) {
                    if (failed_) return;  // a nested instantiation already reported
                    unsupportedType(node, arg,
                                    fmt::format("'{}' at a type argument", tmpl.display));
                    return;
                }
                bindings.push_back({(*tmpl.generics)[i]->name,
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
                unifyBinding((*tmpl.params)[i]->type.get(),
                             TypeBinding{values[i].type, cgDisplay(values[i].type)},
                             *tmpl.generics, bindings);
            }
        }

        // In declaration order, whatever order inference found them in: the key is built
        // from this list, and `f<A, B>(b: B, a: A)` would otherwise be two names for one
        // instantiation depending on which call site reached it first.
        Substitution ordered;
        for (auto& p : *tmpl.generics) {
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
                                          "argument mentions", tmpl.display, p->name));
            return;
        }

        const std::string key = mangledName(tmpl.keyBase, ordered);
        if (!functions_.count(key) && !instantiateTemplate(tmpl, ordered, key)) return;
        auto instance = functions_.find(key);
        if (instance == functions_.end()) return;  // already reported
        const FnInfo& info = instance->second;
        if (info.paramTypes.size() != values.size()) {
            unsupported(node, fmt::format("a call to '{}' with too few arguments", tmpl.display));
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

    // `let id <auto> = fun <T>(x: T) <T> { return x; };` -- a name bound to a template.
    //
    // The declaration emits nothing at all: no alloca, no store, and no entry in
    // `scopes_`. That is the monomorphisation ruling applied to a lambda rather than a
    // choice made here -- `id<int>` and `id<double>` are two functions and a bare `id`
    // names neither, so there is no value for a slot to hold, and a template nothing
    // calls costs nothing exactly as `AGenericFunctionNobodyCallsLowersToNothing` says
    // for a named one. `lambdas.fin`'s two are both uncalled and this is why they are
    // free.
    //
    // Returns false having already reported.
    bool registerLambdaTemplate(VariableDeclaration& node, LambdaExpression& lambda) {
        if (!currentFn_) {
            // A module-scope one, which declareGlobals has already refused by name -- so
            // this is unreachable rather than a case, and it refuses instead of asserting
            // because there is no table here to put the template in either.
            unsupported(node, fmt::format("the generic lambda '{}' declared outside a "
                                          "function", node.name));
            return false;
        }
        if (!node.attributes.empty()) {
            // Not even `#[slaveof]`, which an ordinary variable's path accepts as a
            // no-op. It is a no-op there because the attribute asks for a lifetime that
            // this backend's storage already has; here there is no storage, so the
            // request is not satisfied-by-construction, it is unanswerable.
            unsupported(node, fmt::format("the attribute '{}' on the generic lambda '{}'",
                                          node.attributes.front()->name, node.name));
            return false;
        }
        if (!lambda.body && !lambda.expression_body) {
            unsupported(node, "a generic lambda with no body");
            return false;
        }
        // The annotation, which is not mapped and must not be: `fn<T>(m: T) -> T` has no
        // representation for the same reason the template has no address, and
        // TypeMapper::mapFunction says so by refusing every generic `fn`. What it is
        // instead is the template's own type, so the only thing checked is that it is one
        // -- `<auto>`, or a `fn` that declares type parameters. The front end has already
        // matched the annotation against the lambda parameter-name for parameter-name
        // (`fn<U>(x: U) -> U` rejects a `fun <T>` lambda), so there is nothing left here
        // to compare.
        if (node.type) {
            const bool isAuto = node.type->name == "auto" && node.type->generics.empty() &&
                                !node.type->is_array && node.type->pointer_depth == 0;
            auto* fnType = dynamic_cast<FunctionTypeNode*>(node.type.get());
            if (!isAuto && (!fnType || fnType->generic_params.empty())) {
                unsupportedType(node, node.type.get(), "a generic lambda's variable");
                return false;
            }
        }
        if (scopes_.empty() || lambdaTemplates_.empty()) {
            unsupported(node, fmt::format("the generic lambda '{}' declared outside a "
                                          "scope", node.name));
            return false;
        }
        if (scopes_.back().count(node.name) || nested_.back().count(node.name) ||
            lambdaTemplates_.back().count(node.name)) {
            // One name over a slot, a symbol and a template in one scope is the state
            // this file's tables cannot all hold, and it is the same refusal
            // emitNestedFunction gives one level along: picking either silently gets a
            // program wrong.
            unsupported(node, fmt::format("a second declaration of '{}' in one scope",
                                          node.name));
            return false;
        }

        LambdaTemplate tmpl;
        tmpl.node = &lambda;
        // From the same counter a non-generic lambda's symbol comes from, so a module's
        // lambdas are numbered in one sequence and no instance can collide with a plain
        // one: `fin.lambda.3` and `fin.lambda.3<int>` are different symbols, and 3 is
        // spent either way.
        tmpl.id = lambdas_++;
        // The environment, snapshotted here because here is where it is true. An instance
        // is emitted from the middle of a call, by which time these scopes are gone --
        // see instantiateTemplate, which installs these three rather than reading the
        // live tables.
        tmpl.enclosing = enclosingNames_;
        for (const auto& scope : scopes_)
            for (const auto& entry : scope) tmpl.enclosing.push_back(entry.first);
        tmpl.nested = visibleNested();
        tmpl.visible = std::make_shared<LambdaEnv>();
        tmpl.visible->table = visibleLambdas();
        if (const Substitution* active = types_.bindings()) tmpl.outer = *active;
        lambdaTemplates_.back()[node.name] = std::move(tmpl);
        debugLog(fmt::format("registered the generic lambda {}", node.name));
        return true;
    }

    void visit(VariableDeclaration& node) override {
        if (registeredGlobals_.count(&node)) return;  // declareGlobals did it
        // A generic lambda is not a value, so this declaration is not a declaration of
        // one: it registers a template and emits nothing. Checked first, ahead of the
        // attribute loop and the type mapping, because both of those would otherwise
        // report the wrong thing -- the annotation `fn<T>(m: T) -> T` has no
        // representation and `types_.map` would say so, which is true of the annotation
        // and false about the program.
        if (auto* lambda = dynamic_cast<LambdaExpression*>(node.initializer.get())) {
            if (!lambda->generic_params.empty()) {
                registerLambdaTemplate(node, *lambda);
                return;
            }
        }
        // `#[slaveof(...)]` on a local is a no-op today, and that is a ruling rather
        // than an omission (2026-08-28).
        //
        // Both corpus forms ask for a lifetime *at least* as long as something else:
        // `#[slaveof(z)]` (variables.fin:27) ties `m`'s storage to `z`'s, and
        // `#[slaveof($Fin)]` (:35) asks for "until the program exits". Neither can be
        // violated by this backend, because **nothing here frees anything implicitly.**
        // Memory management is a library in Fin (ADR 0003), so a heap allocation is
        // released only by an explicit `delete` -- measured: an object built from a
        // scope that allocates references `malloc` and not `free`, and a `new int(5)`
        // whose scope has closed is still readable through a pointer that outlived it.
        //
        // So an allocation nobody deletes already lives until the program exits, which
        // is what `$Fin` asks for, and it already outlives any named variable, which is
        // what `slaveof(z)` asks for. Emitting nothing satisfies both requests rather
        // than ignoring them, and that is the difference between this and the refusals
        // below: an unread attribute is refused when it *could* change the generated
        // code, and this one provably cannot.
        //
        // The day scope-based freeing exists -- a destructor running implicitly, or an
        // owning pointer released at scope exit -- this becomes a real rule and has to
        // grow one. Soundness_Codegen.ASlaveofAttributeKeepsItsAllocationAlive is what
        // fails then, because it reads through a pointer whose scope has closed.
        //
        // Other attributes still refuse. An attribute this file does not read may be
        // one that changes where the variable lives, and ignoring that is how a working
        // program ends up in the wrong section.
        for (auto& attr : node.attributes) {
            if (attr->name == "slaveof") continue;
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
            // Everything after a `return` in the same block is dead. Emitting into
            // a terminated block is an LLVM error, and inventing a fresh block for
            // code the program cannot reach would only hide it.
            if (terminated()) break;
            stmt->accept(*this);
            if (failed_) {
                // The next statement is its own finding. The only thing this one can
                // have left behind for it to trip over is a name with no storage: a
                // refused `foreach`, `m1778` or expression declares nothing at all.
                if (auto* var = dynamic_cast<VariableDeclaration*>(stmt.get())) {
                    poison(var->name);
                }
                failed_ = false;
            }
        }
        popScope();
    }

    // The object a constructor's `return` names, stored into the caller's storage.
    //
    // Both shapes the corpus writes land here. `return new S{...}` yields a pointer to
    // a heap copy, which is read back and copied into the caller's slot -- the heap
    // block is then unreferenced, which is the same bargain every other allocation in
    // this file strikes (see ADR 0003: memory management is a library, and nothing here
    // frees). `return S{...}` yields the aggregate directly and is stored as it is.
    //
    // Returns false having already reported.
    bool emitConstructedValue(ReturnStatement& node, const CgVal& v) {
        Local* self = findLocal("self");
        if (!self) {
            // A constructor is declared with a receiver or not declared at all, so
            // arriving here without one is this file disagreeing with itself.
            unsupported(node, "a constructor's 'return' with no receiver in scope");
            return false;
        }
        if (self->type.pointee == nullptr || !self->type.pointee->isStruct()) {
            unsupported(node, "a constructor whose receiver is not a pointer to a struct");
            return false;
        }
        const CgType object = *self->type.pointee;
        llvm::Value* dest = builder_.CreateLoad(self->type.llvmType, self->slot, "self.ptr");

        llvm::Value* stored = nullptr;
        if (v.type.isPointer() && v.type.pointee && v.type.pointee->isStruct() &&
            v.type.pointee->llvmType == object.llvmType) {
            stored = builder_.CreateLoad(object.llvmType, v.value, "constructed");
        } else {
            stored = convert(node, v, object);
        }
        if (!stored) return false;
        builder_.CreateStore(stored, dest);
        return true;
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
        if (currentFn_->isConstructor) {
            // `return new S{...}` -- what six of the fifteen constructors in the corpus
            // and the library write. A constructor's emitted result is void and the
            // object it builds is the caller's storage at parameter 0, so a returned
            // value is not returned: it is *the* value of the object, and it is stored
            // through the receiver before the void return.
            if (!emitConstructedValue(node, v)) return;
            builder_.CreateRetVoid();
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
                value_ = CgVal{builder_.CreateGlobalString(decodeLiteral(node.value)), t};
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
        // A nested function named as a value -- `let f <fn(int) -> int> = h;` with `h`
        // declared in this body. It is an ordinary code pointer for the same reason a
        // module-scope function is: it captures nothing, because a capture is refused.
        //
        // First, for the reason the call path checks it first: `nestedFor` answers only
        // when the nested declaration is at least as inner as any local of the name, so
        // the locals lose nothing by being asked second, and a nested `fun h` inside a
        // block that an outer `let h` encloses resolves to the function -- which is what
        // the analyzer resolved it to.
        if (const std::string* symbol = nestedFor(node.name)) {
            auto nestedFn = functions_.find(*symbol);
            if (nestedFn != functions_.end()) {
                std::optional<CgType> type = fnValueType(nestedFn->second);
                if (!type) {
                    // A nested function is neither variadic (the grammar has no `...` on
                    // one) nor `main`, so this is unreachable rather than a case; it
                    // refuses instead of asserting because the alternative is handing
                    // back a Fin type the code does not have.
                    unsupported(node, fmt::format("the nested function '{}' used as a "
                                                  "value", node.name));
                    return;
                }
                value_ = CgVal{nestedFn->second.fn, *type};
                return;
            }
        }
        if (Local* local = findLocal(node.name)) {
            auto loaded = builder_.CreateLoad(local->type.llvmType, local->slot, node.name);
            value_ = CgVal{loaded, local->type, local->slot};
            return;
        }
        if (failed_) return;  // a name refused above; see findLocal
        // Then the globals, which are the same kind of thing as a local with a
        // different home -- and after them for the same reason: a local of the name
        // shadows one (ALocalOutranksAGlobalOfTheSameName).
        auto global = globals_.find(node.name);
        if (global != globals_.end()) {
            auto loaded = builder_.CreateLoad(global->second.type.llvmType,
                                              global->second.var, node.name);
            value_ = CgVal{loaded, global->second.type, global->second.var};
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
        // A function named as a value. An llvm::Function *is* a pointer constant, so
        // there is nothing to emit -- the decision this used to refuse for (a bare
        // pointer, or a closure pair) is settled at TypeMapper::mapFunction, and a named
        // function captures nothing by construction.
        auto fn = functions_.find(node.name);
        if (fn != functions_.end()) {
            std::optional<CgType> type = fnValueType(fn->second);
            if (!type) {
                // A variadic or `main`. Both have a signature no `fn` type can spell:
                // `fn` has no `...`, and `main`'s LLVM signature is C's whatever Fin
                // wrote, so a value of it would advertise a Fin type the code does not
                // have. Refused rather than given the Fin type it is not.
                unsupported(node, fmt::format("the function '{}' used as a value", node.name));
                return;
            }
            value_ = CgVal{fn->second.fn, *type};
            return;
        }
        // A template used as a value, which is not the same refusal: what is missing is
        // not the representation but the code. `ident<int>` and `ident<char>` are two
        // functions and a bare `ident` names neither, so there is no address to take
        // until something says which instantiation is meant.
        if (fnTemplates_.count(node.name)) {
            unsupported(node, fmt::format("the generic function '{}' used as a value",
                                         node.name));
            return;
        }
        // The same refusal for a generic lambda, which is the same thing one scope in.
        // Reached because the declaration registered no slot, so `findLocal` above missed
        // -- and reached *after* it, so a later local of the name is read as the local it
        // is rather than blamed on the template.
        if (lambdaTemplateFor(node.name)) {
            unsupported(node, fmt::format("the generic lambda '{}' used as a value",
                                          node.name));
            return;
        }
        if (refuseIfCapture(node, node.name)) return;
        unsupported(node, fmt::format("the name '{}'", node.name));
    }

    // The `fn` type of an existing function, or nothing when it has none to have.
    //
    // Built from FnInfo rather than from the declaration's TypeNodes, because this has
    // to be the signature the *emitted* function actually has: declareFunction is what
    // decides that, and it rewrites `main` and folds a receiver into parameter 0.
    std::optional<CgType> fnValueType(const FnInfo& info) const {
        if (info.isVarArg || info.isMain || !info.fn) return std::nullopt;
        CgType t;
        t.kind = CgType::Kind::Fn;
        // Through the mapper rather than `PointerType::getUnqual(ctx_)`: this method is
        // const, Emitter owns its LLVMContext by value, and TypeMapper holds it by
        // reference -- so the mapper is the one that can still hand out a type here.
        t.llvmType = types_.pointerType().llvmType;
        t.result = std::make_shared<CgType>(info.returnType);
        for (const CgType& p : info.paramTypes) t.params.push_back(std::make_shared<CgType>(p));
        // The function's own type, so a call through the value is the call the callee
        // was compiled to answer -- not one rebuilt from the Fin types, which would
        // disagree about `main` and about a receiver.
        t.llvmSignature = info.fn->getFunctionType();
        return t;
    }

    // Whether `name` is a local of the function this lambda was written inside, which
    // makes reading it a capture. Reports and returns true when it is.
    //
    // Checked only after every other way of resolving a name has missed, so that a
    // lambda's own parameter, a global, an enumerator and a function all still win --
    // a lambda reading `printf` (lambdas.fin:58) is reading a symbol and not closing
    // over anything.
    //
    // Named as a capture rather than as an unknown name because the two send a reader
    // to different places: "the name 'x'" reads as a front-end bug, and this is a
    // deliberate boundary. A bare code pointer has nowhere to put `x`, so lowering it
    // would have to either read the enclosing frame after it is gone or silently pass
    // a different value.
    bool refuseIfCapture(ASTNode& node, const std::string& name) {
        for (const auto& n : enclosingNames_) {
            if (n != name) continue;
            unsupported(node, fmt::format("{} capturing '{}'", captureKind_, name));
            return true;
        }
        return false;
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
        const OperatorDeclaration* declared = findOperator(*owner, node.op);
        if (!declared) {
            // The base's operator, on the terms an inherited method is called on: the
            // left operand's address is already a valid pointer to the base, so the
            // base's function is called with it unchanged -- and only when the base's
            // fields sit where its own body indexes them, which for a second base with
            // bytes they do not.
            const StructInfo* provider = operatorProvider(node, *owner, node.op);
            if (failed_) return;
            if (provider && provider != owner) {
                if (!baseSharesLayout(*owner, *provider)) {
                    unsupported(node, fmt::format("an operator '{}' inherited from '{}', "
                                                  "whose fields are not at the offsets "
                                                  "they have in '{}'",
                                                  spelling, provider->finName,
                                                  owner->finName));
                    return;
                }
                owner = provider;
                declared = findOperator(*owner, node.op);
            }
        }
        if (!declared) {
            unsupported(node, fmt::format("an undeclared operator '{}' on struct '{}'",
                                          spelling, owner->finName));
            return;
        }
        const std::string key = operatorKey(owner->finName, node.op);
        auto found = functions_.find(key);
        if (found == functions_.end()) {
            // A generic operator with a body is not missing, it is uninstantiated --
            // declareStructMethods declares nothing for it because there is no signature
            // until something says what `T` is, and writing the operator is that
            // something. One bound by `implements` has no body and still refuses.
            if (!declared->generic_params.empty() && declared->body) {
                emitGenericOperator(node, *owner, *declared);
                return;
            }
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

    // `m + 2` where the struct declares `operator + : <T>(other: <T>)` --
    // operators.fin:15. The operator half of the generic-method unit: the struct's
    // bindings come from the left operand, the operator's own come from the right, and
    // the composition is the same one a written method call goes through.
    void emitGenericOperator(BinaryOp& node, const StructInfo& owner,
                             const OperatorDeclaration& tmpl) {
        // The receiver is the left operand's *address*, for the reason the non-generic
        // path says: an operator may assign through `self`, and this one reads `self.val`
        // through the same pointer, so a receiver spilled to a temporary would be an
        // operator that writes into a copy.
        auto receiver = baseAddress(*node.left, CgType::Kind::Struct);
        if (failed_) return;
        if (!receiver) {
            unsupported(node, fmt::format("an operator '{}' on a left operand with no "
                                          "address", spellOperator(node.op)));
            return;
        }
        // The right operand, emitted before the instance exists, because its type is
        // what the instantiation is inferred from -- and after the left's address, so
        // that a generic operator evaluates its two sides in the order they are written.
        CgVal rhs = emit(*node.right);
        if (failed_) return;
        if (!rhs.ok()) { unsupported(node, "this operand"); return; }
        const std::vector<CgVal> values{rhs};

        // `MyInt.operator+<int>`, which is operatorKey's name with the operator's own
        // substitution appended -- the same shape as `Box<int>.set_x<int>`, because an
        // operator is a method with a spelled name and its instances need telling apart
        // on exactly the same terms.
        const std::string name = std::string("operator") + spellOperator(node.op);
        const std::string key = instantiateGenericMethod(
            node, owner, types_.pointerTo(receiver->type),
            const_cast<OperatorDeclaration&>(tmpl), name, tmpl.generic_params,
            tmpl.params, *tmpl.body, values);
        if (key.empty()) return;  // already reported
        emitInstanceCall(node, key, receiver->ptr, values);
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

    // What a key search found. `index` and `found` are loads from slots the loop wrote
    // rather than phis, so both are live in whatever block the caller goes on to build
    // -- and the caller is left standing in the join block, with the loop behind it.
    struct PrototypeScan {
        llvm::Value* found = nullptr;       // i1
        llvm::Value* index = nullptr;       // i32, meaningful only where found is true
        llvm::Value* keysData = nullptr;
        llvm::Value* valuesData = nullptr;
        llvm::Value* length = nullptr;      // i32, the length as it was before any edit
    };

    // The one linear scan every prototype operation is built on. `p[k]`, `p[k] = v`,
    // `get`, `contains` and `remove` all begin by asking where a key is, if it is here
    // at all, and one copy of that loop is what keeps them agreeing: insertion order is
    // the data structure, so a second traversal written differently would be a second
    // answer to the same question.
    //
    // Linear on purpose. ADR 0028 stages this -- a correct baseline before an optimised
    // table -- and what replaces it is the hashing trait behind the intrinsic boundary,
    // not a rewrite of these callers.
    //
    // Returns nullopt having already reported.
    std::optional<PrototypeScan> emitPrototypeScan(ASTNode& node, const CgType& proto,
                                                   llvm::Value* pair, const CgVal& key,
                                                   const char* what) {
        if (!currentFn_) {
            unsupported(node, fmt::format("{} outside a function", what));
            return std::nullopt;
        }
        if (!proto.keys || !proto.values || !proto.keys->element ||
            !proto.values->element || !pair || !key.value) {
            unsupported(node, fmt::format("{} with an incomplete prototype "
                                          "representation", what));
            return std::nullopt;
        }
        const CgType& keyType = *proto.keys->element;
        if (key.type.kind != keyType.kind || key.type.llvmType != keyType.llvmType) {
            unsupported(node, fmt::format("{} with an incompatible key", what));
            return std::nullopt;
        }

        PrototypeScan scan;
        llvm::Value* keysPair = builder_.CreateExtractValue(pair, {0}, "prototype.keys");
        llvm::Value* valuesPair = builder_.CreateExtractValue(pair, {1}, "prototype.values");
        scan.keysData = builder_.CreateExtractValue(keysPair, {0}, "prototype.keys.data");
        scan.length = builder_.CreateExtractValue(keysPair, {1}, "prototype.keys.len");
        scan.valuesData = builder_.CreateExtractValue(valuesPair, {0}, "prototype.values.data");

        auto* fn = currentFn_->fn;
        auto* indexSlot = builder_.CreateAlloca(builder_.getInt32Ty(), nullptr,
                                                "prototype.index");
        auto* foundSlot = builder_.CreateAlloca(builder_.getInt1Ty(), nullptr,
                                                "prototype.found");
        builder_.CreateStore(builder_.getInt32(0), indexSlot);
        builder_.CreateStore(builder_.getInt1(false), foundSlot);

        auto* loop = llvm::BasicBlock::Create(ctx_, "prototype.scan", fn);
        auto* body = llvm::BasicBlock::Create(ctx_, "prototype.scan.body", fn);
        auto* hit  = llvm::BasicBlock::Create(ctx_, "prototype.scan.hit", fn);
        auto* next = llvm::BasicBlock::Create(ctx_, "prototype.scan.next", fn);
        auto* done = llvm::BasicBlock::Create(ctx_, "prototype.scan.done", fn);
        builder_.CreateBr(loop);

        builder_.SetInsertPoint(loop);
        auto* i = builder_.CreateLoad(builder_.getInt32Ty(), indexSlot, "prototype.i");
        builder_.CreateCondBr(builder_.CreateICmpULT(i, scan.length, "prototype.more"),
                              body, done);

        builder_.SetInsertPoint(body);
        auto* kp = builder_.CreateInBoundsGEP(keyType.llvmType, scan.keysData, i,
                                              "prototype.key.ptr");
        auto* candidate = builder_.CreateLoad(keyType.llvmType, kp, "prototype.key");
        llvm::Value* equal = emitKeyEquality(node, keyType, candidate, key.value);
        if (!equal) return std::nullopt;
        builder_.CreateCondBr(equal, hit, next);

        builder_.SetInsertPoint(hit);
        builder_.CreateStore(builder_.getInt1(true), foundSlot);
        builder_.CreateBr(done);

        builder_.SetInsertPoint(next);
        builder_.CreateStore(builder_.CreateAdd(i, builder_.getInt32(1)), indexSlot);
        builder_.CreateBr(loop);

        builder_.SetInsertPoint(done);
        scan.index = builder_.CreateLoad(builder_.getInt32Ty(), indexSlot, "prototype.at");
        scan.found = builder_.CreateLoad(builder_.getInt1Ty(), foundSlot, "prototype.hit");
        return scan;
    }

    // Store through a prototype subscript. The baseline representation has no
    // capacity word, so a new key grows both parallel arrays with realloc; existing
    // keys update only the value slot. This keeps insertion order and the key/value
    // index invariant without inventing a sentinel or a second storage format.
    bool emitPrototypeStore(ArrayAccess& access, const CgVal& rhs, BinaryOp& node) {
        auto* baseExpr = access.array.get();
        if (!baseExpr) return false;
        auto base = emitAddress(*baseExpr);
        if (failed_ || !base || !base->type.isPrototype() || !base->type.keys ||
            !base->type.values || !base->type.keys->element ||
            !base->type.values->element) return false;
        const CgType& proto = base->type;
        const CgType& keyType = *proto.keys->element;
        const CgType& valueType = *proto.values->element;
        CgVal key = emit(*access.index);
        if (failed_ || !key.ok()) return true;

        llvm::Value* pair = builder_.CreateLoad(proto.llvmType, base->ptr, "prototype");
        auto scan = emitPrototypeScan(access, proto, pair, key, "a prototype store");
        if (!scan) return true;
        // Converted once, in the block the scan left us in, so the value is evaluated
        // exactly once whichever way the store goes -- an update and an append store
        // the same value and must not be two evaluations of the right-hand side.
        llvm::Value* stored = convert(node, rhs, valueType);
        if (!stored) return true;

        auto* fn = currentFn_->fn;
        auto* update = llvm::BasicBlock::Create(ctx_, "prototype.store.update", fn);
        auto* append = llvm::BasicBlock::Create(ctx_, "prototype.store.append", fn);
        auto* done   = llvm::BasicBlock::Create(ctx_, "prototype.store.done", fn);
        builder_.CreateCondBr(scan->found, update, append);

        builder_.SetInsertPoint(update);
        builder_.CreateStore(stored, builder_.CreateInBoundsGEP(valueType.llvmType,
                                                                scan->valuesData,
                                                                scan->index));
        builder_.CreateBr(done);

        builder_.SetInsertPoint(append);
        auto* newLen = builder_.CreateAdd(scan->length, builder_.getInt32(1));
        auto* i64 = builder_.CreateZExt(newLen, builder_.getInt64Ty());
        auto* keyBytes = builder_.CreateMul(i64, llvm::ConstantExpr::getSizeOf(keyType.llvmType));
        auto* valueBytes = builder_.CreateMul(i64, llvm::ConstantExpr::getSizeOf(valueType.llvmType));
        auto reallocTy = llvm::FunctionType::get(llvm::PointerType::get(ctx_, 0),
                                                   {llvm::PointerType::get(ctx_, 0), builder_.getInt64Ty()}, false);
        auto reallocFn = runtimeFn(access, "realloc", reallocTy, "prototype growth");
        if (!reallocFn) return true;
        auto* newKeysRaw = builder_.CreateCall(reallocFn, {scan->keysData, keyBytes});
        auto* newValuesRaw = builder_.CreateCall(reallocFn, {scan->valuesData, valueBytes});
        auto* newKeys = builder_.CreateBitCast(newKeysRaw, llvm::PointerType::get(ctx_, 0));
        auto* newValues = builder_.CreateBitCast(newValuesRaw, llvm::PointerType::get(ctx_, 0));
        builder_.CreateStore(key.value, builder_.CreateInBoundsGEP(keyType.llvmType, newKeys, scan->length));
        builder_.CreateStore(stored, builder_.CreateInBoundsGEP(valueType.llvmType, newValues, scan->length));
        builder_.CreateStore(prototypePair(proto, newKeys, newValues, newLen), base->ptr);
        builder_.CreateBr(done);

        builder_.SetInsertPoint(done);
        value_ = CgVal{stored, valueType};
        return true;
    }

    // The `{ {keys,len}, {values,len} }` value, rebuilt from its parts. One place that
    // knows the shape, because the two lengths are one number -- key i pairs with value
    // i is the whole invariant -- and a caller that inserted them separately could set
    // one and forget the other.
    llvm::Value* prototypePair(const CgType& proto, llvm::Value* keysData,
                               llvm::Value* valuesData, llvm::Value* length) {
        llvm::Value* keysPair = llvm::UndefValue::get(proto.keys->llvmType);
        keysPair = builder_.CreateInsertValue(keysPair, keysData, {0});
        keysPair = builder_.CreateInsertValue(keysPair, length, {1});
        llvm::Value* valuesPair = llvm::UndefValue::get(proto.values->llvmType);
        valuesPair = builder_.CreateInsertValue(valuesPair, valuesData, {0});
        valuesPair = builder_.CreateInsertValue(valuesPair, length, {1});
        llvm::Value* pair = llvm::UndefValue::get(proto.llvmType);
        pair = builder_.CreateInsertValue(pair, keysPair, {0});
        pair = builder_.CreateInsertValue(pair, valuesPair, {1});
        return pair;
    }

    void emitAssignment(BinaryOp& node) {
        if (node.op == ASTTokenKind::EQUAL) {
            if (auto* access = dynamic_cast<ArrayAccess*>(node.left.get())) {
                CgVal rhs = emit(*node.right);
                if (failed_) return;
                if (emitPrototypeStore(*access, rhs, node)) return;
            }
        }
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
            if (emitAddressOfLiteral(node)) return;
            // `&make()`. The value is real and has no home, so taking its address means
            // putting it in a fresh slot -- which answers "how long does that slot live,
            // and what does the pointer mean afterwards" by picking one. Nothing in the
            // corpus reads such a pointer, so nothing would catch the wrong pick. A
            // *literal* is the case where both halves of that are settled; see
            // emitAddressOfLiteral.
            unsupported(node, "the address of a value with no home");
            return;
        }
        value_ = CgVal{addr->ptr, types_.pointerTo(addr->type)};
    }

    // `&"Hello world"` -- tests/samples/variables.fin:11,
    // `let Complex <&string> = &"Hello world";`.
    //
    // Two questions had to be answered and they were answered separately.
    //
    // REPRESENTATION, ruled by the owner 2026-08-28: `&string` is **a pointer to a cell
    // holding the string**, not the string's own pointer. So `*Complex` is the string
    // and `&string` behaves like `&T` for every other T. The alternative -- that
    // `&"..."` is the same pointer the `string` already is, making `&string` and
    // `string` one representation and `*Complex` a *char* -- was rejected. Both
    // compiled, and no measurement could separate them: variables.fin:11 is the only
    // `&"..."` and the only `&string` in the corpus or lib/std, and nothing reads
    // `Complex`. Soundness_Codegen.TheAddressOfAStringLiteralIsACellHoldingIt carries
    // the argument, and it is worth reading before changing this: a test written in
    // whichever reading was chosen passes for that reason alone, which is the trap this
    // one is shaped to avoid.
    //
    // LIFETIME, which is not a choice: a literal's value exists before the program
    // starts and depends on nothing the program does, so a holder for it can have
    // static storage -- and static cannot dangle under any later use. At module scope,
    // where variables.fin:11 sits, static is not merely safe but *forced*: there is no
    // function to put an alloca in. That is what separates this from `&make()`, where
    // every candidate lifetime is a real choice with a program that can tell them apart.
    //
    // A fresh holder per occurrence, not one per distinct value. LLVM may merge the
    // literal's character *data* with an identical literal's, which is fine because
    // that data is `constant`; the holder is not, since `let Complex <&string>` is
    // mutable and a write through this pointer must not reach a second `&"..."`
    // elsewhere in the program. Soundness_Codegen.TwoAddressesOfEqualLiteralsAreDistinct
    // is what holds that.
    //
    // Creating a GlobalVariable emits no instruction, so this still folds inside
    // constantInitializer's throwaway function, whose block has to come out empty.
    bool emitAddressOfLiteral(UnaryOp& node) {
        auto* lit = dynamic_cast<Literal*>(node.operand.get());
        if (!lit) return false;

        node.operand->accept(*this);
        if (failed_) return true;  // already reported; do not add a second refusal
        auto* konst = llvm::dyn_cast<llvm::Constant>(value_.value);
        if (!konst) {
            // A literal whose lowering is not a constant would break the reasoning
            // above rather than merely being unhandled, so the refusal says so in those
            // terms instead of blaming the address.
            unsupported(node, "the address of a literal that does not lower to a constant");
            return true;
        }

        const CgType pointee = value_.type;
        auto* holder = new llvm::GlobalVariable(
            module_, pointee.llvmType, /*isConstant=*/false,
            llvm::GlobalValue::InternalLinkage, konst, ".fin.literal.addr");
        value_ = CgVal{holder, types_.pointerTo(pointee)};
        return true;
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
        // A nested function, before the locals rather than after them. `nestedFor` is
        // the thing that decides between the two: it walks the local and the nested
        // tables in lockstep and answers only when the nested declaration is at least as
        // inner as any local of the name, so asking it first costs the locals nothing and
        // is what makes `let h <int> = 3; { fun h() <int> {...} h() }` call the function
        // the analyzer resolved -- reaching findLocal first would find the outer variable
        // and refuse the call as one through a non-function.
        if (const std::string* symbol = nestedFor(node.name)) {
            auto nestedFn = functions_.find(*symbol);
            if (nestedFn == functions_.end()) {
                // Registered by emitNestedFunction only once declareFunction succeeded,
                // so a name in the table with no function is this file disagreeing with
                // itself.
                unsupported(node, fmt::format("a call to the nested function '{}'",
                                              node.name));
                return;
            }
            std::vector<llvm::Value*> args;
            if (!emitCallArgs(node, nestedFn->second, node.name, argList(node.args), args))
                return;
            emitCall(nestedFn->second, args);
            return;
        }
        // A local of function type, which shadows a *module-scope* function of the same
        // name -- and checked before those for that reason, which is also the order the
        // analyzer uses.
        if (Local* local = findLocal(node.name)) {
            if (!local->type.isFn()) {
                unsupported(node, fmt::format("a call through the variable '{}' of "
                                              "non-function type", node.name));
                return;
            }
            llvm::Value* callee = builder_.CreateLoad(local->type.llvmType, local->slot,
                                                      node.name);
            emitIndirectCall(node, local->type, callee, node.name, argList(node.args));
            return;
        }
        if (failed_) return;  // a name refused above; see findLocal
        // A global of function type, for the same reason and in the same order the
        // identifier path uses: a local of the name shadows one.
        auto globalFn = globals_.find(node.name);
        if (globalFn != globals_.end()) {
            if (!globalFn->second.type.isFn()) {
                unsupported(node, fmt::format("a call through the global '{}' of "
                                              "non-function type", node.name));
                return;
            }
            llvm::Value* callee = builder_.CreateLoad(globalFn->second.type.llvmType,
                                                      globalFn->second.var, node.name);
            emitIndirectCall(node, globalFn->second.type, callee, node.name,
                             argList(node.args));
            return;
        }
        // A generic lambda bound to a name in this body -- `let id <auto> = fun <T>(x:
        // T) <T> { return x; }; id(7)`. Ahead of `fnTemplates_` because a local of the
        // name shadows a module-scope declaration, which is the order every other name
        // in this function uses; behind the locals for the reason the declaration
        // allocates no slot, so `findLocal` above cannot have answered for one.
        if (LambdaTemplate* lambda = lambdaTemplateFor(node.name)) {
            emitTemplateCall(node, calleeOf(node.name, *lambda));
            return;
        }
        // Before the ordinary lookup, because a template is deliberately not in
        // functions_: it has no signature until this call says what its parameters are.
        auto tmpl = fnTemplates_.find(node.name);
        if (tmpl != fnTemplates_.end()) {
            emitTemplateCall(node, calleeOf(*tmpl->second));
            return;
        }
        // A constructor call on a generic struct: `HashMap::<string, Data>()`
        // (deeptest4.fin:11), `Box::<int>(7)`. Before the turbofish refusal below and
        // before the struct lookup, because a struct template is deliberately not in
        // structs_ for the reason a function template is not in functions_ -- it has no
        // layout, and therefore no constructor symbol, until this call says what its
        // arguments are.
        //
        // The instantiation is built through literalStructName, which is the same
        // synthetic-TypeNode probe `Box::<int>{ val: 100 }` (complex.fin:12) uses. One
        // path, so a turbofish on a call and a turbofish on a literal reach the same
        // mangled name, the same layout and the same refusals -- including the erasure
        // marker's, which instantiateGeneric checks and this site therefore inherits.
        auto structTmpl = templates_.find(node.name);
        if (structTmpl != templates_.end()) {
            if (node.generic_args.empty()) {
                // `Box(7)`, with the arguments meant to say what T is. Refused naming
                // the question rather than inferred, and the question is *where the
                // arguments come from* rather than how to unify them: unifyBinding over
                // the constructor's parameters would answer this spelling, and
                // `let b <Box<int>> = Box(7);` -- which is the same call with the
                // annotation carrying the answer -- would still have to reach the same
                // instantiation by a different route. That is the booked
                // StructInstantiation-does-not-infer-from-an-annotation gap, and half of
                // it landing here would make two spellings of one call disagree about
                // which of the two sources wins. `Box::<int>(7)` is the spelling that
                // says it once.
                unsupported(node, fmt::format("a constructor call on the generic struct "
                                              "'{}' with no type arguments", node.name));
                return;
            }
            const std::string instance =
                literalStructName(node, node.name, node.generic_args);
            if (instance.empty()) return;  // already reported
            emitNamedCall(node, instance);
            return;
        }
        if (!node.generic_args.empty() &&
            (functions_.count(node.name) || structs_.count(node.name))) {
            // A turbofish on a name this file *does* declare and that declares no type
            // parameters. Read by emitTemplateCall for a function template, by the
            // constructor path above for a struct template, and by nobody here, so it is
            // refused rather than dropped -- a written type argument that changed nothing
            // would be a silent disagreement with whatever the writer expected it to
            // change.
            //
            // Conditioned on the name being known, which is what stopped this from being
            // the blanket refusal it was. A turbofish on a name this file has *no*
            // declaration for is not a fact about turbofishes: the analyzer resolved it,
            // so it resolved to a module's declaration, and a module's AST does not reach
            // this pass at all (HANDOFF's imported-declaration gap). Falling through
            // makes that say `a call to 'HashMap'` -- the same thing an imported
            // *function* already says -- instead of blaming a spelling that now lowers.
            unsupported(node, fmt::format("a call with explicit generic arguments to "
                                          "the non-generic '{}'", node.name));
            return;
        }
        // `Point(7)` -- a call whose name is a struct's. It is the constructor symbol
        // declared beside the struct, and the analyzer has already selected
        // constructors[0]; this pass deliberately uses the same single-symbol rule
        // rather than inventing an overload resolution the front end does not have.
        emitNamedCall(node, node.name);
    }

    // A call to a name that is either a free function's or a struct's, once the name is
    // settled. Split out of visit(FunctionCall&) so that `Box::<int>(7)` reaches the
    // *same* code as `Point(7)`: the name it is given is the instantiation's mangled one
    // rather than the written one, and nothing else about a constructor call differs.
    // A second copy would be a second calling convention for one spelling.
    void emitNamedCall(FunctionCall& node, const std::string& name) {
        std::string emittedName = name;
        auto asStruct = structs_.find(name);
        const bool isCtorCall = asStruct != structs_.end();
        if (isCtorCall) emittedName = methodKey(name, "constructor");
        auto found = functions_.find(emittedName);
        if (found == functions_.end()) {
            // A struct with no declared constructor, or a name that is neither. Not a
            // zeroed default-construct: a constructor is the only thing that runs field
            // defaults here (declareStructConstructor queues the body that does), so
            // synthesising one would hand back an object whose `= null` fields were
            // never written -- an answer, and the wrong one. `P{}` is the spelling that
            // means "the defaults", and it already works.
            unsupported(node, fmt::format("a call to '{}'", name));
            return;
        }
        const FnInfo& info = found->second;

        std::vector<llvm::Value*> args;
        llvm::Value* ctorStorage = nullptr;
        if (info.isConstructor) {
            if (!isCtorCall || !asStruct->second.complete) {
                // The symbol is a constructor and the name is not the struct's, which
                // is not a spelling that exists: `Struct.constructor` is not writable.
                unsupported(node, fmt::format("a call to the constructor '{}'", name));
                return;
            }
            // The caller owns the object. It is allocated here, its address is passed
            // as parameter 0, and the call's value is what the constructor left in it.
            ctorStorage = builder_.CreateAlloca(asStruct->second.llvmType, nullptr, name);
            // Zeroed first, so a field no constructor assigns reads as zero rather
            // than as whatever the frame held -- the answer a local with no
            // initialiser gets here too.
            builder_.CreateStore(llvm::Constant::getNullValue(asStruct->second.llvmType),
                                 ctorStorage);
            args.push_back(ctorStorage);
        }
        if (!emitCallArgs(node, info, name, argList(node.args), args)) return;
        emitCall(info, args);
        if (ctorStorage) {
            auto object = types_.structByName(asStruct->second.finName);
            if (!object) {
                unsupported(node, fmt::format("a call to the constructor of '{}'", name));
                return;
            }
            value_ = CgVal{builder_.CreateLoad(object->llvmType, ctorStorage, "constructed"),
                           *object};
        }
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

    // A call through a function value, given the pointer to call and the type that says
    // what is at the other end.
    //
    // The arguments go through emitCallArgs and not through a second copy of the
    // conversion rules, which is the reason for the synthetic FnInfo below: an argument
    // widened one way at a direct call and another way here would be a silent ABI
    // difference between `add(1, 2)` and `f(1, 2)` for one `f = add`. The FnInfo has no
    // `fn` -- there is no llvm::Function to name, that is the whole point of an indirect
    // call -- so it is built for the arguments and discarded, and the call is emitted
    // from `llvmSignature` instead.
    void emitIndirectCall(ASTNode& node, const CgType& fnType, llvm::Value* callee,
                          const std::string& name,
                          const std::vector<Expression*>& argNodes) {
        if (!fnType.llvmSignature || !fnType.result) {
            // A Fn CgType with no signature is this file disagreeing with itself:
            // mapFunction sets both or returns nothing at all.
            unsupported(node, fmt::format("a call through '{}' with no signature", name));
            return;
        }
        FnInfo synthetic;
        synthetic.returnType = *fnType.result;
        for (const auto& p : fnType.params) synthetic.paramTypes.push_back(*p);

        std::vector<llvm::Value*> args;
        if (!emitCallArgs(node, synthetic, name, argNodes, args)) return;

        auto* call = builder_.CreateCall(fnType.llvmSignature, callee, args);
        value_ = synthetic.returnType.isVoid() ? CgVal{}
                                               : CgVal{call, synthetic.returnType};
    }

    // The declaration behind a method name, for the sole purpose of saying why a call
    // to it did not find a function. Null for a name this struct does not declare.
    static const FunctionDeclaration* findMethod(const StructInfo& info,
                                                 const std::string& name) {
        if (info.decl)
            for (auto& m : info.decl->methods)
                if (m->name == name) return m.get();
        // And a method an `implements` block wrote for it, for the reason findOperator
        // reads the same list: the block's method is the struct's method.
        if (info.extras)
            for (FunctionDeclaration* m : info.extras->methods)
                if (m->name == name) return m;
        return nullptr;
    }

    // The struct whose declaration a member named on `info` reaches: `info` itself when
    // it declares one, otherwise the nearest base that does. Null for a name nowhere in
    // the hierarchy.
    //
    // `keyOf` turns a struct's name into the functions_ key to look for, which is what
    // lets one walk serve both a method and an operator -- an operator is a method with
    // a spelled name, and inheriting one is the same question about the same table.
    //
    // Breadth-first, so an override wins over what it overrides -- `Student.to_string`
    // is found before `Person.to_string` because it is found a level earlier
    // (deeptest2.fin:78 writes exactly that pair). Two bases at the *same* distance
    // that both declare the name is a language question -- which one `d.f()` means --
    // and `conflict` reports it back rather than being answered by declaration order,
    // the same way declareStructs refuses a second inherited *field* of one name.
    //
    // Reports nothing itself, because one of its callers has no node to report at:
    // interfaceVtable resolves a name for a table slot and answers a missing one with a
    // null entry.
    template <typename KeyOf>
    const StructInfo* findProvider(const StructInfo& info, KeyOf keyOf,
                                   const StructInfo** conflict = nullptr) const {
        std::vector<const StructInfo*> level{&info};
        std::set<const StructInfo*> seen{&info};
        while (!level.empty()) {
            std::vector<const StructInfo*> declaring;
            for (const StructInfo* s : level)
                if (functions_.count(keyOf(s->finName))) declaring.push_back(s);
            if (declaring.size() > 1) {
                if (conflict) *conflict = declaring[1];
                return declaring[0];
            }
            if (declaring.size() == 1) return declaring[0];
            std::vector<const StructInfo*> next;
            for (const StructInfo* s : level) {
                if (!s->decl) continue;
                for (auto& parent : s->decl->parents) {
                    // An interface contributes no methods to look up here: what a
                    // struct owes an interface it declares itself, and a call *through*
                    // an interface reference goes through the vtable instead.
                    if (!parent || parentIsInterface(*parent)) continue;
                    auto base = structs_.find(parent->name);
                    // A base this file did not lower was already refused at the derived
                    // struct's declaration (lowerableStruct), so there is nothing here
                    // to report a second time.
                    if (base == structs_.end()) continue;
                    // A diamond reaches one base along two paths and it is one base.
                    if (!seen.insert(&base->second).second) continue;
                    next.push_back(&base->second);
                }
            }
            level = std::move(next);
        }
        return nullptr;
    }

    const StructInfo* findMethodProvider(const StructInfo& info, const std::string& method,
                                         const StructInfo** conflict = nullptr) const {
        return findProvider(info,
                            [&](const std::string& s) { return methodKey(s, method); },
                            conflict);
    }

    // findMethodProvider, with the ambiguity reported. Null having reported, or null
    // having said nothing when the name is simply not there -- the caller's own
    // reportMissingMethod is the better message for that, and `failed_` tells the two
    // apart.
    const StructInfo* methodProvider(ASTNode& node, const StructInfo& info,
                                     const std::string& method) {
        const StructInfo* conflict = nullptr;
        const StructInfo* provider = findMethodProvider(info, method, &conflict);
        if (conflict && provider) {
            unsupported(node, fmt::format("a call to the method '{}' on struct '{}', which "
                                          "inherits one from '{}' and one from '{}'",
                                          method, info.finName, provider->finName,
                                          conflict->finName));
            return nullptr;
        }
        return provider;
    }

    // The same walk for an operator, with the same ambiguity refusal: `a + b` where the
    // base declares `operator +` and the derived struct does not.
    const StructInfo* operatorProvider(ASTNode& node, const StructInfo& info,
                                       ASTTokenKind op) {
        const StructInfo* conflict = nullptr;
        const StructInfo* provider = findProvider(
            info, [&](const std::string& s) { return operatorKey(s, op); }, &conflict);
        if (conflict && provider) {
            unsupported(node, fmt::format("an operator '{}' on struct '{}', which inherits "
                                          "one from '{}' and one from '{}'",
                                          spellOperator(op), info.finName,
                                          provider->finName, conflict->finName));
            return nullptr;
        }
        return provider;
    }

    // Whether a `&derived` may be handed to `base`'s methods unchanged.
    //
    // Measured against the data layout rather than assumed from the declaration. The
    // first base's fields splice in at offset 0, so for it the answer is always yes and
    // the upcast emits no instruction -- but a *second* base's fields start after the
    // first's, and a method of it GEPs at the indices it has in its own struct, which in
    // the derived object are the first base's fields. That is a silent read of the wrong
    // field rather than a missing feature, so the offsets are compared and the call is
    // refused when they disagree.
    //
    // Every field is compared, the base's own inherited ones included, which is what
    // makes the answer transitive: a base that shares its layout with the derived struct
    // shares it for whatever its own methods pass further up.
    bool baseSharesLayout(const StructInfo& derived, const StructInfo& base) const {
        if (!derived.complete || !base.complete) return false;
        if (!derived.llvmType || !base.llvmType) return false;
        const auto& dl = module_.getDataLayout();
        const auto* d = dl.getStructLayout(derived.llvmType);
        const auto* b = dl.getStructLayout(base.llvmType);
        for (size_t i = 0; i < base.fields.size(); ++i) {
            size_t index = 0;
            if (!derived.find(base.fields[i].name, index)) return false;
            if (index >= derived.llvmType->getNumElements()) return false;
            if (d->getElementOffset(index) != b->getElementOffset(i)) return false;
        }
        return true;
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

    // One instance of a generic method or operator: its own type parameters inferred
    // from the argument values, composed onto the struct's, declared under a key that
    // names both substitutions, and its body queued.
    //
    // This is the layer declareStructMethods deliberately stops short of, and the reason
    // it has to be here rather than there is that a method template has no signature
    // until a call says what its parameters are -- the same reason a generic *function*
    // is instantiated at its call and not at its declaration. What is new is that there
    // are now two substitutions live at once: the struct's, which the receiver fixed,
    // and the method's, which the arguments fix.
    //
    // Shared by the method and the operator because the two differ in exactly three
    // things -- where the receiver comes from, how the arguments are spelled, and the
    // name -- and in none of the composition. An operator arrives as its pieces for the
    // reason PendingBody does: an OperatorDeclaration and a FunctionDeclaration are
    // unrelated classes with the same three members.
    //
    // Returns the key, or an empty string having already reported.
    std::string instantiateGenericMethod(
            ASTNode& node, const StructInfo& owner, const CgType& receiver,
            ASTNode& decl, const std::string& name,
            const std::vector<std::unique_ptr<GenericParam>>& genericParams,
            const std::vector<std::unique_ptr<Parameter>>& params, Block& body,
            const std::vector<CgVal>& values) {
        // The marker, before the inference that would otherwise decide the
        // representation for it. This site had no check at all until now and was the
        // hole the move exposed: `fun peek<U: Castable>(u: U)` on a non-generic struct
        // reached declareFunction through this path, monomorphised at the argument's
        // type, and ran -- the silent different program ADR 0002 names, with none of
        // the declaration-site refusal's cover, because a method's type parameters are
        // not in the struct's `generic_params` and lowerableTemplate never saw them.
        //
        // The struct's own parameters are not re-checked here: they were checked at the
        // instantiation that produced `owner`, so a method of `maybe<int>` is
        // unreachable already.
        if (refuseIfErased(node, genericParams,
                           fmt::format("the generic method '{}' of struct '{}'", name,
                                       owner.finName))) {
            return {};
        }

        // The written parameters, without the receiver. A written `self` is the
        // receiver and not an argument -- declareFunction drops it from the signature
        // for exactly this reason -- so it must not be unified against argument 0
        // either, or `set_x(5)` would try to bind `U` from the struct's own pointer.
        std::vector<const Parameter*> written;
        for (auto& p : params) {
            if (p->is_vararg) {
                // No corpus site, and nothing to infer from: a `...` position has no
                // declared type for a binding to unify against.
                unsupported(*p, fmt::format("'...' on the generic method '{}' of struct "
                                            "'{}'", name, owner.finName));
                return {};
            }
            if (p->name == "self") continue;
            written.push_back(p.get());
        }
        if (written.size() != values.size()) {
            // The analyzer already checked arity; reaching here is the two passes
            // disagreeing, so it says so rather than padding.
            unsupported(node, fmt::format("a call to '{}' on struct '{}' with {} "
                                          "argument(s) where it declares {}",
                                          name, owner.finName, values.size(),
                                          written.size()));
            return {};
        }

        // The inference, off the argument values rather than off the analyzer -- the
        // same one-directional unification a generic free call uses, and for the same
        // reason: a turbofish binds nothing in Analyzer_Expr (booked), so an answer read
        // from there would be wrong for the one call that spells its arguments out.
        Substitution inferred;
        for (size_t i = 0; i < values.size(); ++i) {
            unifyBinding(written[i]->type.get(),
                         TypeBinding{values[i].type, cgDisplay(values[i].type)},
                         genericParams, inferred);
        }

        // In declaration order, whatever order inference found them in, because the key
        // is built from this list: `set<A, B>(b: B, a: A)` would otherwise be two names
        // for one instance depending on which call site reached it first.
        Substitution ordered;
        for (auto& p : genericParams) {
            bool found = false;
            for (auto& b : inferred) {
                if (b.first != p->name) continue;
                ordered.push_back(b);
                found = true;
                break;
            }
            if (found) continue;
            // Nothing to infer it from. Refused naming the parameter, because the
            // alternative is picking a type, and a method instantiated at a type the
            // program never named is a method the program did not write. A turbofish on
            // a method is how this one would be spelled and is refused in visit(Method-
            // Call&), so there is no second way in.
            unsupported(node, fmt::format("a call to '{}' on struct '{}' whose type "
                                          "argument '{}' no argument mentions",
                                          name, owner.finName, p->name));
            return {};
        }

        // A method type parameter that reuses a name the struct already bound is
        // refused, and this is the one refusal in this unit that is not about a missing
        // feature. TypeMapper::boundBinding returns the *first* match in the list, and
        // the composition below appends -- so `fun set<T>(v: T)` on a `Box<T>` would
        // silently resolve its own `T` to the struct's and instantiate at the field's
        // type, whatever the argument was. Shadowing would be the other answer and it
        // is not this file's to choose: struct_methods.fin:14 says of `set_x<U>` that
        // "its separated from the struct generic itself so it cant have the same name
        // as `T`", which is the corpus ruling that the collision is not written.
        // `Self` and the template's bare name are in that list too, on the same terms.
        for (auto& m : ordered) {
            for (auto& already : owner.methodBindings) {
                if (already.first != m.first) continue;
                unsupported(node,
                            fmt::format("a call to '{}' on struct '{}' whose type "
                                        "parameter '{}' the struct already binds",
                                        name, owner.finName, m.first));
                return {};
            }
        }

        // `Box<int>.set_x<int>` -- both substitutions in the name, because both are
        // needed to tell two instances apart and either alone would collide. Asking
        // twice finds the first, which is what makes two calls at the same arguments one
        // symbol rather than a second definition of it.
        const std::string key = methodKey(owner.finName, mangledName(name, ordered));
        if (functions_.count(key)) return key;

        // Composed *onto* the struct's, not replacing them: the body says `self.val`
        // and `new_x` in the same statement, so `T` and `U` have to resolve at the same
        // time. Stored before anything is emitted, because the mapper holds a pointer to
        // it for the whole of the signature and the body -- and in a node-based map, so
        // that pointer survives the further instantiations a body may add.
        Substitution composed = owner.methodBindings;
        composed.insert(composed.end(), ordered.begin(), ordered.end());
        fnInstances_[key] = composed;
        ScopedBindings bound(types_, &fnInstances_[key]);

        declareFunction(decl, key, key, params, returnTypeOf(decl), /*isVarArg=*/false,
                        /*isExtern=*/false, &receiver);
        auto declared = functions_.find(key);
        if (declared == functions_.end()) return {};  // declareFunction reported

        // Weak, for the reason every method and every generic instance in this file is
        // weak: two objects that each declare this struct and each make this call both
        // publish this symbol and neither knows the other exists, so identical
        // definitions and let the linker keep one.
        declared->second.fn->setLinkage(llvm::Function::LinkOnceODRLinkage);

        // Deferred to the same queue a non-generic method's body goes on, and for the
        // stronger of that queue's two reasons: this runs from the middle of the
        // caller's body, where emitting straight away would mean nesting two insert
        // points. run() drains after the statement loop, and drainPendingBodies re-reads
        // size() -- so an instance asked for while emitting another instance is emitted
        // too.
        pendingBodies_.push_back(
            PendingBody{&decl, &params, &body, key, &fnInstances_[key]});
        return key;
    }

    // The return type of a method or an operator declaration, which is the one piece
    // instantiateGenericMethod cannot take apart for itself: its `decl` is an ASTNode,
    // because the two classes it may be share no base that has a return type.
    static const TypeNode* returnTypeOf(ASTNode& decl) {
        if (auto* f = dynamic_cast<FunctionDeclaration*>(&decl)) return f->return_type.get();
        if (auto* o = dynamic_cast<OperatorDeclaration*>(&decl)) return o->return_type.get();
        return nullptr;
    }

    // The already-emitted arguments of a generic method call, converted to the instance's
    // parameter types and called.
    //
    // Not emitCallArgs, and that is the point: the arguments were emitted *before* the
    // instance existed, because their types are what the instantiation was inferred
    // from. Emitting them again here would evaluate `f()` in `b.set(f())` twice, which
    // is a wrong program rather than a missing feature.
    void emitInstanceCall(ASTNode& node, const std::string& key, llvm::Value* receiver,
                          const std::vector<CgVal>& values) {
        auto instance = functions_.find(key);
        if (instance == functions_.end()) return;  // already reported
        const FnInfo info = instance->second;
        if (info.paramTypes.size() != values.size() + 1) {
            unsupported(node, fmt::format("a call to '{}' with too few arguments", key));
            return;
        }
        std::vector<llvm::Value*> args{receiver};
        args.reserve(values.size() + 1);
        for (size_t i = 0; i < values.size(); ++i) {
            llvm::Value* converted = convert(node, values[i], info.paramTypes[i + 1]);
            if (!converted) return;
            args.push_back(converted);
        }
        emitCall(info, args);
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
    // An interface emits nothing, and nothing is the whole of its lowering.
    //
    // This is the one place in this file where "emitted nothing" is not the skip the
    // founding rule forbids, so the distinction is worth stating. That rule is about
    // a statement with *runtime meaning*: an assignment or a call that is dropped
    // still had an effect the program was entitled to, and dropping it silently is a
    // miscompile. An interface declaration has no such effect to lose. It allocates
    // no storage -- an interface is never the type of a variable or a field here --
    // it defines no symbol, and each of its members is a *requirement* on some other
    // type rather than code of its own. So there is no third state between "emitted
    // nothing" and "lowered completely"; they are the same state.
    //
    // Which is why the members are inspected rather than assumed empty. A body
    // written inside an interface *is* code, and emitting nothing for the declaration
    // that holds it would discard it -- the miscompile, not the harmless nothing. Who
    // inherits a default is an open ruling (section 8 of docs/HANDOFF.md,
    // "interface-member defaults"), and a construct waiting on a ruling is refused
    // where it is written, not guessed at.
    //
    // Today the grammar only admits one of these forms: a method body parses, an
    // operator body does not (`expecting SEMICOLON`), and no destructor spelling
    // inside an interface parses at all. The rest of the check is here because it
    // costs one condition each and a grammar that widens should not widen this
    // silently.
    void visit(InterfaceDeclaration& node) override {
        for (auto& m : node.methods) {
            if (m && m->body) {
                unsupported(*m, fmt::format("a body on interface method '{}'", m->name));
                return;
            }
        }
        for (auto& o : node.operators) {
            if (o && (o->body || o->implements_expr || o->implements_type)) {
                unsupported(*o, fmt::format("an implemented operator in interface '{}'", node.name));
                return;
            }
        }
        for (auto& c : node.constructors) {
            if (c) {
                unsupported(*c, fmt::format("a constructor in interface '{}'", node.name));
                return;
            }
        }
        if (node.destructor) {
            unsupported(*node.destructor, fmt::format("a destructor in interface '{}'", node.name));
            return;
        }
        // A member's *default* is an expression, and which implementor evaluates it is
        // the same unmade ruling as a default body. The type it declares needs nothing:
        // no layout is computed for an interface, so an unlowerable member type here is
        // not an error either.
        for (auto& m : node.members) {
            if (m && m->default_value) {
                unsupported(*m, fmt::format("a default on interface field '{}'", m->name));
                return;
            }
        }
    }
    void visit(EnumDeclaration& node) override {
        // Registered above and emits nothing: an enum's members are constants folded
        // into their uses, so there is no symbol and no storage. One declareEnums never
        // saw is refused rather than assumed handled -- an enum nested inside a
        // function body would land here.
        if (registeredEnums_.count(&node)) return;
        unsupported(node, fmt::format("a declaration of enum '{}' here", node.name));
    }
    void visit(ClassDeclaration& node) override { unsupported(node, "a class declaration"); }
    // A block whose members a struct took emits nothing here, and nothing is the whole
    // of it: its methods, operators and constructors were declared with that struct's
    // (declareStructMethods, through StructInfo::extras) and their bodies are on the
    // same queue, so by the time the statement walk reaches the block there is nothing
    // left to do. The interface it names contributes no bytes and no check -- what a
    // struct owes an interface is the analyzer's question, which it answers with
    // `'X' does not fully implement interface 'Y'`.
    //
    // One that was *not* consumed is refused, and the reason it was not is always the
    // same: its target is not a struct this file lowered. An enum target
    // (stdlib/typing.fin:27), an interface, a name declared nowhere, a struct refused
    // for one of lowerableStruct's shapes -- and the single-member overwrite form, whose
    // right-hand side is a value rather than a declaration (collectImplementsBlocks says
    // why it is not collected). Refusing here rather than at collection time is what
    // puts the diagnostic on the block's own line.
    void visit(ImplementsBlock& node) override {
        if (registeredBlocks_.count(&node)) return;
        if (!node.overwrite_member.empty()) {
            unsupported(node, fmt::format("an implements block overwriting the member "
                                          "'{}' of '{}'",
                                          node.overwrite_member, node.target_type));
            return;
        }
        unsupported(node, fmt::format("an implements block on '{}', which is not a "
                                      "struct this file lowered",
                                      node.target_type));
    }
    void visit(OperatorDeclaration& node) override { unsupported(node, "an operator declaration"); }
    void visit(ConstructorDeclaration& node) override { unsupported(node, "a constructor"); }
    void visit(DestructorDeclaration& node) override { unsupported(node, "a destructor"); }
    void visit(SpecialDeclaration& node) override { unsupported(node, "a '@special' declaration"); }
    // Six statements wear this one node (src/ast/decls/TypeDef.hpp): a type alias
    // `type Integer = int;`, a union alias `type Number = int | uint;`, the erasure
    // marker `type Any<...> = any implements <...>`, a symbol resolution
    // `pub implements c_printf = printf;` (stdlib/stdio.fin:15), an extern alias
    // `extern myns::myfunc as myfunc;` (extern_as.fin:19) and a wildcard extern
    // `extern * from a_namespace;` (extern_as.fin:32, :39). All six bind a name, and a
    // name is not something this file emits -- so all six are lowered as nothing.
    //
    // Emitting nothing is not the skip the founding rule forbids, and the node's shape
    // is the proof rather than the claim. A TypeDefinition holds a name, generic
    // parameters, TypeNodes and flags: no Block, no Expression, no Statement child.
    // There is no statement inside it that emitting nothing could drop, so "emitted
    // nothing" and "lowered completely" are one state here instead of two that look
    // alike from outside. Contrast visit(InterfaceDeclaration&) above, which has
    // refusals precisely because an interface method *can* carry a body; and compare
    // visit(EnumDeclaration&), which emits nothing for exactly this reason.
    //
    // What the name means is settled before here: Soundness_ExternAlias
    // (tests/test_soundness.cpp) fixes that an extern alias carries its target's type,
    // that an alias of a type is still a type, and that a wildcard extern is a no-op
    // because a namespace's contents are already spliced into the enclosing statement
    // list. So a *declaration* never needs the alias table. A *use* of a renaming
    // alias does, and this backend resolves names literally -- `shortname()` for
    // `myns::realname` reaches no function, `myglobv_diffname` reaches no global,
    // `<Integer>` reaches no type. Each of those refuses at its own use site, in
    // visitCall, in the identifier path and in the type mapper respectively, which is
    // why nothing needs refusing here to keep the boundary visible. Three
    // KnownDefect_Codegen tests hold that boundary; if resolution lands before codegen
    // they invert.
    //
    // The attributes are the exception, because an attribute is a demand and not a
    // name. `#[llvm_name]` is honoured on a struct and on a function, so discarding one
    // on an alias would be a naming request silently dropped -- the same reasoning that
    // refuses an attribute on a global above.
    void visit(TypeDefinition& node) override {
        if (!node.attributes.empty()) {
            unsupported(node, fmt::format("an attribute on the alias '{}'", node.name));
            return;
        }
    }
    void visit(StructMember& node) override { unsupported(node, "a struct member"); }
    void visit(Parameter& node) override { unsupported(node, "a parameter"); }

    // A macro or an import that survives to codegen is a pass that did not run:
    // Macro declarations are compile-time definitions. They have no runtime symbol
    // or storage to emit, so imported macro libraries and standalone macro files can
    // both leave the declaration in the AST.
    void visit(MacroDeclaration&) override {}
    void visit(MacroCall& node) override { unsupported(node, "a macro call (macro expansion did not consume it)"); }
    void visit(MacroInvocation& node) override {
        const auto* builtin = builtinmacros::find(node.name);
        if (!builtin) {
            unsupported(node, "a macro invocation (macro expansion did not consume it)");
            return;
        }
        // One row, one lowering, matched by name. A second row added to the table
        // without a lowering here would otherwise fall through to `format!`'s, which
        // would build a string for a macro that promised something else -- so the
        // dispatch is explicit and the unimplemented row refuses by its own name.
        if (builtin->name == "format") { emitFormatMacro(node); return; }
        unsupported(node, fmt::format("the compiler-implemented macro '{}!'", node.name));
    }
    void visit(QuoteExpression& node) override { unsupported(node, "a quote"); }
    void visit(ImportModule& node) override { unsupported(node, "an import (the module loader did not consume it)"); }

    // `foreach (e <T> in xs)` walks an array from the front, binding each element to a
    // copy of it, and the two-binding form binds the position beside it.
    //
    // Derived rather than chosen. tests/samples/loops.fin:19 and :24 are the only
    // `foreach` sites in the corpus and both walk the fixed `[int, 5]` declared at :12,
    // one in each spelling; :20's body is `blame element == a[idx]`, which fixes three
    // things at once -- the element at `idx` is the element the loop binds, the index
    // counts from 0 in step with it, and the binding is a *value* of the element type
    // rather than a reference (an `int` compares equal to `a[idx]` either way, but only
    // a copy makes the two spellings at :19 and :24 the same loop). `lib/std/hashmap.fin`
    // :316 and `lib/std/collection.fin`:59 both record that there is no iteration
    // protocol and that index-based iteration is what the library types support, so an
    // array is the whole of what is iterable here and anything else is a refusal with
    // that ruling named, not a lowering invented for it.
    //
    // The shape is `visit(ForLoop&)`'s, because that is what this is: a counter, a
    // comparison against a bound, an indexed read, and an increment `continue` reaches.
    // What it adds is that the counter and the bound are the loop's own rather than the
    // program's, so the two cannot disagree the way `i < a.length - 1` (loops.fin:14)
    // does with the array it walks.
    void visit(ForeachLoop& node) override {
        if (!currentFn_) { unsupported(node, "a 'foreach' outside a function"); return; }
        if (!node.iterable) { unsupported(node, "a 'foreach' with no iterable"); return; }

        // The bindings live in a scope of their own, exactly as a `for`'s init does:
        // `element` is the loop's name and not the enclosing body's.
        pushScope();

        // The iterable is reached once, before any block exists, and for its *address*.
        //
        // Once, because the iterable is an expression and may be a call: emitting it in
        // the condition would call it on every iteration, which is the classic way to
        // turn a walk of an array into a walk of N fresh arrays.
        //
        // Through an address, because indexing needs a home -- the same rule
        // `visit(ArrayAccess&)` follows (LLVM's extractvalue takes a constant index, so
        // an array that is only a value cannot be indexed at all). `baseAddress` also
        // accepts a pointer to an array, which is what makes `foreach` over a
        // `&[int, 3]` walk the array rather than the pointer, matching `ptr_to_arr[0]`
        // (deeptest3.fin:111) and `.length` one screen up.
        std::optional<Addr> seq = baseAddress(*node.iterable, CgType::Kind::Array);
        if (failed_) { popScope(); return; }
        if (!seq || !seq->type.isArray() || !seq->type.element ||
            !seq->type.element->llvmType) {
            // The two reasons said apart, because they send a reader to different
            // places: "not an array" is the ruling that there is no iteration protocol,
            // and "no home" is the same temporary-has-no-address gap `give()[0]` has.
            // Emitting the iterable again to tell them apart is safe here and is what
            // `.length` does for the same question: nothing that arrives here has an
            // address, so nothing was emitted by the attempt above.
            CgVal v = emit(*node.iterable);
            const bool isArray = !failed_ && v.ok() &&
                                 (v.type.isArray() ||
                                  (v.type.isPointer() && v.type.pointee &&
                                   v.type.pointee->isArray()));
            popScope();
            unsupported(node, isArray ? "a 'foreach' over an array with no home"
                                      : "a 'foreach' over something that is not an array");
            return;
        }

        const CgType element = *seq->type.element;

        // The written binding type has to *be* the element type.
        //
        // Nothing before this point checks it -- the analyzer defines both bindings from
        // their written types and never asks the iterable what it yields
        // (KnownDefect_Foreach.ABindingTypeIsNeverCheckedAgainstTheIterable) -- so
        // `foreach (e <string> in a)` over an `[int]` arrives here as a well-typed
        // program. Converting each element to the written type would make that compile
        // and read four bytes of an integer as a pointer; refusing it is the only answer
        // that does not invent a rule the front end has not ruled on. The corpus writes
        // the element's own type at both sites, so nothing measured needs more.
        std::optional<CgType> bound = types_.map(node.var_type.get());
        if (!bound) {
            popScope();
            unsupportedType(node, node.var_type.get(), "a 'foreach' binding");
            return;
        }
        if (!sameType(*bound, element)) {
            popScope();
            unsupported(node, fmt::format("a 'foreach' binding of type '{}' over "
                                          "elements of another type",
                                          typeName(node.var_type.get())));
            return;
        }

        // The index binding of the two-binding form. Any integer type, because what it
        // is handed is a position and every integer width holds one -- and only an
        // integer, because a position converted to a float or a pointer is not the
        // number the body compares against an index (`a[idx]`).
        std::optional<CgType> indexType;
        if (!node.index_name.empty()) {
            indexType = types_.map(node.index_type.get());
            if (!indexType) {
                popScope();
                unsupportedType(node, node.index_type.get(), "a 'foreach' index binding");
                return;
            }
            if (indexType->kind != CgType::Kind::Int || indexType->isBool) {
                popScope();
                unsupported(node, fmt::format("a 'foreach' index binding of type '{}'",
                                              typeName(node.index_type.get())));
                return;
            }
        }

        // The counter is a signed `int`, which is the type `.length` answers with
        // (Soundness_Members.ALengthIsAnIntAndNotAnotherIntegerWidth) and the type a
        // dynamic array's length word already is. One type for the counter and the
        // bound, so the comparison needs no conversion and cannot acquire a signedness
        // this file did not choose.
        const CgType counterType = types_.intType(32, true);

        // A fixed array's extent is a constant of its type; a dynamic array's length is
        // a word in its pair. Either way it is read *once*, before the loop -- a fixed
        // extent cannot be re-read (there is nothing to re-read) and making the two
        // kinds disagree about whether the bound is live would mean `foreach` over
        // `[int, 3]` and over `[int]` were two loops. A body that replaces the array it
        // is walking therefore keeps walking the one it started with; the corpus writes
        // no such body, and the day it does is the day this is a ruling rather than a
        // consequence.
        llvm::Value* limit = nullptr;
        llvm::Value* data = nullptr;  // the dynamic pair's element pointer, or null
        if (seq->type.isDynamicArray) {
            llvm::Value* pair = builder_.CreateLoad(seq->type.llvmType, seq->ptr, "array");
            data = builder_.CreateExtractValue(pair, {0}, "data");
            limit = builder_.CreateExtractValue(pair, {1}, "length");
        } else {
            // An extent past what an `int` holds would be truncated into a bound that
            // walks the wrong number of elements, and silently. `.length` on such an
            // array already misreports it -- that is one defect -- and a loop that runs
            // the wrong count would be a second and a worse one.
            if (seq->type.extent > 0x7fffffffull) {
                popScope();
                unsupported(node, "a 'foreach' over an array with more elements than an "
                                  "'int' can count");
                return;
            }
            limit = llvm::ConstantInt::get(counterType.llvmType,
                                           (uint64_t)seq->type.extent, true);
        }

        // The three slots, before the loop rather than inside it: an alloca in the body
        // is a fresh frame slot on every iteration.
        auto* counter = builder_.CreateAlloca(counterType.llvmType, nullptr, "foreach.i");
        builder_.CreateStore(llvm::ConstantInt::get(counterType.llvmType, 0), counter);
        auto* elementSlot = builder_.CreateAlloca(element.llvmType, nullptr, node.var_name);
        llvm::AllocaInst* indexSlot = nullptr;
        if (indexType) {
            indexSlot = builder_.CreateAlloca(indexType->llvmType, nullptr, node.index_name);
        }
        // Ordinary locals from here on, which is what makes the body's reads of them
        // the same code any other read of a local is.
        scopes_.back()[node.var_name] = Local{elementSlot, element};
        if (indexType) scopes_.back()[node.index_name] = Local{indexSlot, *indexType};

        auto* condBB = llvm::BasicBlock::Create(ctx_, "foreach.cond", currentFn_->fn);
        auto* bodyBB = llvm::BasicBlock::Create(ctx_, "foreach.body", currentFn_->fn);
        auto* stepBB = llvm::BasicBlock::Create(ctx_, "foreach.step", currentFn_->fn);
        auto* endBB = llvm::BasicBlock::Create(ctx_, "foreach.end", currentFn_->fn);

        builder_.CreateBr(condBB);
        builder_.SetInsertPoint(condBB);
        llvm::Value* at = builder_.CreateLoad(counterType.llvmType, counter, "i");
        builder_.CreateCondBr(builder_.CreateICmpSLT(at, limit, "foreach.more"), bodyBB,
                              endBB);

        builder_.SetInsertPoint(bodyBB);
        llvm::Value* i = builder_.CreateLoad(counterType.llvmType, counter, "i");
        // Widened to i64 before it becomes a GEP index, and sign-extended because the
        // counter is signed -- the same two lines `emitAddress`'s index path writes, and
        // for the same reason: a single-index GEP on an array pointer strides by the
        // whole array, so a fixed array needs the leading zero and a dynamic one, whose
        // pointer is already to an element, must not have it.
        llvm::Value* wide = builder_.CreateSExt(i, builder_.getInt64Ty());
        llvm::Value* elementPtr =
            data ? builder_.CreateInBoundsGEP(element.llvmType, data, wide, "elem")
                 : builder_.CreateInBoundsGEP(seq->type.llvmType, seq->ptr,
                                              {builder_.getInt64(0), wide}, "elem");
        builder_.CreateStore(builder_.CreateLoad(element.llvmType, elementPtr, "element"),
                             elementSlot);
        if (indexSlot) {
            llvm::Value* stored = convert(node, CgVal{i, counterType}, *indexType);
            if (!stored) { popScope(); return; }
            builder_.CreateStore(stored, indexSlot);
        }

        // `continue` goes to the step, so it advances the counter. Skipping it would be
        // an infinite loop, and here it would be one with no visible increment to blame.
        loops_.push_back({stepBB, endBB});
        if (node.body) node.body->accept(*this);
        if (!terminated()) builder_.CreateBr(stepBB);
        loops_.pop_back();

        builder_.SetInsertPoint(stepBB);
        llvm::Value* next = builder_.CreateAdd(
            builder_.CreateLoad(counterType.llvmType, counter, "i"),
            llvm::ConstantInt::get(counterType.llvmType, 1), "next");
        builder_.CreateStore(next, counter);
        builder_.CreateBr(condBB);

        builder_.SetInsertPoint(endBB);
        popScope();
    }
    // `delete p` returns the allocation. deeptest3.fin:44 says what it is:
    // "(Calls destructor if defined, then frees memory)".
    //
    // No destructor call is emitted, and that is sound rather than pending: a struct
    // with a destructor is refused outright at its declaration (lowerableStruct), so a
    // `delete` reaching here provably has nothing to run. The day destructors lower is
    // the day this line has to grow one, and ADR 0016 (destructors compose) is where
    // the order comes from.
    // Can this expression's address be taken twice without the program noticing?
    //
    // Asked by the one caller that has to try an address, may not like what it finds, and
    // then hand the same expression to a path that will address it again
    // (emitPrototypeDelete, whose fall-through is `delete &a[i]` on an array). A slot, a
    // GEP off one and a load are all repeatable; a call in an index is not, so
    // `delete &tables[next()][k]` would run `next()` twice.
    //
    // Repeatable rather than emits-nothing: a dynamic array's element address loads the
    // pair, and loading it twice asks the same question twice. What must not repeat is an
    // effect.
    //
    // Conservative in the safe direction: an unknown form answers false, and the caller
    // refuses rather than lowering it twice.
    static bool addressIsRepeatable(Expression& expr) {
        if (dynamic_cast<Identifier*>(&expr)) return true;
        if (auto* member = dynamic_cast<MemberAccess*>(&expr)) {
            return !member->is_static && member->object &&
                   addressIsRepeatable(*member->object);
        }
        if (auto* access = dynamic_cast<ArrayAccess*>(&expr)) {
            return access->array && access->index &&
                   addressIsRepeatable(*access->array) && isRepeatableIndex(*access->index);
        }
        return false;
    }

    // An index expression whose evaluation is worth nothing to repeat: a constant, or a
    // name read out of a slot. Deliberately a short list -- everything else, arithmetic
    // included, could contain a call.
    static bool isRepeatableIndex(Expression& expr) {
        if (dynamic_cast<Literal*>(&expr)) return true;
        if (dynamic_cast<Identifier*>(&expr)) return true;
        if (auto* member = dynamic_cast<MemberAccess*>(&expr)) {
            return !member->is_static && member->object &&
                   addressIsRepeatable(*member->object);
        }
        return false;
    }

    // `delete &p[key]` -- tests/samples/prototype_test.fin:23, whose comment calls it "the
    // manual way" against `a.rm("b")` on the next line as "the functional way". One
    // operation with two spellings, so this is emitPrototypeRemove and not a `free`.
    //
    // Matched as a whole statement rather than by giving `&p[key]` an address that
    // `delete` then frees, and that is a correctness point and not a style one: a value
    // slot's address points *into* the values buffer, so handing it to `free` would give
    // libc a block it never allocated. The bare `&p[key]` stays refused for that reason
    // and one more -- an appending store reallocs both buffers, which leaves any such
    // pointer dangling with nothing to warn its holder.
    //
    // Returns false for anything that is not this shape, including a `&x[i]` on an array,
    // which is the `free` the rest of visit(DeleteStatement&) lowers.
    bool emitPrototypeDelete(DeleteStatement& node) {
        auto* unary = dynamic_cast<UnaryOp*>(node.expr.get());
        if (!unary || unary->op != ASTTokenKind::AMPERSAND || unary->is_postfix ||
            !unary->operand) return false;
        auto* access = dynamic_cast<ArrayAccess*>(unary->operand.get());
        if (!access || !access->array || !access->index) return false;
        if (!addressIsRepeatable(*access->array)) {
            // The shape is right and the base cannot be addressed twice, so this cannot
            // be answered by trying the prototype path and falling back. Refused with the
            // reason, rather than reported as "no home" by the `&` that follows.
            unsupported(node, "a 'delete' whose subject is indexed through an expression "
                              "that cannot be evaluated twice");
            return true;
        }
        auto base = baseOf(emitAddress(*access->array), CgType::Kind::Prototype);
        if (failed_) return true;
        if (!base) return false;
        CgVal key = emit(*access->index);
        if (failed_ || !key.ok()) return true;
        // The bool says whether anything was there. A statement has nobody to tell, and
        // removing a key that is absent is not an error -- the same rule `rm` follows.
        emitPrototypeRemove(node, *base, key);
        return true;
    }

    void visit(DeleteStatement& node) override {
        if (!currentFn_) { unsupported(node, "'delete' outside a function"); return; }
        if (!node.expr) { unsupported(node, "'delete' with no operand"); return; }
        if (emitPrototypeDelete(node)) return;
        CgVal v = emit(*node.expr);
        if (failed_) return;
        if (!v.ok()) {
            unsupported(node, "'delete' of an invalid value");
            return;
        }
        llvm::Value* address = v.value;
        if (v.type.isDynamicArray) {
            address = builder_.CreateExtractValue(v.value, {0}, "array_data");
        } else if (!v.type.isPointer()) {
            unsupported(node, "'delete' of a non-pointer");
            return;
        }
        llvm::FunctionCallee release = runtimeFn(
            node, "free",
            llvm::FunctionType::get(llvm::Type::getVoidTy(ctx_),
                                    {llvm::PointerType::getUnqual(ctx_)}, false),
            "a deallocation");
        if (!release) return;
        builder_.CreateCall(release, {address});
    }

    // A libc entry point, declared on demand.
    //
    // A Fin program may have declared the same name itself -- deeptest2.fin:4 writes
    // `@define free(ptr: &void) <noret>;`, which is this exact signature -- and one of
    // *those* is the same symbol, so the declaration is shared rather than duplicated.
    // A name already declared with a different signature refuses: calling through a
    // FunctionCallee whose type disagrees with the callee's would link and pass its
    // arguments in the wrong places.
    //
    // `what` names the construct that wanted it, because the refusal is read by someone
    // looking at their own `@define` and not at this file: "an allocation" and "a
    // 'blame'" send them to different lines of their own program.
    llvm::FunctionCallee runtimeFn(ASTNode& node, const char* name,
                                   llvm::FunctionType* type, const char* what) {
        if (auto* existing = module_.getFunction(name)) {
            if (existing->getFunctionType() != type) {
                unsupported(node, fmt::format("{}, because '{}' is declared "
                                              "here with a different signature", what, name));
                return llvm::FunctionCallee();
            }
            return llvm::FunctionCallee(type, existing);
        }
        return module_.getOrInsertFunction(name, type);
    }
    // `try { ... } catch (E as e) { ... }` lowers as the try block alone.
    //
    // Ruled 2026-08-28, and it is a ruling about what Fin currently *is* rather than
    // about how to implement exceptions. Nothing in this language raises anything a
    // `catch` could receive: `blame`'s assert form prints and calls `abort` (ADR is in
    // the f5aedc3 commit message), so it does not unwind, and its raise form is still
    // refused. The corpus has exactly one `try` -- readonly.fin:48, wrapping
    // `a.v1 = 5` with `catch (Error as err)` -- and the assignment it guards cannot
    // raise: assigning to a readonly field is a *compile-time* error in every other
    // sample that does it.
    //
    // So the catch block is unreachable, and emitting nothing for it is not a dropped
    // statement -- it is the honest lowering of a handler for an event that cannot
    // occur. Emitting landing pads and a personality function instead would be
    // machinery no corpus site exercises, which is the thing this project has
    // consistently declined to build.
    //
    // The catch block is still *analysed* -- the front end walks it and type-checks
    // its body (Analyzer_Stmt.cpp:148-156) -- so a mistake inside one is still a
    // diagnostic. What is skipped is only the code generation.
    //
    // The day a raise form lowers, this becomes wrong and has to grow a real
    // mechanism. Soundness_Codegen.ATryBlockRunsAndItsCatchDoesNot is what will fail
    // then, because it asserts the catch body's printf never runs.
    void visit(TryCatch& node) override {
        if (!node.try_block) { unsupported(node, "a 'try' with no block"); return; }
        node.try_block->accept(*this);
    }


    // `blame c` and `blame c, "why"` -- the assert form, which prints where it failed
    // and aborts.
    //
    // One keyword, two statements, told apart by the operand's type and by nothing else
    // because they are written identically (SemanticAnalyzer::visit(BlameStatement&)).
    // `blame val > 0` asserts and `blame CollectionError("...")` raises. Only the assert
    // form is lowered here: a raise needs the runtime shape of a raised value, which is
    // unsettled, and every raise in the corpus is in a sample the front end stops before
    // this file sees it. So the raise form refuses, by that name.
    //
    // Not catchable, and that is a decision rather than an omission: `abort()` unwinds
    // nothing, so a `blame` inside a `try` would leave the `catch` unreached. Nothing in
    // the corpus asks -- readonly.fin's `try` wraps `a.v1 = 5` and its `blame` at :56 is
    // outside it -- so catchability would be a mechanism built on no evidence, and the
    // whole point of `try` is that something can reach the handler.
    //
    // `fprintf` and `abort`, on the same footing as the `malloc` and `free` that `new`
    // and `delete` already declare on demand: libc entry points, declared through
    // runtimeFn so that a Fin program which declared one itself shares the declaration
    // instead of colliding with it. `llvm.trap` was rejected because it discards the
    // message, and the corpus wrote "Value must be positive" deliberately.
    void visit(BlameStatement& node) override {
        if (!currentFn_) { unsupported(node, "'blame' outside a function"); return; }
        if (!node.condition) {
            // `blame;` with no operand. The grammar does not produce one, so this is the
            // parser and this file disagreeing rather than a program error -- and a
            // `blame` that checked nothing would be a statement that silently never
            // fires.
            unsupported(node, "'blame' with no condition");
            return;
        }

        CgVal cond = emit(*node.condition);
        if (failed_) return;
        if (!cond.ok()) { unsupported(node, "this blamed expression"); return; }
        if (cond.type.isStruct() || cond.type.isArray()) {
            // The raise form. Named as a raise and not as "this condition", because the
            // two are one keyword and a reader told "condition" would go looking for a
            // comparison they did not write.
            unsupported(node, "'blame' raising a value");
            return;
        }
        llvm::Value* test = asCondition(node, cond);
        if (!test) return;

        auto* failBB = llvm::BasicBlock::Create(ctx_, "blame.fail", currentFn_->fn);
        auto* okBB = llvm::BasicBlock::Create(ctx_, "blame.ok", currentFn_->fn);
        // The failing edge is the cold one, and saying so is free: the branch weight is
        // what stops an assertion from displacing the code it guards.
        builder_.CreateCondBr(test, okBB, failBB);

        builder_.SetInsertPoint(failBB);
        if (!emitBlameReport(node)) return;
        builder_.SetInsertPoint(okBB);
    }

    // A runtime Fin blame with a fixed reason: print `<file>:<line>: Fin blames
    // <reason>` on stderr, then abort. This is the shared shape ADR 0028 asks for when
    // an operation fails at run time for a reason the program itself did not write --
    // a missing prototype key is the first of them -- and it is deliberately the same
    // stream, the same location and the same abort a failed `blame` uses, so a reader
    // learns one diagnostic form and not two.
    //
    // The reason is a compile-time constant of this file's own, and it still goes
    // through `%s` rather than being the format string: a reason is text, and text that
    // reaches printf as a format is a vararg read waiting to happen the first time one
    // of them contains a `%`.
    //
    // Terminates the block with `unreachable`, because `abort` does not return and a
    // block without a terminator is invalid IR. Returns false having already reported.
    bool emitRuntimeBlame(ASTNode& node, const std::string& reason, const char* what) {
        llvm::Type* ptrTy = llvm::PointerType::getUnqual(ctx_);
        llvm::Type* i32Ty = llvm::Type::getInt32Ty(ctx_);
        llvm::GlobalVariable* errStream = module_.getGlobalVariable("stderr");
        if (!errStream) {
            errStream = new llvm::GlobalVariable(
                module_, ptrTy, /*isConstant=*/false,
                llvm::GlobalValue::ExternalLinkage, /*Initializer=*/nullptr, "stderr");
        }
        llvm::FunctionCallee report = runtimeFn(
            node, "fprintf",
            llvm::FunctionType::get(i32Ty, {ptrTy, ptrTy}, /*isVarArg=*/true), what);
        if (!report) return false;
        llvm::FunctionCallee stop = runtimeFn(
            node, "abort", llvm::FunctionType::get(llvm::Type::getVoidTy(ctx_), false),
            what);
        if (!stop) return false;
        llvm::Value* stream = builder_.CreateLoad(ptrTy, errStream, "stderr");
        llvm::Value* format = builder_.CreateGlobalString("%s:%d: Fin blames %s\n");
        llvm::Value* file = builder_.CreateGlobalString(sourceName_);
        llvm::Value* line = llvm::ConstantInt::get(i32Ty, node.loc.begin.line);
        llvm::Value* text = builder_.CreateGlobalString(reason);
        builder_.CreateCall(report, {stream, format, file, line, text});
        builder_.CreateCall(stop, {});
        builder_.CreateUnreachable();
        return true;
    }

    // The failing path: print, then abort. Returns false having already reported.
    //
    // The message is emitted *here*, inside the failing block, and not beside the
    // condition. It is an expression -- the analyzer only requires it to be a `string`,
    // not a literal -- so evaluating it where the condition is would run its side
    // effects on every pass of an assertion that never fires.
    //
    // `"%s"` for the message rather than the message as the format string. They look
    // equivalent for every message in the corpus and are not: a message containing `%d`
    // used as a format would read a vararg the caller never passed. The one place this
    // file builds a printf call for a string it did not write is the one place that
    // matters.
    bool emitBlameReport(BlameStatement& node) {
        llvm::Type* ptrTy = llvm::PointerType::getUnqual(ctx_);
        llvm::Type* i32Ty = llvm::Type::getInt32Ty(ctx_);

        llvm::Value* message = nullptr;
        if (node.message) {
            CgVal m = emit(*node.message);
            if (failed_) return false;
            if (!m.ok() || !m.type.isPointer()) {
                // The analyzer checks the message against `string`, so a non-pointer
                // here is the two passes disagreeing -- and handing an integer to
                // `%s` would read it as an address.
                unsupported(node, "this blame message");
                return false;
            }
            message = m.value;
        }

        // `stderr` is an external `FILE*`, which is what it is on every libc this
        // compiler has a target for. Declared as one machine word with no pointee,
        // because nothing here looks inside it -- it is loaded and passed straight on.
        llvm::GlobalVariable* errStream = module_.getGlobalVariable("stderr");
        if (!errStream) {
            errStream = new llvm::GlobalVariable(
                module_, ptrTy, /*isConstant=*/false,
                llvm::GlobalValue::ExternalLinkage, /*Initializer=*/nullptr, "stderr");
        }

        llvm::FunctionCallee report = runtimeFn(
            node, "fprintf",
            llvm::FunctionType::get(i32Ty, {ptrTy, ptrTy}, /*isVarArg=*/true),
            "a 'blame'");
        if (!report) return false;
        llvm::FunctionCallee stop = runtimeFn(
            node, "abort", llvm::FunctionType::get(llvm::Type::getVoidTy(ctx_), false),
            "a 'blame'");
        if (!stop) return false;

        // The location, which is the whole reason a failed assertion is worth more than
        // a bare abort: `blame_assert.fin:5` is where to look, and the line is a
        // compile-time constant so it costs an immediate rather than a lookup.
        llvm::Value* file = builder_.CreateGlobalString(sourceName_);
        llvm::Value* line = llvm::ConstantInt::get(i32Ty, node.loc.begin.line);

        llvm::Value* stream = builder_.CreateLoad(ptrTy, errStream, "stderr");
        if (message) {
            llvm::Value* format =
                builder_.CreateGlobalString("%s:%d: assertion failed: %s\n");
            builder_.CreateCall(report, {stream, format, file, line, message});
        } else {
            llvm::Value* format = builder_.CreateGlobalString("%s:%d: assertion failed\n");
            builder_.CreateCall(report, {stream, format, file, line});
        }
        builder_.CreateCall(stop, {});
        // `abort` does not return, and the block has to be terminated or the function is
        // invalid IR. `unreachable` rather than a branch to the surviving block, because
        // a branch would tell every later pass that execution continues past a failed
        // assertion.
        builder_.CreateUnreachable();
        return true;
    }

    // The C conversion one Fin value needs inside a `format!`, or null for a value
    // this cannot format.
    //
    // Read off the *promoted* value, because that is what reaches the callee:
    // promoteVararg widens a float to a double and anything narrower than an int to an
    // int, so `%g` and `%d` are the conversions those two become and not the ones their
    // Fin types would suggest.
    //
    // `%lld` and not `%ld` for a 64-bit integer. A C `long` is 32 bits on Windows and
    // 64 on Linux, and a `long long` is 64 everywhere it exists -- and this string is
    // the compiler's own rather than a program's, so it does not get to be as loose as
    // the corpus's `printf("%ld", n)` (Layout.hpp records that spelling as the reason
    // `long` is i64 in the first place).
    //
    // `%g` and not `%f` for a float, because `{}` asks for the value and `%f` asks for
    // six decimal places: `format!("{}", 1.5)` reads "1.5" and not "1.500000". Which is
    // a choice about what `{}` *means* and not about what a double is -- the corpus
    // writes `printf("%.2f", x)` when it wants a width, and `{}` has no syntax for one.
    //
    // A `char` prints as a number. It shares its representation with `int8` exactly --
    // one 8-bit signed integer, from one row of Layout.cpp's table -- so this cannot
    // tell them apart, and `%c` for both would print a byte for a small number. No
    // corpus site formats a char, so the reading that never invents a character wins.
    //
    // A pointer with a pointee is an address and prints as one. A pointer *without* one
    // is a `string` or a bare `null` -- the only two pointee-less pointers this file
    // builds (byName's Pointer scalar and visit(Literal&)'s KW_NULL) -- so `%s` reads
    // the bytes, which is what `printf("%s", s)` already does with the same value. A
    // `null` there prints "(null)" on glibc; it is not a shape the analyzer can refuse,
    // since the variadic tail is unchecked by design, and it is not a shape any corpus
    // site writes.
    static const char* conversionFor(const CgType& t) {
        switch (t.kind) {
            case CgType::Kind::Int:
                if (t.bits < 32) return "%d";      // promoted to an int
                if (t.bits == 32) return t.isSigned ? "%d" : "%u";
                if (t.bits == 64) return t.isSigned ? "%lld" : "%llu";
                return nullptr;
            case CgType::Kind::Float:
                return "%g";
            case CgType::Kind::Ptr:
                return t.pointee ? "%p" : "%s";
            default:
                return nullptr;
        }
    }

    // What to call a value `format!` cannot format, for the refusal that names it.
    //
    // A phrase and not a type spelling: a CgType has no name -- the written type is a
    // TypeNode somewhere upstream of the value, and an argument's may be an expression
    // with no written type at all -- and the kind is what the refusal turns on.
    static const char* unformattableKind(const CgType& t) {
        if (t.isInterface) return "an interface reference";
        if (t.isStruct()) return "a struct";
        if (t.isArray()) return "an array";
        if (t.isPrototype()) return "a prototype";
        if (t.isFn()) return "a function value";
        return "a value of no type";
    }

    // `format!("{} and {}", a, b)` -- the one macro the compiler implements (ADR 0023
    // step 7), and the only expression in this file whose shape a compile-time string
    // decides.
    //
    // Lowered as the C idiom for building a string whose length nobody knows until the
    // values are formatted: `snprintf` into no buffer to measure, `malloc` that many
    // bytes plus the NUL, `snprintf` again to fill it. The arguments are emitted once
    // and handed to both calls -- emitting them twice would run the `next()` in
    // `format!("{}", next())` twice, which is a wrong program rather than a slow one.
    //
    // `snprintf` and `malloc` are declared on demand through runtimeFn, on the footing
    // the `fprintf` and `abort` of a `blame` and the `malloc` of a dynamic array are
    // already on: a libc entry point this file needs, shared with a Fin program that
    // declared it itself rather than colliding with it.
    //
    // Nobody frees the buffer, and that is not a decision available here. `string` is
    // `i8*` with no length and no owner -- ADR 0003 leaves the representation to the
    // library -- and the corpus writes `return format!(...)` out of a method
    // (deeptest2.fin:63), so a stack buffer would be dangling before the caller read
    // it. Freeing it needs the tracing collector ADR 0003 commits to, which is a wave
    // of its own and not this step.
    //
    // The conversion per argument comes from the type the *backend* emitted. ADR 0023
    // says "the type the analyzer recorded" and there is no separate record to read:
    // the analyzer's answer reaches this file the way every other type does, through
    // the value, and a second copy attached to the node could only ever disagree with
    // the CgType that selects the instruction.
    void emitFormatMacro(MacroInvocation& node) {
        if (!currentFn_) { unsupported(node, "a 'format!' outside a function"); return; }
        if (node.args.empty()) {
            // The analyzer reported this already -- "expects at least 1 argument, got
            // 0" -- so reaching it is the two passes disagreeing and not a program's
            // mistake arriving unreported.
            unsupported(node, "a 'format!' with no format string");
            return;
        }

        // The format has to be a literal *here*, and this is the one thing the language
        // allows that this lowering does not do.
        //
        // `{}` is type-directed -- which conversion it becomes depends on the value
        // beside it -- so translating it needs the text at compile time.
        // `tests/samples/stdlib/stdio.fin:36` writes `format!(fmt, ...objects)` with
        // `fmt` a runtime parameter, and that call is the reason `format!` is a builtin
        // at all rather than a macro pasting a literal into a template (ADR 0023). It
        // still does not lower: the translation would have to run at run time, over a
        // format the compiler never sees, with the conversions carried into that loop as
        // data -- which is a runtime routine and not a format string, and no corpus site
        // reaches codegen needing it. Refused by name rather than mislowered, because
        // the alternative is handing `{}` to snprintf as literal text and printing the
        // placeholder.
        auto* literal = dynamic_cast<Literal*>(node.args[0].get());
        if (!literal || literal->kind != ASTTokenKind::STRING_LITERAL) {
            unsupported(node, "a 'format!' whose format string is not a literal");
            return;
        }

        // The values, emitted in written order before the format is translated: each
        // `{}` becomes the conversion its own value's type asks for, so there is nothing
        // to translate until the values exist.
        std::vector<llvm::Value*> values;
        std::vector<const char*> conversions;
        for (size_t i = 1; i < node.args.size(); ++i) {
            if (!node.args[i]) { unsupported(node, "a 'format!' argument with no value"); return; }
            CgVal v = emit(*node.args[i]);
            if (failed_) return;
            if (!v.ok()) { unsupported(node, "this 'format!' argument"); return; }
            const char* conversion = conversionFor(v.type);
            if (!conversion) {
                unsupported(node, fmt::format("{} formatted by 'format!'",
                                              unformattableKind(v.type)));
                return;
            }
            llvm::Value* promoted = promoteVararg(node, v);
            if (!promoted) return;
            values.push_back(promoted);
            conversions.push_back(conversion);
        }

        // The C format string, built here and never at run time. Three things in one
        // pass: `{}` becomes the conversion its argument needs, a `%` the program wrote
        // becomes `%%`, and everything else is copied.
        //
        // The `%%` is not a nicety. A Fin format string is text -- `format!("100% of
        // {}", n)` is a program somebody will write -- and text handed to snprintf as a
        // format is a vararg read the caller never made. The same reasoning that sends
        // a `blame` message through `%s` instead of making it the format.
        std::string cFormat;
        size_t placeholders = 0;
        const std::string text = decodeLiteral(literal->value);
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '%') { cFormat += "%%"; continue; }
            if (text[i] != '{') { cFormat += text[i]; continue; }
            // `{` opens a placeholder and opens nothing else. `{0}`, `{name}` and
            // `{:>8}` are all forms `format!` could grow and none of them is written
            // anywhere in the corpus, so each is refused rather than copied through: a
            // program that printed `{0}` literally would be this file guessing that the
            // author meant text.
            if (i + 1 >= text.size() || text[i + 1] != '}') {
                unsupported(node, "a 'format!' placeholder that is not '{}'");
                return;
            }
            ++i;
            if (placeholders < conversions.size()) cFormat += conversions[placeholders];
            ++placeholders;
        }

        // Counted against each other rather than trusted, and counted here because here
        // is where both numbers exist. The analyzer checks `format!`'s fixed parameter
        // and leaves the variadic tail alone by design, and the tail is what these
        // placeholders consume -- so a missing value would be a vararg read that was
        // never pushed, and a spare one would be a value the program formatted into
        // nothing.
        if (placeholders != conversions.size()) {
            unsupported(node, fmt::format("a 'format!' with {} '{{}}' and {} value{}",
                                          placeholders, conversions.size(),
                                          conversions.size() == 1 ? "" : "s"));
            return;
        }

        llvm::PointerType* ptrTy = llvm::PointerType::getUnqual(ctx_);
        llvm::Type* i32Ty = llvm::Type::getInt32Ty(ctx_);
        llvm::Type* i64Ty = llvm::Type::getInt64Ty(ctx_);
        llvm::FunctionCallee write = runtimeFn(
            node, "snprintf",
            llvm::FunctionType::get(i32Ty, {ptrTy, i64Ty, ptrTy}, /*isVarArg=*/true),
            "a 'format!'");
        if (!write) return;
        llvm::FunctionCallee alloc = runtimeFn(
            node, "malloc", llvm::FunctionType::get(ptrTy, {i64Ty}, false), "a 'format!'");
        if (!alloc) return;

        llvm::Value* format = builder_.CreateGlobalString(cFormat);

        // `snprintf(null, 0, ...)` returns the length it *would* have written, which is
        // the measurement this idiom rests on: it is defined to write nothing when the
        // size is zero, and defined to return the full length rather than the truncated
        // one.
        std::vector<llvm::Value*> measure{llvm::ConstantPointerNull::get(ptrTy),
                                          llvm::ConstantInt::get(i64Ty, 0), format};
        measure.insert(measure.end(), values.begin(), values.end());
        llvm::Value* length = builder_.CreateCall(write, measure, "format.len");

        // A negative return is an encoding error, and clamping it to zero costs two
        // instructions and buys the invariant that matters: the buffer is at least one
        // byte and the second call NUL-terminates it. Without the clamp a negative
        // length would allocate nothing and leave an unterminated pointer typed
        // `string`, which every reader downstream would walk off the end of.
        llvm::Value* zero = llvm::ConstantInt::get(i32Ty, 0);
        llvm::Value* written = builder_.CreateSelect(
            builder_.CreateICmpSLT(length, zero), zero, length, "format.written");
        llvm::Value* size = builder_.CreateAdd(builder_.CreateSExt(written, i64Ty),
                                               llvm::ConstantInt::get(i64Ty, 1),
                                               "format.size");
        llvm::Value* buffer = builder_.CreateCall(alloc, {size}, "format.buf");

        // Not checked against null, exactly as the `malloc` of a `new` and of a dynamic
        // array are not: a failed allocation is a runtime story this language has not
        // told yet, and inventing a check here would be one of three sites behaving
        // differently.
        std::vector<llvm::Value*> fill{buffer, size, format};
        fill.insert(fill.end(), values.begin(), values.end());
        builder_.CreateCall(write, fill);

        value_ = CgVal{buffer, *types_.byName("string")};
    }

    // `p.get()`, and `q.get()` where q is a `&Point`.
    void visit(MethodCall& node) override {
        // A call the analyzer resolved to a free call: `stdio.printf("Big")`
        // (complex.fin:14), whose qualifier is a module. The front end checked it
        // against the module member's signature and left the call it resolves to on the
        // node, so what is lowered here is a plain call on a name this program declares
        // -- which is the only shape this file has ever handled. Nothing below runs, and
        // in particular `baseAddress` is not asked for the address of a module.
        //
        // Delegated rather than re-implemented, so a rewritten call goes through exactly
        // the argument conversion, vararg promotion and template selection a written one
        // does; a second copy of visit(FunctionCall&)'s dispatch here would be free to
        // drift from it.
        if (node.resolved_call) {
            node.resolved_call->accept(*this);
            return;
        }
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
        //
        // Taken once and asked about twice. A prototype's methods are answered by this
        // file rather than found in a `methods` table, and a prototype is not a struct,
        // so the address has two questions to answer -- but only one emission, because
        // taking the address of `p[i++]` runs the increment.
        auto direct = emitAddress(*node.object);
        if (failed_) return;
        if (auto proto = baseOf(direct, CgType::Kind::Prototype)) {
            if (emitPrototypeMethod(node, &*proto, nullptr)) return;
        }
        // A prototype with no home -- `mk().get(1)`, whose receiver is a returned value.
        // Emitting it is safe here and only here: `direct` is empty, so nothing of the
        // receiver has been emitted yet, and the name is checked first so an ordinary
        // method call does not evaluate its receiver twice on the way to the struct path.
        //
        // Reading one is honest where a struct's method is not (see the refusal below):
        // `get` and `contains` do not write, so a copy answers exactly what the original
        // would. `remove` does write, and refuses inside emitPrototypeMethod rather than
        // editing a table nobody can name.
        if (!direct && isPrototypeMethodName(node.method_name)) {
            CgVal object = emit(*node.object);
            if (failed_) return;
            if (object.ok() && object.type.isPrototype() &&
                emitPrototypeMethod(node, nullptr, &object)) {
                return;
            }
        }
        auto receiver = baseOf(direct, CgType::Kind::Struct);
        if (failed_) return;
        if (receiver && receiver->type.isInterface && receiver->type.interfaceInfo) {
            auto object = builder_.CreateLoad(receiver->type.llvmType, receiver->ptr, "interface");
            emitInterfaceMethodCall(node, CgVal{object, receiver->type}, *receiver->type.interfaceInfo);
            return;
        }
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
            // A generic method with a body is not missing, it is uninstantiated: nothing
            // was declared for it because a template has no signature until a call says
            // what its parameters are, and this is that call. A generic method with *no*
            // body still refuses below -- there would be nothing to emit.
            const FunctionDeclaration* tmpl = findMethod(owner, node.method_name);
            if (tmpl && !tmpl->generic_params.empty() && tmpl->body) {
                emitGenericMethodCall(node, owner, *receiver, *tmpl);
                return;
            }
            // `d.get_a()` where `get_a` is the *base*'s. The base's fields splice into
            // this struct at offset 0 (declareStructs' second pass), so the receiver
            // already is a valid pointer to the base and the method the source named is
            // the base's method: it is called, not re-emitted. Which base is decided by
            // methodProvider rather than here.
            const StructInfo* provider = methodProvider(node, owner, node.method_name);
            if (failed_) return;
            if (provider && provider != &owner) {
                emitInheritedMethodCall(node, owner, *provider, *receiver);
                return;
            }
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

    // The names a prototype answers for, ADR 0028's initial API and the analyzer's list
    // (SemanticAnalyzer::checkPrototypeMethod). Asked before a receiver is emitted, so
    // it must not depend on the receiver.
    static bool isPrototypeMethodName(const std::string& name) {
        return name == "get" || name == "try_get" || name == "contains" ||
               name == "remove" || name == "rm";
    }

    // `p.get(k)`, `p.contains(k)`, `p.remove(k)` / `p.rm(k)` on a prototype.
    //
    // The analyzer has already decided that the name is one of these and that the
    // argument is the key's type (checkPrototypeMethod), so what is left here is which
    // primitive to reach for.
    //
    // Exactly one of `base` and `value` is given. An address is what `remove` needs --
    // it writes a shorter length back -- and the reading methods take either, because a
    // search reads the same answer out of a copy as out of the original.
    //
    // Returns false when the name is not a prototype's, so a struct that declares its
    // own `get` -- lib/std/collection.fin does -- falls through untouched.
    bool emitPrototypeMethod(MethodCall& node, const Addr* base, const CgVal* value) {
        const std::string& name = node.method_name;
        if (!isPrototypeMethodName(name)) return false;
        const bool mutates = name == "remove" || name == "rm";
        if (mutates && !base) {
            unsupported(node, fmt::format("a call to '{}' on a prototype with no address",
                                          name));
            return true;
        }
        if (node.args.size() != 1 || !node.args[0]) {
            unsupported(node, fmt::format("a call to '{}' on a prototype without a key",
                                          name));
            return true;
        }
        CgVal key = emit(*node.args[0]);
        if (failed_ || !key.ok()) return true;

        if (mutates) {
            value_ = emitPrototypeRemove(node, *base, key);
            return true;
        }
        if (name == "try_get") {
            // `V?`, and a nullable value is not lowered by this slice at all -- there is
            // no representation for "a V or nothing" yet, and inventing one here would be
            // choosing the option representation in the backend, ahead of the stdlib
            // boundary ADR 0028 puts it behind. Refused rather than answered with the
            // `get` lowering, which would blame on a key the program asked about safely.
            unsupported(node, "a call to 'try_get' on a prototype, whose optional result "
                              "has no representation yet");
            return true;
        }
        CgVal object = value ? *value
                             : CgVal{builder_.CreateLoad(base->type.llvmType, base->ptr,
                                                          "prototype"), base->type};
        if (name == "contains") {
            value_ = emitPrototypeContains(node, object, key);
            return true;
        }
        value_ = emitPrototypeLookup(node, object, key, "a prototype 'get'");
        return true;
    }

    // `d.get_a()` where `get_a` is declared on `d`'s base -- deeptest2.fin:67-79 and
    // love.fin's two structs, whose `name` comes from `Person`.
    //
    // The base's function is called with the derived pointer unchanged. Nothing is
    // emitted for the upcast because there is nothing to emit: LLVM has one `ptr`, and
    // the base's fields are at the offsets the base's own methods GEP at -- which is
    // checked and not assumed (baseSharesLayout), because for a second base it is false.
    //
    // Not a re-declaration of the method under the derived struct's name. One body per
    // written method is what keeps `self` meaning one type inside it: emitting a copy
    // bound to the derived struct would be an instantiation, and a method is not a
    // template.
    void emitInheritedMethodCall(MethodCall& node, const StructInfo& derived,
                                 const StructInfo& base, const Addr& receiver) {
        if (!baseSharesLayout(derived, base)) {
            // A second base, whose fields begin after the first's. Its methods index
            // from zero and would read the first base's fields instead -- the wrong
            // field, silently, which is the one outcome worth a refusal here. What the
            // derived object should look like when it has two bases with bytes is the
            // owner's ruling (deeptest2.fin:83 writes `MultiInherit: <Person, Student>`
            // and the sample's own comment says the behaviour is unsettled).
            unsupported(node, fmt::format("a call to the method '{}' inherited from '{}', "
                                          "whose fields are not at the offsets they have "
                                          "in '{}'",
                                          node.method_name, base.finName, derived.finName));
            return;
        }
        auto found = functions_.find(methodKey(base.finName, node.method_name));
        if (found == functions_.end()) {
            // methodProvider answered with this struct because functions_ has the key,
            // so losing it here is this file disagreeing with itself.
            reportMissingMethod(node, base, node.method_name);
            return;
        }
        const FnInfo& info = found->second;
        if (!info.hasReceiver) {
            // A static method of the base, reached through a derived value. Same
            // refusal the struct's own static method gets one branch up, and for the
            // same reason: there is no `self` to be given and dropping the receiver the
            // source wrote would be a silent reinterpretation.
            unsupported(node, fmt::format("a call to the static method '{}' inherited "
                                          "from '{}' through a value",
                                          node.method_name, base.finName));
            return;
        }
        std::vector<llvm::Value*> args{receiver.ptr};
        if (!emitCallArgs(node, info, node.method_name, argList(node.args), args)) return;
        emitCall(info, args);
    }

    // `b.set_x(5)` where `set_x` is `fun set_x<U>(new_x: U)` -- struct_methods.fin:14.
    void emitGenericMethodCall(MethodCall& node, const StructInfo& owner,
                               const Addr& receiver, const FunctionDeclaration& tmpl) {
        if (tmpl.is_static) {
            // A generic static method has no receiver to fix the struct's half of the
            // substitution, and it is reached through visit(StaticMethodCall&) rather
            // than here -- so arriving here at all is a spelling the analyzer let
            // through and this path cannot serve.
            unsupported(node, fmt::format("a call to the generic static method '{}' on "
                                          "struct '{}' through a value",
                                          tmpl.name, owner.finName));
            return;
        }
        // The arguments, emitted before the instance exists, because the parameter's
        // type is what is being inferred *from* them. So an argument that cannot be
        // typed on its own -- an array literal written at a call site -- refuses here
        // rather than being offered a type, which is the same bargain emitTemplateCall
        // strikes and for the same reason.
        std::vector<CgVal> values;
        values.reserve(node.args.size());
        for (auto& arg : node.args) {
            CgVal a = emit(*arg);
            if (failed_) return;
            if (!a.ok()) { unsupported(node, "this argument"); return; }
            values.push_back(a);
        }

        const CgType param = types_.pointerTo(receiver.type);
        const std::string key = instantiateGenericMethod(
            node, owner, param, const_cast<FunctionDeclaration&>(tmpl), tmpl.name,
            tmpl.generic_params, tmpl.params, *tmpl.body, values);
        if (key.empty()) return;  // already reported
        emitInstanceCall(node, key, receiver.ptr, values);
    }

    // `Point::make(1, 2)` (struct_methods.fin:8), and `Box::<int>::zero()` where the
    // type arguments are on the *type* and not on the method.
    void visit(StaticMethodCall& node) override {
        if (!node.generic_args.empty()) {
            unsupported(node, fmt::format("a '::' call to '{}' with explicit generic "
                                          "arguments", node.method_name));
            return;
        }
        // The target with its type arguments, which for `Vec2::from_angle(0.7854)`
        // (letssee.fin:59) is not the target the source wrote: the analyzer inferred
        // `Vec2<float>` and recorded it, because the annotation on the left of that line
        // is what says which Vec2 it is and an annotation is not a thing this pass has.
        // Written where a `::` call on a non-generic struct, on `Self`, or on a template
        // that already spells its arguments (`Box::<int>::zero()`) leaves it null, so
        // those go through the same map() of the same node they always did.
        const TypeNode* target_type = node.resolved_target ? node.resolved_target.get()
                                                           : node.target_type.get();
        // Through the mapper, so `Box::<int>::zero()` instantiates `Box<int>` on the way
        // -- including its methods, which is what puts `Box<int>.zero` in functions_ for
        // the lookup below to find. A bare `Box` written inside `Box<T>`'s own method
        // reaches its own instantiation through the same call, by the binding.
        auto target = types_.map(target_type);
        if (!target) {
            if (failed_) return;
            // A template with no arguments and nothing that resolved them -- `Vec2::make(
            // 1, 2)` on a `struct Vec2<T>` where neither an argument nor an annotation
            // says what T is (letssee.fin:26). The mapper cannot map it because there is
            // nothing to lay out until T is known. Named specifically because "of type
            // 'Vec2'" on its own reads as an unknown type rather than as a template
            // missing its arguments.
            if (target_type && templates_.count(target_type->name) &&
                target_type->generics.empty()) {
                unsupported(node, fmt::format("a '::' call to '{}' on the generic struct "
                                              "'{}' with no type arguments",
                                              node.method_name, target_type->name));
                return;
            }
            unsupportedType(node, target_type, "a '::' call on a target");
            return;
        }
        if (!target->isStruct() || !target->structInfo) {
            // An enum, or a scalar. `Colour::Red` is a member access and not this, and
            // a `::` call on anything but a struct is a shape the corpus does not have.
            unsupported(node, fmt::format("a '::' call to '{}' on type '{}'",
                                          node.method_name, typeName(target_type)));
            return;
        }
        const StructInfo& owner = *target->structInfo;

        auto found = functions_.find(methodKey(owner.finName, node.method_name));
        if (found == functions_.end()) {
            // `Derived::make()` where `make` is the base's static. Inherited on the same
            // terms as an instance method, and with no layout question to ask: a static
            // method takes no receiver, so there is no pointer being reinterpreted and
            // nothing for baseSharesLayout to decide. The refusal for an *instance*
            // method reached this way is still below and still applies.
            const StructInfo* provider = methodProvider(node, owner, node.method_name);
            if (failed_) return;
            if (provider && provider != &owner)
                found = functions_.find(methodKey(provider->finName, node.method_name));
        }
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
            // The address is kept rather than recomputed. It used to be asked for
            // twice -- once to learn the type and again, inside the dynamic branch, to
            // load from -- and the second ask is the one that fails for an array with
            // no home: `give().length` on a function returning `[int]`, or
            // `mk().xs.length` on a field of a returned struct. `baseAddress` answers
            // nullopt there, which is a normal answer and not a refusal, so the branch
            // returned with `value_` unset and the caller reported whatever it was
            // doing -- `this conversion is not lowered yet` at the top of the file, or
            // `an array passed to a C variadic`, neither about the length.
            std::optional<Addr> arrayAddr = baseAddress(*node.object, CgType::Kind::Array);
            if (arrayAddr) arrayType = arrayAddr->type;
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
                if (arrayType->isDynamicArray) {
                    // The length word out of the pair: loaded through the address when
                    // there is one, extracted from the value when there is not. A
                    // temporary has no address to borrow and needs none -- the pair is
                    // already in a register, and `extractvalue` reads a field of it.
                    llvm::Value* pair = nullptr;
                    if (arrayAddr) {
                        pair = builder_.CreateLoad(arrayType->llvmType, arrayAddr->ptr);
                    } else if (objectValue && objectValue->ok() &&
                               objectValue->type.isDynamicArray) {
                        pair = objectValue->value;
                    } else if (objectValue && objectValue->ok()) {
                        // A pointer to a `[T]`: the pointer is the address.
                        pair = builder_.CreateLoad(arrayType->llvmType, objectValue->value);
                    }
                    if (!pair) {
                        unsupported(node, "the length of an array with no representation here");
                        return;
                    }
                    value_ = CgVal{builder_.CreateExtractValue(pair, {1}, "length"), i32};
                } else {
                    value_ = CgVal{llvm::ConstantInt::get(i32.llvmType, arrayType->extent, true),
                                   i32};
                }
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
        CgVal object = objectValue ? *objectValue : emit(*node.object);
        if (!object.ok()) { unsupported(node, "this member's object"); return; }
        if (object.type.isInterface && object.type.interfaceInfo) {
            size_t slot = 0;
            for (; slot < object.type.interfaceInfo->fields.size(); ++slot)
                if (object.type.interfaceInfo->fields[slot].name == node.member) break;
            if (slot == object.type.interfaceInfo->fields.size()) {
                unsupported(node, fmt::format("the member '{}' of interface '{}', which has no such field",
                                              node.member, object.type.interfaceName));
                return;
            }
            auto* data = builder_.CreateExtractValue(object.value, {0}, "data");
            auto* table = builder_.CreateExtractValue(object.value, {1}, "vtable");
            auto* offsetPtr = builder_.CreateInBoundsGEP(llvm::PointerType::get(ctx_, 0), table,
                                                         llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx_), slot));
            auto* offset = builder_.CreateLoad(llvm::PointerType::get(ctx_, 0), offsetPtr, "field.offset");
            auto* bytePtr = builder_.CreateInBoundsGEP(llvm::Type::getInt8Ty(ctx_), data,
                                                       builder_.CreatePtrToInt(offset, llvm::Type::getInt64Ty(ctx_)));
            auto& field = object.type.interfaceInfo->fields[slot];
            auto* typed = builder_.CreateBitCast(bytePtr, field.type.llvmType->getPointerTo());
            value_ = CgVal{builder_.CreateLoad(field.type.llvmType, typed, node.member), field.type};
            return;
        }

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
        // A prototype that is a value and not a variable -- `keys(mk().0)` on a
        // function returning one. `extractvalue` reads the half out of the register,
        // which is what the struct path four lines down does for the same shape; the
        // half is a `{ptr, len}` pair and a pair is a value, so nothing is copied that
        // an assignment of it would not copy anyway.
        if (object.type.isPrototype()) {
            size_t position = 0;
            if (positionalMember(node.member, position) && position < 2 &&
                object.type.keys && object.type.values) {
                const CgType& half = position == 0 ? *object.type.keys
                                                   : *object.type.values;
                value_ = CgVal{builder_.CreateExtractValue(object.value,
                                                           {(unsigned)position},
                                                           position == 0 ? "keys"
                                                                         : "values"),
                               half};
                return;
            }
            unsupported(node, fmt::format("the member '{}' of a prototype", node.member));
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
    // A dynamic `[T]` built from a list of element expressions: a fresh heap buffer,
    // the elements stored into it in order, and the `{ptr, len}` pair as a value.
    //
    // Extracted from visit(ArrayLiteral&)'s dynamic branch so that a prototype literal
    // builds its two halves through the same code an `[T]` literal does. Two copies of
    // this would be two allocation rules and two pair layouts that agree today: the
    // pair is ADR 0025's, and the whole point of the ADR is that there is one of it.
    //
    // Takes borrowed pointers rather than the AST vector, because a prototype's keys
    // are not a vector -- they are the first of each pair in `elements`, and building a
    // vector of `unique_ptr` copies to pass them is not a thing a unique_ptr can do.
    //
    // Returns null having already reported, or having had `failed_` set by an element.
    llvm::Value* buildDynamicArray(ASTNode& node, const CgType& type,
                                   const std::vector<Expression*>& elements) {
        if (!type.element || !type.element->llvmType) {
            unsupported(node, "an array with no element type here");
            return nullptr;
        }
        llvm::Value* count = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx_),
                                                    elements.size());
        auto* elemTy = type.element->llvmType;
        auto* bytes = llvm::ConstantExpr::getSizeOf(elemTy);
        llvm::Value* byteCount = builder_.CreateMul(
            bytes, builder_.CreateZExt(count, llvm::Type::getInt64Ty(ctx_)));
        llvm::FunctionCallee mallocFn = runtimeFn(node, "malloc", llvm::FunctionType::get(
            llvm::PointerType::get(ctx_, 0), {llvm::Type::getInt64Ty(ctx_)}, false),
            "a dynamic array allocation");
        if (!mallocFn) return nullptr;
        llvm::Value* raw = builder_.CreateCall(mallocFn, {builder_.CreateZExt(byteCount, llvm::Type::getInt64Ty(ctx_))});
        llvm::Value* data = builder_.CreateBitCast(raw, elemTy->getPointerTo());
        for (size_t i = 0; i < elements.size(); ++i) {
            if (!elements[i]) { unsupported(node, "an array element with no value"); return nullptr; }
            // Each element gets the *element's* type as its own hint, which is what
            // visit(ArrayLiteral&)'s fixed branch does and what makes a nested literal
            // lower: an `[[int]]`'s elements are array literals, and a
            // `prototype<int, [int]>`'s values are too. Set from the element's kind
            // rather than left null, because a literal reaching here with no hint has
            // nothing to be an array *of* and would refuse -- which is what
            // `{ 1: [1, 2] }` did before this line, with a diagnostic about an array
            // literal in a program whose author wrote a prototype.
            auto* savedArray = arrayHint_;
            auto* savedProto = prototypeHint_;
            arrayHint_ = type.element->isArray() ? type.element.get() : nullptr;
            prototypeHint_ = type.element->isPrototype() ? type.element.get() : nullptr;
            CgVal v = emit(*elements[i]);
            arrayHint_ = savedArray;
            prototypeHint_ = savedProto;
            if (failed_ || !v.ok()) return nullptr;
            llvm::Value* stored = convert(node, v, *type.element);
            if (!stored) return nullptr;
            auto* slot = builder_.CreateInBoundsGEP(elemTy, data,
                                                     llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx_), i));
            builder_.CreateStore(stored, slot);
        }
        llvm::Value* pair = llvm::UndefValue::get(type.llvmType);
        pair = builder_.CreateInsertValue(pair, data, {0});
        pair = builder_.CreateInsertValue(pair, count, {1});
        return pair;
    }

    // `{ 10: 1.5, 20: 2.5 }` becomes the two arrays side by side: every key into the
    // first, every value into the second, in written order and index by index. That
    // pairing *is* the data structure -- key i belongs to value i -- so the written
    // order is not an author's convenience the way a struct literal's is, and nothing
    // here may reorder it.
    //
    // The type comes from the hint the declaration set, for exactly the reason an array
    // literal's does: `{ 10: 1.5 }` is not `prototype<int, float>` by inspection. The
    // front end may have typed those constants against an annotation this file cannot
    // see -- `<{long, double}>` accepts the same text -- and reading the key type off
    // the first key is how the two passes come to disagree about a stride. So a
    // literal with no hint is refused rather than guessed, which is what makes
    // `let p <auto> = { 10: 1.5 }` a refusal and not an invented prototype.
    void visit(PrototypeLiteral& node) override {
        if (!prototypeHint_ || !prototypeHint_->keys || !prototypeHint_->values) {
            unsupported(node, "a prototype literal with no declared type");
            return;
        }
        const CgType type = *prototypeHint_;

        std::vector<Expression*> keys;
        std::vector<Expression*> values;
        keys.reserve(node.elements.size());
        values.reserve(node.elements.size());
        for (auto& entry : node.elements) {
            keys.push_back(entry.first.get());
            values.push_back(entry.second.get());
        }

        // The keys' array first, then the values'. Two allocations, and the order
        // between them is observable only through what `malloc` returns, which no Fin
        // program may depend on -- but the *element* evaluations interleave nothing:
        // all the keys run, then all the values. That is a real decision and it is the
        // one a reader can predict from the code, the same argument
        // visit(StructInstantiation&) makes for running written values before defaults.
        llvm::Value* keyPair = buildDynamicArray(node, *type.keys, keys);
        if (!keyPair) return;
        llvm::Value* valuePair = buildDynamicArray(node, *type.values, values);
        if (!valuePair) return;

        llvm::Value* pair = llvm::UndefValue::get(type.llvmType);
        pair = builder_.CreateInsertValue(pair, keyPair, {0});
        pair = builder_.CreateInsertValue(pair, valuePair, {1});
        value_ = CgVal{pair, type};
    }
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
        if (type.isDynamicArray) {
            std::vector<Expression*> elements;
            elements.reserve(node.elements.size());
            for (auto& e : node.elements) elements.push_back(e.get());
            llvm::Value* pair = buildDynamicArray(node, type, elements);
            if (!pair) return;
            value_ = CgVal{pair, type};
            return;
        }
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
    // Are these two keys the same key? ADR 0028's structural equality, derived
    // recursively and explicitly rather than taken from raw memory.
    //
    // Raw bytes would be shorter and would be wrong: a struct's padding is undefined,
    // so two keys a program built from the same field values could compare unequal, and
    // a string compares as an address rather than as text -- which is the case
    // stdlib/memory.fin:32 writes, `info["MemoryCardModel"] = ...`, where the key is a
    // literal in one place and a literal in another and the two are not one pointer.
    //
    // What is derivable is listed here and nothing else is: integers and bools by
    // value, floats by IEEE equality, strings by their bytes, other pointers by
    // identity, structs field by field, and fixed arrays element by element. A dynamic
    // array key needs a run-time length loop, and `object` needs a run-time type, so
    // both are refused at compile time with a reason rather than answered wrongly.
    //
    // Returns null having already reported.
    llvm::Value* emitKeyEquality(ASTNode& node, const CgType& keyType,
                                 llvm::Value* a, llvm::Value* b) {
        switch (keyType.kind) {
            case CgType::Kind::Int:
                // Bools included: a bool is an i1 here, and `true == true` is the same
                // instruction `1 == 1` is.
                return builder_.CreateICmpEQ(a, b, "key.eq");
            case CgType::Kind::Float:
                // Ordered equality, so a NaN key never matches -- itself included. ADR
                // 0028 reserves the NaN and signed-zero ruling for the hashing trait;
                // until that lands this is the one behaviour that cannot silently claim
                // two different values are one.
                return builder_.CreateFCmpOEQ(a, b, "key.eq");
            case CgType::Kind::Ptr: {
                if (keyType.pointee) {
                    // A real pointer. Identity is the only equality a `&T` has here:
                    // comparing pointees would be a load through a key the program may
                    // have freed, and Fin has no ruling that two addresses holding equal
                    // values are one key.
                    return builder_.CreateICmpEQ(a, b, "key.eq");
                }
                // A string: the bytes, through libc's own comparison. Address equality
                // would make two spellings of the same key two keys.
                llvm::FunctionCallee cmp = runtimeFn(
                    node, "strcmp",
                    llvm::FunctionType::get(builder_.getInt32Ty(),
                                            {llvm::PointerType::getUnqual(ctx_),
                                             llvm::PointerType::getUnqual(ctx_)}, false),
                    "a string key comparison");
                if (!cmp) return nullptr;
                llvm::Value* diff = builder_.CreateCall(cmp, {a, b}, "key.strcmp");
                return builder_.CreateICmpEQ(diff, builder_.getInt32(0), "key.eq");
            }
            case CgType::Kind::Struct: {
                if (!keyType.structInfo) {
                    unsupported(node, "a struct key with no fields to compare");
                    return nullptr;
                }
                if (keyType.structInfo->fields.empty()) {
                    // A fieldless struct is one value, so every one of them is the same
                    // key. tests/samples/prototype_test.fin:37 keys a prototype on
                    // exactly that (`struct CustomDT {}`), and saying `true` here is the
                    // honest answer rather than a refusal: there is nothing to differ.
                    return builder_.getInt1(true);
                }
                llvm::Value* equal = builder_.getInt1(true);
                for (size_t i = 0; i < keyType.structInfo->fields.size(); ++i) {
                    const CgType& field = keyType.structInfo->fields[i].type;
                    llvm::Value* fa = builder_.CreateExtractValue(a, {(unsigned)i});
                    llvm::Value* fb = builder_.CreateExtractValue(b, {(unsigned)i});
                    llvm::Value* one = emitKeyEquality(node, field, fa, fb);
                    if (!one) return nullptr;
                    equal = builder_.CreateAnd(equal, one, "key.eq");
                }
                return equal;
            }
            case CgType::Kind::Array: {
                if (keyType.isDynamicArray || !keyType.element) {
                    // `[int]` as a key. Its length is a run-time value, so the
                    // comparison is a loop and not an expression -- and a loop here
                    // would have to be built in the caller's blocks, which is a unit of
                    // its own. prototype_test.fin:41 writes one and is booked as
                    // unimplemented; refused rather than compared by pointer, which
                    // would make two equal arrays two keys.
                    unsupported(node, "a dynamic array as a prototype key, whose "
                                      "structural equality needs a run-time loop");
                    return nullptr;
                }
                llvm::Value* equal = builder_.getInt1(true);
                for (uint64_t i = 0; i < keyType.extent; ++i) {
                    llvm::Value* ea = builder_.CreateExtractValue(a, {(unsigned)i});
                    llvm::Value* eb = builder_.CreateExtractValue(b, {(unsigned)i});
                    llvm::Value* one = emitKeyEquality(node, *keyType.element, ea, eb);
                    if (!one) return nullptr;
                    equal = builder_.CreateAnd(equal, one, "key.eq");
                }
                return equal;
            }
            default:
                unsupported(node, fmt::format("{} as a prototype key, which has no "
                                              "derived structural equality",
                                              describe(keyType)));
                return nullptr;
        }
    }

    // A prototype subscript is a key search, never an array offset. The current
    // representation stores parallel dynamic arrays, so this baseline scans them in
    // insertion order. The intrinsic boundary can later replace this body with a hash
    // table without changing the prototype type or its source syntax.
    // `p[key]` and `p.get(key)`: the value at a key, and a blame when there is none.
    //
    // ADR 0028 rules out a sentinel outright -- a generic V has no value that means
    // "absent" -- so the missing branch does not return, and the caller downstream is
    // reached only on the found path.
    CgVal emitPrototypeLookup(ASTNode& node, const CgVal& object, const CgVal& key,
                              const char* what) {
        const CgType& proto = object.type;
        if (!proto.values || !proto.values->element || !object.value) {
            unsupported(node, fmt::format("{} with an incomplete prototype "
                                          "representation", what));
            return CgVal{};
        }
        const CgType& valueType = *proto.values->element;
        auto scan = emitPrototypeScan(node, proto, object.value, key, what);
        if (!scan) return CgVal{};

        auto* fn = currentFn_->fn;
        auto* hit = llvm::BasicBlock::Create(ctx_, "prototype.lookup.hit", fn);
        auto* missing = llvm::BasicBlock::Create(ctx_, "prototype.lookup.missing", fn);
        builder_.CreateCondBr(scan->found, hit, missing);

        builder_.SetInsertPoint(missing);
        // The reason names the operation the program wrote, so the author reads their own
        // subscript back rather than a word about the table's internals.
        if (!emitRuntimeBlame(node, "this lookup because the key is not in the prototype",
                              "a missing prototype key"))
            return CgVal{};

        builder_.SetInsertPoint(hit);
        auto* vp = builder_.CreateInBoundsGEP(valueType.llvmType, scan->valuesData,
                                              scan->index, "prototype.value.ptr");
        return CgVal{builder_.CreateLoad(valueType.llvmType, vp, "prototype.value"),
                     valueType};
    }

    // `p.contains(key)` -- the scan's own answer, with nothing else to compute.
    CgVal emitPrototypeContains(ASTNode& node, const CgVal& object, const CgVal& key) {
        auto scan = emitPrototypeScan(node, object.type, object.value, key,
                                      "a prototype 'contains'");
        if (!scan) return CgVal{};
        return CgVal{scan->found, *types_.byName("bool")};
    }

    // `p.remove(key)` and `p.rm(key)` -- prototype_test.fin:24 writes the second and
    // calls it "the functional way".
    //
    // Removal closes the gap by shifting every later entry down one, in both arrays
    // together, and shortens the length. That is what insertion order costs: a swap with
    // the last entry would be one store instead of a loop and would reorder the table,
    // and ADR 0028 makes iteration order part of what a prototype *is*. The buffers keep
    // their allocation -- a shorter length is what "removed" means here, and shrinking
    // them would be a second realloc for no observable difference.
    //
    // Answers with whether anything was removed, so `if (p.remove(k))` is a question a
    // program can ask; a key that was not there is not an error.
    CgVal emitPrototypeRemove(ASTNode& node, const Addr& base, const CgVal& key) {
        const CgType& proto = base.type;
        if (!proto.keys || !proto.values || !proto.keys->element ||
            !proto.values->element) {
            unsupported(node, "a prototype removal with an incomplete representation");
            return CgVal{};
        }
        const CgType& keyType = *proto.keys->element;
        const CgType& valueType = *proto.values->element;
        llvm::Value* pair = builder_.CreateLoad(proto.llvmType, base.ptr, "prototype");
        auto scan = emitPrototypeScan(node, proto, pair, key, "a prototype removal");
        if (!scan) return CgVal{};

        auto* fn = currentFn_->fn;
        auto* shift = llvm::BasicBlock::Create(ctx_, "prototype.remove.shift", fn);
        auto* move  = llvm::BasicBlock::Create(ctx_, "prototype.remove.move", fn);
        auto* close = llvm::BasicBlock::Create(ctx_, "prototype.remove.close", fn);
        auto* done  = llvm::BasicBlock::Create(ctx_, "prototype.remove.done", fn);

        auto* cursor = builder_.CreateAlloca(builder_.getInt32Ty(), nullptr,
                                             "prototype.remove.at");
        builder_.CreateStore(scan->index, cursor);
        auto* last = builder_.CreateSub(scan->length, builder_.getInt32(1),
                                        "prototype.remove.last");
        builder_.CreateCondBr(scan->found, shift, done);

        builder_.SetInsertPoint(shift);
        auto* at = builder_.CreateLoad(builder_.getInt32Ty(), cursor, "prototype.remove.i");
        builder_.CreateCondBr(builder_.CreateICmpULT(at, last), move, close);

        builder_.SetInsertPoint(move);
        auto* from = builder_.CreateAdd(at, builder_.getInt32(1));
        auto* keyFrom = builder_.CreateInBoundsGEP(keyType.llvmType, scan->keysData, from);
        auto* keyTo = builder_.CreateInBoundsGEP(keyType.llvmType, scan->keysData, at);
        builder_.CreateStore(builder_.CreateLoad(keyType.llvmType, keyFrom), keyTo);
        auto* valueFrom = builder_.CreateInBoundsGEP(valueType.llvmType, scan->valuesData, from);
        auto* valueTo = builder_.CreateInBoundsGEP(valueType.llvmType, scan->valuesData, at);
        builder_.CreateStore(builder_.CreateLoad(valueType.llvmType, valueFrom), valueTo);
        builder_.CreateStore(from, cursor);
        builder_.CreateBr(shift);

        builder_.SetInsertPoint(close);
        builder_.CreateStore(prototypePair(proto, scan->keysData, scan->valuesData, last),
                             base.ptr);
        builder_.CreateBr(done);

        builder_.SetInsertPoint(done);
        return CgVal{scan->found, *types_.byName("bool")};
    }

    void visit(ArrayAccess& node) override {
        // Prototype indexing searches by key. Do this before the ordinary address
        // path, whose GEP semantics are correct for arrays but wrong for dictionaries.
        CgVal object = emit(*node.array);
        if (failed_) return;
        if (object.ok() && object.type.isPrototype()) {
            CgVal key = emit(*node.index);
            if (failed_) return;
            value_ = emitPrototypeLookup(node, object, key, "a prototype lookup");
            return;
        }
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
    // `new [T, n]{}` -- the only allocation whose result is not a pointer.
    //
    // Returns false having reported, per the convention in this file. Split out of
    // visit(NewExpression&) because the two share nothing: this one never maps the
    // written type, and everything it emits is about the pair.
    bool emitArrayAllocation(NewExpression& node, ArrayTypeNode& arrayType) {
        if (!node.init_fields.empty()) {
            // `new [int, 3]{x: 1}`. The braces in the corpus's spelling are always
            // empty -- `new [T, amount]{}` -- and a field name on an array is not a
            // thing the language has anywhere else.
            unsupported(node, "'new' of an array with field initialisers");
            return false;
        }
        if (!node.args.empty()) {
            unsupported(node, "'new' of an array with an initial value");
            return false;
        }
        if (!arrayType.element_type) {
            unsupported(node, "'new' of an array with no element type");
            return false;
        }
        if (!arrayType.size) {
            // `new [int]` -- no extent at all. The parser accepts it; there is no count
            // to allocate, and zero would be a guess rather than an answer.
            unsupported(node, "'new' of an array with no extent");
            return false;
        }

        auto element = types_.map(arrayType.element_type.get());
        if (!element || element->isVoid() || !element->llvmType ||
            !element->llvmType->isSized()) {
            unsupportedType(node, arrayType.element_type.get(), "'new' of an array of");
            return false;
        }

        // The pair the result *is*, taken from the mapper rather than built here, so
        // the field order and the length's width come from mapArray and are stated
        // once. `[T]` with no extent is what the analyzer typed this expression as.
        ArrayTypeNode dynamicNode(std::move(arrayType.element_type));
        auto pairType = types_.map(&dynamicNode);
        arrayType.element_type = std::move(dynamicNode.element_type);
        if (!pairType) {
            unsupportedType(node, &arrayType, "'new'");
            return false;
        }

        // The extent, as the expression it is. Any integer type: stdio.fin:112 and
        // :124 both allocate with a `ulong` count, which the analyzer accepts on
        // purpose (Soundness_ArrayExtent.AnAllocationsExtentMayBeAnyIntegerType).
        CgVal count = emit(*arrayType.size);
        if (failed_) return false;
        if (!count.ok() || count.type.kind != CgType::Kind::Int) {
            unsupported(node, "an array allocation whose extent is not an integer");
            return false;
        }
        auto* i64 = llvm::Type::getInt64Ty(ctx_);
        auto* i32 = llvm::Type::getInt32Ty(ctx_);
        // Widened for the byte arithmetic and narrowed for the length word, both from
        // the same value and both explicitly signed-or-not: a `ulong` count near the
        // top of its range sign-extends to a negative byte count otherwise, and
        // `malloc` is handed a number it will refuse for the wrong reason.
        llvm::Value* wide = builder_.CreateIntCast(count.value, i64, count.type.isSigned);
        llvm::Value* len = builder_.CreateIntCast(count.value, i32, count.type.isSigned);

        // getTypeAllocSize and not getSizeOf: the stride, including the padding an
        // element carries in an array, and the same table `sizeof` and every GEP in
        // this file read.
        const uint64_t stride = module_.getDataLayout().getTypeAllocSize(element->llvmType);
        llvm::Value* bytes = builder_.CreateMul(wide, builder_.getInt64(stride), "array_bytes");

        llvm::FunctionCallee alloc = runtimeFn(
            node, "malloc",
            llvm::FunctionType::get(llvm::PointerType::getUnqual(ctx_), {i64}, false),
            "an array allocation");
        if (!alloc) return false;
        llvm::Value* raw = builder_.CreateCall(alloc, {bytes}, "array_new");

        // Zeroed, which is the answer `new int*` gets three functions down and the
        // answer a local with no initialiser gets: undefined contents is the one
        // answer no test can pin. `{}` is written at every corpus allocation site and
        // it is the empty initialiser, so zero is also what it reads as.
        builder_.CreateMemSet(raw, builder_.getInt8(0), bytes, llvm::MaybeAlign(1));

        llvm::Value* pair = llvm::UndefValue::get(pairType->llvmType);
        pair = builder_.CreateInsertValue(pair, raw, {0});
        pair = builder_.CreateInsertValue(pair, len, {1});
        value_ = CgVal{pair, *pairType};
        return true;
    }

    void visit(NewExpression& node) override {
        if (!currentFn_) { unsupported(node, "'new' outside a function"); return; }

        // An array allocation before the general path, because it is not the general
        // path: every other `new` is a pointer to one of something, and this one is a
        // `[T]` -- a `{ptr, len}` pair, no extra indirection, which is what the
        // analyzer types it as (Analyzer_Expr.cpp, visit(NewExpression&)) and what the
        // corpus stores into a `[T]` field at stdlib/collection.fin:54.
        //
        // It has to be its own branch rather than a case of the code below for two
        // reasons that pull in opposite directions. `new [T, n]` with a run-time `n`
        // maps to nothing at all -- `mapArray` needs a constant extent to build an
        // `[N x T]` -- and `new [int, 3]` maps to *too much*: a fixed `[3 x i32]`,
        // making the result a `&[int, 3]`, which is not the `[int]` the analyzer said
        // and does not convert to one. So the extent is never read as a type here. It
        // is emitted as the expression it is, and only the element is mapped.
        if (auto* arrayType = dynamic_cast<ArrayTypeNode*>(node.type.get())) {
            if (!emitArrayAllocation(node, *arrayType)) return;
            return;
        }

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
                                    {llvm::Type::getInt64Ty(ctx_)}, false),
            "an allocation");
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
    // An anonymous function, emitted as a real llvm::Function and named by its pointer.
    //
    // A bare code pointer, which the corpus settles rather than a preference: all
    // thirteen lambdas in the samples read nothing but their own parameters. The one
    // that looks like an exception, `(msg: string) <void> => printf("Log: %s\n", msg)`
    // at lambdas.fin:58, reads a symbol and not a variable. So there is nothing for a
    // second word of a closure pair to hold, and adding one would put it in every `fn`
    // field, parameter and return in the language for no reader.
    //
    // A capture is refused rather than lowered, and that is what keeps this shape from
    // being a guess: `enclosingNames_` holds the names that were in scope where the
    // lambda was written, so a body reading one is refused *as a capture* instead of
    // silently reading a frame that is about to be gone. The day the corpus writes one
    // is the day the pair has to be designed, and nothing before then compiles wrong.
    //
    // `functions_.count(name)` cannot collide: the name is generated from a counter
    // that only ever goes up, and `fin.lambda.` is not a spelling a Fin program can
    // write -- the lexer has no `.` in an identifier.
    void visit(LambdaExpression& node) override {
        if (!currentFn_) {
            // A lambda at module scope has no enclosing body, so there is no answer to
            // what its captures would be and no insert point to resume to.
            unsupported(node, "a lambda outside a function");
            return;
        }
        if (!node.generic_params.empty()) {
            // A generic lambda reached as a *value*, which is the one thing it cannot be.
            // It is a template: `id<int>` and `id<double>` are two functions, a bare `id`
            // names neither, and a value has to be one address. So the only position that
            // lowers is the one that gives it a name to be instantiated under -- `let id
            // <auto> = fun <T>...`, handled in visit(VariableDeclaration&) before the
            // initialiser is ever emitted -- and everything else arrives here.
            //
            // The message names the position rather than the construct, because "a
            // generic lambda" would now be false: lambdas.fin's two are lowered, as
            // nothing, by the declaration path. What is refused is passing one, returning
            // one, or writing one where a value is wanted.
            unsupported(node, "a generic lambda used as a value");
            return;
        }
        if (!node.body && !node.expression_body) {
            unsupported(node, "a lambda with no body");
            return;
        }

        const std::string name = fmt::format("fin.lambda.{}", lambdas_++);
        // Declared exactly the way a top-level function is, so a lambda's parameters get
        // the same refusals a function's do -- an aggregate on an extern, a `void`
        // parameter, a return type with no representation. `isExtern` is false and there
        // is no receiver: a lambda is neither.
        declareFunction(node, name, name, node.params, node.return_type.get(),
                        /*isVarArg=*/false, /*isExtern=*/false);
        auto declared = functions_.find(name);
        if (declared == functions_.end()) return;  // declareFunction already reported
        // Internal, because nothing outside this object file can name it. Set here
        // rather than threaded through declareFunction as a flag, since this is the only
        // caller that wants it and the linkage is the only thing that differs.
        declared->second.fn->setLinkage(llvm::Function::InternalLinkage);

        // The names visible *here*, snapshotted before emitBodyOf's ScopedEmission moves
        // the scopes out of reach. Saved and restored around the body.
        //
        // Added to what is already there, so that a lambda inside a lambda counts the
        // outer lambda's parameters *and* the names the outer lambda was written among:
        // neither frame is reachable from a bare code pointer, so a read of either is the
        // same capture and says so, instead of the innermost one being reported as an
        // unknown name.
        std::vector<std::string> enclosing = enclosingNames_;
        for (const auto& scope : scopes_)
            for (const auto& entry : scope) enclosing.push_back(entry.first);
        enclosing.swap(enclosingNames_);
        // The nested functions visible here are callable from inside the lambda, for the
        // reason a capture is not: a nested function is a symbol and calling one needs no
        // frame. A lambda written beside `fun one()` and calling it is the same call the
        // enclosing body would emit.
        nestedCarry_ = visibleNested();
        // And the generic lambdas, by the same rule: a template is a node and a snapshot,
        // and instantiating one needs no frame, so `(n: int) <int> => id(n) + 1` written
        // beside `let id <auto> = fun <T>...` builds the same instance the enclosing body
        // would.
        lambdaCarry_ = visibleLambdas();
        emitBodyOf(node, node.params, node.body.get(), node.expression_body.get(), name);
        enclosing.swap(enclosingNames_);
        if (failed_) return;

        std::optional<CgType> type = fnValueType(declared->second);
        if (!type) {
            unsupported(node, "this lambda used as a value");
            return;
        }
        value_ = CgVal{declared->second.fn, *type};
    }
    void visit(SuperExpression& node) override { unsupported(node, "'super'"); }
    void visit(TypeLiteralExpression& node) override { unsupported(node, "a type literal"); }

    // A type node reached as an expression is a bug in whoever dispatched, not a
    // construct: TypeMapper is the only thing that should read one.
    void visit(TypeNode& node) override { unsupported(node, "a type in expression position"); }
    void visit(FunctionTypeNode& node) override { unsupported(node, "a function type in expression position"); }
    void visit(PointerTypeNode& node) override { unsupported(node, "a pointer type in expression position"); }
    void visit(ArrayTypeNode& node) override { unsupported(node, "an array type in expression position"); }

    // ---- state ------------------------------------------------------------

    DiagnosticEngine& diag_;
    bool debug_ = false;
    // The path the program was read from, for the one thing in emitted code that names
    // it: a failed `blame` prints `<file>:<line>: assertion failed`. Held as a string
    // rather than read from a node's location because a node's location has no filename
    // -- the lexer initialises every one with a null one (see CodeGen.hpp on why this is
    // a parameter of generateObject and not recoverable from anything else it gets).
    std::string sourceName_;
    // Two flags, because "the compile failed" and "the unit in hand cannot be
    // finished" are different facts and one bool was doing both jobs.
    //
    // `failed_` is the transient one and keeps the meaning every check in this file
    // already gives it: stop unwinding this construct, there is no value to carry on
    // with. It is what makes "refused, never skipped" hold -- nothing downstream of a
    // refusal is treated as though it lowered. It is cleared at a resume point, and
    // only there.
    //
    // `everFailed_` is sticky and is the one the driver reads. Once a refusal is
    // reported this stays set for the rest of the run, so no path can clear its way
    // back to a successful compile and no object is ever written for a program that
    // was refused.
    bool failed_ = false;
    bool everFailed_ = false;

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

    // What each module-scope `implements` block added, by the name of the struct it
    // names. Filled by collectImplementsBlocks before declareStructs and never written
    // again, which is what makes StructInfo::extras a pointer that stays valid: a
    // node-based map, so growing it moves no entry an instantiation is already pointing
    // at.
    std::unordered_map<std::string, StructExtras> implementsExtras_;
    // The blocks whose members a struct actually took, so the statement walk can refuse
    // one whose target this file never lowered -- an enum, an interface, a template
    // nobody instantiated -- instead of emitting nothing for it.
    std::set<const ImplementsBlock*> registeredBlocks_;

    // Every interface declared at module scope, by name.
    //
    // An interface has no representation here -- visit(InterfaceDeclaration&) emits
    // nothing at all -- so this is not a table of types. It exists for one question
    // that cannot be answered without it: a struct's `parents` vector holds base
    // structs and implemented interfaces *together* (parser.y puts `struct S : <I>`
    // and `struct S : <Base>` in the same place), and the two do opposite things to a
    // layout. A base struct's fields splice in at offset 0; an interface contributes
    // no bytes, and reserving a slot for one would move every field after it for
    // something with no run-time existence.
    //
    // src/types/Layout.cpp:427 asks the same question and answers it from
    // `StructType::is_interface`, which this file has no access to -- it works from
    // the AST, where the only witness is which kind of declaration carried the name.
    // Populated by declareInterfaces before declareStructs, for the same reason
    // declareEnums runs before it: a name has to be classified before a declaration
    // that mentions it is read.
    std::set<std::string> interfaceNames_;
    std::unordered_map<std::string, InterfaceInfo> interfaces_;
    std::unordered_map<std::string, llvm::GlobalVariable*> interfaceVtables_;

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
                    int optLevel, bool debugCodegen, const std::string& sourceName) {
    // The target comes first, before a single instruction is emitted, because the
    // module's DataLayout is an *input* to emission and not a stamp applied to the
    // result: `sizeof` folds to a number the layout decides, and a module laid out
    // after the fact would answer it from LLVM's default layout -- which agrees with
    // x86-64 by luck and with nothing else at all.
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    // A `llvm::Triple` and not the string it was parsed from: LLVM 21 moved
    // createTargetMachine and Module::setTargetTriple from a triple *string* to a
    // parsed triple, and ADR 0010 pins exactly one major, so the pinned major's
    // spelling is the only one that has to compile -- a version fork here would be
    // the "two compilers wearing one version number" the ADR exists to stop.  The
    // string is kept alongside it for the diagnostics, which name the triple in the
    // form a person recognises.
    const llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
    const std::string tripleName = triple.str();
    std::string lookupError;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, lookupError);
    if (!target) {
        diag.reportError("codegen: no LLVM target for " + tripleName, lookupError);
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
        diag.reportError("codegen: could not create a target machine for " + tripleName);
        return false;
    }

    Emitter emitter(diag, debugCodegen, sourceName);
    llvm::Module& module = emitter.module();
    module.setTargetTriple(triple);
    module.setDataLayout(machine->createDataLayout());
    // What a failed `blame` prints, and what a debugger reads to find the source. The
    // module's own name stays "fin": that is its identity, and the file it came from is
    // a different fact.
    module.setSourceFileName(sourceName);

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
