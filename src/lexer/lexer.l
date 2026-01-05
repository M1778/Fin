%{
#include <string>
#include <iostream>
#include "parser.hpp" 

#define YY_DECL fin::parser::symbol_type yylex()

// Global location tracking
int current_line = 1;
int current_col = 1;
fin::parser::location_type loc;

// Helper to update location for every token
void update_loc() {
    loc.step();
    loc.columns(yyleng);
}

// Helper to handle newlines
void handle_newline() {
    loc.lines(yyleng); 
    loc.step();
}

// Implementation of the reset function
namespace fin {
    void reset_lexer_location() {
        current_line = 1;
        current_col = 1;
        // Reset Bison's location tracker
        loc.initialize(nullptr, 1, 1);
    }
}

#define MAKE(kind) \
    update_loc(); \
    return fin::parser::make_##kind(loc);

#define MAKE_VAL(kind, val) \
    update_loc(); \
    return fin::parser::make_##kind(val, loc);

%}

%option noyywrap
%option nounput
%option noinput
%option nounistd
%option never-interactive

DIGIT       [0-9]
ALPHA       [a-zA-Z_]
ID          {ALPHA}({ALPHA}|{DIGIT})*
WS          [ \t\r]+

%%

    /* Initialize location on first run (Safety check) */
%{
    static bool init = false;
    if (!init) {
        loc.initialize(nullptr, 1, 1);
        init = true;
    }
%}

    /* Whitespace */
{WS}        { loc.step(); loc.columns(yyleng); }
\n          { loc.lines(1); loc.step(); }
"//".*      { /* Ignore */ }

    /* Keywords */
"let"       { MAKE(KW_LET); }
"bez"       { MAKE(KW_BEZ); }
"const"     { MAKE(KW_CONST); }
"beton"     { MAKE(KW_BETON); }
"auto"      { MAKE(KW_AUTO); }

"fun"       { MAKE(KW_FUN); }
"<noret>"   { MAKE(KW_NORET); }
"return"    { MAKE(KW_RETURN); }

"struct"    { MAKE(KW_STRUCT); }
"enum"      { MAKE(KW_ENUM); }
"interface" { MAKE(KW_INTERFACE); }

"macro"     { MAKE(KW_MACRO); }
"static"    { MAKE(KW_STATIC); }
"null"      { MAKE(KW_NULL); }

"while"     { MAKE(KW_WHILE); }
"for"       { MAKE(KW_FOR); }
"foreach"   { MAKE(KW_FOREACH); }
"break"     { MAKE(KW_BREAK); }
"continue"  { MAKE(KW_CONTINUE); }

"if"        { MAKE(KW_IF); }
"else"      { MAKE(KW_ELSE); }
"elseif"    { MAKE(KW_ELSEIF); }
"in"        { MAKE(KW_IN); }

"import"    { MAKE(KW_IMPORT); }
"as"        { MAKE(KW_AS); }
"from"      { MAKE(KW_FROM); }

"new"       { MAKE(KW_NEW); }
"delete"    { MAKE(KW_DELETE); }
"sizeof"    { MAKE(KW_SIZEOF); }
"typeof"    { MAKE(KW_TYPEOF); }
"as_ptr"    { MAKE(KW_AS_PTR); }
"cast"      { MAKE(KW_CAST); }

"pub"       { MAKE(KW_PUB); }
"priv"      { MAKE(KW_PRIV); }

"operator"  { MAKE(KW_OPERATOR); }

"try"       { MAKE(KW_TRY); }
"catch"     { MAKE(KW_CATCH); }
"blame"     { MAKE(KW_BLAME); }
"super"     { MAKE(KW_SUPER); }
"Self"      { MAKE(KW_SELF_TYPE); }

"special"   { MAKE(KW_SPECIAL); }
"@return"   { MAKE(KW_AT_RETURN); }
"fn"        { MAKE(KW_FN_TYPE); }
"define"    { MAKE(KW_DEFINE); }
"quote"     { MAKE(KW_QUOTE); }
"#for"      { MAKE(HASH_FOR); }
"#index"    { MAKE(HASH_INDEX); }

"m1778"     { MAKE(KW_M1778); }

    /* Types */
"int"       { MAKE(TYPE_INT); }
"long"      { MAKE(TYPE_LONG); }
"float"     { MAKE(TYPE_FLOAT); }
"double"    { MAKE(TYPE_DOUBLE); }
"bool"      { MAKE(TYPE_BOOL); }
"string"    { MAKE(TYPE_STRING); }
"char"      { MAKE(TYPE_CHAR); }
"void"      { MAKE(TYPE_VOID); }
"noret"     { MAKE(TYPE_VOID); }

    /* Operators & Punctuation */
"=>"        { MAKE(ARROW); }
"->"        { MAKE(RARROW); }
"::"        { MAKE(DOUBLE_COLON); }
"..."       { MAKE(ELLIPSIS); }

"++"        { MAKE(INCREMENT); }
"--"        { MAKE(DECREMENT); }

"+="        { MAKE(PLUSEQUAL); }
"-="        { MAKE(MINUSEQUAL); }
"*="        { MAKE(MULTEQUAL); }
"/="        { MAKE(DIVEQUAL); }

"=="        { MAKE(EQEQ); }
"!="        { MAKE(NOTEQ); }
"<="        { MAKE(LTEQ); }
">="        { MAKE(GTEQ); }
"&&"        { MAKE(AND); }
"||"        { MAKE(OR); }

"("         { MAKE(LPAREN); }
")"         { MAKE(RPAREN); }
"{"         { MAKE(LBRACE); }
"}"         { MAKE(RBRACE); }
"["         { MAKE(LBRACKET); }
"]"         { MAKE(RBRACKET); }

";"         { MAKE(SEMICOLON); }
":"         { MAKE(COLON); }
","         { MAKE(COMMA); }
"."         { MAKE(DOT); }
"@"         { MAKE(AT); }
"$"         { MAKE(DOLLAR); }
"#"         { MAKE(HASH); }
"?"         { MAKE(QUESTION); }

"="         { MAKE(EQUAL); }
"+"         { MAKE(PLUS); }
"-"         { MAKE(MINUS); }
"*"         { MAKE(MULT); }
"/"         { MAKE(DIV); }
"%"         { MAKE(MOD); }
"<"         { MAKE(LT); }
">"         { MAKE(GT); }
"!"         { MAKE(NOT); }
"&"         { MAKE(AMPERSAND); }  
"~"         { MAKE(TILDE); }

  

    /* Literals */
{DIGIT}+"."{DIGIT}+ { MAKE_VAL(FLOAT, yytext); }
{DIGIT}+            { MAKE_VAL(INTEGER, yytext); }
\"([^\\\"]|\\.)*\"  { MAKE_VAL(STRING_LITERAL, yytext); }
\'([^\\\']|\\.)\'   { MAKE_VAL(CHAR_LITERAL, yytext); }

    /* Identifiers */
{ID}                { MAKE_VAL(IDENTIFIER, yytext); }

.           { std::cerr << "Lexer Error: " << yytext << std::endl; }
<<EOF>>     { return fin::parser::make_END(loc); }

%%