%{
#include <string>
#include <iostream>
#include "parser.hpp" 
#include "diagnostics/DiagnosticEngine.hpp"
#include <fmt/core.h>

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

// Where the block comment currently being skipped opened. Kept so an
// unterminated one can point at the `/*` that started it rather than at EOF.
fin::parser::location_type comment_open_loc;
int comment_depth = 0;

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

    /* Block comments are a start condition rather than one regex, so that a
       newline inside a comment still advances the line counter. */
%x BLOCK_COMMENT

DIGIT       [0-9]
ALPHA       [a-zA-Z_]
ID          {ALPHA}({ALPHA}|{DIGIT})*
    /* CR is deliberately not whitespace: see the line-terminator rules. */
WS          [ \t]+

    /* A well-formed UTF-8 sequence, by length. Only the catch-all rule uses
       these: no Fin token contains a non-ASCII byte, so anything matching here
       is being rejected -- but it has to be rejected as one character. Matching
       byte by byte reported `§` twice, and the second report pointed into the
       middle of a character.
 
       These are the Unicode-legal sequences (RFC 3629), not `[\xC0-\xF7][\x80-\xBF]*`:
       an overlong or surrogate encoding is not a character and should be
       reported per byte, which is what falling through to `.` does. */
UTF8_2      [\xC2-\xDF][\x80-\xBF]
UTF8_3      \xE0[\xA0-\xBF][\x80-\xBF]|[\xE1-\xEC][\x80-\xBF]{2}|\xED[\x80-\x9F][\x80-\xBF]|[\xEE-\xEF][\x80-\xBF]{2}
UTF8_4      \xF0[\x90-\xBF][\x80-\xBF]{2}|[\xF1-\xF3][\x80-\xBF]{3}|\xF4[\x80-\x8F][\x80-\xBF]{2}
UTF8_CHAR   {UTF8_2}|{UTF8_3}|{UTF8_4}

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

    /* Line terminators. `\r\n` is ONE terminator, not a column followed by a
       line break, and a lone `\r` is a terminator too. CR used to be part of
       {WS}, so on a CRLF file every line's last column was one past where the
       text ends -- which is invisible until a diagnostic points at the end of a
       line, and this corpus pins columns in `//@ error <line>:<col>`.
       Longest-match puts `\r\n` ahead of the single-character rules. */
\r\n        { loc.lines(1); loc.step(); }
\n          { loc.lines(1); loc.step(); }
\r          { loc.lines(1); loc.step(); }
"//".*      { /* Ignore */ }

    /* Block comments. Absent entirely until now: `/*` lexed as DIV, so
       macro_definitions.fin and macros2.fin failed on their opening line.
 
       Two properties this has to get right. Newlines inside the comment must
       count, or every diagnostic after a ten-line comment is ten lines off --
       and this corpus pins `//@ error <line>:<col>`. And an unterminated
       comment must be an error: silently treating the rest of the file as a
       comment is how a truncated source reports success.
 
       Nesting: `/*` inside a comment opens a second level, so a commented-out
       region that itself contains a comment closes where it was written to.
       C does not do this; the corpus does not exercise either way, and the
       nesting choice is the one that cannot silently swallow code. */
"/*"                    { loc.step(); loc.columns(yyleng);
                          comment_open_loc = loc; comment_depth = 1;
                          BEGIN(BLOCK_COMMENT); }
<BLOCK_COMMENT>"/*"     { loc.step(); loc.columns(yyleng); ++comment_depth; }
<BLOCK_COMMENT>"*/"     { loc.step(); loc.columns(yyleng);
                          if (--comment_depth == 0) BEGIN(INITIAL); }
<BLOCK_COMMENT>\r\n     { loc.lines(1); loc.step(); }
<BLOCK_COMMENT>[\n\r]    { loc.lines(1); loc.step(); }
<BLOCK_COMMENT>[^*/\n\r]+ { loc.step(); loc.columns(yyleng); }
<BLOCK_COMMENT>[*/]     { loc.step(); loc.columns(yyleng); }
<BLOCK_COMMENT><<EOF>>  {
        BEGIN(INITIAL);
        comment_depth = 0;
        const std::string msg = "unterminated block comment";
        if (auto* d = fin::getLexerDiagnostics()) d->reportError(comment_open_loc, msg);
        else fmt::print(stderr, "error: {}\n", msg);
        return fin::parser::make_END(loc);
    }

    /* Keywords */
"let"       { MAKE(KW_LET); }
"const"     { MAKE(KW_CONST); }
"auto"      { MAKE(KW_AUTO); }

"fun"       { MAKE(KW_FUN); }
"return"    { MAKE(KW_RETURN); }

"struct"    { MAKE(KW_STRUCT); }
"enum"      { MAKE(KW_ENUM); }
"interface" { MAKE(KW_INTERFACE); }

"namespace" { MAKE(KW_NAMESPACE); }

"macro"     { MAKE(KW_MACRO); }
"static"    { MAKE(KW_STATIC); }
"null"      { MAKE(KW_NULL); }
    /* Boolean literals. Without these both words fall through to the
       identifier rule and resolve as variables, so every Fin program that
       writes a boolean down fails with `Undefined variable 'true'`. */
"true"      { MAKE(KW_TRUE); }
"false"     { MAKE(KW_FALSE); }

"while"     { MAKE(KW_WHILE); }
"do"        { MAKE(KW_DO); }
"for"       { MAKE(KW_FOR); }
"foreach"   { MAKE(KW_FOREACH); }
"break"     { MAKE(KW_BREAK); }
"continue"  { MAKE(KW_CONTINUE); }

"if"        { MAKE(KW_IF); }
"else"      { MAKE(KW_ELSE); }
"in"        { MAKE(KW_IN); }

"extern"    { MAKE(KW_EXTERN); }
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
"readonly"  { MAKE(KW_READONLY); }
"priv"      { MAKE(KW_PRIV); }

"operator"  { MAKE(KW_OPERATOR); }

"try"       { MAKE(KW_TRY); }
"catch"     { MAKE(KW_CATCH); }
"blame"     { MAKE(KW_BLAME); }
"super"     { MAKE(KW_SUPER); }
"Self"      { MAKE(KW_SELF_TYPE); }

"type"      { MAKE(KW_TYPE); }
"class"     { MAKE(KW_CLASS); }
"implements" { MAKE(KW_IMPLEMENTS); }
"any"       { MAKE(KW_ANY); }

"special"   { MAKE(KW_SPECIAL); }
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

"<<"        { MAKE(SHIFTLEFT); }
"<<="       { MAKE(SHIFTLEFTEQUAL); }
">>="       { MAKE(SHIFTRIGHTEQUAL); }

    /* No `>>` rule: two GTs are returned instead, and the parser rebuilds the
       shift operator from `GT GT`.
 
       A grammar cannot split a token, and a nested generic needs the halves
       separately: `M<M<int>>` closes two levels with one `>>`, and
       `Result<Result<int>>>` closes three with `>>>`. The parser used to carry a
       `IDENTIFIER LT type_list SHIFTRIGHT` rule for this, which let the INNER
       type swallow the whole `>>` and so left the outer one unclosed -- the
       construct never parsed at any depth.
 
       `>>=` stays one token, because `operator >>=` is declared in
       tests/samples/stdlib/operators.fin:107 and `>=` would otherwise win the
       longest match on its second character. The cost is that a generic closing
       immediately against an `=`, written `<Box<int>>=v` with no space, lexes as
       GT then `>>=`. Every closing generic in the corpus is followed by a space.
       Splitting `>>=` too would not fix it: `>>>=` would then lex as GT GT GTEQ
       and a three-deep generic assignment would break in the same place. */
"|"         { MAKE(PIPE); }
"^"         { MAKE(CARET); }
"`"         { MAKE(BACKTICK); }

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
    /* `%{ ... }%` opens an attribute block: one attribute list applied to
       every statement inside. Listed before the one-character `%` and `}`
       rules -- flex takes the longest match, so `%{` wins over `%` on its
       own, but writing them adjacent keeps that visible. `}%` is unambiguous
       in practice: nothing closes a brace and immediately applies modulo. */
"%{"        { MAKE(PERCENT_LBRACE); }
"}%"        { MAKE(RBRACE_PERCENT); }
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

    /* Rejected input. The UTF-8 rule comes first so a multi-byte character is
       one diagnostic rather than one per byte. Columns stay byte-based, which is
       what the context renderer indexes the source line by. */
{UTF8_CHAR} { update_loc(); fin::reportLexerError(loc, yytext); }
.           { update_loc(); fin::reportLexerError(loc, yytext); }
<<EOF>>     { return fin::parser::make_END(loc); }

%%