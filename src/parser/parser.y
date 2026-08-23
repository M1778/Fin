%skeleton "lalr1.cc"
%require "3.2"
%defines
%define api.token.constructor
%define api.value.type variant
%define api.namespace {fin}
%define parse.assert
%locations
%define parse.error detailed

%code requires {
    #include <string>
    #include <vector>
    #include <memory>
    #include <utility>
    
    namespace fin { class location; }
    #include "ast/ASTNode.hpp"
    namespace fin { class DiagnosticEngine; }
}

%code {
    #include "lexer/lexer.hpp"
    #include "diagnostics/DiagnosticEngine.hpp"
    fin::parser::symbol_type yylex();
    
    namespace fin {
        std::unique_ptr<fin::Program> root;
    }
    // Where a declaration keeps its attributes and its visibility.
    //
    // There is no common base holding either: `attributes` is declared
    // separately on nine node types and `is_public` on seven, so attaching
    // `#[...]` or `pub`/`priv` to "a declaration" is a dispatch over node types.
    // It lives here because there are now three callers -- an attribute above a
    // declaration, `pub`/`priv` before one, and the `%{ ... }%` block that
    // applies one list to every statement inside it -- and the copy that was
    // inlined at the first two had already silently lost `class`:
    // ClassDeclaration derives from Statement, not from StructDeclaration, so
    // no branch of that chain matched it and every `#[...]` and `pub` on a class
    // was dropped.
    struct DeclFields {
        std::vector<std::unique_ptr<fin::Attribute>>* attributes = nullptr;
        bool* is_public = nullptr;
    };

    DeclFields decl_fields_of(fin::Statement* s) {
        if (!s) return {};
        if (auto* n = dynamic_cast<fin::FunctionDeclaration*>(s))  return {&n->attributes, &n->is_public};
        if (auto* n = dynamic_cast<fin::ClassDeclaration*>(s))     return {&n->attributes, &n->is_public};
        if (auto* n = dynamic_cast<fin::StructDeclaration*>(s))    return {&n->attributes, &n->is_public};
        if (auto* n = dynamic_cast<fin::InterfaceDeclaration*>(s)) return {&n->attributes, &n->is_public};
        if (auto* n = dynamic_cast<fin::EnumDeclaration*>(s))      return {&n->attributes, &n->is_public};
        if (auto* n = dynamic_cast<fin::VariableDeclaration*>(s))  return {&n->attributes, &n->is_public};
        if (auto* n = dynamic_cast<fin::TypeDefinition*>(s))       return {&n->attributes, &n->is_public};
        if (auto* n = dynamic_cast<fin::SpecialDeclaration*>(s))   return {&n->attributes, nullptr};
        if (auto* n = dynamic_cast<fin::DefineDeclaration*>(s))     return {&n->attributes, nullptr};
        if (auto* n = dynamic_cast<fin::MacroDeclaration*>(s))      return {&n->attributes, nullptr};
        if (auto* n = dynamic_cast<fin::ImportModule*>(s))         return {&n->attributes, nullptr};
        return {};
    }

    // Moves an attribute list onto a declaration. Silent when the node has no
    // attribute field, which is the pre-existing behaviour of the chain this
    // replaces.
    void attach_attributes(fin::Statement* s,
                           std::vector<std::unique_ptr<fin::Attribute>> attrs) {
        DeclFields f = decl_fields_of(s);
        if (!f.attributes) return;
        for (auto& a : attrs) if (a) f.attributes->push_back(std::move(a));
    }

    // Copies an attribute list onto a declaration, for the `%{ ... }%` block:
    // one written list, N statements, so the nodes cannot share the originals.
    // Attribute is plain data (name, value, flag), so this is a real copy and
    // needs no help from CloneVisitor.
    void copy_attributes_onto(fin::Statement* s,
                              const std::vector<std::unique_ptr<fin::Attribute>>& attrs) {
        DeclFields f = decl_fields_of(s);
        if (!f.attributes) return;
        for (const auto& a : attrs) {
            if (!a) continue;
            auto dup = a->is_flag ? std::make_unique<fin::Attribute>(a->name, true)
                                  : std::make_unique<fin::Attribute>(a->name, a->value_str);
            dup->setLoc(a->loc);
            f.attributes->push_back(std::move(dup));
        }
    }

    void set_visibility(fin::Statement* s, bool is_public) {
        DeclFields f = decl_fields_of(s);
        if (f.is_public) *f.is_public = is_public;
    }

    std::string flatten_macro_name(fin::Expression* expr) {
        if (auto* id = dynamic_cast<fin::Identifier*>(expr)) {
            return id->name;
        }
        if (auto* mem = dynamic_cast<fin::MemberAccess*>(expr)) {
            std::string left = flatten_macro_name(mem->object.get());
            if (!left.empty()) return left + "." + mem->member;
        }
        return "";
    }
}

%parse-param { fin::DiagnosticEngine& diag }

/* ========================================================================== */
/*                                   TOKENS                                   */
/* ========================================================================== */

%token END 0 "end of file"

/* Literals */
%token <std::string> IDENTIFIER INTEGER FLOAT STRING_LITERAL CHAR_LITERAL

/* Keywords */
%token KW_LET KW_CONST KW_AUTO
%token KW_FUN KW_RETURN
%token KW_PUB KW_PRIV KW_READONLY
%token KW_STRUCT KW_ENUM KW_INTERFACE
%token KW_MACRO KW_STATIC KW_NULL
%token KW_TRUE KW_FALSE
%token KW_NAMESPACE
%token KW_WHILE KW_DO KW_FOR KW_FOREACH KW_BREAK KW_CONTINUE
%token KW_IF KW_ELSE KW_IN
%token KW_TRY KW_CATCH KW_BLAME KW_SUPER KW_SELF_TYPE
%token KW_IMPORT KW_AS KW_FROM
/* `extern X as Y;` -- tests/samples/extern_as.fin, whose whole subject it is, and
   enums.fin:19-20. `extern` lexed as an IDENTIFIER before this, so the line read as
   two identifiers in a row. No corpus file uses `extern` as a name. */
%token KW_EXTERN
%token KW_NEW KW_DELETE KW_SIZEOF KW_TYPEOF KW_AS_PTR KW_CAST
%token KW_OPERATOR
%token KW_SPECIAL KW_FN_TYPE KW_DEFINE
%token KW_M1778
%token KW_TYPE KW_CLASS KW_IMPLEMENTS KW_ANY

/* Types */
%token TYPE_INT TYPE_FLOAT TYPE_DOUBLE TYPE_BOOL 
%token TYPE_STRING TYPE_CHAR TYPE_VOID TYPE_LONG

/* Punctuation */
%token LPAREN RPAREN LBRACE RBRACE LBRACKET RBRACKET
%token SEMICOLON COLON DOUBLE_COLON COMMA DOT ELLIPSIS
%token AT DOLLAR HASH
%token PERCENT_LBRACE RBRACE_PERCENT /* %{ ... }% */
%token TILDE QUESTION

/* Operators */
%token EQUAL PLUSEQUAL MINUSEQUAL MULTEQUAL DIVEQUAL
%token EQEQ NOTEQ LT GT LTEQ GTEQ
%token AND OR NOT
%token PLUS MINUS MULT DIV MOD
%token AMPERSAND
%token INCREMENT DECREMENT

/* Special Operators */
%token ARROW RARROW /* => and -> */
%token KW_QUOTE HASH_FOR HASH_INDEX
%token SHIFTLEFT SHIFTRIGHT SHIFTLEFTEQUAL SHIFTRIGHTEQUAL
%token PIPE CARET BACKTICK

/* ========================================================================== */
/*                                PRECEDENCE                                  */
/* ========================================================================== */

/* Lowest precedence */
%precedence TYPE_ANNOT_PREC

/* After `( IDENTIFIER`, a following `:` is ambiguous between a lambda parameter
   (`(x: int) <int> -> {...}`) and a parenthesised conditional whose condition is
   a bare name (`(x : a ? b)`, ADR 0005 order). LALR must choose at the `:`, one
   token too early to tell them apart. Ranking a bare-identifier reduction below
   COLON makes the parameter win, which is the reading the corpus needs: lambdas
   with named parameters are everywhere, while a conditional on a bare name can
   always be written `((x) : a ? b)`. Declared rather than left to bison's
   shift-by-default so the choice is recorded and the conflict count stays 0. */
%precedence PARAM_NAME_PREC
/* `Type::name` read as a value (an enum member) against `Type::name(...)`, a
   static method call. The parser has to decide at the `(`, so the two are one
   token apart and it is a shift/reduce. Ranked below LPAREN so the call always
   wins: `IFaceOptions::First(x)` is a static call of `First`, never a call of
   whatever the constant `IFaceOptions::First` holds. Declared rather than left
   to shift-by-default so the choice is recorded and the conflict count stays 0. */
%precedence STATIC_VALUE_PREC
%right ARROW
%right EQUAL PLUSEQUAL MINUSEQUAL MULTEQUAL DIVEQUAL SHIFTLEFTEQUAL SHIFTRIGHTEQUAL
%right KW_NEW KW_CAST KW_SIZEOF
%right QUESTION COLON
%left OR
%left AND
%left PIPE
%left CARET
%left AMPERSAND
%left EQEQ NOTEQ
%left LT GT LTEQ GTEQ
%left SHIFTLEFT SHIFTRIGHT
%left PLUS MINUS
%left MULT DIV MOD
%right NOT UMINUS ADDRESSOF_PREC DEREFERENCE_PREC
/* LPAREN is the call operator and a call is postfix, so it binds tighter than every
   infix operator. It used to sit with KW_NEW/KW_CAST/KW_SIZEOF near the bottom of
   this table, where a prefix keyword belongs, and the effect was that `i < g . LPAREN`
   compared the rule `expression LT expression` against the lookahead LPAREN, found the
   rule tighter and reduced -- so `i < g` became the callee and the whole comparison was
   rebuilt as `FunctionCall("unknown")`. Every binary operator was affected, and so was
   unary minus: `-g()` reduced to `(-g)()`. Soundness_Precedence.ACallOnTheRightOfA-
   BinaryOperatorIsStillACall is the sixteen-operator record of it.
   `STATIC_VALUE_PREC` above is documented as "ranked below LPAREN so the call always
   wins"; moving LPAREN up keeps that true. */
%left LBRACKET DOT LBRACE LPAREN
%left INCREMENT DECREMENT
%precedence DENULLIFY
%left HIGH_PREC
%left TYPE_PREC /* Specific precedence for resolving < in types */

/* Control Flow Precedence */
%precedence KW_IFX
%precedence KW_ELSE

/* ========================================================================== */
/*                                    TYPES                                   */
/* ========================================================================== */

%type <std::vector<fin::MacroRule>> macro_rules
%type <fin::MacroRule> macro_rule

/* Core */
%type <std::unique_ptr<fin::Program>> program
%type <std::vector<std::unique_ptr<fin::Statement>>> statements block_stmts statement_group namespace_block attribute_block
%type <std::unique_ptr<fin::Statement>> statement 
%type <std::unique_ptr<fin::Block>> block

/* Declarations */
%type <std::unique_ptr<fin::Statement>> variable_declaration 
%type <std::unique_ptr<fin::Statement>> import_statement annotated_statement define_declaration macro_declaration
%type <std::unique_ptr<fin::Statement>> declaration_body
%type <std::unique_ptr<fin::Statement>> annotated_declaration declaration_with_vis bare_declaration
%type <std::unique_ptr<fin::Statement>> type_definition special_declaration implements_block

%type <std::unique_ptr<fin::TypeNode>> type base_type pointer_type array_type fn_type type_no_annot
%type <std::vector<std::unique_ptr<fin::TypeNode>>> type_list
%type <std::vector<std::unique_ptr<fin::TypeNode>>> union_tail
%type <std::unique_ptr<fin::TypeNode>> union_alternative
%type <std::vector<std::unique_ptr<fin::Parameter>>> params param_list
%type <std::unique_ptr<fin::Parameter>> param

/* Generics & Attributes */
%type <std::vector<std::unique_ptr<fin::GenericParam>>> generic_params_opt generic_param_list
%type <std::unique_ptr<fin::GenericParam>> generic_param
%type <std::vector<std::unique_ptr<fin::Attribute>>> attributes_opt attribute_list
%type <std::unique_ptr<fin::Attribute>> attribute
%type <std::string> attr_id
%type <std::string> attr_arg

/* Extern Params */
%type <std::pair<std::vector<std::unique_ptr<fin::Parameter>>, bool>> extern_params

/* Structs & Interfaces */
%type <std::unique_ptr<fin::StructDeclaration>> struct_body_content
%type <std::unique_ptr<fin::InterfaceDeclaration>> interface_body_content
%type <std::vector<std::unique_ptr<fin::TypeNode>>> inheritance_opt
%type <std::unique_ptr<fin::ASTNode>> interface_item_rest

/* Struct Items */
%type <std::unique_ptr<fin::ImplementsBlock>> implements_body_content
%type <std::unique_ptr<fin::ASTNode>> struct_item_rest implements_item_rest

/* Enums & Imports */
%type <std::vector<fin::EnumMember>> enum_values
%type <fin::EnumMember> enum_value
%type <std::vector<std::string>> import_list
%type <std::vector<std::unique_ptr<fin::GenericParam>>> operator_generics_opt
%type <std::vector<std::unique_ptr<fin::Parameter>>> operator_params_opt

/* Control Flow */
%type <std::unique_ptr<fin::TypeNode>> implements_opt
%type <std::vector<std::unique_ptr<fin::Expression>>> macro_arg_item macro_arg_list_body macro_arguments
%type <std::unique_ptr<fin::Statement>> if_statement while_loop for_loop foreach_loop try_catch_statement blame_statement return_statement expression_statement
%type <std::unique_ptr<fin::Statement>> control_statement delete_statement

/* Expressions */
%type <std::unique_ptr<fin::Expression>> expression primary_no_struct
%type <std::unique_ptr<fin::Expression>> literal lambda_expression overwrite_body
%type <std::unique_ptr<fin::Expression>> super_expression prototype_literal no_struct_expression static_method_call
%type <std::vector<std::unique_ptr<fin::Expression>>> expression_list arguments
%type <std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>>> field_assignments
%type <std::vector<std::pair<std::unique_ptr<fin::Expression>, std::unique_ptr<fin::Expression>>>> prototype_elements
%type <bool> visibility_opt
%type <int> member_visibility
%type <bool> implements_visibility fun_kw readonly_opt visibility_label

/* Added missing types */
%type <std::string> dotted_path primitive_type
%type <std::string> extern_path
%type <int> ptr_stars
%type <std::pair<std::string, std::unique_ptr<fin::TypeNode>>> fn_type_param
%type <std::vector<std::pair<std::string, std::unique_ptr<fin::TypeNode>>>> fn_type_params
%type <fin::MemberInit> ctor_init
%type <std::vector<fin::MemberInit>> ctor_inits
/* A module path: `.first` is the module, `.second` the `::`-separated namespace
   tail. `error::std` is the module `error` and the namespace `std` inside it. */
%type <std::pair<std::string, std::string>> module_path
%type <fin::ASTTokenKind> operator_symbol
%type <std::vector<fin::MacroParam>> macro_param_list
%type <fin::MacroParam> macro_param

%%


/* ========================================================================== */
/*                                   GRAMMAR                                  */
/* ========================================================================== */

program:
    statements { 
        $$ = std::make_unique<fin::Program>(std::move($1)); 
        $$->setLoc(@$);
        fin::root = std::move($$);
    }
    | %empty {
        $$ = std::make_unique<fin::Program>(std::vector<std::unique_ptr<fin::Statement>>());
        $$->setLoc(@$);
        fin::root = std::move($$);
    }
    ;

statements:
    statements statement {
        if ($2) $1.push_back(std::move($2));
        $$ = std::move($1);
    }
    | statement {
        std::vector<std::unique_ptr<fin::Statement>> vec;
        if ($1) vec.push_back(std::move($1));
        $$ = std::move(vec);
    }
    | statements statement_group {
        $$ = std::move($1);
        for (auto& s : $2) if (s) $$.push_back(std::move(s));
    }
    | statement_group {
        std::vector<std::unique_ptr<fin::Statement>> vec;
        for (auto& s : $1) if (s) vec.push_back(std::move(s));
        $$ = std::move(vec);
    }
    ;

/* A construct that contributes *several* statements to the list around it
   rather than one node.  Splicing is what lets these parse without a new
   NodeKind: see the note on `namespace_block`. */
statement_group:
    namespace_block { $$ = std::move($1); }
    | attribute_block { $$ = std::move($1); }
    ;

/* `#[stdimport] %{ ... }%` -- one attribute list applied to every statement in
   the block, which stdlib/collection.fin:3 documents in place as "multi set
   attribute for statements". Five stdlib files use it, four to mark a run of
   imports and stdlib/operators.fin over 130 lines of declarations.
 
   The block is not a node: the statements are spliced into the list around it
   with a copy of the attribute list on each. An `AttributeBlock` node would
   have to be unwrapped by every later pass to mean anything, since the block
   itself carries no scope and no order -- so the unwrapping happens once, here,
   where the attributes are still in hand. */
attribute_block:
    attribute_list PERCENT_LBRACE block_stmts RBRACE_PERCENT {
        for (auto& st : $3) copy_attributes_onto(st.get(), $1);
        $$ = std::move($3);
    }
    ;

/* `namespace std { ... }` — eleven of the twelve files under tests/samples/stdlib
   open with one, and stdlib/operators.fin nests `namespace ops` inside it to
   spell `std::ops`.
 
   The contents are spliced into the enclosing statement list and THE NAMESPACE
   NAME IS DISCARDED.  That is deliberate for this wave and it is not the end
   state.  Two reasons it is right for now:
 
     - A namespace is not a scope in the `Block` sense.  Wrapping the body in a
       Block would hide every stdlib declaration from the importer, so
       `import { Error } from error::std` would resolve the module and then find
       nothing.  Splicing keeps the declarations visible, which is what makes the
       `::` import paths reachable at all.
     - Keeping the name needs somewhere to put it, and every candidate is a
       `NamespaceDeclaration` node.  Registering one means editing Visitor.hpp
       (accept dispatch), src/ast/cloning/** and src/semantics/**, none of which
       this wave owns.
 
   So: parsing is complete here, name resolution is not.  `ImportModule::
   namespace_path` already carries the `::` tail from the import side; the two
   halves meet when namespaces get a node and a scope. */
namespace_block:
    KW_NAMESPACE IDENTIFIER LBRACE block_stmts RBRACE {
        $$ = std::move($4);
    }
    ;

statement:
      annotated_declaration { $$ = std::move($1); }
    | annotated_statement    { $$ = std::move($1); }
    | declaration_with_vis   { $$ = std::move($1); }
    | bare_declaration       { $$ = std::move($1); }
    | define_declaration     { $$ = std::move($1); }
    | macro_declaration      { $$ = std::move($1); }
    | import_statement       { $$ = std::move($1); }
    | if_statement           { $$ = std::move($1); }
    | while_loop             { $$ = std::move($1); }
    | for_loop               { $$ = std::move($1); }
    | foreach_loop           { $$ = std::move($1); }
    | control_statement      { $$ = std::move($1); }
    | delete_statement       { $$ = std::move($1); }
    | try_catch_statement    { $$ = std::move($1); }
    | blame_statement        { $$ = std::move($1); }
    | return_statement       { $$ = std::move($1); }
    | expression_statement   { $$ = std::move($1); }
    | special_declaration    { $$ = std::move($1); }
    | implements_block       { $$ = std::move($1); }
    /* A bare `{` in statement position always opens a scope (ADR 0011). A
       brace-initialised value only ever appears after something that introduces
       it -- `st{}`, `= map!{...}`, `= {...}` -- so the two never compete for this
       position. tests/samples/variables.fin:17 opens a scope commented "Custom
       Scope"; before this the `{` was parsed as an expression and the error
       pointed at the `let` inside, three lines from the cause. */
    | block                  { $$ = std::move($1); }
    | SEMICOLON              { $$ = nullptr; }
    ;

block:
    LBRACE block_stmts RBRACE {
        $$ = std::make_unique<fin::Block>(std::move($2));
        $$->setLoc(@$);
    }
    ;

block_stmts:
    statements { $$ = std::move($1); }
    | %empty { $$ = std::vector<std::unique_ptr<fin::Statement>>(); }
    ;

/* --- FACTORED DECLARATIONS --- */

annotated_declaration:
    attribute_list visibility_opt declaration_body {
        $$ = std::move($3);
        attach_attributes($$.get(), std::move($1));
        set_visibility($$.get(), $2);
        $$->setLoc(@$);
    }
    ;

declaration_with_vis:
    KW_PUB declaration_body {
        $$ = std::move($2);
        set_visibility($$.get(), true);
        $$->setLoc(@$);
    }
    | KW_PRIV declaration_body {
        $$ = std::move($2);
        set_visibility($$.get(), false);
        $$->setLoc(@$);
    }
    ;

bare_declaration:
    declaration_body { $$ = std::move($1); }
    ;

declaration_body:
    fun_kw IDENTIFIER generic_params_opt LPAREN params RPAREN LT type GT block {
        if ($1) $8->is_nullable = true;
        $$ = std::make_unique<fin::FunctionDeclaration>($2, std::move($5), std::move($8), std::move($10));
        static_cast<fin::FunctionDeclaration*>($$.get())->generic_params = std::move($3);
        $$->setLoc(@$);
    }
    | fun_kw IDENTIFIER generic_params_opt LPAREN params RPAREN LT type GT SEMICOLON {
        if ($1) $8->is_nullable = true;
        $$ = std::make_unique<fin::FunctionDeclaration>($2, std::move($5), std::move($8), nullptr);
        static_cast<fin::FunctionDeclaration*>($$.get())->generic_params = std::move($3);
        $$->setLoc(@$);
    }
    /* Forward declaration: `struct Stream;` -- tests/samples/stdlib/stdio.fin:42,
       where the comment above it says the compiler "will look into the future if
       necessary". A StructDeclaration with no body and `is_forward_declaration`
       set, so the name exists before the definition without pretending to be an
       empty struct -- `struct IOError: <Error> {}` two lines below is a real
       empty struct and the two must not look alike. */
    | KW_STRUCT IDENTIFIER generic_params_opt SEMICOLON {
        auto sd = std::make_unique<fin::StructDeclaration>($2, std::vector<std::unique_ptr<fin::StructMember>>(), false);
        sd->generic_params = std::move($3);
        sd->is_forward_declaration = true;
        $$ = std::move(sd);
        $$->setLoc(@$);
    }
    /* setLoc, like every other production that builds a node the analyzer can raise
       a diagnostic against. It was missing here and on the three declaration forms
       below -- only the forward-declaration case above had it -- so a conformance
       failure was reported at 1:1, which in the corpus is the `//@` expectation
       comment. Soundness_DiagnosticLocation.ADeclarationReportsWhereItWasWritten. */
    | KW_STRUCT IDENTIFIER generic_params_opt inheritance_opt LBRACE struct_body_content RBRACE {
        $6->name = $2;
        $6->generic_params = std::move($3);
        $6->parents = std::move($4);
        $$ = std::move($6);
        $$->setLoc(@$);
    }
    | KW_INTERFACE IDENTIFIER generic_params_opt LBRACE interface_body_content RBRACE {
        $5->name = $2;
        $5->generic_params = std::move($3);
        $$ = std::move($5);
        $$->setLoc(@$);
    }
    /* `generic_params_opt` because tests/samples/stdlib/typing.fin:14 declares
       `pub enum Result <T: Any<...>, U: ErrorLike> { Ok, Err }` -- an enum whose
       members carry types is generic like any other container, and the `<` there
       was a syntax error. */
    | KW_ENUM IDENTIFIER generic_params_opt LBRACE enum_values RBRACE {
        std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> values;
        std::vector<fin::EnumPayload> payloads;
        values.reserve($5.size());
        payloads.reserve($5.size());
        for (auto& member : $5) {
            values.emplace_back(member.name, std::move(member.value));
            payloads.push_back(fin::EnumPayload{member.name, std::move(member.payload), member.is_public});
        }
        auto en = std::make_unique<fin::EnumDeclaration>($2, std::move(values), false);
        en->generic_params = std::move($3);
        en->member_payloads = std::move(payloads);
        $$ = std::move(en);
        $$->setLoc(@$);
    }
    | KW_CLASS IDENTIFIER generic_params_opt inheritance_opt LBRACE struct_body_content RBRACE {
        auto cls = std::make_unique<fin::ClassDeclaration>($2, std::move($6->members), false);
        cls->methods = std::move($6->methods);
        cls->operators = std::move($6->operators);
        cls->constructors = std::move($6->constructors);
        cls->destructor = std::move($6->destructor);
        cls->attributes = std::move($6->attributes);
        cls->generic_params = std::move($3);
        cls->parents = std::move($4);
        $$ = std::move(cls);
        $$->setLoc(@$);
    }
    | KW_LET IDENTIFIER LT type GT EQUAL expression SEMICOLON {
        $$ = std::make_unique<fin::VariableDeclaration>(true, $2, std::move($4), std::move($7));
    }
    | KW_CONST IDENTIFIER LT type GT EQUAL expression SEMICOLON {
        $$ = std::make_unique<fin::VariableDeclaration>(false, $2, std::move($4), std::move($7));
    }
    | KW_LET IDENTIFIER LT type GT SEMICOLON {
        $$ = std::make_unique<fin::VariableDeclaration>(true, $2, std::move($4), nullptr);
    }
    /* Nullable variable, `let x? <A>` -- tests/samples/nullifier.fin:27, :34, :39.
       `_` is an ordinary IDENTIFIER to the lexer, so `let _? <int>` needs no
       separate form. */
    | KW_LET IDENTIFIER QUESTION LT type GT EQUAL expression SEMICOLON {
        $5->is_nullable = true;
        $$ = std::make_unique<fin::VariableDeclaration>(true, $2, std::move($5), std::move($8));
    }
    | KW_LET IDENTIFIER QUESTION LT type GT SEMICOLON {
        $5->is_nullable = true;
        $$ = std::make_unique<fin::VariableDeclaration>(true, $2, std::move($5), nullptr);
    }
    | KW_CONST IDENTIFIER QUESTION LT type GT EQUAL expression SEMICOLON {
        $5->is_nullable = true;
        $$ = std::make_unique<fin::VariableDeclaration>(false, $2, std::move($5), std::move($8));
    }
    | type_definition { $$ = std::move($1); }
    ;

/* --- ATTRIBUTES --- */

attributes_opt:
    attribute_list { $$ = std::move($1); }
    | %empty { $$ = std::vector<std::unique_ptr<fin::Attribute>>(); }
    ;

attribute_list:
    attribute_list attribute { $1.push_back(std::move($2)); $$ = std::move($1); }
    | attribute { std::vector<std::unique_ptr<fin::Attribute>> v; v.push_back(std::move($1)); $$ = std::move(v); }
    ;

attribute:
    HASH LBRACKET attr_id EQUAL STRING_LITERAL RBRACKET {
        $$ = std::make_unique<fin::Attribute>($3, $5);
        $$->setLoc(@$);
    }
    | HASH LBRACKET attr_id RBRACKET {
        $$ = std::make_unique<fin::Attribute>($3, true);
        $$->setLoc(@$);
    }
    | HASH LBRACKET attr_id LPAREN attr_arg RPAREN RBRACKET {
        $$ = std::make_unique<fin::Attribute>($3, $5);
        $$->setLoc(@$);
    }
    ;

/* An attribute name is a word, and several of the words the corpus uses are
   keywords: `#[class]`, `#[type(enum)]`, `#[implements]`. Widened rather than
   made a free `IDENTIFIER` plus a keyword-to-string map, because the set is
   closed -- these are the only three in the corpus -- and a map would accept
   `#[if]` and `#[return]` too. */
attr_id:
    IDENTIFIER
    | KW_CLASS { $$ = "class"; }
    | KW_TYPE { $$ = "type"; }
    | KW_IMPLEMENTS { $$ = "implements"; }
    ;

/* An attribute argument is a dotted path (`#[use(compiler.components.types)]`), a
   type word (`#[type(enum)]`, `#[type(any)]`, `#[type(string)]`) or an unquote
   (`#[slaveof($Fin)]`). Separate from `dotted_path` so widening it here does not
   widen every module path in the grammar. */
attr_arg:
    dotted_path { $$ = std::move($1); }
    | primitive_type { $$ = std::move($1); }
    | KW_ANY { $$ = "any"; }
    | KW_ENUM { $$ = "enum"; }
    | KW_STRUCT { $$ = "struct"; }
    | KW_INTERFACE { $$ = "interface"; }
    | DOLLAR IDENTIFIER { $$ = "$" + $2; }
    ;

/* --- GENERICS --- */

generic_params_opt:
    LT generic_param_list GT { $$ = std::move($2); }
    | %empty { $$ = std::vector<std::unique_ptr<fin::GenericParam>>(); }
    ;

generic_param_list:
    generic_param_list COMMA generic_param { $1.push_back(std::move($3)); $$ = std::move($1); }
    | generic_param { std::vector<std::unique_ptr<fin::GenericParam>> v; v.push_back(std::move($1)); $$ = std::move(v); }
    ;

generic_param:
    IDENTIFIER { 
        $$ = std::make_unique<fin::GenericParam>($1); 
        $$->setLoc(@$);
    }
    | IDENTIFIER COLON type {
        $$ = std::make_unique<fin::GenericParam>($1, std::move($3));
        $$->setLoc(@$);
    }
    /* A constraint set as the constraint: `<T: any implements Struct>` --
       tests/samples/literal_struct.fin:4, which is that file's whole subject and
       its first error ("unexpected KW_IMPLEMENTS, expecting COMMA or GT"). Written
       without the angle brackets that `type Any<...> = any implements <...>;` uses
       (stdlib/types.fin:74), so both spellings are accepted here. The constraint is
       the erasure marker `any` carrying `implements_list`, exactly as the union
       alternative form builds it, so nothing new has to be understood downstream. */
    | IDENTIFIER COLON KW_ANY KW_IMPLEMENTS type {
        auto any_type = std::make_unique<fin::TypeNode>("any");
        any_type->implements_list.push_back(std::move($5));
        any_type->setLoc(@3);
        $$ = std::make_unique<fin::GenericParam>($1, std::move(any_type));
        $$->setLoc(@$);
    }
    | IDENTIFIER COLON KW_ANY KW_IMPLEMENTS LT type_list GT {
        auto any_type = std::make_unique<fin::TypeNode>("any");
        any_type->implements_list = std::move($6);
        any_type->setLoc(@3);
        $$ = std::make_unique<fin::GenericParam>($1, std::move(any_type));
        $$->setLoc(@$);
    }
    /* The erasure marker as the whole parameter list: `type Any<...> = any
       implements <...>;` -- tests/samples/stdlib/types.fin:74, which is the
       declaration of `Any` that `T: Any<...>` all over the corpus refers to. The
       marker was already a type (`base_type: ELLIPSIS`) so it could be passed as
       an argument, but not declared as a parameter. Named `...` rather than given
       an empty name so that a pass reading `generic_params` cannot mistake it for
       an ordinary parameter. */
    | ELLIPSIS {
        $$ = std::make_unique<fin::GenericParam>("...");
        $$->setLoc(@$);
    }
    ;

/* --- IMPORTS --- */

/* An attribute list above a statement that is not a `declaration_body`.
 
   `annotated_declaration` covers `#[...] pub struct ...` and friends; this
   covers the four statement forms that sit outside that nonterminal. Written
   once rather than per form: the `@special` case used to be a second, nearly
   identical eleven-symbol production, and `import` and `@define` were simply
   missing -- so `#[stdimport]` above an import failed to parse in four stdlib
   files and `#[llvm_name="c_printf"]` above an `@define` in
   tests/samples/stdlib/stdio.fin:11 had nowhere to attach. */
annotated_statement:
    attribute_list import_statement {
        $$ = std::move($2);
        attach_attributes($$.get(), std::move($1));
        $$->setLoc(@$);
    }
    | attribute_list define_declaration {
        $$ = std::move($2);
        attach_attributes($$.get(), std::move($1));
        $$->setLoc(@$);
    }
    | attribute_list macro_declaration {
        $$ = std::move($2);
        attach_attributes($$.get(), std::move($1));
        $$->setLoc(@$);
    }
    | attribute_list special_declaration {
        $$ = std::move($2);
        attach_attributes($$.get(), std::move($1));
        $$->setLoc(@$);
    }
    ;

import_statement:
    /* Case 1: import "file.fin"; */
    KW_IMPORT STRING_LITERAL SEMICOLON {
        std::vector<std::string> empty;
        // Strip quotes from string literal if present
        std::string src = $2;
        if (src.size() >= 2 && src.front() == '"' && src.back() == '"') {
            src = src.substr(1, src.size() - 2);
        }
        $$ = std::make_unique<fin::ImportModule>(src, false, "", empty);
        $$->setLoc(@$);
    }
    /* Case 2: import lib.mod; / import mod::ns; */
    | KW_IMPORT module_path SEMICOLON {
        std::vector<std::string> empty;
        auto imp = std::make_unique<fin::ImportModule>($2.first, true, "", empty);
        imp->namespace_path = $2.second;
        $$ = std::move(imp);
        $$->setLoc(@$);
    }
    /* Case 3: import lib.mod as alias; */
    | KW_IMPORT module_path KW_AS IDENTIFIER SEMICOLON {
        std::vector<std::string> empty;
        auto imp = std::make_unique<fin::ImportModule>($2.first, true, $4, empty);
        imp->namespace_path = $2.second;
        $$ = std::move(imp);
        $$->setLoc(@$);
    }
    /* Case 4: import { A, B } from "file.fin"; */
    | KW_IMPORT LBRACE import_list RBRACE KW_FROM STRING_LITERAL SEMICOLON {
        std::string src = $6;
        if (src.size() >= 2 && src.front() == '"' && src.back() == '"') {
            src = src.substr(1, src.size() - 2);
        }
        $$ = std::make_unique<fin::ImportModule>(src, false, "", $3);
        $$->setLoc(@$);
    }
    /* Case 6: import * from m; -- every symbol, tests/samples/importing.fin:11.
       The empty symbol list already means "the whole module" for Case 2, so the
       star is recorded as a `*` entry rather than as a new flag: an empty list and
       a star mean different things to the loader (`import m;` binds the module
       name, `import * from m;` binds its symbols). */
    | KW_IMPORT MULT KW_FROM module_path SEMICOLON {
        std::vector<std::string> star{"*"};
        auto imp = std::make_unique<fin::ImportModule>($4.first, true, "", star);
        imp->namespace_path = $4.second;
        $$ = std::move(imp);
        $$->setLoc(@$);
    }
    /* Case 5: import { A, B } from lib.mod; / from mod::ns; */
    | KW_IMPORT LBRACE import_list RBRACE KW_FROM module_path SEMICOLON {
        auto imp = std::make_unique<fin::ImportModule>($6.first, true, "", $3);
        imp->namespace_path = $6.second;
        $$ = std::move(imp);
        $$->setLoc(@$);
    }
    ;
import_list:
    import_list COMMA IDENTIFIER { $1.push_back($3); $$ = std::move($1); }
    | IDENTIFIER { std::vector<std::string> v; v.push_back($1); $$ = std::move(v); }
    ;

/* The left-hand side of an `extern`: a symbol, possibly inside a namespace or an
   enum -- `myns::myfunc` (tests/samples/extern_as.fin:19), `Result::Ok`
   (enums.fin:19). Separate from `module_path`, which carries a module name and a
   member and is only reachable after `import`. */
extern_path:
    IDENTIFIER { $$ = $1; }
    | extern_path DOUBLE_COLON IDENTIFIER { $$ = $1 + "::" + $3; }
    ;

dotted_path:
    IDENTIFIER { $$ = $1; }
    | dotted_path DOT IDENTIFIER { $$ = $1 + "." + $3; }
    ;

/* `mod`, `lib.mod`, `mod::ns`, `mod::ns::inner`. The leftmost dotted run names
   the module the loader must find; every `::` segment after it names a namespace
   inside that module. tests/samples/importing.fin:14 records the meaning. */
module_path:
    dotted_path { $$ = std::make_pair($1, std::string()); }
    | module_path DOUBLE_COLON IDENTIFIER {
        $$ = std::move($1);
        if ($$.second.empty()) $$.second = $3;
        else $$.second += "::" + $3;
    }
    ;

operator_params_opt:
    LPAREN params RPAREN { $$ = std::move($2); }
    | %empty { $$ = std::vector<std::unique_ptr<fin::Parameter>>(); }
    ;

/* --- STRUCTS --- */

inheritance_opt:
    COLON LT type_list GT { $$ = std::move($3); }
    | %empty { $$ = std::vector<std::unique_ptr<fin::TypeNode>>(); }
    ;

struct_body_content:
      struct_body_content attributes_opt visibility_label {
        /* `attributes_opt` is here only so that a KW_PUB lookahead has one
           action and not two: without it the parser must choose between
           reducing the empty attribute list and shifting KW_PUB, a token
           before the COLON says which of `pub:` and `pub fun` this is.
           An attribute written before a label decorates nothing, so it is
           dropped rather than carried to the next item. */
        $1->label_public = $3;
        $$ = std::move($1);
    }
    | struct_body_content attributes_opt member_visibility readonly_opt struct_item_rest {
        const bool vis = $3 < 0 ? $1->label_public : ($3 == 1);
        if (auto* member = dynamic_cast<fin::StructMember*>($5.get())) {
            member->attributes = std::move($2);
            member->is_public = vis;
            member->is_readonly = $4;
            $1->members.push_back(std::unique_ptr<fin::StructMember>(static_cast<fin::StructMember*>($5.release())));
        }
        else if (auto* func = dynamic_cast<fin::FunctionDeclaration*>($5.get())) {
            func->attributes = std::move($2);
            func->is_public = vis;
            $1->methods.push_back(std::unique_ptr<fin::FunctionDeclaration>(static_cast<fin::FunctionDeclaration*>($5.release())));
        }
        else if (auto* op = dynamic_cast<fin::OperatorDeclaration*>($5.get())) {
            op->is_public = vis;
            $1->operators.push_back(std::unique_ptr<fin::OperatorDeclaration>(static_cast<fin::OperatorDeclaration*>($5.release())));
        }
        else if (auto* ctor = dynamic_cast<fin::ConstructorDeclaration*>($5.get())) {
            $1->constructors.push_back(std::unique_ptr<fin::ConstructorDeclaration>(static_cast<fin::ConstructorDeclaration*>($5.release())));
        }
        else if (auto* dtor = dynamic_cast<fin::DestructorDeclaration*>($5.get())) {
            $1->destructor = std::unique_ptr<fin::DestructorDeclaration>(static_cast<fin::DestructorDeclaration*>($5.release()));
        }
        $$ = std::move($1);
    }
    | %empty { 
        std::vector<std::unique_ptr<fin::StructMember>> m;
        $$ = std::make_unique<fin::StructDeclaration>("", std::move(m), false); 
    }
    ;


struct_item_rest:
    /* Member */
    IDENTIFIER LT type GT COMMA {
        $$ = std::make_unique<fin::StructMember>($1, std::move($3), false);
        $$->setLoc(@$);
    }
    | IDENTIFIER LT type GT {
        $$ = std::make_unique<fin::StructMember>($1, std::move($3), false);
        $$->setLoc(@$);
    }
    | IDENTIFIER LT type GT EQUAL expression COMMA {
        auto member = std::make_unique<fin::StructMember>($1, std::move($3), false);
        member->default_value = std::move($6);
        $$ = std::move(member);
        $$->setLoc(@$);
    }
    | IDENTIFIER LT type GT EQUAL expression {
        auto member = std::make_unique<fin::StructMember>($1, std::move($3), false);
        member->default_value = std::move($6);
        $$ = std::move(member);
        $$->setLoc(@$);
    }
    /* Nullable member, `b? <int>` -- tests/samples/nullifier.fin:4 and :11. All
       four spellings above get the `?` form: a grammar where `b? <int>,` parses
       and `b? <int>` as the last member does not is a trap, not a smaller
       change. Written out rather than routed through an optional-`?`
       nonterminal, because an empty-reducible nonterminal after IDENTIFIER
       collides with `primary_no_struct: IDENTIFIER` wherever a member position
       and an expression position share a state. */
    | IDENTIFIER QUESTION LT type GT COMMA {
        $4->is_nullable = true;
        $$ = std::make_unique<fin::StructMember>($1, std::move($4), false);
        $$->setLoc(@$);
    }
    | IDENTIFIER QUESTION LT type GT {
        $4->is_nullable = true;
        $$ = std::make_unique<fin::StructMember>($1, std::move($4), false);
        $$->setLoc(@$);
    }
    | IDENTIFIER QUESTION LT type GT EQUAL expression COMMA {
        $4->is_nullable = true;
        auto member = std::make_unique<fin::StructMember>($1, std::move($4), false);
        member->default_value = std::move($7);
        $$ = std::move(member);
        $$->setLoc(@$);
    }
    | IDENTIFIER QUESTION LT type GT EQUAL expression {
        $4->is_nullable = true;
        auto member = std::make_unique<fin::StructMember>($1, std::move($4), false);
        member->default_value = std::move($7);
        $$ = std::move(member);
        $$->setLoc(@$);
    }
    /* Method */
    | fun_kw IDENTIFIER generic_params_opt LPAREN params RPAREN LT type GT block {
        if ($1) $8->is_nullable = true;
        $$ = std::make_unique<fin::FunctionDeclaration>($2, std::move($5), std::move($8), std::move($10));
        static_cast<fin::FunctionDeclaration*>($$.get())->generic_params = std::move($3);
        $$->setLoc(@$);
    }
    /* Static Method */
    | KW_STATIC fun_kw IDENTIFIER generic_params_opt LPAREN params RPAREN LT type GT block {
        if ($2) $9->is_nullable = true;
        $$ = std::make_unique<fin::FunctionDeclaration>($3, std::move($6), std::move($9), std::move($11));
        auto* func = static_cast<fin::FunctionDeclaration*>($$.get());
        func->generic_params = std::move($4);
        func->is_static = true;
        $$->setLoc(@$);
    }
    /* Operator */
    | KW_OPERATOR operator_symbol operator_generics_opt operator_params_opt implements_opt LT type GT block {
        auto op = std::make_unique<fin::OperatorDeclaration>($2, std::move($4), std::move($7), std::move($9), false);
        op->generic_params = std::move($3);
        op->implements_type = std::move($5);
        $$ = std::move(op);
        $$->setLoc(@$);
    }
    | KW_OPERATOR operator_symbol operator_generics_opt operator_params_opt implements_opt LT type GT SEMICOLON {
        auto op = std::make_unique<fin::OperatorDeclaration>($2, std::move($4), std::move($7), nullptr, false);
        op->generic_params = std::move($3);
        op->implements_type = std::move($5);
        $$ = std::move(op);
        $$->setLoc(@$);
    }
    /* `pub operator[] implements cast<fn(Self, T)>(__get);` --
       tests/samples/stdlib/hashmap.fin:50-51. The operator is implemented by an
       existing function cast to the operator's own signature, so what follows
       `implements` is an expression and there is no `<ReturnType>` at all --
       `implements_opt` accepts only `<Type>`, which is what "expecting LT" was.

       The whole cast is kept in `implements_expr`, which holds both the signature
       and the function being bound; `implements_type` stays null because the type
       is inside the cast. `operator_generics_opt operator_params_opt` are here
       (both empty at these two sites) so that the parser reaches this production
       through the same prefix as the two above and no conflict appears on
       `implements`. */
    | KW_OPERATOR operator_symbol operator_generics_opt operator_params_opt KW_IMPLEMENTS KW_CAST LT type GT LPAREN expression RPAREN SEMICOLON {
        auto cast = std::make_unique<fin::CastExpression>(std::move($8), std::move($11));
        cast->setLoc(@6);
        auto op = std::make_unique<fin::OperatorDeclaration>($2, std::move($4), nullptr, nullptr, false);
        op->generic_params = std::move($3);
        op->implements_expr = std::move(cast);
        $$ = std::move(op);
        $$->setLoc(@$);
    }
    /* Constructor */
    | IDENTIFIER LPAREN params RPAREN block {
        $$ = std::make_unique<fin::ConstructorDeclaration>($1, std::move($3), std::move($5));
        $$->setLoc(@$);
    }
    /* A constructor with a member-initialiser list:
       `Person():age(10), name("Hello") { ... }` -- tests/samples/deeptest2.fin:40,
       whose own comment calls it "c++-like variable assignment in constructor" and
       says it "works best with const declared variables", which is the reason it
       cannot be rewritten as assignments in the body. The initialisers are kept as
       a member name plus its arguments, so a member built from more than one
       argument survives too. */
    | IDENTIFIER LPAREN params RPAREN COLON ctor_inits block {
        auto ctor = std::make_unique<fin::ConstructorDeclaration>($1, std::move($3), std::move($7));
        ctor->member_inits = std::move($6);
        $$ = std::move(ctor);
        $$->setLoc(@$);
    }
    | IDENTIFIER LPAREN params RPAREN LT type GT block {
        $$ = std::make_unique<fin::ConstructorDeclaration>($1, std::move($3), std::move($8), std::move($6));
        $$->setLoc(@$);
    }
    /* Destructor */
    | TILDE IDENTIFIER LPAREN RPAREN block {
        $$ = std::make_unique<fin::DestructorDeclaration>($2, std::move($5));
        $$->setLoc(@$);
    }
    ;

operator_symbol:
    PLUS { $$ = fin::ASTTokenKind::PLUS; }
    | MINUS { $$ = fin::ASTTokenKind::MINUS; }
    | MULT { $$ = fin::ASTTokenKind::MULT; }
    | DIV { $$ = fin::ASTTokenKind::DIV; }
    | MOD { $$ = fin::ASTTokenKind::MOD; }
    | EQEQ { $$ = fin::ASTTokenKind::EQEQ; }
    | NOTEQ { $$ = fin::ASTTokenKind::NOTEQ; }
    | LT { $$ = fin::ASTTokenKind::LT; }
    | GT { $$ = fin::ASTTokenKind::GT; }
    | LTEQ { $$ = fin::ASTTokenKind::LTEQ; }
    | GTEQ { $$ = fin::ASTTokenKind::GTEQ; }
    | AMPERSAND { $$ = fin::ASTTokenKind::AMPERSAND; }
    | AND { $$ = fin::ASTTokenKind::AND; }
    | PIPE { $$ = fin::ASTTokenKind::PIPE; }
    | OR { $$ = fin::ASTTokenKind::OR; }
    | CARET { $$ = fin::ASTTokenKind::CARET; }
    | SHIFTLEFT { $$ = fin::ASTTokenKind::SHIFTLEFT; }
    | GT GT { $$ = fin::ASTTokenKind::SHIFTRIGHT; } /* `operator >>` -- two GTs, see lexer.l */
    /* Compound assignment operators. tests/samples/stdlib/operators.fin:41
       declares `pub operator +=(rhs: any) <Output>;` and the interface it belongs
       to is what makes `a += b` overloadable at all; `>>=` and `<<=` were already
       here, so their four arithmetic siblings were the omission. */
    | PLUSEQUAL { $$ = fin::ASTTokenKind::PLUSEQUAL; }
    | MINUSEQUAL { $$ = fin::ASTTokenKind::MINUSEQUAL; }
    | MULTEQUAL { $$ = fin::ASTTokenKind::MULTEQUAL; }
    | DIVEQUAL { $$ = fin::ASTTokenKind::DIVEQUAL; }
    | SHIFTLEFTEQUAL { $$ = fin::ASTTokenKind::SHIFTLEFTEQUAL; }
    | SHIFTRIGHTEQUAL { $$ = fin::ASTTokenKind::SHIFTRIGHTEQUAL; }
    | NOT { $$ = fin::ASTTokenKind::NOT; }
    | EQUAL { $$ = fin::ASTTokenKind::EQUAL; }
    | LBRACKET RBRACKET { $$ = fin::ASTTokenKind::INDEX; }
    | LBRACKET RBRACKET EQUAL { $$ = fin::ASTTokenKind::INDEX_ASSIGN; }
    /* `%=`, `&=` and `|=` are two tokens each because the lexer has no single
       token for them -- nothing in an expression uses them -- so they are spelled
       out here the way `operator >>` and `operator []=` already are.
       tests/samples/stdlib/operators.fin:71,77,83. */
    | MOD EQUAL { $$ = fin::ASTTokenKind::MODEQUAL; }
    | AMPERSAND EQUAL { $$ = fin::ASTTokenKind::AMPERSANDEQUAL; }
    | PIPE EQUAL { $$ = fin::ASTTokenKind::PIPEEQUAL; }
    | BACKTICK MULT { $$ = fin::ASTTokenKind::DEREF; }
    | BACKTICK MINUS { $$ = fin::ASTTokenKind::UNARY_MINUS; }
    | LPAREN ELLIPSIS IDENTIFIER RPAREN { $$ = fin::ASTTokenKind::VARIADIC_CALL; }
    /* The same call operator with the variadic's element type spelled out:
       `pub operator (...args: <[any]>) <Output>;` -- stdlib/operators.fin:134.
       Both the name and the type are dropped, because `operator_symbol` yields a
       token kind and the operator's own `operator_params_opt` is empty here;
       the bare form above already drops the name, so this loses nothing that was
       being kept. Representing the parameter needs the four operator
       declaration productions to take it, which is a wider change than parsing. */
    | LPAREN ELLIPSIS IDENTIFIER COLON LT type GT RPAREN { $$ = fin::ASTTokenKind::VARIADIC_CALL; }
    ;

operator_generics_opt:
    COLON LT generic_param_list GT { $$ = std::move($3); }
    | %empty { $$ = std::vector<std::unique_ptr<fin::GenericParam>>(); }
    ;

implements_opt:
      KW_IMPLEMENTS LT type GT { $$ = std::move($3); }
    | %empty { $$ = nullptr; }
    ;

/* --- INTERFACES --- */

interface_body_content:
    interface_body_content attributes_opt visibility_label {
        /* `attributes_opt` is here only so that a KW_PUB lookahead has one
           action and not two: without it the parser must choose between
           reducing the empty attribute list and shifting KW_PUB, a token
           before the COLON says which of `pub:` and `pub fun` this is.
           An attribute written before a label decorates nothing, so it is
           dropped rather than carried to the next item. */
        $1->label_public = $3;
        $$ = std::move($1);
    }
    | interface_body_content attributes_opt member_visibility readonly_opt interface_item_rest {
        const bool vis = $3 < 0 ? $1->label_public : ($3 == 1);
        if (auto* member = dynamic_cast<fin::StructMember*>($5.get())) {
            member->attributes = std::move($2);
            member->is_public = vis;
            member->is_readonly = $4;
            $1->members.push_back(std::unique_ptr<fin::StructMember>(static_cast<fin::StructMember*>($5.release())));
        }
        else if (auto* func = dynamic_cast<fin::FunctionDeclaration*>($5.get())) {
            func->attributes = std::move($2);
            func->is_public = vis;
            $1->methods.push_back(std::unique_ptr<fin::FunctionDeclaration>(static_cast<fin::FunctionDeclaration*>($5.release())));
        }
        else if (auto* op = dynamic_cast<fin::OperatorDeclaration*>($5.get())) {
            op->is_public = vis;
            $1->operators.push_back(std::unique_ptr<fin::OperatorDeclaration>(static_cast<fin::OperatorDeclaration*>($5.release())));
        }
        else if (auto* ctor = dynamic_cast<fin::ConstructorDeclaration*>($5.get())) {
            $1->constructors.push_back(std::unique_ptr<fin::ConstructorDeclaration>(static_cast<fin::ConstructorDeclaration*>($5.release())));
        }
        else if (auto* dtor = dynamic_cast<fin::DestructorDeclaration*>($5.get())) {
            $1->destructor = std::unique_ptr<fin::DestructorDeclaration>(static_cast<fin::DestructorDeclaration*>($5.release()));
        }
        $$ = std::move($1);
    }
    | %empty { 
        std::vector<std::unique_ptr<fin::StructMember>> m;
        std::vector<std::unique_ptr<fin::FunctionDeclaration>> f;
        std::vector<std::unique_ptr<fin::OperatorDeclaration>> o;
        std::vector<std::unique_ptr<fin::ConstructorDeclaration>> c;
        std::unique_ptr<fin::DestructorDeclaration> d = nullptr;
        $$ = std::make_unique<fin::InterfaceDeclaration>("", std::move(m), std::move(f), std::move(o), std::move(c), std::move(d), false); 
    }
    ;

interface_item_rest:
    /* Field: name <type>; */
    IDENTIFIER LT type GT SEMICOLON {
        $$ = std::make_unique<fin::StructMember>($1, std::move($3), false);
        $$->setLoc(@$);
    }
    /* Nullable field: `name? <type>;` */
    | IDENTIFIER QUESTION LT type GT SEMICOLON {
        $4->is_nullable = true;
        $$ = std::make_unique<fin::StructMember>($1, std::move($4), false);
        $$->setLoc(@$);
    }
    /* The same two fields written with a comma, the way a struct body writes them:
       `interface _PlayerStructLike { pub health <uint>, pub damage <uint>, }` --
       tests/samples/literal_struct.fin:10-13, which describes the shape of a struct
       and so is written like one. Both terminators are accepted rather than one
       replacing the other; every other interface in the corpus uses the semicolon. */
    | IDENTIFIER LT type GT COMMA {
        $$ = std::make_unique<fin::StructMember>($1, std::move($3), false);
        $$->setLoc(@$);
    }
    | IDENTIFIER QUESTION LT type GT COMMA {
        $4->is_nullable = true;
        $$ = std::make_unique<fin::StructMember>($1, std::move($4), false);
        $$->setLoc(@$);
    }
    /* Method: fun ... */
    | declaration_body { $$ = std::move($1); }
    
    /* Abstract Operator */
    | KW_OPERATOR operator_symbol operator_generics_opt operator_params_opt LT type GT SEMICOLON {
        auto op = std::make_unique<fin::OperatorDeclaration>($2, std::move($4), std::move($6), nullptr, false);
        op->generic_params = std::move($3);
        $$ = std::move(op);
        $$->setLoc(@$);
    }
    
    /* Abstract Constructor: Self(...); */
    | KW_SELF_TYPE LPAREN params RPAREN SEMICOLON {
        $$ = std::make_unique<fin::ConstructorDeclaration>("Self", std::move($3), nullptr);
        $$->setLoc(@$);
    }
    
    /* Abstract Destructor: ~Self(); */
    | TILDE KW_SELF_TYPE LPAREN RPAREN SEMICOLON {
        $$ = std::make_unique<fin::DestructorDeclaration>("Self", nullptr);
        $$->setLoc(@$);
    }
    ;

/* --- PARAMETERS --- */

/* `fun` or `fun?`. The `?` says the function returns its declared type OR null
   (tests/samples/nullifier.fin:6, :16 and undefined_behavior.fin:9), so it sets
   `is_nullable` on the return type. A nonterminal rather than a second copy of
   every function production: there are six of those, and duplicating each one to
   carry a flag is six chances to get the action wrong. */
fun_kw:
    KW_FUN { $$ = false; }
    | KW_FUN QUESTION { $$ = true; }
    ;

params:
    param_list { $$ = std::move($1); }
    | %empty { $$ = std::vector<std::unique_ptr<fin::Parameter>>(); }
    ;

param_list:
    param_list COMMA param { $1.push_back(std::move($3)); $$ = std::move($1); }
    | param { std::vector<std::unique_ptr<fin::Parameter>> v; v.push_back(std::move($1)); $$ = std::move(v); }
    ;

param:
    IDENTIFIER COLON type {
        $$ = std::make_unique<fin::Parameter>($1, std::move($3), nullptr, false);
        $$->setLoc(@$);
    }
    /* The bracketed form, `name: <type>`. Not an `@define` peculiarity, which is
       how docs/baseline.md group G reads it: tests/samples/operators.fin:4 writes
       `@define printf(fmt: <string>, ...)` and :134 writes
       `pub operator (...args: <[any]>)`, so it is an alternative spelling of a
       parameter type wherever one appears. Both spellings are in the corpus --
       fourteen `@define` lines use the bare form -- so both are accepted and
       neither is preferred here. Unambiguous because no `type` can begin with
       `<`. */
    | IDENTIFIER COLON LT type GT {
        $$ = std::make_unique<fin::Parameter>($1, std::move($4), nullptr, false);
        $$->setLoc(@$);
    }
    | ELLIPSIS IDENTIFIER COLON type {
        $$ = std::make_unique<fin::Parameter>($2, std::move($4), nullptr, true);
        $$->setLoc(@$);
    }
    | ELLIPSIS IDENTIFIER COLON LT type GT {
        $$ = std::make_unique<fin::Parameter>($2, std::move($5), nullptr, true);
        $$->setLoc(@$);
    }
    /* Nullable parameter, `n?: int` -- tests/samples/nullifier.fin:16. The `?`
       binds to the name, so it is read before the colon; both type spellings get
       the form for the same reason the member spellings all do. */
    | IDENTIFIER QUESTION COLON type {
        $4->is_nullable = true;
        $$ = std::make_unique<fin::Parameter>($1, std::move($4), nullptr, false);
        $$->setLoc(@$);
    }
    | IDENTIFIER QUESTION COLON LT type GT {
        $5->is_nullable = true;
        $$ = std::make_unique<fin::Parameter>($1, std::move($5), nullptr, false);
        $$->setLoc(@$);
    }
    /* A const parameter, `fun test(const a: int)` -- tests/samples/const.fin:11,
       :24 and :37, which is what that whole sample is about. Absent from wave 2's
       list in docs/plan.md; found because it is const.fin's first error.

       Recorded on the type as readonly-ness of the binding. `Parameter` has no
       mutability field and adding one would have to be read by the same pass that
       already refuses assignment to an immutable local, so this marks the type
       and leaves the enforcement to it. */
    | KW_CONST IDENTIFIER COLON type {
        $4->is_const = true;
        $$ = std::make_unique<fin::Parameter>($2, std::move($4), nullptr, false);
        $$->setLoc(@$);
    }
    | KW_CONST IDENTIFIER COLON LT type GT {
        $5->is_const = true;
        $$ = std::make_unique<fin::Parameter>($2, std::move($5), nullptr, false);
        $$->setLoc(@$);
    }
    /* By-reference on the name rather than on the type: `const &arr: [any]` --
       tests/samples/stdlib/types.fin:102, the corpus's only site. `array: &[T]`
       (arrays.fin:11) is the other spelling and puts the `&` on the type, so the
       two mean the same thing and this builds the same PointerTypeNode. Only the
       `const` form is accepted because only it appears; a bare `&name:` can be
       added the day a sample writes one. */
    | KW_CONST AMPERSAND IDENTIFIER COLON type {
        auto ptr = std::make_unique<fin::PointerTypeNode>(std::move($5));
        ptr->setLoc(@5);
        ptr->is_const = true;
        $$ = std::make_unique<fin::Parameter>($3, std::move(ptr), nullptr, false);
        $$->setLoc(@$);
    }
    /* A default parameter value, `err_code: int = null` --
       tests/samples/stdlib/error.fin:11 and stdlib/stdio.fin:109
       (`nbytes: ulong = -1`). `Parameter::default_value` already existed and
       every construction passed nullptr into it; only the syntax was missing. */
    | IDENTIFIER COLON type EQUAL expression {
        $$ = std::make_unique<fin::Parameter>($1, std::move($3), std::move($5), false);
        $$->setLoc(@$);
    }
    | IDENTIFIER COLON LT type GT EQUAL expression {
        $$ = std::make_unique<fin::Parameter>($1, std::move($4), std::move($7), false);
        $$->setLoc(@$);
    }
    ;

/* --- SUPER EXPRESSIONS --- */

super_expression:
    /* super { ... } */
    KW_SUPER LBRACE field_assignments RBRACE {
        $$ = std::make_unique<fin::SuperExpression>(std::move($3));
        $$->setLoc(@$);
    }
    /* super::Parent { ... } */
    | KW_SUPER DOUBLE_COLON IDENTIFIER LBRACE field_assignments RBRACE {
        $$ = std::make_unique<fin::SuperExpression>($3, std::move($5));
        $$->setLoc(@$);
    }
    /* super::Parent(...) */
    | KW_SUPER DOUBLE_COLON IDENTIFIER LPAREN arguments RPAREN {
        $$ = std::make_unique<fin::SuperExpression>($3, std::move($5));
        $$->setLoc(@$);
    }
    /* `super::<Person>::name` -- tests/samples/deeptest2.fin:71-73, whose comment
       reads "this is how we access fields and functions of parent class using
       `super::<ParentClass>::Member`". The parent is named as a generic argument
       here, unlike the three forms above which name it directly, so assigning to an
       inherited field was a syntax error: "unexpected LT, expecting IDENTIFIER".

       The result is a MemberAccess whose object is the `super::<Person>` qualifier,
       which is what makes line 73's `super::<Person>::get_age()` parse with no
       further production: the postfix-call rule in `expression` already turns a
       MemberAccess into a MethodCall, and it keeps the object, so the parent
       survives the conversion. `is_qualifier` marks a SuperExpression that is
       standing in for a type name rather than calling the parent's constructor. */
    | KW_SUPER DOUBLE_COLON LT type_list GT DOUBLE_COLON IDENTIFIER {
        std::string parent = $4.empty() ? std::string() : $4[0]->name;
        auto sup = std::make_unique<fin::SuperExpression>(parent, std::vector<std::unique_ptr<fin::Expression>>());
        sup->is_qualifier = true;
        sup->parent_generics = std::move($4);
        sup->setLoc(@1);
        auto access = std::make_unique<fin::MemberAccess>(std::move(sup), $7);
        access->is_static = true;
        $$ = std::move(access);
        $$->setLoc(@$);
    }
    /* super(...) */
    | KW_SUPER LPAREN arguments RPAREN {
        $$ = std::make_unique<fin::SuperExpression>("", std::move($3));
        $$->setLoc(@$);
    }
    ;

/* --- EXTERN / DEFINE --- */

define_declaration:
    AT KW_DEFINE IDENTIFIER LPAREN extern_params RPAREN LT type GT SEMICOLON {
        $$ = std::make_unique<fin::DefineDeclaration>($3, std::move($5.first), std::move($8), $5.second);
        $$->setLoc(@$);
    }
    ;

extern_params:
    param_list COMMA ELLIPSIS { $$ = std::make_pair(std::move($1), true); }
    | param_list { $$ = std::make_pair(std::move($1), false); }
    | ELLIPSIS { $$ = std::make_pair(std::vector<std::unique_ptr<fin::Parameter>>(), true); }
    | %empty { $$ = std::make_pair(std::vector<std::unique_ptr<fin::Parameter>>(), false); }
    ;

/* --- MACROS --- */

macro_declaration:
    AT KW_MACRO IDENTIFIER LPAREN macro_param_list RPAREN block {
        $$ = std::make_unique<fin::MacroDeclaration>($3, std::move($5), std::move($7));
        $$->setLoc(@$);
    }
    | KW_MACRO IDENTIFIER LBRACE macro_rules RBRACE {
        $$ = std::make_unique<fin::MacroDeclaration>($2, std::move($4));
        $$->setLoc(@$);
    }
    ;

macro_rules:
    macro_rules macro_rule { $1.push_back(std::move($2)); $$ = std::move($1); }
    | macro_rule { std::vector<fin::MacroRule> v; v.push_back(std::move($1)); $$ = std::move(v); }
    ;

macro_rule:
    LPAREN IDENTIFIER RPAREN ARROW block { 
        $$ = fin::MacroRule{$2, std::move($5)}; 
    }
    | LPAREN STRING_LITERAL RPAREN ARROW block { 
        $$ = fin::MacroRule{$2, std::move($5)}; 
    }
    | LPAREN RPAREN ARROW block {
        $$ = fin::MacroRule{"", std::move($4)};
    }
    ;

macro_param_list:
    macro_param_list COMMA macro_param { $1.push_back($3); $$ = std::move($1); }
    | macro_param { std::vector<fin::MacroParam> v; v.push_back($1); $$ = std::move(v); }
    | %empty { $$ = std::vector<fin::MacroParam>(); }
    ;

macro_param:
    /* Case: a */
    IDENTIFIER { 
        $$ = fin::MacroParam{$1, "expr", false}; 
    }
    /* Case: a... */
    | IDENTIFIER ELLIPSIS { 
        $$ = fin::MacroParam{$1, "expr", true}; 
    }
    /* Case: a: expr */
    | IDENTIFIER COLON IDENTIFIER { 
        $$ = fin::MacroParam{$1, $3, false}; 
    }
    /* Case: a: expr... */
    | IDENTIFIER COLON IDENTIFIER ELLIPSIS { 
        $$ = fin::MacroParam{$1, $3, true}; 
    }
    ;

/* --- TYPE DEFINITIONS --- */

type_definition:
    /* Generic alias: type Array<T> = <[T]>;  (Also covers simple alias since generic_params_opt can be empty) */
    KW_TYPE IDENTIFIER generic_params_opt EQUAL LT type GT SEMICOLON {
        auto td = std::make_unique<fin::TypeDefinition>($2, std::move($6));
        td->generic_params = std::move($3);
        $$ = std::move(td);
        $$->setLoc(@$);
    }
    /* Bare alias: `type IntArray = [int];`. tests/samples/arrays.fin:4-7 writes
       four of these -- an array, a pointer to an array and an array of pointers
       -- and tests/samples/stdlib/typing.fin, memory.fin and operators.fin each
       open with one. The bracketed `= <type>;` above stays: fourteen corpus lines
       use it, so both spellings are accepted and neither is preferred. No
       ambiguity between them because no `type` begins with `<`. */
    | KW_TYPE IDENTIFIER generic_params_opt EQUAL type SEMICOLON {
        auto td = std::make_unique<fin::TypeDefinition>($2, std::move($5));
        td->generic_params = std::move($3);
        $$ = std::move(td);
        $$->setLoc(@$);
    }
    /* Union alias: `type Number = int | uint | float | ...;` --
       tests/samples/arrays.fin:9, stdlib/types.fin:53 and stdlib/typing.fin:10,
       which is the first error in all three. `aliased_type` keeps holding the
       first alternative so that every consumer of a plain alias keeps working,
       and `union_members` holds the alternatives after it.

       typing.fin:10 ends its alternation with `any implements <Error>`, so an
       alternative can be a constraint set and not just a type. That spelling is
       accepted only after a `|`, because `= any implements <...>` in first
       position is the production below and having both would be ambiguous. */
    | KW_TYPE IDENTIFIER generic_params_opt EQUAL type union_tail SEMICOLON {
        auto td = std::make_unique<fin::TypeDefinition>($2, std::move($5));
        td->generic_params = std::move($3);
        td->union_members = std::move($6);
        $$ = std::move(td);
        $$->setLoc(@$);
    }
    /* An alias whose name is a built-in type word: `pub type string = [char];`
       -- tests/samples/stdlib/types.fin:59, commented there as "overwrite default
       type". The standard library defines what `string` means, so the name on the
       left is TYPE_STRING and not an IDENTIFIER, and the alias would not parse.
       Only the bare `= type;` spelling is widened, which is the only one the
       corpus uses for this. */
    | KW_TYPE primitive_type generic_params_opt EQUAL type SEMICOLON {
        auto td = std::make_unique<fin::TypeDefinition>($2, std::move($5));
        td->generic_params = std::move($3);
        $$ = std::move(td);
        $$->setLoc(@$);
    }
    /* `extern myns::myfunc as myfunc;` -- a second name for an existing symbol,
       the subject of tests/samples/extern_as.fin (lines 9, 19, 23, 32, 39) and how
       enums.fin:19-20 shortens `Result::Ok` to `Ok`. A TypeDefinition carrying
       `is_extern_alias`, for the reason the symbol resolution below is one: the
       shape is a new name bound to an existing one, and a Statement class of its
       own needs src/ast/Visitor.hpp. `aliased_type` holds the left-hand path and
       `name` the new name, so nothing about either side is lost.

       `extern int as Integer;` (line 23) needs the second production because a
       built-in type word is its own token, not an IDENTIFIER -- the file's own
       comment calls this spelling legal but discouraged. */
    | KW_EXTERN extern_path KW_AS IDENTIFIER SEMICOLON {
        auto rhs = std::make_unique<fin::TypeNode>($2);
        rhs->setLoc(@2);
        auto td = std::make_unique<fin::TypeDefinition>($4, std::move(rhs));
        td->is_extern_alias = true;
        $$ = std::move(td);
        $$->setLoc(@$);
    }
    | KW_EXTERN primitive_type KW_AS IDENTIFIER SEMICOLON {
        auto rhs = std::make_unique<fin::TypeNode>($2);
        rhs->setLoc(@2);
        auto td = std::make_unique<fin::TypeDefinition>($4, std::move(rhs));
        td->is_extern_alias = true;
        $$ = std::move(td);
        $$->setLoc(@$);
    }
    /* `extern * from a_namespace;` -- tests/samples/extern_as.fin:32 and :39, which
       take every name out of a namespace and out of an enum. Not an
       ImportDeclaration: the right-hand side is a namespace or an enum in this
       file, and routing it through the import machinery would send the module
       loader looking for a file of that name. `name` is `*` and `is_extern_wildcard`
       says which of the two forms this is. */
    | KW_EXTERN MULT KW_FROM extern_path SEMICOLON {
        auto rhs = std::make_unique<fin::TypeNode>($4);
        rhs->setLoc(@4);
        auto td = std::make_unique<fin::TypeDefinition>("*", std::move(rhs));
        td->is_extern_alias = true;
        td->is_extern_wildcard = true;
        $$ = std::move(td);
        $$->setLoc(@$);
    }
    /* Symbol resolution: `pub implements c_printf = printf;` --
       tests/samples/stdlib/stdio.fin:15, which is how the standard library binds
       the name it exports to the extern it declared two lines above. A
       TypeDefinition carrying `is_symbol_resolution`: the shape is exactly a name
       bound to another name, and the alternative is a new Statement class, which
       needs src/ast/Visitor.hpp. Sitting in `type_definition` is also what gives
       it `pub` and the `#[export]` above it for free. */
    | KW_IMPLEMENTS IDENTIFIER EQUAL IDENTIFIER SEMICOLON {
        auto rhs = std::make_unique<fin::TypeNode>($4);
        rhs->setLoc(@4);
        auto td = std::make_unique<fin::TypeDefinition>($2, std::move(rhs));
        td->is_symbol_resolution = true;
        $$ = std::move(td);
        $$->setLoc(@$);
    }
    /* The same constraint set on a named type rather than on the erasure marker:
       `type PlayerStructLike = Struct implements <_PlayerStructLike>;` --
       tests/samples/literal_struct.fin:15, which names the meta-type `Struct` and
       then constrains it. Only `any implements <...>` was accepted, so a constrained
       meta-type was a syntax error. `aliased_type` holds the type that was written
       instead of an invented `any`, and the constraint list goes where the `any`
       form's does, so a pass that already reads `implements_list` needs no change.

       This replaces the `= any implements <...>` production that used to sit here:
       `any` is itself a type (`base_type: KW_ANY` yields the same TypeNode("any")
       this builds), so keeping both meant the parser had to choose between shifting
       KW_IMPLEMENTS and reducing `any` to a type with nothing to tell them apart --
       one shift/reduce conflict, silently resolved. `type Any<...> = any
       implements <...>;` (stdlib/types.fin:74) still parses, through this rule. */
    | KW_TYPE IDENTIFIER generic_params_opt EQUAL type KW_IMPLEMENTS LT type_list GT SEMICOLON {
        auto td = std::make_unique<fin::TypeDefinition>($2, std::move($5));
        td->generic_params = std::move($3);
        td->has_implements = true;
        td->implements_list = std::move($8);
        $$ = std::move(td);
        $$->setLoc(@$);
    }
    ;

/* --- IMPLEMENTS BLOCK (Trait Implementation) --- */

implements_block:
    /* MyStruct implements <MyInterface> { ... } */
    IDENTIFIER KW_IMPLEMENTS LT type GT LBRACE implements_body_content RBRACE {
        $7->target_type = $1;
        $7->interface_type = std::move($4);
        $$ = std::move($7);
        $$->setLoc(@$);
    }
    /* A generic target: `Result<T, U> implements <IResult> { ... }` --
       tests/samples/stdlib/typing.fin:27, stdlib/stdio.fin:54 and
       stdlib/collection.fin. The target's own arguments have to be named because
       the methods inside use them (`fun unwrap(enum_: Result<T, U>) <T>`). */
    /* The single-member overwrite: `@implements Result<T, E>::unwrap = fun(...) {...}`
       (tests/samples/enums.fin:25) and `@implements(pub) Collection<T>::push_back =
       (...) <noret> => {}` (tests/samples/stdlib/collection.fin:97). Neither site
       ends in a semicolon, so none is accepted here, and the right-hand side is a
       block-bodied lambda -- `overwrite_body` rather than `expression`. It has to
       end in a brace: with nothing terminating the production, whatever ends it is
       followed by whatever can follow a statement, and that includes LBRACE (a bare
       `{ }` scope, ADR 0011), which is also how `new [T, n] { ... }` continues. Both
       corpus sites are block lambdas, so the restriction costs the corpus nothing
       and it is the difference between a grammar with no conflicts and one whose
       ambiguity is only hidden by a precedence declaration. */
    | AT KW_IMPLEMENTS IDENTIFIER LT type_list GT DOUBLE_COLON IDENTIFIER EQUAL overwrite_body {
        auto blk = std::make_unique<fin::ImplementsBlock>($3, nullptr);
        blk->target_generics = std::move($5);
        blk->is_overwriter = true;
        blk->overwrite_member = $8;
        blk->overwrite_value = std::move($10);
        $$ = std::move(blk);
        $$->setLoc(@$);
    }
    | AT KW_IMPLEMENTS implements_visibility IDENTIFIER LT type_list GT DOUBLE_COLON IDENTIFIER EQUAL overwrite_body {
        auto blk = std::make_unique<fin::ImplementsBlock>($4, nullptr);
        blk->target_generics = std::move($6);
        blk->is_overwriter = true;
        blk->overwrite_member = $9;
        blk->overwrite_value = std::move($11);
        blk->overwrite_public = $3;
        $$ = std::move(blk);
        $$->setLoc(@$);
    }
    /* The overwriter: `@implements Collection<T> { ... }` --
       tests/samples/stdlib/collection.fin:93, whose comment reads "overwrites or
       adds methods/operators". It names no interface: the body's methods are
       added to the target type or replace its own, so `interface_type` is null and
       `is_overwriter` says which of the two forms this is. */
    | AT KW_IMPLEMENTS IDENTIFIER LT type_list GT LBRACE implements_body_content RBRACE {
        $8->target_type = $3;
        $8->target_generics = std::move($5);
        $8->is_overwriter = true;
        $$ = std::move($8);
        $$->setLoc(@$);
    }
    | IDENTIFIER LT type_list GT KW_IMPLEMENTS LT type GT LBRACE implements_body_content RBRACE {
        $10->target_type = $1;
        $10->target_generics = std::move($3);
        $10->interface_type = std::move($7);
        $$ = std::move($10);
        $$->setLoc(@$);
    }
    ;

/* The right-hand side of a single-member overwrite: the two block-bodied lambda
   spellings, `fun(...) <T> { ... }` (tests/samples/enums.fin:25) and
   `(...) <noret> => { ... }` (tests/samples/stdlib/collection.fin:97). See the
   comment on the productions that use it for why it cannot simply be `expression`. */
overwrite_body:
    KW_FUN LPAREN params RPAREN LT type GT block {
        $$ = std::make_unique<fin::LambdaExpression>(std::move($3), std::move($6), std::move($8));
        $$->setLoc(@$);
    }
    | LPAREN params RPAREN LT type GT ARROW block {
        $$ = std::make_unique<fin::LambdaExpression>(std::move($2), std::move($5), std::move($8));
        $$->setLoc(@$);
    }
    ;

/* The parenthesised visibility of a single-member overwrite: `@implements(pub)`
   at tests/samples/stdlib/collection.fin:97. Spelled out as two alternatives
   instead of reusing `visibility_opt` because that one can be empty, and an empty
   reduce after `@implements (` would collide with the empty `arguments` of the
   `@implements(...)` call expression. */
implements_visibility:
    LPAREN KW_PUB RPAREN { $$ = true; }
    | LPAREN KW_PRIV RPAREN { $$ = false; }
    ;

implements_body_content:
    /* A `pub:` / `priv:` label inside an implements body, exactly as in a struct
       or interface body: tests/samples/stdlib/stdio.fin:55 and
       stdlib/typing.fin:28. Routed through `attributes_opt` for the same reason
       the struct one is -- the label and an item share their prefix, and the
       parser has to decide one token before the colon. */
    implements_body_content attributes_opt visibility_label {
        $1->label_public = $3;
        $$ = std::move($1);
    }
    | implements_body_content attributes_opt member_visibility implements_item_rest {
        const bool vis = $3 < 0 ? $1->label_public : ($3 == 1);
        if (auto* func = dynamic_cast<fin::FunctionDeclaration*>($4.get())) {
            func->attributes = std::move($2);
            func->is_public = vis;
            $1->methods.push_back(std::unique_ptr<fin::FunctionDeclaration>(static_cast<fin::FunctionDeclaration*>($4.release())));
        }
        else if (auto* ctor = dynamic_cast<fin::ConstructorDeclaration*>($4.get())) {
            $1->constructors.push_back(std::unique_ptr<fin::ConstructorDeclaration>(static_cast<fin::ConstructorDeclaration*>($4.release())));
        }
        else if (auto* op = dynamic_cast<fin::OperatorDeclaration*>($4.get())) {
            op->is_public = vis;
            $1->operators.push_back(std::unique_ptr<fin::OperatorDeclaration>(static_cast<fin::OperatorDeclaration*>($4.release())));
        }
        $$ = std::move($1);
    }
    | %empty { 
        $$ = std::make_unique<fin::ImplementsBlock>("", nullptr);
    }
    ;

implements_item_rest:
    /* Method */
    fun_kw IDENTIFIER generic_params_opt LPAREN params RPAREN LT type GT block {
        if ($1) $8->is_nullable = true;
        $$ = std::make_unique<fin::FunctionDeclaration>($2, std::move($5), std::move($8), std::move($10));
        static_cast<fin::FunctionDeclaration*>($$.get())->generic_params = std::move($3);
        $$->setLoc(@$);
    }
    /* Static Method */
    | KW_STATIC fun_kw IDENTIFIER generic_params_opt LPAREN params RPAREN LT type GT block {
        if ($2) $9->is_nullable = true;
        $$ = std::make_unique<fin::FunctionDeclaration>($3, std::move($6), std::move($9), std::move($11));
        auto* func = static_cast<fin::FunctionDeclaration*>($$.get());
        func->generic_params = std::move($4);
        func->is_static = true;
        $$->setLoc(@$);
    }
    /* Operator */
    | KW_OPERATOR operator_symbol operator_generics_opt operator_params_opt LT type GT block {
        auto op = std::make_unique<fin::OperatorDeclaration>($2, std::move($4), std::move($6), std::move($8), false);
        op->generic_params = std::move($3);
        $$ = std::move(op);
        $$->setLoc(@$);
    }
    /* Constructor -- the same two spellings a struct body accepts.
       `Collection() { ... }` at tests/samples/stdlib/collection.fin:104 is how the
       block satisfies the `Self();` its interface declares (line 99), and an
       implements body that takes only methods and operators had no way to say it:
       "unexpected IDENTIFIER, expecting KW_FUN or KW_STATIC or KW_OPERATOR". */
    | IDENTIFIER LPAREN params RPAREN block {
        $$ = std::make_unique<fin::ConstructorDeclaration>($1, std::move($3), std::move($5));
        $$->setLoc(@$);
    }
    | IDENTIFIER LPAREN params RPAREN LT type GT block {
        $$ = std::make_unique<fin::ConstructorDeclaration>($1, std::move($3), std::move($8), std::move($6));
        $$->setLoc(@$);
    }
    ;

/* --- SPECIAL (COMPILE-TIME) FUNCTIONS --- */

special_declaration:
    /* @special name(...) <type> { ... } */
    AT KW_SPECIAL IDENTIFIER LPAREN params RPAREN LT type GT block {
        $$ = std::make_unique<fin::SpecialDeclaration>($3, std::move($5), std::move($8), std::move($10));
        $$->setLoc(@$);
    }
    /* `@special(pub) typeid(T: $type) <int> { ... }` -- the standard library's
       spelling, and the first error in three of its files:
       stdlib/types.fin:24, stdlib/error.fin:26 and stdlib/enums.fin:12. The
       visibility rides in parentheses on the header instead of a leading `pub`.
       `visibility_opt` rather than a required KW_PUB/KW_PRIV so that `@special()`
       is not a second spelling to keep working. */
    | AT KW_SPECIAL LPAREN visibility_opt RPAREN IDENTIFIER LPAREN params RPAREN LT type GT block {
        auto sd = std::make_unique<fin::SpecialDeclaration>($6, std::move($8), std::move($11), std::move($13));
        sd->is_public = $4;
        $$ = std::move(sd);
        $$->setLoc(@$);
    }
    ;

/* --- VARIABLES --- */

variable_declaration:
    KW_LET IDENTIFIER LT type GT EQUAL expression SEMICOLON {
        $$ = std::make_unique<fin::VariableDeclaration>(true, $2, std::move($4), std::move($7));
        $$->setLoc(@$);
    }
    | KW_CONST IDENTIFIER LT type GT EQUAL expression SEMICOLON {
        $$ = std::make_unique<fin::VariableDeclaration>(false, $2, std::move($4), std::move($7));
        $$->setLoc(@$);
    }
    | KW_LET IDENTIFIER LT type GT SEMICOLON {
        $$ = std::make_unique<fin::VariableDeclaration>(true, $2, std::move($4), nullptr);
        $$->setLoc(@$);
    }
    /* Nullable variable, `let x? <A>` -- tests/samples/nullifier.fin:27, :34, :39.
       `_` is an ordinary IDENTIFIER to the lexer, so `let _? <int>` needs no
       separate form. */
    | KW_LET IDENTIFIER QUESTION LT type GT EQUAL expression SEMICOLON {
        $5->is_nullable = true;
        $$ = std::make_unique<fin::VariableDeclaration>(true, $2, std::move($5), std::move($8));
        $$->setLoc(@$);
    }
    | KW_LET IDENTIFIER QUESTION LT type GT SEMICOLON {
        $5->is_nullable = true;
        $$ = std::make_unique<fin::VariableDeclaration>(true, $2, std::move($5), nullptr);
        $$->setLoc(@$);
    }
    | KW_CONST IDENTIFIER QUESTION LT type GT EQUAL expression SEMICOLON {
        $5->is_nullable = true;
        $$ = std::make_unique<fin::VariableDeclaration>(false, $2, std::move($5), std::move($8));
        $$->setLoc(@$);
    }
    ;

/* --- TYPES --- */



type:
    type_no_annot { $$ = std::move($1); }
    | base_type LBRACE expression_list RBRACE %prec LBRACE {
        $$ = std::move($1);
        $$->annotations = std::move($3);
    }
    | ELLIPSIS { $$ = std::make_unique<fin::TypeNode>("..."); $$->setLoc(@$); }
    ;

type_no_annot:
    base_type %prec TYPE_ANNOT_PREC { $$ = std::move($1); }
    | pointer_type { $$ = std::move($1); }
    | array_type { $$ = std::move($1); }
    ;

/* Every production here sets a location, and the ones that did not are why:
   SemanticAnalyzer reports `Undefined type 'X'` at the TypeNode's own `loc`
   (Analyzer_Core.cpp:204), so a TypeNode built without one sent all 99 of the
   corpus's `Undefined type` diagnostics -- the largest single error class in it --
   to line 1, column 1. Line 1 of a sample is its `//@` expectation comment, so
   the effect was that the diagnostic pointed at the sentence describing it.
   Held by Soundness_DiagnosticLocation in tests/test_soundness.cpp, which names
   the production behind each case, and corpus-wide by
   Soundness_DiagnosticAttribution in tests/test_cli.cpp, which fails if any
   diagnostic anywhere lands on a `//@` line again. Removing the setLoc below from
   `IDENTIFIER` alone fails both. `fn_type` and `LPAREN type RPAREN`
   below are pass-throughs and keep the inner type's location on purpose: it is
   the type that failed to resolve, not the parentheses around it. */
base_type:
    primitive_type { $$ = std::make_unique<fin::TypeNode>($1); $$->setLoc(@$); }
    | IDENTIFIER { $$ = std::make_unique<fin::TypeNode>($1); $$->setLoc(@$); }
    | IDENTIFIER LT type_list GT %prec TYPE_PREC { 
        $$ = std::make_unique<fin::TypeNode>($1); 
        $$->generics = std::move($3);
        $$->setLoc(@$);
    }
    | LBRACE type_list RBRACE { 
        $$ = std::make_unique<fin::TypeNode>("prototype"); 
        $$->is_prototype = true;
        $$->generics = std::move($2);
        $$->setLoc(@$);
    }
    | KW_AUTO { $$ = std::make_unique<fin::TypeNode>("auto"); $$->setLoc(@$); }
    | KW_SELF_TYPE { $$ = std::make_unique<fin::TypeNode>("Self"); $$->setLoc(@$); }
    /* `Self<T>` -- tests/samples/stdlib/stdptr.fin:16 writes `readonly restrict
       <&Self<T>>;` inside a generic struct, where `Self` alone would name the
       unapplied type. Every other named type already takes generic arguments
       (`IDENTIFIER LT type_list GT` above); Self was the exception. */
    | KW_SELF_TYPE LT type_list GT %prec TYPE_PREC {
        $$ = std::make_unique<fin::TypeNode>("Self");
        $$->generics = std::move($3);
        $$->setLoc(@$);
    }
    | KW_ANY { $$ = std::make_unique<fin::TypeNode>("any"); $$->setLoc(@$); }
    | KW_ANY LT type_list GT %prec TYPE_PREC {
        $$ = std::make_unique<fin::TypeNode>("any");
        $$->generics = std::move($3);
        $$->setLoc(@$);
    }
    | fn_type { $$ = std::move($1); }
    | LPAREN type RPAREN { $$ = std::move($2); }
    /* The meta-type is a family, not a single thing: `$struct` and `$interface`
       stand beside `$type` (tests/samples/literal_interface.fin:5, :18 and
       literal_struct.fin:4). The grammar accepted `$type` only. */
    | DOLLAR KW_TYPE { $$ = std::make_unique<fin::TypeNode>("$type"); $$->setLoc(@$); }
    | DOLLAR KW_STRUCT { $$ = std::make_unique<fin::TypeNode>("$struct"); $$->setLoc(@$); }
    | DOLLAR KW_INTERFACE { $$ = std::make_unique<fin::TypeNode>("$interface"); $$->setLoc(@$); }
    /* Any other meta-type: `$enum_member` (tests/samples/stdlib/enums.fin:22),
       `$type` / `$struct` / `$interface` above being the three the grammar knew.
       Kept as one production rather than a fourth keyword because the set is the
       compiler's to grow and a `$word` in type position can be nothing else. */
    | DOLLAR IDENTIFIER { $$ = std::make_unique<fin::TypeNode>("$" + $2); $$->setLoc(@$); }
    ;

fn_type:
    KW_FN_TYPE LPAREN type_list RPAREN ARROW type {
        $$ = std::make_unique<fin::FunctionTypeNode>(std::move($3), std::move($6));
        $$->setLoc(@$);
    }
    | KW_FN_TYPE LPAREN type_list RPAREN RARROW type {
        $$ = std::make_unique<fin::FunctionTypeNode>(std::move($3), std::move($6));
        $$->setLoc(@$);
    }
    /* No return type: `cast<fn(Self, T)>(__get)` --
       tests/samples/stdlib/hashmap.fin:50-51, where the signature is being matched
       against an operator whose return type is already declared elsewhere. The
       return type is left null rather than invented, so a pass that needs one can
       tell it was not written. */
    | KW_FN_TYPE LPAREN type_list RPAREN {
        $$ = std::make_unique<fin::FunctionTypeNode>(std::move($3), nullptr);
        $$->setLoc(@$);
    }
    /* `fn<T: Castable>(m: T) -> T` -- tests/samples/lambdas.fin:69, the type of that
       file's generic lambda and its first error ("unexpected LT, expecting LPAREN").
       Two things are new in it and both are kept: the type has generic parameters of
       its own, and its parameters are named. A name is written down rather than
       thrown away -- `param_names` is index-parallel with `param_types` -- because
       the corpus writes one, and a signature that silently loses it would compare
       equal to a different signature. */
    | KW_FN_TYPE LT generic_param_list GT LPAREN fn_type_params RPAREN RARROW type {
        std::vector<std::unique_ptr<fin::TypeNode>> types;
        std::vector<std::string> names;
        for (auto& pr : $6) { names.push_back(pr.first); types.push_back(std::move(pr.second)); }
        auto fnty = std::make_unique<fin::FunctionTypeNode>(std::move(types), std::move($9));
        fnty->param_names = std::move(names);
        fnty->generic_params = std::move($3);
        $$ = std::move(fnty);
        $$->setLoc(@$);
    }
    /* Handle empty params fn() => int */
    | KW_FN_TYPE LPAREN RPAREN ARROW type {
        std::vector<std::unique_ptr<fin::TypeNode>> empty;
        $$ = std::make_unique<fin::FunctionTypeNode>(std::move(empty), std::move($5));
        $$->setLoc(@$);
    }
    ;

/* The alternatives after the first in a union alias. */
union_tail:
    PIPE union_alternative { $$.push_back(std::move($2)); }
    | union_tail PIPE union_alternative { $1.push_back(std::move($3)); $$ = std::move($1); }
    ;

union_alternative:
    type { $$ = std::move($1); }
    /* `any implements <Error>` as one alternative: stdlib/typing.fin:10. The
       constraint set rides on the alternative rather than on the alias, because
       it constrains that one alternative and says nothing about `string`. */
    | KW_ANY KW_IMPLEMENTS LT type_list GT {
        $$ = std::make_unique<fin::TypeNode>("any");
        $$->implements_list = std::move($4);
        $$->setLoc(@$);
    }
    ;

type_list:
    type_list COMMA type { $1.push_back(std::move($3)); $$ = std::move($1); }
    | type { std::vector<std::unique_ptr<fin::TypeNode>> v; v.push_back(std::move($1)); $$ = std::move(v); }
    ;

/* The parameter list of an `fn<...>(...)` type, where a parameter may be named:
   `(m: T)` at tests/samples/lambdas.fin:69. The name is empty for the unnamed
   spelling every other fn type in the corpus uses. */
fn_type_params:
    fn_type_param { std::vector<std::pair<std::string, std::unique_ptr<fin::TypeNode>>> v; v.push_back(std::move($1)); $$ = std::move(v); }
    | fn_type_params COMMA fn_type_param { $1.push_back(std::move($3)); $$ = std::move($1); }
    ;

fn_type_param:
    type { $$ = std::make_pair(std::string(), std::move($1)); }
    | IDENTIFIER COLON type { $$ = std::make_pair($1, std::move($3)); }
    ;

/* The member-initialiser list of a constructor: `:age(10), name("Hello")`
   (tests/samples/deeptest2.fin:40). */
ctor_inits:
    ctor_init { std::vector<fin::MemberInit> v; v.push_back(std::move($1)); $$ = std::move(v); }
    | ctor_inits COMMA ctor_init { $1.push_back(std::move($3)); $$ = std::move($1); }
    ;

ctor_init:
    IDENTIFIER LPAREN arguments RPAREN { $$ = fin::MemberInit{$1, std::move($3)}; }
    ;

/* One or more postfix stars, counted: the `**` of `new int**`
   (tests/samples/simple_pointers.fin:28). */
ptr_stars:
    MULT { $$ = 1; }
    | ptr_stars MULT { $$ = $1 + 1; }
    ;

pointer_type:
    AMPERSAND type {
        $$ = std::make_unique<fin::PointerTypeNode>(std::move($2));
        $$->setLoc(@$);
    }
    | MULT type {
        $$ = std::make_unique<fin::PointerTypeNode>(std::move($2));
        $$->setLoc(@$);
    }
    | AND type {
        /* Handle && as a double pointer/reference */
        auto inner = std::make_unique<fin::PointerTypeNode>(std::move($2));
        inner->setLoc(@$);
        $$ = std::make_unique<fin::PointerTypeNode>(std::move(inner));
        $$->setLoc(@$);
    }
    ;

array_type:
    LBRACKET type RBRACKET {
        $$ = std::make_unique<fin::ArrayTypeNode>(std::move($2), nullptr);
        $$->setLoc(@$);
    }
    | LBRACKET type COMMA expression RBRACKET {
        $$ = std::make_unique<fin::ArrayTypeNode>(std::move($2), std::move($4));
        $$->setLoc(@$);
    }
    ;

primitive_type:
    TYPE_INT      { $$ = "int"; } 
    | TYPE_LONG   { $$ = "long"; } 
    | TYPE_FLOAT  { $$ = "float"; } 
    | TYPE_DOUBLE { $$ = "double"; } 
    | TYPE_STRING { $$ = "string"; }
    | TYPE_CHAR   { $$ = "char"; } 
    | TYPE_VOID   { $$ = "void"; } 
    | TYPE_BOOL   { $$ = "bool"; }
    ;

/* --- CONTROL FLOW --- */

if_statement:
    KW_IF LPAREN expression RPAREN block %prec KW_IFX {
        $$ = std::make_unique<fin::IfStatement>(std::move($3), std::move($5), nullptr);
        $$->setLoc(@$);
    }
    /* A single statement as the body, no braces: `if (nbytes > self.stream_length
       || nbytes == -1) nbytes = self.stream_length;` --
       tests/samples/stdlib/stdio.fin:110, the corpus's only such line. Restricted
       to an expression statement rather than any statement: a bare `{` in
       statement position opens a scope (ADR 0011), so `statement` here would make
       `if (c) { ... }` ambiguous between this production and the braced one above.
       The body is wrapped in a Block so that every consumer of an IfStatement
       still finds one. */
    | KW_IF LPAREN expression RPAREN expression_statement %prec KW_IFX {
        std::vector<std::unique_ptr<fin::Statement>> stmts;
        stmts.push_back(std::move($5));
        auto body = std::make_unique<fin::Block>(std::move(stmts));
        body->setLoc(@5);
        $$ = std::make_unique<fin::IfStatement>(std::move($3), std::move(body), nullptr);
        $$->setLoc(@$);
    }
    | KW_IF LPAREN expression RPAREN block KW_ELSE block {
        $$ = std::make_unique<fin::IfStatement>(std::move($3), std::move($5), std::move($7));
        $$->setLoc(@$);
    }
    /* `else if` -- no corpus sample writes one, and `elseif` (the deleted
       KW_ELSEIF) was the only chained form the grammar ever had, so deleting
       that token left the language with no way to chain at all. `else_stmt` is
       already a Statement rather than a Block, so the chain needs one
       production and no node change. */
    | KW_IF LPAREN expression RPAREN block KW_ELSE if_statement {
        $$ = std::make_unique<fin::IfStatement>(std::move($3), std::move($5), std::move($7));
        $$->setLoc(@$);
    }
    ;

while_loop:
    KW_WHILE LPAREN expression RPAREN block {
        $$ = std::make_unique<fin::WhileLoop>(std::move($3), std::move($5));
        $$->setLoc(@$);
    }
    /* `do { ... } while (cond);` -- tests/samples/loops.fin:35, which did not
       parse at all before this: `do` was not even a keyword.

       A flag on WhileLoop rather than a DoWhileLoop node. The two loops differ
       only in whether the first pass tests the condition, and the condition and
       body type-check identically, so the whole difference is one bit -- while a
       new node would need an `accept` override in src/ast/Visitor.hpp and a
       branch in src/ast/cloning/**, neither of which this wave owns (ADR 0004
       does not reach `accept`, which is pure virtual).

       Deliberately NOT desugared into `while (true) { body; if (!cond) break; }`.
       In that shape a `continue` in the body skips the appended test and spins
       forever, so the loop would parse and then run wrong -- which docs/plan.md
       argues about macro arguments is worse than refusing. */
    | KW_DO block KW_WHILE LPAREN expression RPAREN SEMICOLON {
        auto loop = std::make_unique<fin::WhileLoop>(std::move($5), std::move($2));
        loop->is_do_while = true;
        $$ = std::move(loop);
        $$->setLoc(@$);
    }
    ;

for_loop:
    KW_FOR LPAREN variable_declaration expression SEMICOLON expression RPAREN block {
        $$ = std::make_unique<fin::ForLoop>(std::move($3), std::move($4), std::move($6), std::move($8));
        $$->setLoc(@$);
    }
    /* `for (i : int = 0; i <= 10; i++)` -- tests/samples/loops.fin:8 and :14,
       tests/samples/arrays.fin:17 and :18. The header declares its counter with
       the parameter spelling `name : type`, not the statement spelling
       `let name <type>`.

       A production here rather than another `variable_declaration` form,
       because `i : int = 0;` accepted as a statement anywhere would put a bare
       IDENTIFIER before COLON in every statement position -- the ambiguity
       PARAM_NAME_PREC already arbitrates for lambda parameters against the
       ADR 0005 conditional. Confined to the for header, no such state exists. */
    | KW_FOR LPAREN IDENTIFIER COLON type EQUAL expression SEMICOLON expression SEMICOLON expression RPAREN block {
        auto init = std::make_unique<fin::VariableDeclaration>(true, $3, std::move($5), std::move($7));
        init->setLoc(@3);
        $$ = std::make_unique<fin::ForLoop>(std::move(init), std::move($9), std::move($11), std::move($13));
        $$->setLoc(@$);
    }
    ;

foreach_loop:
    /* Use no_struct_expression to avoid ambiguity with block start */
    KW_FOREACH IDENTIFIER LT type GT KW_IN no_struct_expression block {
        $$ = std::make_unique<fin::ForeachLoop>($2, std::move($4), std::move($7), std::move($8));
        $$->setLoc(@$);
    }
    /* The parenthesised spelling, which is the only one the corpus writes:
       tests/samples/loops.fin:19 and :24, deeptest3.fin and stdlib/collection.fin.
       Before this, `foreach (element <int> in a)` did not parse at all -- the
       grammar had only the bare `foreach element <int> in a` form, and the LPAREN
       was reported as "unexpected LPAREN, expecting IDENTIFIER". `expression` is
       safe inside the parens where the bare form needs `no_struct_expression`,
       because RPAREN, not LBRACE, is what ends the iterable. */
    | KW_FOREACH LPAREN IDENTIFIER LT type GT KW_IN expression RPAREN block {
        $$ = std::make_unique<fin::ForeachLoop>($3, std::move($5), std::move($8), std::move($10));
        $$->setLoc(@$);
    }
    | KW_FOREACH LPAREN IDENTIFIER LT type GT COMMA IDENTIFIER LT type GT KW_IN expression RPAREN block {
        auto loop = std::make_unique<fin::ForeachLoop>($8, std::move($10), std::move($13), std::move($15));
        loop->index_name = $3;
        loop->index_type = std::move($5);
        $$ = std::move(loop);
        $$->setLoc(@$);
    }
    /* `foreach (idx <int>, element <int> in a)` -- tests/samples/loops.fin:19,
       where the sample's own comment names the first binding the index and the
       second the element. Two extra fields on ForeachLoop for the same reason
       do-while is a flag: a second node needs Visitor.hpp. */
    | KW_FOREACH IDENTIFIER LT type GT COMMA IDENTIFIER LT type GT KW_IN no_struct_expression block {
        auto loop = std::make_unique<fin::ForeachLoop>($7, std::move($9), std::move($12), std::move($13));
        loop->index_name = $2;
        loop->index_type = std::move($4);
        $$ = std::move(loop);
        $$->setLoc(@$);
    }
    ;

control_statement:
    KW_BREAK SEMICOLON { $$ = std::make_unique<fin::BreakStatement>(); $$->setLoc(@$); }
    | KW_CONTINUE SEMICOLON { $$ = std::make_unique<fin::ContinueStatement>(); $$->setLoc(@$); }
    ;

delete_statement:
    KW_DELETE expression SEMICOLON {
        $$ = std::make_unique<fin::DeleteStatement>(std::move($2));
        $$->setLoc(@$);
    }
    ;

try_catch_statement:
    /* `catch (Error as err)` -- type on the left, binding on the right. This read the
       two the other way round (`IDENTIFIER KW_AS type`), so readonly.fin:50 handed its
       own binding name to resolveTypeFromAST and reported `Undefined type 'err'`.

       The corpus writes one catch clause and it writes this order, and every other `as`
       in Fin agrees with it: `import stdio as io` and `extern original as local`
       (extern_as.fin, whose whole subject it is) both name something that exists on the
       left and bind it on the right. Soundness_TryCatch holds all three claims --
       the type resolves, the name is bound, and an unresolved *type* is what gets
       reported. */
    KW_TRY block KW_CATCH LPAREN type KW_AS IDENTIFIER RPAREN block {
        $$ = std::make_unique<fin::TryCatch>(std::move($2), $7, std::move($5), std::move($9));
        $$->setLoc(@$);
    }
    ;

blame_statement:
    KW_BLAME expression SEMICOLON {
        $$ = std::make_unique<fin::BlameStatement>(std::move($2));
        $$->setLoc(@$);
    }
    | KW_BLAME expression COMMA expression SEMICOLON {
        $$ = std::make_unique<fin::BlameStatement>(std::move($2), std::move($4));
        $$->setLoc(@$);
    }
    ;

return_statement:
    KW_RETURN expression SEMICOLON { $$ = std::make_unique<fin::ReturnStatement>(std::move($2)); }
    | KW_RETURN SEMICOLON { $$ = std::make_unique<fin::ReturnStatement>(nullptr); }
    ;

/* --- EXPRESSIONS (SPLIT) --- */

expression_statement:
    expression SEMICOLON { $$ = std::make_unique<fin::ExpressionStatement>(std::move($1)); }
    ;

lambda_expression:
      /* Case 1: Anonymous Fun: fun(a: <int>) <int> { ... } */
      KW_FUN LPAREN params RPAREN LT type GT block {
          $$ = std::make_unique<fin::LambdaExpression>(std::move($3), std::move($6), std::move($8));
          $$->setLoc(@$);
      }
      /* Case 2: Arrow Block: (a: <int>) <int> => { ... } */
      | LPAREN params RPAREN LT type GT ARROW block {
          $$ = std::make_unique<fin::LambdaExpression>(std::move($2), std::move($5), std::move($8));
          $$->setLoc(@$);
      }
      /* Generic lambdas -- tests/samples/lambdas.fin:69 and :71, the file's own
         "Case 8: Lambda with generics". Line 69 writes the parameters in front of the
         arrow form (`<T: Castable>(m: T) <T> => m`) and line 71 after `fun`
         (`fun <Generic: Addable>(a: Generic, b: Generic) <Generic> { ... }`); both
         were syntax errors, so no lambda could be generic. Only the body shape each
         corpus line uses is accepted, an expression for the first and a block for the
         second. A leading `<` cannot be confused with a comparison: `<` opens the
         list only where an expression may start, and there is never a complete
         expression on the stack at that point. */
      | LT generic_param_list GT LPAREN params RPAREN LT type GT ARROW expression %prec ARROW {
          auto lam = std::make_unique<fin::LambdaExpression>(std::move($5), std::move($8), std::move($11));
          lam->generic_params = std::move($2);
          $$ = std::move(lam);
          $$->setLoc(@$);
      }
      | KW_FUN LT generic_param_list GT LPAREN params RPAREN LT type GT block {
          auto lam = std::make_unique<fin::LambdaExpression>(std::move($6), std::move($9), std::move($11));
          lam->generic_params = std::move($3);
          $$ = std::move(lam);
          $$->setLoc(@$);
      }
      /* Case 3: Arrow Expression (Full): (a: <int>) <int> => expr */
      | LPAREN params RPAREN LT type GT ARROW expression %prec ARROW {
          $$ = std::make_unique<fin::LambdaExpression>(std::move($2), std::move($5), std::move($8));
          $$->setLoc(@$);
      }
      ;


/* The main expression rule includes everything */
expression:
    /* Binary Ops */
    expression EQUAL expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::EQUAL, std::move($3)); $$->setLoc(@$); }
    | expression PLUSEQUAL expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::PLUSEQUAL, std::move($3)); $$->setLoc(@$); }
    | expression MINUSEQUAL expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::MINUSEQUAL, std::move($3)); $$->setLoc(@$); }
    | expression MULTEQUAL expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::MULTEQUAL, std::move($3)); $$->setLoc(@$); }
    | expression DIVEQUAL expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::DIVEQUAL, std::move($3)); $$->setLoc(@$); }
    | expression OR expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::OR, std::move($3)); $$->setLoc(@$); }
    | expression AND expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::AND, std::move($3)); $$->setLoc(@$); }
    | expression EQEQ expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::EQEQ, std::move($3)); $$->setLoc(@$); }
    | expression NOTEQ expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::NOTEQ, std::move($3)); $$->setLoc(@$); }
    | expression LT expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::LT, std::move($3)); $$->setLoc(@$); }
    | expression GT expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::GT, std::move($3)); $$->setLoc(@$); }
    | expression LTEQ expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::LTEQ, std::move($3)); $$->setLoc(@$); }
    | expression GTEQ expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::GTEQ, std::move($3)); $$->setLoc(@$); }
    | expression PLUS expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::PLUS, std::move($3)); $$->setLoc(@$); }
    | expression MINUS expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::MINUS, std::move($3)); $$->setLoc(@$); }
    | expression MULT expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::MULT, std::move($3)); $$->setLoc(@$); }
    | expression DIV expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::DIV, std::move($3)); $$->setLoc(@$); }
    | expression MOD expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::MOD, std::move($3)); $$->setLoc(@$); }
    | expression SHIFTLEFT expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::SHIFTLEFT, std::move($3)); $$->setLoc(@$); }
    /* `>>` arrives as two GTs (see lexer.l), so the operator is rebuilt here.
       %prec keeps it at shift precedence rather than the comparison precedence
       its last terminal would otherwise give it. */
    | expression GT GT expression %prec SHIFTRIGHT { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::SHIFTRIGHT, std::move($4)); $$->setLoc(@$); }
    
    /* Unary Ops */
    | MINUS expression %prec UMINUS { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::MINUS, std::move($2)); $$->setLoc(@$); }
    | NOT expression { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::NOT, std::move($2)); $$->setLoc(@$); }
    | AMPERSAND expression %prec ADDRESSOF_PREC { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::AMPERSAND, std::move($2)); $$->setLoc(@$); }
    | MULT expression %prec DEREFERENCE_PREC { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::MULT, std::move($2)); $$->setLoc(@$); }
    | INCREMENT expression { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::INCREMENT, std::move($2)); $$->setLoc(@$); }
    | DECREMENT expression { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::DECREMENT, std::move($2)); $$->setLoc(@$); }
    
    /* `@name(args)` -- calling a special function. tests/samples/stdlib/enums.fin:18
       (`@getenumkeyid(value)`), stdlib/memory.fin:14 (`@Alloc(size)`) and
       literal_interface.fin:6 (`@implements(struct_, iface)`), which is the first
       error in each. A FunctionCall carrying `is_special` rather than a node of
       its own: the call has a name and arguments and nothing else, and a new
       Expression class needs src/ast/Visitor.hpp.

       `@implements` needs its own alternative because `implements` is a keyword.
       No ambiguity with the four declaration headers that also start with AT:
       each of those has KW_DEFINE, KW_MACRO or KW_SPECIAL as its second token. */
    | AT IDENTIFIER LPAREN arguments RPAREN {
        auto call = std::make_unique<fin::FunctionCall>($2, std::move($4));
        call->is_special = true;
        $$ = std::move(call);
        $$->setLoc(@$);
    }
    | AT KW_IMPLEMENTS LPAREN arguments RPAREN {
        auto call = std::make_unique<fin::FunctionCall>("implements", std::move($4));
        call->is_special = true;
        $$ = std::move(call);
        $$->setLoc(@$);
    }

    /* Postfix */
    | expression LPAREN arguments RPAREN {
        if (auto* id = dynamic_cast<fin::Identifier*>($1.get())) {
            $$ = std::make_unique<fin::FunctionCall>(id->name, std::move($3));
        } else if (auto* mem = dynamic_cast<fin::MemberAccess*>($1.get())) {
            $$ = std::make_unique<fin::MethodCall>(std::move(mem->object), mem->member, std::move($3));
        } else {
            $$ = std::make_unique<fin::FunctionCall>("unknown", std::move($3));
        }
        $$->setLoc(@$);
    }
    /* `p.0` -- a member by position rather than by name.
       tests/samples/stdlib/prototypes.fin:11 reads `prtp.0` (the keys of a
       prototype) and enums.fin:27,29 read `enum_.0` and `enum_.1` (the payload of
       an enum member). The digits become the member name, so a positional access
       is a MemberAccess like any other and nothing downstream needs a new shape;
       `0` cannot collide with a declared field, which must start with a letter. */
    | expression DOT INTEGER {
        $$ = std::make_unique<fin::MemberAccess>(std::move($1), $3);
        $$->setLoc(@$);
    }
    | expression DOT IDENTIFIER {
        $$ = std::make_unique<fin::MemberAccess>(std::move($1), $3);
        $$->setLoc(@$);
    }
    /* Turbofish on a dotted path: `a.b.c::<T>()`. Every other turbofish
       production in this grammar begins with a bare IDENTIFIER, so `foo::<T>()`
       and `mod::<T>::bar()` parsed and `compiler.structs.select_field::<int>()`
       did not -- which made tests/samples/stdlib/types.fin:25 a syntax error, and
       that line is `typeid`, the function the rest of the standard library is
       built on. Needs the DOT prefix rather than a general
       `expression DOUBLE_COLON ...`: the general form collides on DOUBLE_COLON
       with the five `IDENTIFIER DOUBLE_COLON ...` productions, because the parser
       would have to choose between reducing IDENTIFIER to an expression and
       shifting, one token before it can tell which. */
    | expression DOT IDENTIFIER DOUBLE_COLON LT type_list GT LPAREN arguments RPAREN {
        $$ = std::make_unique<fin::MethodCall>(std::move($1), $3, std::move($9), std::move($6));
        $$->setLoc(@$);
    }
    | expression INCREMENT { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::INCREMENT, std::move($1)); $$->setLoc(@$); }
    | expression DECREMENT { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::DECREMENT, std::move($1)); $$->setLoc(@$); }
    /* Postfix denullify: `make_A(-1)?` panics on null and yields the unwrapped
       value otherwise (tests/samples/nullifier.fin:31, :36, :40, :42 and
       undefined_behavior.fin:16). Chains, so `make_A(10)?.get_b()?` is two of
       them. It shares QUESTION with the conditional's second half, which is why
       ADR 0005 moved the conditional to `cond : then ? otherwise` -- with `?`
       leading, `x ? -1 : 2` and `x?` followed by a minus are indistinguishable
       without unbounded lookahead. */
    | expression QUESTION %prec DENULLIFY { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::QUESTION, std::move($1)); $$->setLoc(@$); }
    | expression LBRACKET expression RBRACKET {
           $$ = std::make_unique<fin::ArrayAccess>(std::move($1), std::move($3));
           $$->setLoc(@$);
    }
    /* The conditional is `cond : then ? otherwise`, not C's `cond ? then : otherwise`
       (ADR 0005). The C order is deleted rather than kept as a second spelling:
       accepting both would make `cond : a ? b` and `cond ? a : b` mean opposite
       things by punctuation order alone. `?` is already Fin's postfix denullifier,
       so in C order the parser cannot tell `x ? -1 : 2` from `x?` followed by a
       binary minus without unbounded lookahead. Both corpus conditionals —
       stdlib/error.fin:28 and stdlib/stdio.fin:160 — are written in this order. */
    | expression COLON expression QUESTION expression {
        $$ = std::make_unique<fin::TernaryOp>(std::move($1), std::move($3), std::move($5));
        $$->setLoc(@$);
    }

    | expression NOT LPAREN arguments RPAREN {
        std::string name = flatten_macro_name($1.get());
        if (name.empty()) {
            error(@1, "Invalid macro name (must be identifier or dotted path)");
            $$ = nullptr; // Error recovery
        } else {
            $$ = std::make_unique<fin::MacroInvocation>(name, std::move($4));
            $$->setLoc(@$);
        }
    }
    /* `map!{ "alex" => 10 }` and `coll![1,2,3]` -- tests/samples/useful_macros.fin:5
       and :10, both on the right of an `=`. The braced and bracketed macro calls
       existed only in `no_struct_expression`, the half of the grammar used where a
       bare `{` would be a block, so a dict-shaped macro was a syntax error in every
       initialiser: "unexpected LBRACE, expecting LPAREN". No ambiguity with struct
       instantiation, which is `IDENTIFIER LBRACE` with no `!`. */
    | expression NOT LBRACE macro_arguments RBRACE {
        std::string name = flatten_macro_name($1.get());
        if (name.empty()) {
            error(@1, "Invalid macro name");
            $$ = nullptr;
        } else {
            $$ = std::make_unique<fin::MacroInvocation>(name, std::move($4));
            $$->setLoc(@$);
        }
    }
    | expression NOT LBRACKET macro_arguments RBRACKET {
        std::string name = flatten_macro_name($1.get());
        if (name.empty()) {
            error(@1, "Invalid macro name");
            $$ = nullptr;
        } else {
            $$ = std::make_unique<fin::MacroInvocation>(name, std::move($4));
            $$->setLoc(@$);
        }
    }

    /* Primary */
    | primary_no_struct { $$ = std::move($1); }

    | lambda_expression { $$ = std::move($1); }

    /* Struct Instantiations - simple identifier */
    | IDENTIFIER LBRACE field_assignments RBRACE {
        std::vector<std::unique_ptr<fin::TypeNode>> empty_generics;
        $$ = std::make_unique<fin::StructInstantiation>($1, std::move($3), std::move(empty_generics));
        $$->setLoc(@$);
    }
    /* Struct Instantiations - with turbofish generics */
    | IDENTIFIER DOUBLE_COLON LT type_list GT LBRACE field_assignments RBRACE {
        $$ = std::make_unique<fin::StructInstantiation>($1, std::move($7), std::move($4));
        $$->setLoc(@$);
    }
    /* New expression - simple type */
    | KW_NEW IDENTIFIER LBRACE field_assignments RBRACE {
        auto type = std::make_unique<fin::TypeNode>($2);
        type->setLoc(@2);  /* the name, not the whole `new ... {}`: the name is what fails to resolve */
        $$ = std::make_unique<fin::NewExpression>(std::move(type), std::move($4));
        $$->setLoc(@$);
    }
    /* New expression - with turbofish generics */
    | KW_NEW IDENTIFIER DOUBLE_COLON LT type_list GT LBRACE field_assignments RBRACE {
        auto type = std::make_unique<fin::TypeNode>($2);
        type->setLoc(@2);
        type->generics = std::move($5);
        $$ = std::make_unique<fin::NewExpression>(std::move(type), std::move($8));
        $$->setLoc(@$);
    }
    /* New expression - pointer type */
    /* `new int(5)` -- tests/samples/variables.fin:28, :36 and
       simple_pointers.fin:24. Every other `new` form starts with an IDENTIFIER,
       so heap-allocating a builtin was a syntax error: "unexpected TYPE_INT,
       expecting IDENTIFIER". `primitive_type` rather than `type` keeps it out of
       the way of the five IDENTIFIER forms -- TYPE_INT and friends are their own
       tokens, so no state has to choose. */
    | KW_NEW primitive_type LPAREN arguments RPAREN {
        auto ty = std::make_unique<fin::TypeNode>($2);
        ty->setLoc(@2);
        $$ = std::make_unique<fin::NewExpression>(std::move(ty), std::move($4));
        $$->setLoc(@$);
    }
    /* `new int*`, `new int**`, `new int***` -- tests/samples/simple_pointers.fin:23,
       27, 28, 29, where a pointer is heap-allocated and there is nothing to
       initialise it with. The stars are postfix here, unlike everywhere else in Fin
       where a pointer is written `&int` (or `*int`, see `pointer_type`), so each one
       wraps the type in another PointerTypeNode and no depth is lost.

       The ratified rewrite of line 23 to `new &int` becomes unnecessary once this
       parses, so the sample keeps the four lines it was written with. */
    | KW_NEW primitive_type ptr_stars {
        std::unique_ptr<fin::TypeNode> ty = std::make_unique<fin::TypeNode>($2);
        ty->setLoc(@2);
        /* One star fewer than written: the stars describe the type of the
           result, and `new` already yields a pointer to what it allocated. That
           is the reading the sample itself uses -- `***yy = new int*` with `yy`
           declared `<&&&&int>` only balances if `new int*` is an `&int`. */
        for (int i = 1; i < $3; ++i) {
            ty = std::make_unique<fin::PointerTypeNode>(std::move(ty));
            ty->setLoc(@$);
        }
        $$ = std::make_unique<fin::NewExpression>(std::move(ty), std::vector<std::unique_ptr<fin::Expression>>());
        $$->setLoc(@$);
    }
    | KW_NEW AMPERSAND type LBRACE field_assignments RBRACE {
        auto ptr_type = std::make_unique<fin::PointerTypeNode>(std::move($3));
        $$ = std::make_unique<fin::NewExpression>(std::move(ptr_type), std::move($5));
        $$->setLoc(@$);
    }
    /* New expression - array type */
    | KW_NEW LBRACKET type RBRACKET LBRACE field_assignments RBRACE {
        auto arr_type = std::make_unique<fin::ArrayTypeNode>(std::move($3), nullptr);
        $$ = std::make_unique<fin::NewExpression>(std::move(arr_type), std::move($6));
        $$->setLoc(@$);
    }
    | KW_NEW LBRACKET type COMMA expression RBRACKET LBRACE field_assignments RBRACE {
        auto arr_type = std::make_unique<fin::ArrayTypeNode>(std::move($3), std::move($5));
        $$ = std::make_unique<fin::NewExpression>(std::move(arr_type), std::move($8));
        $$->setLoc(@$);
    }
    /* The same allocation with no initialiser: `new [char, nbytes - self.pointer]`
       -- tests/samples/stdlib/stdio.fin:112, where the size is a runtime value and
       there is nothing to initialise the elements with. The braced form above was
       the only spelling, so allocating an array of a computed length was a syntax
       error. */
    | KW_NEW LBRACKET type COMMA expression RBRACKET {
        auto arr_type = std::make_unique<fin::ArrayTypeNode>(std::move($3), std::move($5));
        arr_type->setLoc(@2);
        $$ = std::make_unique<fin::NewExpression>(std::move(arr_type), std::vector<std::unique_ptr<fin::Expression>>());
        $$->setLoc(@$);
    }
    /* New expression - Self type */
    | KW_NEW KW_SELF_TYPE LBRACE field_assignments RBRACE {
        auto type = std::make_unique<fin::TypeNode>("Self");
        type->setLoc(@2);
        $$ = std::make_unique<fin::NewExpression>(std::move(type), std::move($4));
        $$->setLoc(@$);
    }
    ;

/* 
   Restricted expression that does NOT allow top-level struct instantiation 
   starting with IDENTIFIER LBRACE. This resolves the foreach ambiguity.
*/
no_struct_expression:
    /* Binary Ops */
    no_struct_expression EQUAL no_struct_expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::EQUAL, std::move($3)); $$->setLoc(@$); }
    | no_struct_expression PLUSEQUAL no_struct_expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::PLUSEQUAL, std::move($3)); $$->setLoc(@$); }
    | no_struct_expression MINUSEQUAL no_struct_expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::MINUSEQUAL, std::move($3)); $$->setLoc(@$); }
    | no_struct_expression MULTEQUAL no_struct_expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::MULTEQUAL, std::move($3)); $$->setLoc(@$); }
    | no_struct_expression DIVEQUAL no_struct_expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::DIVEQUAL, std::move($3)); $$->setLoc(@$); }
    | no_struct_expression PIPE PIPE no_struct_expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::OR, std::move($4)); $$->setLoc(@$); }
    | no_struct_expression AMPERSAND AMPERSAND no_struct_expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::AND, std::move($4)); $$->setLoc(@$); }
    | no_struct_expression EQEQ no_struct_expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::EQEQ, std::move($3)); $$->setLoc(@$); }
    | no_struct_expression NOTEQ no_struct_expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::NOTEQ, std::move($3)); $$->setLoc(@$); }
    | no_struct_expression LT no_struct_expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::LT, std::move($3)); $$->setLoc(@$); }
    | no_struct_expression GT no_struct_expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::GT, std::move($3)); $$->setLoc(@$); }
    | no_struct_expression LTEQ no_struct_expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::LTEQ, std::move($3)); $$->setLoc(@$); }
    | no_struct_expression GTEQ no_struct_expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::GTEQ, std::move($3)); $$->setLoc(@$); }
    | no_struct_expression PLUS no_struct_expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::PLUS, std::move($3)); $$->setLoc(@$); }
    | no_struct_expression MINUS no_struct_expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::MINUS, std::move($3)); $$->setLoc(@$); }
    | no_struct_expression MULT no_struct_expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::MULT, std::move($3)); $$->setLoc(@$); }
    | no_struct_expression DIV no_struct_expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::DIV, std::move($3)); $$->setLoc(@$); }
    | no_struct_expression MOD no_struct_expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::MOD, std::move($3)); $$->setLoc(@$); }
    | no_struct_expression LT LT no_struct_expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::SHIFTLEFT, std::move($4)); $$->setLoc(@$); }
    | no_struct_expression GT GT no_struct_expression %prec SHIFTRIGHT { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::SHIFTRIGHT, std::move($4)); $$->setLoc(@$); }
    
    /* Unary Ops */
    | MINUS no_struct_expression %prec UMINUS { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::MINUS, std::move($2)); $$->setLoc(@$); }
    | NOT no_struct_expression { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::NOT, std::move($2)); $$->setLoc(@$); }
    | AMPERSAND no_struct_expression %prec ADDRESSOF_PREC { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::AMPERSAND, std::move($2)); $$->setLoc(@$); }
    | MULT no_struct_expression %prec DEREFERENCE_PREC { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::MULT, std::move($2)); $$->setLoc(@$); }
    | INCREMENT no_struct_expression { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::INCREMENT, std::move($2)); $$->setLoc(@$); }
    | DECREMENT no_struct_expression { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::DECREMENT, std::move($2)); $$->setLoc(@$); }
    
    /* `@name(args)` -- calling a special function. tests/samples/stdlib/enums.fin:18
       (`@getenumkeyid(value)`), stdlib/memory.fin:14 (`@Alloc(size)`) and
       literal_interface.fin:6 (`@implements(struct_, iface)`), which is the first
       error in each. A FunctionCall carrying `is_special` rather than a node of
       its own: the call has a name and arguments and nothing else, and a new
       Expression class needs src/ast/Visitor.hpp.

       `@implements` needs its own alternative because `implements` is a keyword.
       No ambiguity with the four declaration headers that also start with AT:
       each of those has KW_DEFINE, KW_MACRO or KW_SPECIAL as its second token. */
    | AT IDENTIFIER LPAREN arguments RPAREN {
        auto call = std::make_unique<fin::FunctionCall>($2, std::move($4));
        call->is_special = true;
        $$ = std::move(call);
        $$->setLoc(@$);
    }
    | AT KW_IMPLEMENTS LPAREN arguments RPAREN {
        auto call = std::make_unique<fin::FunctionCall>("implements", std::move($4));
        call->is_special = true;
        $$ = std::move(call);
        $$->setLoc(@$);
    }

    /* Postfix */
    | no_struct_expression LPAREN arguments RPAREN {
        if (auto* id = dynamic_cast<fin::Identifier*>($1.get())) {
            $$ = std::make_unique<fin::FunctionCall>(id->name, std::move($3));
        } else if (auto* mem = dynamic_cast<fin::MemberAccess*>($1.get())) {
            $$ = std::make_unique<fin::MethodCall>(std::move(mem->object), mem->member, std::move($3));
        } else {
            $$ = std::make_unique<fin::FunctionCall>("unknown", std::move($3));
        }
        $$->setLoc(@$);
    }
    /* `p.0` -- a member by position rather than by name.
       tests/samples/stdlib/prototypes.fin:11 reads `prtp.0` (the keys of a
       prototype) and enums.fin:27,29 read `enum_.0` and `enum_.1` (the payload of
       an enum member). The digits become the member name, so a positional access
       is a MemberAccess like any other and nothing downstream needs a new shape;
       `0` cannot collide with a declared field, which must start with a letter. */
    | no_struct_expression DOT INTEGER {
        $$ = std::make_unique<fin::MemberAccess>(std::move($1), $3);
        $$->setLoc(@$);
    }
    | no_struct_expression DOT IDENTIFIER {
        $$ = std::make_unique<fin::MemberAccess>(std::move($1), $3);
        $$->setLoc(@$);
    }
    | no_struct_expression DOT IDENTIFIER DOUBLE_COLON LT type_list GT LPAREN arguments RPAREN {
        $$ = std::make_unique<fin::MethodCall>(std::move($1), $3, std::move($9), std::move($6));
        $$->setLoc(@$);
    }
    | no_struct_expression INCREMENT { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::INCREMENT, std::move($1)); $$->setLoc(@$); }
    | no_struct_expression DECREMENT { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::DECREMENT, std::move($1)); $$->setLoc(@$); }
    | no_struct_expression QUESTION %prec DENULLIFY { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::QUESTION, std::move($1)); $$->setLoc(@$); }
    | no_struct_expression LBRACKET expression RBRACKET {
           $$ = std::make_unique<fin::ArrayAccess>(std::move($1), std::move($3));
           $$->setLoc(@$);
    }
    | no_struct_expression COLON no_struct_expression QUESTION no_struct_expression {
        $$ = std::make_unique<fin::TernaryOp>(std::move($1), std::move($3), std::move($5));
        $$->setLoc(@$);
    }

    | no_struct_expression NOT LPAREN macro_arguments RPAREN {
        std::string name = flatten_macro_name($1.get());
        if (name.empty()) {
            error(@1, "Invalid macro name");
            $$ = nullptr;
        } else {
            $$ = std::make_unique<fin::MacroInvocation>(name, std::move($4));
            $$->setLoc(@$);
        }
    }
    | no_struct_expression NOT LBRACE macro_arguments RBRACE {
        std::string name = flatten_macro_name($1.get());
        if (name.empty()) {
            error(@1, "Invalid macro name");
            $$ = nullptr;
        } else {
            $$ = std::make_unique<fin::MacroInvocation>(name, std::move($4));
            $$->setLoc(@$);
        }
    }
    | no_struct_expression NOT LBRACKET macro_arguments RBRACKET {
        std::string name = flatten_macro_name($1.get());
        if (name.empty()) {
            error(@1, "Invalid macro name");
            $$ = nullptr;
        } else {
            $$ = std::make_unique<fin::MacroInvocation>(name, std::move($4));
            $$->setLoc(@$);
        }
    }
    
    /* Primary */
    | primary_no_struct { $$ = std::move($1); }
    
    /* Restricted Lambda */
    | KW_FUN LPAREN params RPAREN LT type GT block {
        $$ = std::make_unique<fin::LambdaExpression>(std::move($3), std::move($6), std::move($8));
        $$->setLoc(@$);
    }
    | LPAREN params RPAREN LT type GT ARROW block {
        $$ = std::make_unique<fin::LambdaExpression>(std::move($2), std::move($5), std::move($8));
        $$->setLoc(@$);
    }
    | LPAREN params RPAREN LT type GT ARROW no_struct_expression {
        $$ = std::make_unique<fin::LambdaExpression>(std::move($2), std::move($5), std::move($8));
        $$->setLoc(@$);
    }
    
    /* New expression - simple type */
    | KW_NEW IDENTIFIER LBRACE field_assignments RBRACE {
        auto type = std::make_unique<fin::TypeNode>($2);
        type->setLoc(@2);  /* the name, not the whole `new ... {}`: the name is what fails to resolve */
        $$ = std::make_unique<fin::NewExpression>(std::move(type), std::move($4));
        $$->setLoc(@$);
    }
    /* New expression - with turbofish generics */
    | KW_NEW IDENTIFIER DOUBLE_COLON LT type_list GT LBRACE field_assignments RBRACE {
        auto type = std::make_unique<fin::TypeNode>($2);
        type->setLoc(@2);
        type->generics = std::move($5);
        $$ = std::make_unique<fin::NewExpression>(std::move(type), std::move($8));
        $$->setLoc(@$);
    }
    /* New expression - pointer type */
    /* `new int(5)` -- tests/samples/variables.fin:28, :36 and
       simple_pointers.fin:24. Every other `new` form starts with an IDENTIFIER,
       so heap-allocating a builtin was a syntax error: "unexpected TYPE_INT,
       expecting IDENTIFIER". `primitive_type` rather than `type` keeps it out of
       the way of the five IDENTIFIER forms -- TYPE_INT and friends are their own
       tokens, so no state has to choose. */
    | KW_NEW primitive_type LPAREN arguments RPAREN {
        auto ty = std::make_unique<fin::TypeNode>($2);
        ty->setLoc(@2);
        $$ = std::make_unique<fin::NewExpression>(std::move(ty), std::move($4));
        $$->setLoc(@$);
    }
    /* `new int*` in the struct-free half of the grammar; see the copy of this
       production in `expression` for what it is for. */
    | KW_NEW primitive_type ptr_stars {
        std::unique_ptr<fin::TypeNode> ty = std::make_unique<fin::TypeNode>($2);
        ty->setLoc(@2);
        /* One star fewer than written: the stars describe the type of the
           result, and `new` already yields a pointer to what it allocated. That
           is the reading the sample itself uses -- `***yy = new int*` with `yy`
           declared `<&&&&int>` only balances if `new int*` is an `&int`. */
        for (int i = 1; i < $3; ++i) {
            ty = std::make_unique<fin::PointerTypeNode>(std::move(ty));
            ty->setLoc(@$);
        }
        $$ = std::make_unique<fin::NewExpression>(std::move(ty), std::vector<std::unique_ptr<fin::Expression>>());
        $$->setLoc(@$);
    }
    | KW_NEW AMPERSAND type LBRACE field_assignments RBRACE {
        auto ptr_type = std::make_unique<fin::PointerTypeNode>(std::move($3));
        $$ = std::make_unique<fin::NewExpression>(std::move(ptr_type), std::move($5));
        $$->setLoc(@$);
    }
    /* New expression - Self type */
    | KW_NEW KW_SELF_TYPE LBRACE field_assignments RBRACE {
        auto type = std::make_unique<fin::TypeNode>("Self");
        type->setLoc(@2);
        $$ = std::make_unique<fin::NewExpression>(std::move(type), std::move($4));
        $$->setLoc(@$);
    }
    ;

static_method_call:
    /* Case 1: Vec2::zero() */
    IDENTIFIER DOUBLE_COLON IDENTIFIER LPAREN arguments RPAREN {
        auto type = std::make_unique<fin::TypeNode>($1);
        // @1, not @$: the type name is what fails to resolve, so the caret belongs
        // on it and not on the whole call. Without this the diagnostic had no
        // location at all and came out at 1:1 -- see
        // Soundness_DiagnosticLocation.AStaticCallsTypeReportsWhereItWasWritten.
        type->setLoc(@1);
        $$ = std::make_unique<fin::StaticMethodCall>(std::move(type), $3, std::move($5));
        $$->setLoc(@$);
    }
    /* Case 2: Vec2::<float>::zero() */
    | IDENTIFIER DOUBLE_COLON LT type_list GT DOUBLE_COLON IDENTIFIER LPAREN arguments RPAREN {
        auto type = std::make_unique<fin::TypeNode>($1);
        type->generics = std::move($4);
        type->setLoc(@1);
        $$ = std::make_unique<fin::StaticMethodCall>(std::move(type), $7, std::move($9));
        $$->setLoc(@$);
    }
    /* Case 4: maybe::unpack::<A>() -- turbofish after the method name rather
       than after the type. tests/samples/nullifier.fin:28. Case 2 is the other
       placement and both are in the corpus. */
    | IDENTIFIER DOUBLE_COLON IDENTIFIER DOUBLE_COLON LT type_list GT LPAREN arguments RPAREN {
        auto type = std::make_unique<fin::TypeNode>($1);
        type->setLoc(@1);
        $$ = std::make_unique<fin::StaticMethodCall>(std::move(type), $3, std::move($9), std::move($6));
        $$->setLoc(@$);
    }
    /* `Type::name` as a value: an enum member.
       tests/samples/literal_interface.fin:19 (`IFaceOptions::First`),
       enums.fin:19-20 (`Result::Ok`) and extern_as.fin:44 (`MyEnum::B`). Reading
       one was a syntax error -- only calling a static method was allowed -- so
       nothing could compare against an enum member.

       A MemberAccess carrying `is_static` rather than a node of its own: the
       shape is exactly an object and a member name, and a new Expression class
       needs src/ast/Visitor.hpp. */
    | IDENTIFIER DOUBLE_COLON IDENTIFIER %prec STATIC_VALUE_PREC {
        auto obj = std::make_unique<fin::Identifier>($1);
        obj->setLoc(@1);
        auto access = std::make_unique<fin::MemberAccess>(std::move(obj), $3);
        access->is_static = true;
        $$ = std::move(access);
        $$->setLoc(@$);
    }
    /* Case 3: Self::zero() */
    | KW_SELF_TYPE DOUBLE_COLON IDENTIFIER LPAREN arguments RPAREN {
        auto type = std::make_unique<fin::TypeNode>("Self");
        type->setLoc(@1);
        $$ = std::make_unique<fin::StaticMethodCall>(std::move(type), $3, std::move($5));
        $$->setLoc(@$);
    }
    ;

primary_no_struct:
    IDENTIFIER %prec PARAM_NAME_PREC { $$ = std::make_unique<fin::Identifier>($1); $$->setLoc(@$); }
    | literal { $$ = std::move($1); }
    | LPAREN expression RPAREN { $$ = std::move($2); }
    | prototype_literal { $$ = std::move($1); }
    
    /* Unquote Variable */
    | DOLLAR IDENTIFIER { $$ = std::make_unique<fin::Identifier>("$" + $2); $$->setLoc(@$); }

    | LBRACKET arguments RBRACKET { $$ = std::make_unique<fin::ArrayLiteral>(std::move($2)); $$->setLoc(@$); }
    | KW_CAST LT type GT LPAREN expression RPAREN { $$ = std::make_unique<fin::CastExpression>(std::move($3), std::move($6)); $$->setLoc(@$); }
    | KW_SIZEOF LPAREN type RPAREN { $$ = std::make_unique<fin::SizeofExpression>(std::move($3)); $$->setLoc(@$); }
    
    /* Turbofish Call */
    | IDENTIFIER DOUBLE_COLON LT type_list GT LPAREN arguments RPAREN {
        auto call = std::make_unique<fin::FunctionCall>($1, std::move($7));
        call->generic_args = std::move($4);
        $$ = std::move(call);
        $$->setLoc(@$);
    }

    /* Self Constructor Call */
    | KW_SELF_TYPE LPAREN arguments RPAREN {
        $$ = std::make_unique<fin::FunctionCall>("Self", std::move($3));
        $$->setLoc(@$);
    }

    /* Self Static Method */
    /* Self Static Method - REMOVED (Covered by static_method_call) */
    
    | static_method_call { $$ = std::move($1); }
    
    /* Quote Expression */
    | KW_QUOTE block {
        $$ = std::make_unique<fin::QuoteExpression>(std::move($2));
        $$->setLoc(@$);
    }

    /* Super Expression */
    | super_expression { $$ = std::move($1); }
    ;
    
literal:
    INTEGER { $$ = std::make_unique<fin::Literal>($1, fin::ASTTokenKind::INTEGER); $$->setLoc(@$); }
    | FLOAT { $$ = std::make_unique<fin::Literal>($1, fin::ASTTokenKind::FLOAT); $$->setLoc(@$); }
    | STRING_LITERAL { $$ = std::make_unique<fin::Literal>($1, fin::ASTTokenKind::STRING_LITERAL); $$->setLoc(@$); }
    | CHAR_LITERAL { $$ = std::make_unique<fin::Literal>($1, fin::ASTTokenKind::CHAR_LITERAL); $$->setLoc(@$); }
    | KW_NULL { $$ = std::make_unique<fin::Literal>("null", fin::ASTTokenKind::KW_NULL); $$->setLoc(@$); }
    | KW_TRUE { $$ = std::make_unique<fin::Literal>("true", fin::ASTTokenKind::BOOL); $$->setLoc(@$); }
    | KW_FALSE { $$ = std::make_unique<fin::Literal>("false", fin::ASTTokenKind::BOOL); $$->setLoc(@$); }
    /* `m1778` means "not implemented" and nothing else (ADR 0001). `%token
       KW_M1778` existed with zero productions, so `blame m1778;` -- the idiom the
       standard library uses, tests/samples/stdlib/types.fin:18 and
       tests/samples/enums.fin:41 -- was a syntax error. The two other roles the
       corpus gave the word, `m1778 { }` as an infinite loop and
       `m1778 kilo = 19;` as an inferred declaration, are deliberately not
       accepted; ADR 0001 rewrites those samples instead. */
    | KW_M1778 { $$ = std::make_unique<fin::Literal>("m1778", fin::ASTTokenKind::M1778); $$->setLoc(@$); }
    ;

arguments:
    expression_list { $$ = std::move($1); }
    | %empty { $$ = std::vector<std::unique_ptr<fin::Expression>>(); }
    ;

expression_list:
    expression_list COMMA expression { $1.push_back(std::move($3)); $$ = std::move($1); }
    | expression { std::vector<std::unique_ptr<fin::Expression>> v; v.push_back(std::move($1)); $$ = std::move(v); }
    /* Forwarding a variadic: `format!(fmt, ...objects)` --
       tests/samples/stdlib/stdio.fin:36, where `objects` is the `...objects`
       parameter declared one line above. A UnaryOp(SPREAD) so the argument stays
       one expression and the callee sees where the pack was expanded. */
    | expression_list COMMA ELLIPSIS expression {
        auto spread = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::SPREAD, std::move($4));
        spread->setLoc(@3);
        $1.push_back(std::move(spread));
        $$ = std::move($1);
    }
    | ELLIPSIS expression {
        auto spread = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::SPREAD, std::move($2));
        spread->setLoc(@$);
        std::vector<std::unique_ptr<fin::Expression>> v;
        v.push_back(std::move(spread));
        $$ = std::move(v);
    }
    ;

macro_arg_item:
    expression {
        std::vector<std::unique_ptr<fin::Expression>> v;
        v.push_back(std::move($1));
        $$ = std::move(v);
    }
    | expression COLON expression {
        std::vector<std::unique_ptr<fin::Expression>> v;
        v.push_back(std::move($1));
        v.push_back(std::move($3));
        $$ = std::move(v);
    }
    /* `"alex" => 10` inside a macro call -- tests/samples/useful_macros.fin:6-7.
       The pair is flattened into two arguments, exactly as the `key: value` form
       above it already is, so a macro body reads `$0`/`$1` for the first pair
       either way and no argument is dropped. */
    | expression ARROW expression {
        std::vector<std::unique_ptr<fin::Expression>> v;
        v.push_back(std::move($1));
        v.push_back(std::move($3));
        $$ = std::move(v);
    }
    ;

macro_arg_list_body:
    macro_arg_list_body COMMA macro_arg_item {
        for(auto& e : $3) $1.push_back(std::move(e));
        $$ = std::move($1);
    }
    | macro_arg_item { $$ = std::move($1); }
    ;

macro_arguments:
    macro_arg_list_body { $$ = std::move($1); }
    | macro_arg_list_body COMMA { $$ = std::move($1); }
    | %empty { $$ = std::vector<std::unique_ptr<fin::Expression>>(); }
    ;

field_assignments:
    field_assignments COMMA IDENTIFIER COLON expression {
        $1.push_back({$3, std::move($5)}); $$ = std::move($1);
    }
    | IDENTIFIER COLON expression {
        std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> v;
        v.push_back({$1, std::move($3)}); $$ = std::move(v);
    }
    | %empty { $$ = std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>>(); }
    ;

/* --- HELPERS --- */

/* Visibility where it decides *export*: a top-level declaration, and the
   `@special(pub)` form. Two-state on purpose -- an unwritten modifier and a
   written `priv` mean the same thing there, since neither exports. Whether `pub`
   is required to export at all is an open question for the owner and this
   production does not settle it either way.

   Members use `member_visibility` instead, which distinguishes the two. */
visibility_opt:
    KW_PUB { $$ = true; }
    | KW_PRIV { $$ = false; }
    | %empty { $$ = false; }
    ;

/* Visibility of a member of a struct, interface, implements block or enum, as
   three states: 1 for `pub`, 0 for `priv`, -1 for nothing written. The third is
   the point. A member with no modifier takes the body's default -- the label in
   force, or the language default if there is none -- and that default is
   **public**.

   tests/samples/structs.fin settles it. It is `//@ ok` and therefore normative,
   and it writes `struct Vector3 { x <float>, y <float>, z <float> }` with no
   modifier on any field, then reads all three from `main()` at :10. A private
   default makes that sample unwritable. Three more pieces of evidence agree:

     - `priv` and `priv:` are written in eight corpus files (stdlib/stdptr.fin:11
       and :38, lib/std/stdio.fin:98 and :112, letssee.fin:10,
       lib/std/hashmap.fin:46, stdlib/hashmap.fin:16, stdlib/collection.fin:13,
       :45 and :51, stdlib/types.fin:13, stdlib/enums.fin:12). Every one of those
       is redundant in a private-by-default language.
     - every privacy claim in the corpus is explicit; no sample relies on a field
       being private without saying so.
     - complex.fin:13 reads `b.val` of `struct Box<T> { val <T> }` from `main()`.

   Before this, members shared `visibility_opt` and an unwritten modifier was
   indistinguishable from a written `priv`. */
member_visibility:
    KW_PUB { $$ = 1; }
    | KW_PRIV { $$ = 0; }
    | %empty { $$ = -1; }
    ;

/* A visibility *label*, `pub:` or `priv:`, which changes the default for every
   item after it in the same type body until the next label -- as against
   `member_visibility`, which decorates one item. tests/samples/letssee.fin:10,
   readonly.fin:18 and :35, stdlib/types.fin:13, stdlib/collection.fin:13 and
   stdlib/stdptr.fin:11 all open a body with one.

   No conflict with `member_visibility: KW_PUB`, even though both start with the
   same token: nothing in an item can begin with COLON, so a COLON lookahead
   after KW_PUB can only be this. The state carrying the current default is a
   field on the body accumulator (`label_public`) rather than a parser-global,
   so a nested type body gets its own and does not inherit the label of the body
   it sits in. */
visibility_label:
    KW_PUB COLON { $$ = true; }
    | KW_PRIV COLON { $$ = false; }
    ;

/* `readonly` on a member: readable everywhere, writable only inside the
   declaring type (tests/samples/readonly.fin:9's own comment). Absent from
   wave 2's list in docs/plan.md, but readonly.fin is normative and built
   entirely on it, and stdlib/stdptr.fin:16 uses it too.

   Safe as a keyword: `readonly` never appears as an identifier anywhere in the
   corpus. */
readonly_opt:
    KW_READONLY { $$ = true; }
    | %empty { $$ = false; }
    ;

enum_values:
    enum_values COMMA enum_value { $1.push_back(std::move($3)); $$ = std::move($1); }
    /* Trailing comma after the last member: `enum IFaceOptions { First = 1,
       Second = 2, }` -- tests/samples/literal_interface.fin:15, which is where
       that file's parse stopped. Every other list the corpus writes over several
       lines ends the same way, so this is the language's habit and not a typo. */
    | enum_values COMMA { $$ = std::move($1); }
    | enum_value { std::vector<fin::EnumMember> v; v.push_back(std::move($1)); $$ = std::move(v); }
    | %empty { $$ = std::vector<fin::EnumMember>(); }
    ;

/* A member of an enum. Every production starts with `member_visibility` --
   even the ones that cannot be labelled -- because a `member_visibility` that can
   be empty has to be reduced with the member's own IDENTIFIER as the lookahead,
   and the parser cannot both reduce the empty visibility and shift that IDENTIFIER
   for a production that lacks it. Same reason `struct_body_content` routes its
   label through `attributes_opt`.

   An enum body has no `pub:` / `priv:` labels, so an unwritten modifier resolves
   straight to the language default, which is public -- `$1 != 0` rather than a
   consultation of any accumulator. Nothing reads an enum member's `is_public`
   today; it is spelled this way so that whoever first does gets the same answer a
   struct field gives. */
enum_value:
    member_visibility IDENTIFIER {
        $$ = fin::EnumMember{$2, nullptr, {}, $1 != 0};
    }
    | member_visibility IDENTIFIER EQUAL expression {
        $$ = fin::EnumMember{$2, std::move($4), {}, $1 != 0};
    }
    /* A member with a payload: `RGB(uint{8}, uint{8}, uint{8})` --
       tests/samples/enums.fin:8, `Ok(T)` -- stdlib/typing.fin:15. The types are
       what the member carries, which is what makes a generic enum Fin's sum type. */
    | member_visibility IDENTIFIER LPAREN type_list RPAREN {
        $$ = fin::EnumMember{$2, nullptr, std::move($4), $1 != 0};
    }
    /* The bracketed spelling of the same thing, one type: `pub Err <IOError>` --
       tests/samples/stdlib/stdio.fin:50-51. Both spellings are in the corpus, as
       they are for a parameter type and for a type alias, so both are accepted and
       neither is preferred. */
    | member_visibility IDENTIFIER LT type GT {
        std::vector<std::unique_ptr<fin::TypeNode>> types;
        types.push_back(std::move($4));
        $$ = fin::EnumMember{$2, nullptr, std::move(types), $1 != 0};
    }
    ;

prototype_literal:
    LBRACE prototype_elements RBRACE { 
        $$ = std::make_unique<fin::PrototypeLiteral>(std::move($2)); 
        $$->setLoc(@$); 
    }
    ;

prototype_elements:
      prototype_elements COMMA expression COLON expression { $1.push_back({std::move($3), std::move($5)}); $$ = std::move($1); }
    | expression COLON expression { std::vector<std::pair<std::unique_ptr<fin::Expression>, std::unique_ptr<fin::Expression>>> v; v.push_back({std::move($1), std::move($3)}); $$ = std::move(v); }
    ;

%%

void fin::parser::error(const location_type& l, const std::string& m) {
    diag.reportError(l, m);
}
