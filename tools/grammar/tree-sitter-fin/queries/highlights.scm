; GENERATED FILE -- regenerate: python3 tools/grammar/generate_tree_sitter.py
; Minimal highlights for the first slice (#50 task 2). Keywords and types are
; generated from tools/grammar/fin-keywords.json; node names track grammar.js.

(line_comment) @comment
(block_comment) @comment

(string) @string
(escape_sequence) @string.escape
(char_literal) @string.character

(integer) @number
(float) @number.float

"Self" @keyword
"any" @keyword
"as" @keyword
"auto" @keyword
"blame" @keyword
"break" @keyword
"class" @keyword
"const" @keyword
"continue" @keyword
"define" @keyword
"do" @keyword
"else" @keyword
"enum" @keyword
"fn" @keyword
"for" @keyword
"foreach" @keyword
"from" @keyword
"fun" @keyword
"if" @keyword
"implements" @keyword
"import" @keyword
"in" @keyword
"interface" @keyword
"let" @keyword
"return" @keyword
"struct" @keyword
"type" @keyword
"while" @keyword

"bool" @type
"char" @type
"double" @type
"float" @type
"int" @type
"long" @type
"noret" @type
"string" @type
"void" @type
"Self" @type

-> @operator
=> @operator
:: @operator

(function_definition name: (identifier) @function)
(struct_definition name: (identifier) @type)
(define_declaration name: (identifier) @function)
