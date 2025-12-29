%skeleton "lalr1.cc"
%require "3.2"
%defines
%define api.token.constructor
%define api.value.type variant
%define api.namespace {fin}
%define parse.assert
%locations

%code requires {
    #include <string>
    #include <vector>
    #include <memory>
    #include <utility>
    #include "ast/ASTNode.hpp"
    namespace fin { class DiagnosticEngine; }
}

%code {
    #include "lexer/lexer.hpp"
    #include "diagnostics/DiagnosticEngine.hpp" /* Include full header here */
    fin::parser::symbol_type yylex();
    
    namespace fin {
        std::unique_ptr<fin::Program> root;
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
%token SEMICOLON COLON DOUBLE_COLON COMMA DOT ARROW ELLIPSIS
%token AT DOLLAR HASH

/* Operators */
%token EQUAL PLUSEQUAL MINUSEQUAL MULTEQUAL DIVEQUAL
%token EQEQ NOTEQ LT GT LTEQ GTEQ
%token AND OR NOT
%token PLUS MINUS MULT DIV MOD
%token AMPERSAND
%token INCREMENT DECREMENT

/* Precedence */
%right EQUAL PLUSEQUAL MINUSEQUAL MULTEQUAL DIVEQUAL
%left OR
%left AND
%nonassoc EQEQ NOTEQ LT GT LTEQ GTEQ
%left PLUS MINUS
%left MULT DIV MOD
%right NOT UMINUS ADDRESSOF_PREC DEREFERENCE_PREC
%left LPAREN LBRACKET DOT LBRACE
%left INCREMENT DECREMENT

/* ========================================================================== */
/*                                    TYPES                                   */
/* ========================================================================== */

/* Core */
%type <std::unique_ptr<fin::Program>> program
%type <std::vector<std::unique_ptr<fin::Statement>>> statements block_stmts
%type <std::unique_ptr<fin::Statement>> statement 
%type <std::unique_ptr<fin::Block>> block

/* Declarations */
%type <std::unique_ptr<fin::Statement>> variable_declaration function_declaration struct_declaration enum_declaration
%type <std::unique_ptr<fin::Statement>> import_statement define_declaration macro_declaration
%type <std::unique_ptr<fin::TypeNode>> type base_type pointer_type array_type
%type <std::vector<std::unique_ptr<fin::TypeNode>>> type_list
%type <std::vector<std::unique_ptr<fin::Parameter>>> params param_list
%type <std::unique_ptr<fin::Parameter>> param

/* Generics & Attributes */
%type <std::vector<std::unique_ptr<fin::GenericParam>>> generic_params_opt generic_param_list
%type <std::unique_ptr<fin::GenericParam>> generic_param
%type <std::vector<std::unique_ptr<fin::Attribute>>> attributes_opt attribute_list
%type <std::unique_ptr<fin::Attribute>> attribute

/* Structs & Interfaces */
%type <std::unique_ptr<fin::StructDeclaration>> struct_body_content
%type <std::unique_ptr<fin::StructMember>> struct_member
%type <std::unique_ptr<fin::InterfaceDeclaration>> interface_body_content
%type <std::unique_ptr<fin::Statement>> interface_declaration
%type <std::unique_ptr<fin::TypeNode>> inheritance_opt

/* Control Flow */
%type <std::unique_ptr<fin::Statement>> if_statement while_loop for_loop foreach_loop try_catch_statement blame_statement return_statement expression_statement
%type <std::unique_ptr<fin::Statement>> control_statement delete_statement

/* Expressions */
%type <std::unique_ptr<fin::Expression>> expression assignment_expression conditional_expression logical_or logical_and equality comparison additive multiplicative unary postfix primary
%type <std::unique_ptr<fin::Expression>> literal
%type <std::vector<std::unique_ptr<fin::Expression>>> arguments expression_list
%type <std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>>> field_assignments

/* Helpers */
%type <bool> visibility_opt
%type <std::string> primitive_type dotted_path
%type <fin::ASTTokenKind> operator_symbol
%type <std::vector<std::string>> macro_param_list

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
    | %empty {
        $$ = std::vector<std::unique_ptr<fin::Statement>>();
    }
    ;

statement:
      variable_declaration { $$ = std::move($1); }
    | function_declaration { $$ = std::move($1); }
    | struct_declaration   { $$ = std::move($1); }
    | interface_declaration { $$ = std::move($1); }
    | enum_declaration     { $$ = std::move($1); }
    | define_declaration   { $$ = std::move($1); }
    | macro_declaration    { $$ = std::move($1); }
    | import_statement     { $$ = std::move($1); }
    | if_statement         { $$ = std::move($1); }
    | while_loop           { $$ = std::move($1); }
    | for_loop             { $$ = std::move($1); }
    | foreach_loop         { $$ = std::move($1); }
    | control_statement    { $$ = std::move($1); }
    | delete_statement     { $$ = std::move($1); }
    | try_catch_statement  { $$ = std::move($1); }
    | blame_statement      { $$ = std::move($1); }
    | return_statement     { $$ = std::move($1); }
    | expression_statement { $$ = std::move($1); }
    | SEMICOLON            { $$ = nullptr; }
    ;

block:
    LBRACE block_stmts RBRACE {
        $$ = std::make_unique<fin::Block>(std::move($2));
        $$->setLoc(@$);
    }
    ;

block_stmts:
    statements { $$ = std::move($1); }
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
    KW_IMPORT STRING_LITERAL SEMICOLON {
        std::vector<std::string> empty_targets;
        $$ = std::make_unique<fin::ImportModule>($2, false, "", empty_targets);
        $$->setLoc(@$);
    }
    | KW_IMPORT dotted_path SEMICOLON {
        std::vector<std::string> empty_targets;
        $$ = std::make_unique<fin::ImportModule>($2, true, "", empty_targets);
        $$->setLoc(@$);
    }
    ;

dotted_path:
    IDENTIFIER { $$ = $1; }
    | dotted_path DOT IDENTIFIER { $$ = $1 + "." + $3; }
    ;

/* --- STRUCTS --- */

struct_declaration:
    attributes_opt visibility_opt KW_STRUCT IDENTIFIER generic_params_opt inheritance_opt LBRACE struct_body_content RBRACE {
        $8->name = $4;
        $8->is_public = $2;
        $8->generic_params = std::move($5);
        $8->attributes = std::move($1);
        $8->parent = std::move($6);
        $8->setLoc(@$);
        $$ = std::move($8);
    }
    ;

inheritance_opt:
    COLON LT type GT { $$ = std::move($3); }
    | %empty { $$ = nullptr; }
    ;

struct_body_content:
    struct_body_content struct_member COMMA { 
        $1->members.push_back(std::move($2)); $$ = std::move($1); 
    }
    | struct_body_content struct_member { 
        $1->members.push_back(std::move($2)); $$ = std::move($1); 
    }
    | struct_body_content function_declaration {
        auto* func = static_cast<fin::FunctionDeclaration*>($2.release());
        $1->methods.push_back(std::unique_ptr<fin::FunctionDeclaration>(func));
        $$ = std::move($1);
    }
    /* FIXED: Added attributes_opt to resolve shift/reduce conflict */
    | struct_body_content attributes_opt visibility_opt KW_OPERATOR operator_symbol LPAREN params RPAREN LT type GT block {
        auto op = std::make_unique<fin::OperatorDeclaration>($5, std::move($7), std::move($10), std::move($12), $3);
        // Note: We ignore attributes for operators for now, or you can add them to OperatorDeclaration
        $1->operators.push_back(std::move(op));
        $$ = std::move($1);
    }
    | %empty { 
        std::vector<std::unique_ptr<fin::StructMember>> m;
        $$ = std::make_unique<fin::StructDeclaration>("", std::move(m), false); 
    }
    ;

struct_member:
    attributes_opt visibility_opt IDENTIFIER LT type GT {
        $$ = std::make_unique<fin::StructMember>($3, std::move($5), $2);
        $$->attributes = std::move($1);
        $$->setLoc(@$);
    }
    | attributes_opt visibility_opt IDENTIFIER LT type GT EQUAL expression {
        auto member = std::make_unique<fin::StructMember>($3, std::move($5), $2);
        member->default_value = std::move($8);
        member->attributes = std::move($1);
        $$ = std::move(member);
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

/* --- INTERFACES --- */

interface_declaration:
    visibility_opt KW_INTERFACE IDENTIFIER LBRACE interface_body_content RBRACE {
        $5->name = $3;
        $5->is_public = $1;
        $5->setLoc(@$);
        $$ = std::move($5);
    }
    ;

interface_body_content:
    interface_body_content struct_member SEMICOLON { 
        $1->members.push_back(std::move($2)); $$ = std::move($1); 
    }
    | interface_body_content function_declaration {
        auto* func = static_cast<fin::FunctionDeclaration*>($2.release());
        $1->methods.push_back(std::unique_ptr<fin::FunctionDeclaration>(func));
        $$ = std::move($1);
    }
    | %empty { 
        std::vector<std::unique_ptr<fin::StructMember>> m;
        std::vector<std::unique_ptr<fin::FunctionDeclaration>> f;
        $$ = std::make_unique<fin::InterfaceDeclaration>("", std::move(m), std::move(f), false); 
    }
    ;

/* --- FUNCTIONS --- */

function_declaration:
    attributes_opt visibility_opt KW_FUN IDENTIFIER generic_params_opt LPAREN params RPAREN LT type GT block {
        $$ = std::make_unique<fin::FunctionDeclaration>($4, std::move($7), std::move($10), std::move($12));
        auto* func = static_cast<fin::FunctionDeclaration*>($$.get());
        func->is_public = $2;
        func->generic_params = std::move($5);
        func->attributes = std::move($1);
        $$->setLoc(@$);
    }
    | attributes_opt visibility_opt KW_FUN IDENTIFIER generic_params_opt LPAREN params RPAREN KW_NORET block {
        auto voidType = std::make_unique<fin::TypeNode>("void");
        $$ = std::make_unique<fin::FunctionDeclaration>($4, std::move($7), std::move(voidType), std::move($10));
        auto* func = static_cast<fin::FunctionDeclaration*>($$.get());
        func->is_public = $2;
        func->generic_params = std::move($5);
        func->attributes = std::move($1);
        $$->setLoc(@$);
    }
    | attributes_opt visibility_opt KW_STATIC KW_FUN IDENTIFIER generic_params_opt LPAREN params RPAREN LT type GT block {
        $$ = std::make_unique<fin::FunctionDeclaration>($5, std::move($8), std::move($11), std::move($13));
        auto* func = static_cast<fin::FunctionDeclaration*>($$.get());
        func->is_public = $2;
        func->is_static = true;
        func->generic_params = std::move($6);
        func->attributes = std::move($1);
        $$->setLoc(@$);
    }
    /* Abstract */
    | attributes_opt visibility_opt KW_FUN IDENTIFIER generic_params_opt LPAREN params RPAREN LT type GT SEMICOLON {
        $$ = std::make_unique<fin::FunctionDeclaration>($4, std::move($7), std::move($10), nullptr);
        auto* func = static_cast<fin::FunctionDeclaration*>($$.get());
        func->is_public = $2;
        func->generic_params = std::move($5);
        func->attributes = std::move($1);
        $$->setLoc(@$);
    }
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
    IDENTIFIER COLON LT type GT {
        $$ = std::make_unique<fin::Parameter>($1, std::move($4), nullptr, false);
        $$->setLoc(@$);
    }
    | ELLIPSIS IDENTIFIER COLON LT type GT {
        $$ = std::make_unique<fin::Parameter>($2, std::move($5), nullptr, true);
        $$->setLoc(@$);
    }
    ;

/* --- EXTERN / DEFINE --- */

define_declaration:
    AT KW_DEFINE IDENTIFIER LPAREN params RPAREN LT type GT SEMICOLON {
        $$ = std::make_unique<fin::DefineDeclaration>($3, std::move($5), std::move($8), false);
        $$->setLoc(@$);
    }
    | AT KW_DEFINE IDENTIFIER LPAREN params RPAREN KW_NORET SEMICOLON {
        auto voidType = std::make_unique<fin::TypeNode>("void");
        $$ = std::make_unique<fin::DefineDeclaration>($3, std::move($5), std::move(voidType), false);
        $$->setLoc(@$);
    }
    ;

/* --- MACROS --- */

macro_declaration:
    AT KW_MACRO IDENTIFIER LPAREN macro_param_list RPAREN block {
        $$ = std::make_unique<fin::MacroDeclaration>($3, std::move($5), std::move($7));
        $$->setLoc(@$);
    }
    ;

macro_param_list:
    macro_param_list COMMA IDENTIFIER { $1.push_back($3); $$ = std::move($1); }
    | IDENTIFIER { std::vector<std::string> v; v.push_back($1); $$ = std::move(v); }
    | %empty { $$ = std::vector<std::string>(); }
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
    ;

type_list:
    type_list COMMA type { $1.push_back(std::move($3)); $$ = std::move($1); }
    | type { std::vector<std::unique_ptr<fin::TypeNode>> v; v.push_back(std::move($1)); $$ = std::move(v); }
    ;

pointer_type:
    AMPERSAND type {
        $$ = std::move($2);
        $$->is_pointer = true;
    }
    ;

array_type:
    LBRACKET type RBRACKET {
        $$ = std::move($2);
        $$->is_array = true;
    }
    | LBRACKET type COMMA expression RBRACKET {
        $$ = std::move($2);
        $$->is_array = true;
        $$->array_size = std::move($4);
    }
    ;

primitive_type:
    TYPE_INT { $$ = "int"; } | TYPE_FLOAT { $$ = "float"; } | TYPE_STRING { $$ = "string"; }
    | TYPE_VOID { $$ = "void"; } | TYPE_BOOL { $$ = "bool"; }
    ;

/* --- CONTROL FLOW --- */

if_statement:
    KW_IF LPAREN expression RPAREN block {
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
    KW_FOREACH IDENTIFIER LT type GT KW_IN expression block {
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

/* --- EXPRESSIONS --- */

expression_statement:
    expression SEMICOLON { $$ = std::make_unique<fin::ExpressionStatement>(std::move($1)); }
    ;

expression: assignment_expression { $$ = std::move($1); } ;

assignment_expression:
    conditional_expression { $$ = std::move($1); }
    | unary EQUAL assignment_expression {
        $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::EQUAL, std::move($3));
    }
    | unary PLUSEQUAL assignment_expression {
        $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::PLUSEQUAL, std::move($3));
    }
    ;

conditional_expression: logical_or { $$ = std::move($1); } ;

logical_or:
    logical_and { $$ = std::move($1); }
    | logical_or OR logical_and {
        $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::OR, std::move($3));
    }
    ;

logical_and:
    equality { $$ = std::move($1); }
    | logical_and AND equality {
        $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::AND, std::move($3));
    }
    ;

equality:
    comparison { $$ = std::move($1); }
    | equality EQEQ comparison { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::EQEQ, std::move($3)); }
    | equality NOTEQ comparison { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::NOTEQ, std::move($3)); }
    ;

comparison:
    additive { $$ = std::move($1); }
    | comparison LT additive { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::LT, std::move($3)); }
    | comparison GT additive { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::GT, std::move($3)); }
    ;

additive:
    multiplicative { $$ = std::move($1); }
    | additive PLUS multiplicative { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::PLUS, std::move($3)); }
    | additive MINUS multiplicative { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::MINUS, std::move($3)); }
    ;

multiplicative:
    unary { $$ = std::move($1); }
    | multiplicative MULT unary { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::MULT, std::move($3)); }
    | multiplicative DIV unary { $$ = std::make_unique<fin::BinaryOp>(std::move($1), fin::ASTTokenKind::DIV, std::move($3)); }
    ;

unary:
    postfix { $$ = std::move($1); }
    | MINUS unary %prec UMINUS { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::MINUS, std::move($2)); }
    | NOT unary { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::NOT, std::move($2)); }
    | AMPERSAND unary %prec ADDRESSOF_PREC { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::AMPERSAND, std::move($2)); }
    | MULT unary %prec DEREFERENCE_PREC { $$ = std::make_unique<fin::UnaryOp>(fin::ASTTokenKind::MULT, std::move($2)); }
    ;

postfix:
    primary { $$ = std::move($1); }
    | postfix LPAREN arguments RPAREN {
        // Check if $1 is an Identifier (FunctionCall) or MemberAccess (MethodCall)
        if (auto* id = dynamic_cast<fin::Identifier*>($1.get())) {
            $$ = std::make_unique<fin::FunctionCall>(id->name, std::move($3));
        } 
        else if (auto* mem = dynamic_cast<fin::MemberAccess*>($1.get())) {
            // Transform MemberAccess into MethodCall
            // mem->object is the object, mem->member is the method name
            $$ = std::make_unique<fin::MethodCall>(std::move(mem->object), mem->member, std::move($3));
        }
        else {
            // Fallback for complex callees (e.g. (get_func())() )
            // For now, we can't handle this easily without a CallExpression that takes an Expression*
            // Let's stick to "unknown" for weird cases, but MethodCall covers 99% of cases.
            $$ = std::make_unique<fin::FunctionCall>("unknown", std::move($3));
        }
        $$->setLoc(@$);
    }
    | postfix DOT IDENTIFIER {
        $$ = std::make_unique<fin::MemberAccess>(std::move($1), $3);
        $$->setLoc(@$);
    }
    ;

primary:
    literal { $$ = std::move($1); }
    | IDENTIFIER { $$ = std::make_unique<fin::Identifier>($1); }
    | LPAREN expression RPAREN { $$ = std::move($2); }
    | DOLLAR IDENTIFIER LPAREN arguments RPAREN {
        $$ = std::make_unique<fin::MacroCall>($2, std::move($4));
    }
    /* Standard Struct Init: Point { ... } */
    | IDENTIFIER LBRACE field_assignments RBRACE {
        $$ = std::make_unique<fin::StructInstantiation>($1, std::move($3));
    }
    /* Turbofish Struct Init: Box::<int> { ... } */
    | IDENTIFIER DOUBLE_COLON LT type_list GT LBRACE field_assignments RBRACE {
        $$ = std::make_unique<fin::StructInstantiation>($1, std::move($7), std::move($4));
    }
    | LBRACKET arguments RBRACKET {
        $$ = std::make_unique<fin::ArrayLiteral>(std::move($2));
    }
    | KW_CAST LT type GT LPAREN expression RPAREN {
        $$ = std::make_unique<fin::CastExpression>(std::move($3), std::move($6));
    }
    | KW_NEW type LBRACE field_assignments RBRACE {
        $$ = std::make_unique<fin::NewExpression>(std::move($2), std::move($4));
    }
    | KW_NEW type LPAREN arguments RPAREN {
        $$ = std::make_unique<fin::NewExpression>(std::move($2), std::move($4));
    }
    | KW_SIZEOF LPAREN type RPAREN {
        $$ = std::make_unique<fin::SizeofExpression>(std::move($3));
    }
    /* Turbofish Function Call: func::<T>(...) */
    | IDENTIFIER DOUBLE_COLON LT type_list GT LPAREN arguments RPAREN {
        auto call = std::make_unique<fin::FunctionCall>($1, std::move($7));
        call->generic_args = std::move($4);
        $$ = std::move(call);
    }
    ;

literal:
    INTEGER { $$ = std::make_unique<fin::Literal>($1, fin::ASTTokenKind::INTEGER); }
    | FLOAT { $$ = std::make_unique<fin::Literal>($1, fin::ASTTokenKind::FLOAT); }
    | STRING_LITERAL { $$ = std::make_unique<fin::Literal>($1, fin::ASTTokenKind::STRING_LITERAL); }
    | KW_NULL { $$ = std::make_unique<fin::Literal>("null", fin::ASTTokenKind::KW_NULL); }
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

enum_declaration:
    KW_ENUM IDENTIFIER LBRACE RBRACE {
        std::vector<std::pair<std::string, std::unique_ptr<fin::Expression>>> empty;
        $$ = std::make_unique<fin::EnumDeclaration>($2, std::move(empty));
    }
    ;

%%

void fin::parser::error(const location_type& l, const std::string& m) {
    // Use the engine!
    diag.reportError(l, m);
    
    // Check for typos if the message involves an identifier (simplified logic)
    // In a real implementation, we'd inspect the lookahead token.
}