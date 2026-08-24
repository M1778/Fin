#include "CodeGen.hpp"

#include "../ast/ASTNode.hpp"   // the master AST include
#include "../ast/Visitor.hpp"
#include "../diagnostics/DiagnosticEngine.hpp"
#include "../types/Layout.hpp"

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
    enum class Kind { Void, Int, Float, Ptr, Struct };
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

    bool isVoid() const { return kind == Kind::Void; }
    bool isStruct() const { return kind == Kind::Struct; }
};

// One field, in declaration order. The order is the whole point: the layout pass
// and LLVM both index by position, and the *written* order of a struct literal's
// initialisers is the author's convenience and nothing else
// (Soundness_Codegen.AStructLiteralFollowsDeclarationOrderNotWrittenOrder).
struct StructField {
    std::string name;
    CgType type;
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

struct CgVal {
    llvm::Value* value = nullptr;
    CgType type;
    bool ok() const { return value != nullptr; }
};

struct Local {
    llvm::AllocaInst* slot = nullptr;
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

    std::optional<CgType> map(const TypeNode* node) const {
        if (!node) return voidType();

        // A pointer, an array, a nullable, a function type, a prototype or a
        // generic argument list all mean "not this slice" rather than "the base
        // name" -- silently dropping the decoration is how `&int` would become
        // `int` and start being copied by value.
        if (node->pointer_depth != 0 || node->is_array || node->is_nullable ||
            node->is_prototype || !node->generics.empty() ||
            !node->implements_list.empty() || node->array_size ||
            dynamic_cast<const FunctionTypeNode*>(node) ||
            dynamic_cast<const PointerTypeNode*>(node) ||
            dynamic_cast<const ArrayTypeNode*>(node)) {
            return std::nullopt;
        }
        if (auto scalar = byName(node->name)) return scalar;
        return structByName(node->name);
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
                CgType t;
                t.kind = CgType::Kind::Ptr;
                t.llvmType = llvm::PointerType::getUnqual(ctx_);
                return t;
            }
        }
        return std::nullopt;
    }

    // A name that is not a scalar may still be a struct. Checked second, so a
    // struct called `int` could not shadow the scalar -- the analyzer rejects that
    // name anyway, and the ordering means this file does not depend on it doing so.
    std::optional<CgType> structByName(const std::string& name) const {
        if (!structs_) return std::nullopt;
        auto it = structs_->find(name);
        if (it == structs_->end() || !it->second.complete) return std::nullopt;
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

class Emitter : public Visitor {
public:
    Emitter(DiagnosticEngine& diag, bool debug)
        : diag_(diag), debug_(debug), ctx_(), module_("fin", ctx_), builder_(ctx_),
          types_(ctx_) {
        types_.bindStructs(&structs_);
    }

    bool run(Program& program) {
        // Before the functions, because a function's signature may name a struct.
        declareStructs(program);
        if (failed_) return false;
        declareTopLevel(program);
        if (failed_) return false;
        for (auto& stmt : program.statements) {
            if (failed_) break;
            stmt->accept(*this);
        }
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
        std::string name = type ? type->name : std::string("<none>");
        if (type && (type->pointer_depth || dynamic_cast<const PointerTypeNode*>(type)))
            name = "&" + name;
        if (type && (type->is_array || dynamic_cast<const ArrayTypeNode*>(type)))
            name = "[" + name + "]";
        unsupported(node, fmt::format("{} of type '{}'", role, name));
    }

    void debugLog(const std::string& text) {
        if (debug_) diag_.note("[codegen] " + text);
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
            info.llvmType = llvm::StructType::create(ctx_, "struct." + s->name);
            structs_[s->name] = info;
            registered_.insert(s);
            decls.push_back(s);
        }

        for (StructDeclaration* s : decls) {
            StructInfo& info = structs_[s->name];
            std::vector<llvm::Type*> members;
            for (auto& m : s->members) {
                if (m->default_value) {
                    // The field default parses and nothing honours it. Zeroing the
                    // field instead would be a value the program never wrote, so
                    // this refuses until the pass that honours defaults exists --
                    // Soundness_Codegen.AnOmittedFieldIsZeroed is the rule for a
                    // field with no default, and changes when this does.
                    unsupported(*m, fmt::format(
                        "a default value for field '{}' of struct '{}'", m->name, s->name));
                    return;
                }
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
                info.fields.push_back(StructField{m->name, *t});
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
            debugLog("declared struct " + s->name);
        }
    }

    // The struct shapes this file will not lower, each with the reason it cannot be
    // guessed at. Returns false having already reported.
    bool lowerableStruct(StructDeclaration& s) {
        if (!s.generic_params.empty()) {
            unsupported(s, fmt::format("a generic struct '{}'", s.name));
            return false;
        }
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
        if (!s.parents.empty()) {
            unsupported(s, fmt::format("struct '{}' inheriting another type", s.name));
            return false;
        }
        if (!s.attributes.empty()) {
            // An attribute this file does not read may be one that changes the
            // layout. Ignoring it is the failure mode that produces a working
            // program with the wrong offsets.
            unsupported(s, fmt::format("an attribute on struct '{}'", s.name));
            return false;
        }
        return true;
    }

    void declareTopLevel(Program& program) {
        for (auto& stmt : program.statements) {
            if (auto* fn = dynamic_cast<FunctionDeclaration*>(stmt.get())) {
                declareFunction(*fn, fn->name, fn->name, fn->params, fn->return_type.get(),
                                false, /*isExtern=*/false);
            } else if (auto* def = dynamic_cast<DefineDeclaration*>(stmt.get())) {
                // `#[llvm_name="c_printf"]` renames the *symbol* and not the Fin
                // name: stdlib/stdio.fin:11 declares `@define printf` under it, and
                // that is the only mechanism the corpus has for binding an extern to
                // a C symbol whose spelling differs. The Fin name is still what a
                // call site writes, so the two are tracked separately.
                declareFunction(*def, def->name, symbolNameOf(*def), def->params,
                                def->return_type.get(), def->is_vararg,
                                /*isExtern=*/true);
            }
            if (failed_) return;
        }
    }

    static std::string symbolNameOf(const DefineDeclaration& def) {
        for (auto& attr : def.attributes) {
            if (attr->name == "llvm_name" && !attr->is_flag) return attr->value_str;
        }
        return def.name;
    }

    void declareFunction(ASTNode& node, const std::string& name,
                         const std::string& symbol,
                         const std::vector<std::unique_ptr<Parameter>>& params,
                         const TypeNode* returnType, bool isVarArg, bool isExtern) {
        if (functions_.count(name)) return;  // first declaration wins, as the analyzer's does

        FnInfo info;
        info.isVarArg = isVarArg;

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
        if (isExtern && info.returnType.isStruct()) {
            unsupported(node, fmt::format("an extern '{}' returning a struct", name));
            return;
        }

        std::vector<llvm::Type*> llvmParams;
        for (auto& p : params) {
            // `...` in `@define printf(fmt: string, ...)` is a Parameter with the
            // vararg flag and no type of its own.
            if (p->is_vararg) { info.isVarArg = true; continue; }
            auto t = types_.map(p->type.get());
            if (!t) { unsupportedType(*p, p->type.get(), "a parameter"); return; }
            if (t->isVoid()) { unsupported(*p, "a parameter of type 'void'"); return; }
            if (isExtern && t->isStruct()) {
                unsupported(*p, fmt::format("a struct parameter on extern '{}'", name));
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
            return std::nullopt;
        }
        if (auto* member = dynamic_cast<MemberAccess*>(&expr)) {
            // `Type::name` is an enum member or a static, not a field of an object.
            if (member->is_static) return std::nullopt;
            auto base = emitAddress(*member->object);
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
            unsupported(node, "a generic function");
            return;
        }
        auto found = functions_.find(node.name);
        if (found == functions_.end()) return;  // the refusal was already reported
        const FnInfo info = found->second;

        auto* entry = llvm::BasicBlock::Create(ctx_, "entry", info.fn);
        builder_.SetInsertPoint(entry);

        currentFn_ = &found->second;
        pushScope();

        // Each parameter gets a stack slot, because a parameter is assignable in
        // Fin and an argument register is not.
        size_t index = 0;
        for (auto& p : node.params) {
            if (p->is_vararg) continue;
            if (index >= info.paramTypes.size()) break;
            auto* slot = builder_.CreateAlloca(info.paramTypes[index].llvmType, nullptr,
                                               p->name);
            builder_.CreateStore(info.fn->getArg((unsigned)index), slot);
            scopes_.back()[p->name] = Local{slot, info.paramTypes[index]};
            ++index;
        }

        node.body->accept(*this);

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
                              fmt::format("codegen: emitted invalid IR for '{}'", node.name));
        }
    }

    void visit(DefineDeclaration& node) override { (void)node; }  // prototype only

    void visit(VariableDeclaration& node) override {
        if (!currentFn_) {
            unsupported(node, "a global variable");
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
            init = emit(*node.initializer);
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

        CgVal v = emit(*node.value);
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
                unsupported(node, "'null'");
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
        CgVal rhs = emit(*node.right);
        if (failed_) return;
        if (!lhs.ok() || !rhs.ok()) { unsupported(node, "this operand"); return; }
        value_ = emitArithmetic(node, node.op, lhs, rhs);
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

    void visit(UnaryOp& node) override {
        if (node.op == ASTTokenKind::INCREMENT || node.op == ASTTokenKind::DECREMENT) {
            // Prefix and postfix are the same node here, so which value the
            // expression has cannot be told apart. Refused rather than guessed;
            // `i += 1` is what the corpus writes in a statement position.
            unsupported(node, "'++' and '--'");
            return;
        }
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
        if (!node.generic_args.empty()) {
            unsupported(node, "a call with explicit generic arguments");
            return;
        }
        // A local of function type shadows nothing today -- a function value is not
        // lowered -- but checking locals first is the order the analyzer uses and
        // the order that stays correct when they are.
        if (findLocal(node.name)) {
            unsupported(node, fmt::format("a call through the variable '{}'", node.name));
            return;
        }
        auto found = functions_.find(node.name);
        if (found == functions_.end()) {
            unsupported(node, fmt::format("a call to '{}'", node.name));
            return;
        }
        const FnInfo& info = found->second;

        std::vector<llvm::Value*> args;
        for (size_t i = 0; i < node.args.size(); ++i) {
            CgVal a = emit(*node.args[i]);
            if (failed_) return;
            if (!a.ok()) { unsupported(node, "this argument"); return; }

            if (i < info.paramTypes.size()) {
                llvm::Value* converted = convert(node, a, info.paramTypes[i]);
                if (!converted) return;
                args.push_back(converted);
                continue;
            }
            if (!info.isVarArg) {
                // The analyzer already checked arity; this is a backend
                // inconsistency rather than a program error, so it says so.
                unsupported(node, fmt::format("a call to '{}' with too many arguments",
                                              node.name));
                return;
            }
            llvm::Value* promoted = promoteVararg(node, a);
            if (!promoted) return;
            args.push_back(promoted);
        }
        if (args.size() < info.paramTypes.size()) {
            unsupported(node, fmt::format("a call to '{}' with too few arguments", node.name));
            return;
        }

        auto* call = builder_.CreateCall(info.fn, args);
        value_ = info.returnType.isVoid() ? CgVal{} : CgVal{call, info.returnType};
        // A void call is a statement, not a value. `value_` staying empty is what
        // makes `let x <int> = voidcall();` refuse rather than store a token.
        if (info.returnType.isVoid()) value_ = CgVal{};
    }

    // The C variadic convention, which is not the Fin one: a float is passed as a
    // double and anything narrower than an int is passed as an int. A backend that
    // skipped this compiles and prints garbage, which is why
    // FloatsAreDoublesAtTheVarargBoundary is a run test and not an IR test.
    llvm::Value* promoteVararg(ASTNode& node, const CgVal& v) {
        if (v.type.isStruct()) {
            // `printf("%d", p)` for a struct `p` type-checks -- printf's parameter
            // is `...` and the analyzer does not read the format string. Passing the
            // aggregate would emit a call whose ABI is not the one C's va_arg reads,
            // so the value printed would be arbitrary. Refused, because the default
            // in this function is to pass the value through unchanged and that is
            // the wrong default for an aggregate.
            unsupported(node, "a struct passed to a C variadic");
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
        // Its methods, operators and constructors are deliberately not emitted:
        // naming the symbol needs a mangling scheme, and every way of *reaching*
        // one refuses at the call site instead (Soundness_Codegen.AMethodCallOn-
        // AStructIsRefused). Nothing implicit calls them -- a `P { a: 1 }` literal
        // does not run a constructor, and a destructor, which would run implicitly,
        // is what lowerableStruct refuses on.
        if (registered_.count(&node)) return;
        unsupported(node, fmt::format("a declaration of struct '{}' here", node.name));
    }
    void visit(InterfaceDeclaration& node) override { unsupported(node, "an interface declaration"); }
    void visit(EnumDeclaration& node) override { unsupported(node, "an enum declaration"); }
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
    void visit(DeleteStatement& node) override { unsupported(node, "'delete'"); }
    void visit(TryCatch& node) override { unsupported(node, "'try'/'catch'"); }
    void visit(BlameStatement& node) override { unsupported(node, "'blame'"); }

    void visit(MethodCall& node) override { unsupported(node, "a method call"); }
    void visit(StaticMethodCall& node) override { unsupported(node, "a '::' call"); }
    void visit(MemberAccess& node) override {
        if (node.is_static) {
            // `Colour::Red` names an enum member, and enums are not lowered.
            unsupported(node, fmt::format("the static member '{}'", node.member));
            return;
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
        CgVal object = emit(*node.object);
        if (failed_) return;
        if (!object.ok()) { unsupported(node, "this member's object"); return; }
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
        if (!node.generic_args.empty()) {
            unsupported(node, fmt::format("a generic struct literal '{}'", node.struct_name));
            return;
        }
        auto found = structs_.find(node.struct_name);
        if (found == structs_.end() || !found->second.complete) {
            unsupported(node, fmt::format("a literal of struct '{}'", node.struct_name));
            return;
        }
        const StructInfo& info = found->second;

        // Starts from all-zero, so a field the literal does not name is zero rather
        // than whatever was in the slot. That is the same answer a local with no
        // initialiser gets, and the reason a field carrying a *default* refuses in
        // declareStructs instead of quietly getting this one.
        llvm::Value* aggregate = llvm::Constant::getNullValue(info.llvmType);

        // Walked in the order written, inserted at the index declared. The written
        // order is not the stored order and this is the only place that could
        // confuse them.
        for (auto& entry : node.fields) {
            size_t index = 0;
            if (!info.find(entry.first, index)) {
                unsupported(node, fmt::format("the field '{}', which struct '{}' does not have",
                                              entry.first, node.struct_name));
                return;
            }
            if (!entry.second) { unsupported(node, "a field with no value"); return; }
            CgVal v = emit(*entry.second);
            if (failed_) return;
            if (!v.ok()) {
                unsupported(node, fmt::format("the value for field '{}'", entry.first));
                return;
            }
            llvm::Value* stored = convert(node, v, info.fields[index].type);
            if (!stored) return;
            aggregate = builder_.CreateInsertValue(aggregate, stored, {(unsigned)index},
                                                   entry.first);
        }

        CgType type;
        type.kind = CgType::Kind::Struct;
        type.llvmType = info.llvmType;
        type.structInfo = &found->second;
        value_ = CgVal{aggregate, type};
    }
    void visit(PrototypeLiteral& node) override { unsupported(node, "a prototype literal"); }
    void visit(ArrayLiteral& node) override { unsupported(node, "an array literal"); }
    void visit(ArrayAccess& node) override { unsupported(node, "an index expression"); }
    void visit(NewExpression& node) override { unsupported(node, "'new'"); }
    void visit(SizeofExpression& node) override { unsupported(node, "'sizeof'"); }
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
    // Keyed by Fin name. Never erased from or rehashed after declareStructs, because
    // CgType::structInfo points into it.
    std::unordered_map<std::string, StructInfo> structs_;
    // Which StructDeclaration nodes declareStructs actually took, so that one it
    // never saw is refused rather than assumed handled.
    std::set<const StructDeclaration*> registered_;
    std::vector<std::unordered_map<std::string, Local>> scopes_;
    std::vector<LoopTargets> loops_;
    FnInfo* currentFn_ = nullptr;
    CgVal value_;
};

}  // namespace

bool backendAvailable() { return true; }

bool generateObject(Program& ast, const std::string& objectPath, DiagnosticEngine& diag,
                    int optLevel, bool debugCodegen) {
    Emitter emitter(diag, debugCodegen);
    if (!emitter.run(ast)) return false;

    llvm::Module& module = emitter.module();

    // Verified before anything is written. An invalid module that reaches the
    // object writer is an assertion failure deep in LLVM, which reads as a
    // compiler crash rather than as the compiler bug it is.
    std::string verifyError;
    llvm::raw_string_ostream verifyStream(verifyError);
    if (llvm::verifyModule(module, &verifyStream)) {
        diag.reportError("codegen: emitted invalid IR", verifyStream.str());
        return false;
    }

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

    module.setTargetTriple(triple);
    module.setDataLayout(machine->createDataLayout());

    if (optLevel > 0) {
        // The IR-level pipeline, which is the half a TargetMachine's opt level does
        // not cover. Nothing sets optLevel above 0 yet -- finc has no `-O` flag --
        // so this is the shape and not yet a story (ADR 0002 names the story as a
        // requirement, and it is owed).
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
