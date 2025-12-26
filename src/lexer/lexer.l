%{
#include "tokens.hpp"
#include <string>
#include <iostream>

#define YY_DECL int yylex(fin::Token* yylval)

// Track location manually
int current_line = 1;
int current_col = 1;

// Macro to save token data and return
#define SAVE_TOKEN(k) \
    yylval->kind = k; \
    yylval->text = yytext; \
    yylval->line = current_line; \
    yylval->column = current_col; \
    current_col += yyleng; \
    return 1;

%}

/* Flex Options */
%option noyywrap
%option nounput
%option noinput
%option nounistd
%option never-interactive

/* Regex Definitions */
DIGIT       [0-9]
ALPHA       [a-zA-Z_]
ID          {ALPHA}({ALPHA}|{DIGIT})*
WS          [ \t\r]+

%%

    /* --- Whitespace --- */
{WS}        { current_col += yyleng; }
\n          { current_line++; current_col = 1; }

    /* --- Comments --- */
"//".*      { /* Ignore single line */ }
"/*"([^*]|(\*+[^*/]))*\*+"/" { 
    // Handle newlines inside block comments
    for(int i=0; i<yyleng; i++) {
        if(yytext[i] == '\n') { current_line++; current_col=1; }
        else current_col++;
    }
}

    /* --- Keywords --- */
"let"       { SAVE_TOKEN(fin::TokenKind::KW_LET); }
"bez"       { SAVE_TOKEN(fin::TokenKind::KW_BEZ); }
"const"     { SAVE_TOKEN(fin::TokenKind::KW_CONST); }
"beton"     { SAVE_TOKEN(fin::TokenKind::KW_BETON); }
"auto"      { SAVE_TOKEN(fin::TokenKind::KW_AUTO); }
"fun"       { SAVE_TOKEN(fin::TokenKind::KW_FUN); }
"return"    { SAVE_TOKEN(fin::TokenKind::KW_RETURN); }
"<noret>"   { SAVE_TOKEN(fin::TokenKind::KW_NORET); }
"struct"    { SAVE_TOKEN(fin::TokenKind::KW_STRUCT); }
"enum"      { SAVE_TOKEN(fin::TokenKind::KW_ENUM); }
"interface" { SAVE_TOKEN(fin::TokenKind::KW_INTERFACE); }
"macro"     { SAVE_TOKEN(fin::TokenKind::KW_MACRO); }
"static"    { SAVE_TOKEN(fin::TokenKind::KW_STATIC); }
"null"      { SAVE_TOKEN(fin::TokenKind::KW_NULL); }
"while"     { SAVE_TOKEN(fin::TokenKind::KW_WHILE); }
"for"       { SAVE_TOKEN(fin::TokenKind::KW_FOR); }
"foreach"   { SAVE_TOKEN(fin::TokenKind::KW_FOREACH); }
"break"     { SAVE_TOKEN(fin::TokenKind::KW_BREAK); }
"continue"  { SAVE_TOKEN(fin::TokenKind::KW_CONTINUE); }
"if"        { SAVE_TOKEN(fin::TokenKind::KW_IF); }
"else"      { SAVE_TOKEN(fin::TokenKind::KW_ELSE); }
"elseif"    { SAVE_TOKEN(fin::TokenKind::KW_ELSEIF); }
"in"        { SAVE_TOKEN(fin::TokenKind::KW_IN); }
"import"    { SAVE_TOKEN(fin::TokenKind::KW_IMPORT); }
"as"        { SAVE_TOKEN(fin::TokenKind::KW_AS); }
"from"      { SAVE_TOKEN(fin::TokenKind::KW_FROM); }
"new"       { SAVE_TOKEN(fin::TokenKind::KW_NEW); }
"delete"    { SAVE_TOKEN(fin::TokenKind::KW_DELETE); }
"sizeof"    { SAVE_TOKEN(fin::TokenKind::KW_SIZEOF); }
"typeof"    { SAVE_TOKEN(fin::TokenKind::KW_TYPEOF); }
"as_ptr"    { SAVE_TOKEN(fin::TokenKind::KW_AS_PTR); }
"std_conv"  { SAVE_TOKEN(fin::TokenKind::KW_STD_CONV); }
"operator"  { SAVE_TOKEN(fin::TokenKind::KW_OPERATOR); }
"pub"       { SAVE_TOKEN(fin::TokenKind::KW_PUB); }
"priv"      { SAVE_TOKEN(fin::TokenKind::KW_PRIV); }
"try"       { SAVE_TOKEN(fin::TokenKind::KW_TRY); }
"catch"     { SAVE_TOKEN(fin::TokenKind::KW_CATCH); }
"blame"     { SAVE_TOKEN(fin::TokenKind::KW_BLAME); }
"super"     { SAVE_TOKEN(fin::TokenKind::KW_SUPER); }
"Self"      { SAVE_TOKEN(fin::TokenKind::KW_SELF_TYPE); }
"special"   { SAVE_TOKEN(fin::TokenKind::KW_SPECIAL); }
"@return"   { SAVE_TOKEN(fin::TokenKind::KW_AT_RETURN); }
"fn"        { SAVE_TOKEN(fin::TokenKind::KW_FN_TYPE); }
"define"    { SAVE_TOKEN(fin::TokenKind::KW_DEFINE); }
"m1778"     { SAVE_TOKEN(fin::TokenKind::KW_M1778); }

    /* --- Types --- */
"int"       { SAVE_TOKEN(fin::TokenKind::TYPE_INT); }
"float"     { SAVE_TOKEN(fin::TokenKind::TYPE_FLOAT); }
"double"    { SAVE_TOKEN(fin::TokenKind::TYPE_DOUBLE); }
"bool"      { SAVE_TOKEN(fin::TokenKind::TYPE_BOOL); }
"string"    { SAVE_TOKEN(fin::TokenKind::TYPE_STRING); }
"char"      { SAVE_TOKEN(fin::TokenKind::TYPE_CHAR); }
"void"      { SAVE_TOKEN(fin::TokenKind::TYPE_VOID); }
"noret"     { SAVE_TOKEN(fin::TokenKind::TYPE_VOID); } /* Map noret to void */
"long"      { SAVE_TOKEN(fin::TokenKind::TYPE_LONG); }

    /* --- Operators & Punctuation --- */
"=>"        { SAVE_TOKEN(fin::TokenKind::ARROW); }
"::"        { SAVE_TOKEN(fin::TokenKind::DOUBLE_COLON); }
"..."       { SAVE_TOKEN(fin::TokenKind::ELLIPSIS); }
"++"        { SAVE_TOKEN(fin::TokenKind::INCREMENT); }
"--"        { SAVE_TOKEN(fin::TokenKind::DECREMENT); }
"+="        { SAVE_TOKEN(fin::TokenKind::PLUSEQUAL); }
"-="        { SAVE_TOKEN(fin::TokenKind::MINUSEQUAL); }
"*="        { SAVE_TOKEN(fin::TokenKind::MULTEQUAL); }
"/="        { SAVE_TOKEN(fin::TokenKind::DIVEQUAL); }
"=="        { SAVE_TOKEN(fin::TokenKind::EQEQ); }
"!="        { SAVE_TOKEN(fin::TokenKind::NOTEQ); }
"<="        { SAVE_TOKEN(fin::TokenKind::LTEQ); }
">="        { SAVE_TOKEN(fin::TokenKind::GTEQ); }
"&&"        { SAVE_TOKEN(fin::TokenKind::AND); }
"||"        { SAVE_TOKEN(fin::TokenKind::OR); }

"("         { SAVE_TOKEN(fin::TokenKind::LPAREN); }
")"         { SAVE_TOKEN(fin::TokenKind::RPAREN); }
"{"         { SAVE_TOKEN(fin::TokenKind::LBRACE); }
"}"         { SAVE_TOKEN(fin::TokenKind::RBRACE); }
"["         { SAVE_TOKEN(fin::TokenKind::LBRACKET); }
"]"         { SAVE_TOKEN(fin::TokenKind::RBRACKET); }
";"         { SAVE_TOKEN(fin::TokenKind::SEMICOLON); }
":"         { SAVE_TOKEN(fin::TokenKind::COLON); }
","         { SAVE_TOKEN(fin::TokenKind::COMMA); }
"."         { SAVE_TOKEN(fin::TokenKind::DOT); }
"@"         { SAVE_TOKEN(fin::TokenKind::AT); }
"$"         { SAVE_TOKEN(fin::TokenKind::DOLLAR); }
"#"         { SAVE_TOKEN(fin::TokenKind::HASH); }
"="         { SAVE_TOKEN(fin::TokenKind::EQUAL); }
"+"         { SAVE_TOKEN(fin::TokenKind::PLUS); }
"-"         { SAVE_TOKEN(fin::TokenKind::MINUS); }
"*"         { SAVE_TOKEN(fin::TokenKind::MULT); }
"/"         { SAVE_TOKEN(fin::TokenKind::DIV); }
"%"         { SAVE_TOKEN(fin::TokenKind::MOD); }
"<"         { SAVE_TOKEN(fin::TokenKind::LT); }
">"         { SAVE_TOKEN(fin::TokenKind::GT); }
"!"         { SAVE_TOKEN(fin::TokenKind::NOT); }
"&"         { SAVE_TOKEN(fin::TokenKind::AMPERSAND); }

    /* --- Literals --- */
{DIGIT}+"."{DIGIT}+ { SAVE_TOKEN(fin::TokenKind::FLOAT); }
{DIGIT}+            { SAVE_TOKEN(fin::TokenKind::INTEGER); }
\"([^\\\"]|\\.)*\"  { SAVE_TOKEN(fin::TokenKind::STRING_LITERAL); }
\'([^\\\']|\\.)\'   { SAVE_TOKEN(fin::TokenKind::CHAR_LITERAL); }

    /* --- Identifiers --- */
{ID}        { SAVE_TOKEN(fin::TokenKind::IDENTIFIER); }

    /* --- Catch-all Error --- */
.           { 
    std::cerr << "Lexer Error: Unexpected character '" << yytext << "' at line " << current_line << std::endl;
    SAVE_TOKEN(fin::TokenKind::UNKNOWN); 
}

%%

