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
%token KW_LET KW_BEZ KW_CONST KW_BETON KW_AUTO
%token KW_FUN KW_NORET KW_RETURN
%token KW_PUB KW_PRIV
%token KW_STRUCT KW_ENUM KW_INTERFACE
%token KW_MACRO KW_STATIC KW_NULL
%token KW_WHILE KW_FOR KW_FOREACH KW_BREAK KW_CONTINUE
%token KW_IF KW_ELSE KW_ELSEIF KW_IN
%token KW_TRY KW_CATCH KW_BLAME KW_SUPER KW_SELF_TYPE
%token KW_IMPORT KW_AS KW_FROM
%token KW_NEW KW_DELETE KW_SIZEOF KW_TYPEOF KW_AS_PTR KW_CAST
%token KW_OPERATOR
%token KW_SPECIAL KW_AT_RETURN KW_FN_TYPE KW_DEFINE
%token KW_M1778

/* Types */
%token TYPE_INT TYPE_FLOAT TYPE_DOUBLE TYPE_BOOL 
%token TYPE_STRING TYPE_CHAR TYPE_VOID TYPE_LONG

/* Punctuation */
%token LPAREN RPAREN LBRACE RBRACE LBRACKET RBRACKET
%token SEMICOLON COLON DOUBLE_COLON COMMA DOT ELLIPSIS
%token AT DOLLAR HASH
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

/* ========================================================================== */
/*                                PRECEDENCE                                  */
/* ========================================================================== */

/* Lowest precedence */
%right ARROW
%right EQUAL PLUSEQUAL MINUSEQUAL MULTEQUAL DIVEQUAL
%right QUESTION COLON
%left OR
%left AND
%nonassoc EQEQ NOTEQ LT GT LTEQ GTEQ
%left PLUS MINUS
%left MULT DIV MOD
%right NOT UMINUS ADDRESSOF_PREC DEREFERENCE_PREC KW_SIZEOF KW_NEW KW_CAST
%left LPAREN LBRACKET DOT LBRACE
%left INCREMENT DECREMENT

/* Control Flow Precedence */
%precedence KW_IFX
%precedence KW_ELSE

/* ========================================================================== */
/*                                    TYPES                                   */
/* ========================================================================== */

/* Core */
%type <std::unique_ptr<fin::Program>> program
%type <std::vector<std::unique_ptr<fin::Statement>>> statements block_stmts
%type <std::unique_ptr<fin::Statement>> statement 
%type <std::unique_ptr<fin::Block>> block

/* Declarations */
%type <std::unique_ptr<fin::Statement>> variable_declaration 
%type <std::unique_ptr<fin::Statement>> import_statement define_declaration macro_declaration
%type <std::unique_ptr<fin::Statement>> declaration_body
%type <std::unique_ptr<fin::Statement>> annotated_declaration declaration_with_vis bare_declaration

%type <std::unique_ptr<fin::TypeNode>> type base_type pointer_type array_type
%type <std::vector<std::unique_ptr<fin::TypeNode>>> type_list
%type <std::vector<std::unique_ptr<fin::Parameter>>> params param_list
%type <std::unique_ptr<fin::Parameter>> param

/* Generics & Attributes */
%type <std::vector<std::unique_ptr<fin::GenericParam>>> generic_params_opt generic_param_list
%type <std::unique_ptr<fin::GenericParam>> generic_param
%type <std::vector<std::unique_ptr<fin::Attribute>>> attributes_opt attribute_list
%type <std::unique_ptr<fin::Attribute>> attribute

/* Extern Params */
%type <std::pair<std::vector<std::unique_ptr<fin::Parameter>>, bool>> extern_params

/* Structs & Interfaces */
%type <std::unique_ptr<fin::StructDeclaration>> struct_body_content
%type <std::unique_ptr<fin::InterfaceDeclaration>> interface_body_content
%type <std::vector<std::unique_ptr<fin::TypeNode>>> inheritance_opt
%type <std::unique_ptr<fin::ASTNode>> interface_item_rest

/* Struct Items */
%type <std::unique_ptr<fin::ASTNode>> struct_item_rest

/* Enums & Imports */
%type <std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>>> enum_values
%type <std::pair<std::string, std::unique_ptr<fin::Expression>>> enum_value
%type <std::vector<std::string>> import_list
%type <std::vector<std::unique_ptr<fin::GenericParam>>> operator_generics_opt

/* Control Flow */
%type <std::unique_ptr<fin::Statement>> if_statement while_loop for_loop foreach_loop try_catch_statement blame_statement return_statement expression_statement
%type <std::unique_ptr<fin::Statement>> control_statement delete_statement

/* Expressions */
%type <std::unique_ptr<fin::Expression>> expression no_struct_expression
%type <std::unique_ptr<fin::Expression>> literal
%type <std::vector<std::unique_ptr<fin::Expression>>> arguments expression_list
%type <std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>>> field_assignments
%type <std::unique_ptr<fin::SuperExpression>> super_expression

/* Types */
%type <std::unique_ptr<fin::FunctionTypeNode>> fn_type
%type <std::unique_ptr<fin::LambdaExpression>> lambda_expression
%type <std::unique_ptr<fin::Expression>> primary_no_struct

/* Helpers */
%type <bool> visibility_opt
%type <std::string> primitive_type dotted_path
%type <fin::ASTTokenKind> operator_symbol
%type <std::vector<fin::MacroParam>> macro_param_list
%type <fin::MacroParam> macro_param
%type <std::unique_ptr<fin::StaticMethodCall>> static_method_call

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
        $1.push_back(std::move($2));
        $$ = std::move($1);
    }
    | statement {
        std::vector<std::unique_ptr<fin::Statement>> vec;
        if ($1) vec.push_back(std::move($1));
        $$ = std::move(vec);
    }
    ;

statement:
      annotated_declaration { $$ = std::move($1); }
    | declaration_with_vis   { $$ = std::move($1); }
    | bare_declaration       { $$ = std::move($1); }
    | variable_declaration   { $$ = std::move($1); }
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
        if (auto* func = dynamic_cast<fin::FunctionDeclaration*>($$.get())) {
            func->attributes = std::move($1);
            func->is_public = $2;
        } else if (auto* st = dynamic_cast<fin::StructDeclaration*>($$.get())) {
            st->attributes = std::move($1);
            st->is_public = $2;
        } else if (auto* en = dynamic_cast<fin::EnumDeclaration*>($$.get())) {
            en->attributes = std::move($1);
            en->is_public = $2;
        } else if (auto* in = dynamic_cast<fin::InterfaceDeclaration*>($$.get())) {
            in->attributes = std::move($1);
            in->is_public = $2;
        }
        $$->setLoc(@$);
    }
    ;

declaration_with_vis:
    KW_PUB declaration_body {
        $$ = std::move($2);
        if (auto* func = dynamic_cast<fin::FunctionDeclaration*>($$.get())) func->is_public = true;
        else if (auto* st = dynamic_cast<fin::StructDeclaration*>($$.get())) st->is_public = true;
        else if (auto* en = dynamic_cast<fin::EnumDeclaration*>($$.get())) en->is_public = true;
        else if (auto* in = dynamic_cast<fin::InterfaceDeclaration*>($$.get())) in->is_public = true;
        $$->setLoc(@$);
    }
    | KW_PRIV declaration_body {
        $$ = std::move($2);
        if (auto* func = dynamic_cast<fin::FunctionDeclaration*>($$.get())) func->is_public = false;
        else if (auto* st = dynamic_cast<fin::StructDeclaration*>($$.get())) st->is_public = false;
        else if (auto* en = dynamic_cast<fin::EnumDeclaration*>($$.get())) en->is_public = false;
        else if (auto* in = dynamic_cast<fin::InterfaceDeclaration*>($$.get())) in->is_public = false;
        $$->setLoc(@$);
    }
    ;

bare_declaration:
    declaration_body { $$ = std::move($1); }
    ;

declaration_body:
    KW_FUN IDENTIFIER generic_params_opt LPAREN params RPAREN LT type GT block {
        $$ = std::make_unique<fin::FunctionDeclaration>($2, std::move($5), std::move($8), std::move($10));
        static_cast<fin::FunctionDeclaration*>($$.get())->generic_params = std::move($3);
    }
    | KW_FUN IDENTIFIER generic_params_opt LPAREN params RPAREN KW_NORET block {
        auto voidType = std::make_unique<fin::TypeNode>("void");
        $$ = std::make_unique<fin::FunctionDeclaration>($2, std::move($5), std::move(voidType), std::move($8));
        static_cast<fin::FunctionDeclaration*>($$.get())->generic_params = std::move($3);
    }
    | KW_FUN IDENTIFIER generic_params_opt LPAREN params RPAREN LT type GT SEMICOLON {
        $$ = std::make_unique<fin::FunctionDeclaration>($2, std::move($5), std::move($8), nullptr);
        static_cast<fin::FunctionDeclaration*>($$.get())->generic_params = std::move($3);
    }
    | KW_STRUCT IDENTIFIER generic_params_opt inheritance_opt LBRACE struct_body_content RBRACE {
        $6->name = $2;
        $6->generic_params = std::move($3);
        $6->parents = std::move($4);
        $$ = std::move($6);
    }
    | KW_INTERFACE IDENTIFIER generic_params_opt LBRACE interface_body_content RBRACE {
        $5->name = $2;
        $5->generic_params = std::move($3);
        $$ = std::move($5);
    }
    | KW_ENUM IDENTIFIER LBRACE enum_values RBRACE {
        $$ = std::make_unique<fin::EnumDeclaration>($2, std::move($4), false);
    }
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
    HASH LBRACKET IDENTIFIER EQUAL STRING_LITERAL RBRACKET {
        $$ = std::make_unique<fin::Attribute>($3, $5);
        $$->setLoc(@$);
    }
    | HASH LBRACKET IDENTIFIER RBRACKET {
        $$ = std::make_unique<fin::Attribute>($3, true);
        $$->setLoc(@$);
    }
    | HASH LBRACKET IDENTIFIER LPAREN dotted_path RPAREN RBRACKET {
        $$ = std::make_unique<fin::Attribute>($3, $5);
        $$->setLoc(@$);
    }
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
    ;

/* --- IMPORTS --- */

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
    /* Case 2: import lib.mod; */
    | KW_IMPORT dotted_path SEMICOLON {
        std::vector<std::string> empty;
        $$ = std::make_unique<fin::ImportModule>($2, true, "", empty);
        $$->setLoc(@$);
    }
    /* Case 3: import lib.mod as alias; */
    | KW_IMPORT dotted_path KW_AS IDENTIFIER SEMICOLON {
        std::vector<std::string> empty;
        $$ = std::make_unique<fin::ImportModule>($2, true, $4, empty);
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
    /* Case 5: import { A, B } from lib.mod; */
    | KW_IMPORT LBRACE import_list RBRACE KW_FROM dotted_path SEMICOLON {
        $$ = std::make_unique<fin::ImportModule>($6, true, "", $3);
        $$->setLoc(@$);
    }
    ;
import_list:
    import_list COMMA IDENTIFIER { $1.push_back($3); $$ = std::move($1); }
    | IDENTIFIER { std::vector<std::string> v; v.push_back($1); $$ = std::move(v); }
    ;

dotted_path:
    IDENTIFIER { $$ = $1; }
    | dotted_path DOT IDENTIFIER { $$ = $1 + "." + $3; }
    ;

/* --- STRUCTS --- */

inheritance_opt:
    COLON LT type_list GT { $$ = std::move($3); }
    | %empty { $$ = std::vector<std::unique_ptr<fin::TypeNode>>(); }
    ;

struct_body_content:
    struct_body_content attributes_opt visibility_opt struct_item_rest {
        if (auto* member = dynamic_cast<fin::StructMember*>($4.get())) {
            member->attributes = std::move($2);
            member->is_public = $3;
            $1->members.push_back(std::unique_ptr<fin::StructMember>(static_cast<fin::StructMember*>($4.release())));
        } 
        else if (auto* func = dynamic_cast<fin::FunctionDeclaration*>($4.get())) {
            func->attributes = std::move($2);
            func->is_public = $3;
            $1->methods.push_back(std::unique_ptr<fin::FunctionDeclaration>(static_cast<fin::FunctionDeclaration*>($4.release())));
        }
        else if (auto* op = dynamic_cast<fin::OperatorDeclaration*>($4.get())) {
            op->is_public = $3;
            $1->operators.push_back(std::unique_ptr<fin::OperatorDeclaration>(static_cast<fin::OperatorDeclaration*>($4.release())));
        }
        else if (auto* ctor = dynamic_cast<fin::ConstructorDeclaration*>($4.get())) {
            $1->constructors.push_back(std::unique_ptr<fin::ConstructorDeclaration>(static_cast<fin::ConstructorDeclaration*>($4.release())));
        }
        else if (auto* dtor = dynamic_cast<fin::DestructorDeclaration*>($4.get())) {
            $1->destructor = std::unique_ptr<fin::DestructorDeclaration>(static_cast<fin::DestructorDeclaration*>($4.release()));
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
    /* Method */
    | KW_FUN IDENTIFIER generic_params_opt LPAREN params RPAREN LT type GT block {
        $$ = std::make_unique<fin::FunctionDeclaration>($2, std::move($5), std::move($8), std::move($10));
        static_cast<fin::FunctionDeclaration*>($$.get())->generic_params = std::move($3);
        $$->setLoc(@$);
    }
    | KW_FUN IDENTIFIER generic_params_opt LPAREN params RPAREN KW_NORET block {
        auto voidType = std::make_unique<fin::TypeNode>("void");
        $$ = std::make_unique<fin::FunctionDeclaration>($2, std::move($5), std::move(voidType), std::move($8));
        static_cast<fin::FunctionDeclaration*>($$.get())->generic_params = std::move($3);
        $$->setLoc(@$);
    }
    /* Static Method */
    | KW_STATIC KW_FUN IDENTIFIER generic_params_opt LPAREN params RPAREN LT type GT block {
        $$ = std::make_unique<fin::FunctionDeclaration>($3, std::move($6), std::move($9), std::move($11));
        auto* func = static_cast<fin::FunctionDeclaration*>($$.get());
        func->generic_params = std::move($4);
        func->is_static = true;
        $$->setLoc(@$);
    }
    /* Operator */
    | KW_OPERATOR operator_symbol operator_generics_opt LPAREN params RPAREN LT type GT block {
        auto op = std::make_unique<fin::OperatorDeclaration>($2, std::move($5), std::move($8), std::move($10), false);
        op->generic_params = std::move($3);
        $$ = std::move(op);
        $$->setLoc(@$);
    }
    /* Constructor */
    | IDENTIFIER LPAREN params RPAREN block {
        $$ = std::make_unique<fin::ConstructorDeclaration>($1, std::move($3), std::move($5));
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
    | EQEQ { $$ = fin::ASTTokenKind::EQEQ; }
    ;

operator_generics_opt:
    COLON LT generic_param_list GT { $$ = std::move($3); }
    | %empty { $$ = std::vector<std::unique_ptr<fin::GenericParam>>(); }
    ;

/* --- INTERFACES --- */

interface_body_content:
    interface_body_content attributes_opt visibility_opt interface_item_rest {
        if (auto* member = dynamic_cast<fin::StructMember*>($4.get())) {
            member->attributes = std::move($2);
            member->is_public = $3;
            $1->members.push_back(std::unique_ptr<fin::StructMember>(static_cast<fin::StructMember*>($4.release())));
        }
        else if (auto* func = dynamic_cast<fin::FunctionDeclaration*>($4.get())) {
            func->attributes = std::move($2);
            func->is_public = $3;
            $1->methods.push_back(std::unique_ptr<fin::FunctionDeclaration>(static_cast<fin::FunctionDeclaration*>($4.release())));
        }
        else if (auto* op = dynamic_cast<fin::OperatorDeclaration*>($4.get())) {
            op->is_public = $3;
            $1->operators.push_back(std::unique_ptr<fin::OperatorDeclaration>(static_cast<fin::OperatorDeclaration*>($4.release())));
        }
        else if (auto* ctor = dynamic_cast<fin::ConstructorDeclaration*>($4.get())) {
            $1->constructors.push_back(std::unique_ptr<fin::ConstructorDeclaration>(static_cast<fin::ConstructorDeclaration*>($4.release())));
        }
        else if (auto* dtor = dynamic_cast<fin::DestructorDeclaration*>($4.get())) {
            $1->destructor = std::unique_ptr<fin::DestructorDeclaration>(static_cast<fin::DestructorDeclaration*>($4.release()));
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
    /* Method: fun ... */
    | declaration_body { $$ = std::move($1); }
    
    /* Abstract Operator */
    | KW_OPERATOR operator_symbol operator_generics_opt LPAREN params RPAREN LT type GT SEMICOLON {
        auto op = std::make_unique<fin::OperatorDeclaration>($2, std::move($5), std::move($8), nullptr, false);
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

params:
    param_list { $$ = std::move($1); }
    | %empty { $$ = std::vector<std::unique_ptr<fin::Parameter>>(); }
    ;

param_list:
    param_list COMMA param { $1.push_back(std::move($3)); $$ = std::move($1); }
    | param { std::vector<std::unique_ptr<fin::Parameter>> v; v.push_back(std::move($1)); $$ = std::move(v); }
    ;

param:
    IDENTIFIER COLON LT type GT {
        $$ = std::make_unique<fin::Parameter>($1, std::move($4), nullptr, false);
        $$->setLoc(@$);
    }
    | ELLIPSIS IDENTIFIER COLON LT type GT {
        $$ = std::make_unique<fin::Parameter>($2, std::move($5), nullptr, true);
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
    | AT KW_DEFINE IDENTIFIER LPAREN extern_params RPAREN KW_NORET SEMICOLON {
        auto voidType = std::make_unique<fin::TypeNode>("void");
        $$ = std::make_unique<fin::DefineDeclaration>($3, std::move($5.first), std::move(voidType), $5.second);
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
    ;

/* --- TYPES --- */

type:
    base_type { $$ = std::move($1); }
    | pointer_type { $$ = std::move($1); }
    | array_type { $$ = std::move($1); }
    ;

base_type:
    primitive_type { $$ = std::make_unique<fin::TypeNode>($1); }
    | IDENTIFIER { $$ = std::make_unique<fin::TypeNode>($1); }
    | IDENTIFIER LT type_list GT { 
        $$ = std::make_unique<fin::TypeNode>($1); 
        $$->generics = std::move($3);
    }
    | KW_AUTO { $$ = std::make_unique<fin::TypeNode>("auto"); }
    | KW_SELF_TYPE { $$ = std::make_unique<fin::TypeNode>("Self"); }
    | fn_type { $$ = std::move($1); }
    | LPAREN type RPAREN { $$ = std::move($2); }
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
    /* Handle empty params fn() => int */
    | KW_FN_TYPE LPAREN RPAREN ARROW type {
        std::vector<std::unique_ptr<fin::TypeNode>> empty;
        $$ = std::make_unique<fin::FunctionTypeNode>(std::move(empty), std::move($5));
        $$->setLoc(@$);
    }
    ;

type_list:
    type_list COMMA type { $1.push_back(std::move($3)); $$ = std::move($1); }
    | type { std::vector<std::unique_ptr<fin::TypeNode>> v; v.push_back(std::move($1)); $$ = std::move(v); }
    ;

pointer_type:
    AMPERSAND type {
        $$ = std::make_unique<fin::PointerTypeNode>(std::move($2));
        $$->setLoc(@$);
    }
    | AND type {
        // &&T -> Pointer(Pointer(T))
        auto inner = std::make_unique<fin::PointerTypeNode>(std::move($2));
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
    | KW_IF LPAREN expression RPAREN block KW_ELSE block {
        $$ = std::make_unique<fin::IfStatement>(std::move($3), std::move($5), std::move($7));
        $$->setLoc(@$);
    }
    ;

while_loop:
    KW_WHILE LPAREN expression RPAREN block {
        $$ = std::make_unique<fin::WhileLoop>(std::move($3), std::move($5));
        $$->setLoc(@$);
    }
    ;

for_loop:
    KW_FOR LPAREN variable_declaration expression SEMICOLON expression RPAREN block {
        $$ = std::make_unique<fin::ForLoop>(std::move($3), std::move($4), std::move($6), std::move($8));
        $$->setLoc(@$);
    }
    ;

foreach_loop:
    /* Use no_struct_expression to avoid ambiguity with block start */
    KW_FOREACH IDENTIFIER LT type GT KW_IN no_struct_expression block {
        $$ = std::make_unique<fin::ForeachLoop>($2, std::move($4), std::move($7), std::move($8));
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
    KW_TRY block KW_CATCH LPAREN IDENTIFIER KW_AS type RPAREN block {
        $$ = std::make_unique<fin::TryCatch>(std::move($2), $5, std::move($7), std::move($9));
        $$->setLoc(@$);
    }
    ;

blame_statement:
    KW_BLAME expression SEMICOLON {
        $$ = std::make_unique<fin::BlameStatement>(std::move($2));
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
    /* Case 3: Arrow Expression (Full): (a: <int>) <int> => expr */
    | LPAREN params RPAREN LT type GT ARROW expression {
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
    
    /* Unary Ops */
    | MINUS expression %prec UMINUS { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::MINUS, std::move($2)); $$->setLoc(@$); }
    | NOT expression { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::NOT, std::move($2)); $$->setLoc(@$); }
    | AMPERSAND expression %prec ADDRESSOF_PREC { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::AMPERSAND, std::move($2)); $$->setLoc(@$); }
    | MULT expression %prec DEREFERENCE_PREC { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::MULT, std::move($2)); $$->setLoc(@$); }
    | INCREMENT expression { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::INCREMENT, std::move($2)); $$->setLoc(@$); }
    | DECREMENT expression { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::DECREMENT, std::move($2)); $$->setLoc(@$); }
    
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
    | expression DOT IDENTIFIER {
        $$ = std::make_unique<fin::MemberAccess>(std::move($1), $3);
        $$->setLoc(@$);
    }
    | expression INCREMENT { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::INCREMENT, std::move($1)); $$->setLoc(@$); }
    | expression DECREMENT { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::DECREMENT, std::move($1)); $$->setLoc(@$); }
    | expression LBRACKET expression RBRACKET {
         $$ = std::make_unique<fin::ArrayAccess>(std::move($1), std::move($3));
         $$->setLoc(@$);
    }
    | expression QUESTION expression COLON expression {
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

    /* Primary */
    | primary_no_struct { $$ = std::move($1); }

    | lambda_expression { $$ = std::move($1); }

    /* Struct Instantiations */
    | IDENTIFIER LBRACE field_assignments RBRACE {
        $$ = std::make_unique<fin::StructInstantiation>($1, std::move($3));
        $$->setLoc(@$);
    }
    | IDENTIFIER DOUBLE_COLON LT type_list GT LBRACE field_assignments RBRACE {
        $$ = std::make_unique<fin::StructInstantiation>($1, std::move($7), std::move($4));
        $$->setLoc(@$);
    }
    | KW_NEW type LBRACE field_assignments RBRACE {
        $$ = std::make_unique<fin::NewExpression>(std::move($2), std::move($4));
        $$->setLoc(@$);
    }
    | KW_NEW type LPAREN arguments RPAREN {
        $$ = std::make_unique<fin::NewExpression>(std::move($2), std::move($4));
        $$->setLoc(@$);
    }
    | KW_SELF_TYPE LBRACE field_assignments RBRACE {
        $$ = std::make_unique<fin::StructInstantiation>("Self", std::move($3));
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
    | no_struct_expression OR no_struct_expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::OR, std::move($3)); $$->setLoc(@$); }
    | no_struct_expression AND no_struct_expression { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::AND, std::move($3)); $$->setLoc(@$); }
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
    
    /* Unary Ops */
    | MINUS no_struct_expression %prec UMINUS { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::MINUS, std::move($2)); $$->setLoc(@$); }
    | NOT no_struct_expression { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::NOT, std::move($2)); $$->setLoc(@$); }
    | AMPERSAND no_struct_expression %prec ADDRESSOF_PREC { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::AMPERSAND, std::move($2)); $$->setLoc(@$); }
    | MULT no_struct_expression %prec DEREFERENCE_PREC { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::MULT, std::move($2)); $$->setLoc(@$); }
    | INCREMENT no_struct_expression { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::INCREMENT, std::move($2)); $$->setLoc(@$); }
    | DECREMENT no_struct_expression { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::DECREMENT, std::move($2)); $$->setLoc(@$); }
    
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
    | no_struct_expression DOT IDENTIFIER {
        $$ = std::make_unique<fin::MemberAccess>(std::move($1), $3);
        $$->setLoc(@$);
    }
    | no_struct_expression INCREMENT { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::INCREMENT, std::move($1)); $$->setLoc(@$); }
    | no_struct_expression DECREMENT { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::DECREMENT, std::move($1)); $$->setLoc(@$); }
    | no_struct_expression LBRACKET expression RBRACKET {
         $$ = std::make_unique<fin::ArrayAccess>(std::move($1), std::move($3));
         $$->setLoc(@$);
    }
    | no_struct_expression QUESTION no_struct_expression COLON no_struct_expression {
        $$ = std::make_unique<fin::TernaryOp>(std::move($1), std::move($3), std::move($5));
        $$->setLoc(@$);
    }

    | no_struct_expression NOT LPAREN arguments RPAREN {
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
    
    /* Allowed Struct-like things */
    | KW_NEW type LBRACE field_assignments RBRACE {
        $$ = std::make_unique<fin::NewExpression>(std::move($2), std::move($4));
        $$->setLoc(@$);
    }
    | KW_NEW type LPAREN arguments RPAREN {
        $$ = std::make_unique<fin::NewExpression>(std::move($2), std::move($4));
        $$->setLoc(@$);
    }
    ;

static_method_call:
    /* Case 1: Vec2::zero() */
    IDENTIFIER DOUBLE_COLON IDENTIFIER LPAREN arguments RPAREN {
        auto type = std::make_unique<fin::TypeNode>($1);
        $$ = std::make_unique<fin::StaticMethodCall>(std::move(type), $3, std::move($5));
        $$->setLoc(@$);
    }
    /* Case 2: Vec2::<float>::zero() */
    | IDENTIFIER DOUBLE_COLON LT type_list GT DOUBLE_COLON IDENTIFIER LPAREN arguments RPAREN {
        auto type = std::make_unique<fin::TypeNode>($1);
        type->generics = std::move($4);
        $$ = std::make_unique<fin::StaticMethodCall>(std::move(type), $7, std::move($9));
        $$->setLoc(@$);
    }
    /* Case 3: Self::zero() */
    | KW_SELF_TYPE DOUBLE_COLON IDENTIFIER LPAREN arguments RPAREN {
        auto type = std::make_unique<fin::TypeNode>("Self");
        $$ = std::make_unique<fin::StaticMethodCall>(std::move(type), $3, std::move($5));
        $$->setLoc(@$);
    }
    ;

primary_no_struct:
    IDENTIFIER { $$ = std::make_unique<fin::Identifier>($1); $$->setLoc(@$); }
    | literal { $$ = std::move($1); }
    | LPAREN expression RPAREN { $$ = std::move($2); }
    
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
    | KW_SELF_TYPE DOUBLE_COLON IDENTIFIER LPAREN arguments RPAREN {
        $$ = std::make_unique<fin::FunctionCall>("Self::" + $3, std::move($5));
        $$->setLoc(@$);
    }
    
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
    ;

arguments:
    expression_list { $$ = std::move($1); }
    | %empty { $$ = std::vector<std::unique_ptr<fin::Expression>>(); }
    ;

expression_list:
    expression_list COMMA expression { $1.push_back(std::move($3)); $$ = std::move($1); }
    | expression { std::vector<std::unique_ptr<fin::Expression>> v; v.push_back(std::move($1)); $$ = std::move(v); }
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

visibility_opt:
    KW_PUB { $$ = true; }
    | KW_PRIV { $$ = false; }
    | %empty { $$ = false; }
    ;

enum_values:
    enum_values COMMA enum_value { $1.push_back(std::move($3)); $$ = std::move($1); }
    | enum_value { std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> v; v.push_back(std::move($1)); $$ = std::move(v); }
    | %empty { $$ = std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>>(); }
    ;

enum_value:
    IDENTIFIER { 
        $$ = std::make_pair($1, nullptr); 
    }
    | IDENTIFIER EQUAL expression { 
        $$ = std::make_pair($1, std::move($3)); 
    }
    ;

%%

void fin::parser::error(const location_type& l, const std::string& m) {
    diag.reportError(l, m);
}