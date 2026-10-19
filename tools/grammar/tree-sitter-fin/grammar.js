// GENERATED FILE -- do not hand-edit; keywords are not typed here.
//   regenerate: python3 tools/grammar/generate_tree_sitter.py
//   source:     tools/grammar/fin-keywords.json via tools/grammar/extract_keywords.py
// The KW map below is that JSON inlined so grammar.js is self-contained; rules
// may only use its keys. Rule shapes track src/parser/parser.y productions
// and keep parser.y's names so diffing against the real grammar reads cleanly.
//
// First slice (#50 task 2): parses tests/samples/{basic,structs,functions,loops,
// importing}.fin, plus the blame/foreach/attribute/@define shapes parser.y
// carries.
//
// Keywords unused by this slice (present in fin-keywords.json, no rule yet):
// as_ptr, bool, cast, catch, char, delete, double, extern, false, float, int, long, m1778, macro, match, namespace, new, noret, null, operator, priv, pub, quote, readonly, sizeof, special, static, string, super, true, try, typeof, void
//
// KNOWN GAPS vs parser.y (13):
//   - class/type/alias/namespace declarations; the bare-IDENTIFIER constructor form
//   - visibility prefixes pub/priv/readonly/static/extern on declarations
//   - @macro (macro_declaration, parser.y:1627), @special (special_declaration, parser.y:1954), @implements
//   - operator declarations and struct-body members beyond <type> fields
//   - pointer/reference types &T and *p, handles [T]; nullable `?` on types/names
//   - prototype type literals {"k": v} / {string,int} (parser.y base_type LBRACE rule)
//   - cast/new/delete/sizeof/typeof/as_ptr expressions; try/catch blocks; unary ! for logic
//   - type width annotations <int{64}>; Self<T> generic args; fun? nullable fn
//   - directives #for/#index: lexer word rules exist, no sample parses one yet
//   - prefix ++/--, compound assignments (=+/-=...), expression ellipsis
//   - block comments nest per lexer.l; this slice's regex form ends at the first */
//   - unparenthesized foreach forms (parser.y's first alternative)
//   - keyword m1778 has no rule (blame m1778; parses as identifier here)

const KW = {
  "fun": "fun",
  "let": "let",
  "const": "const",
  "auto": "auto",
  "return": "return",
  "if": "if",
  "else": "else",
  "while": "while",
  "do": "do",
  "for": "for",
  "foreach": "foreach",
  "in": "in",
  "break": "break",
  "continue": "continue",
  "import": "import",
  "as": "as",
  "from": "from",
  "struct": "struct",
  "blame": "blame",
  "fn": "fn",
  "define": "define",
  "class": "class",
  "type": "type",
  "implements": "implements",
  "any": "any",
  "enum": "enum",
  "interface": "interface",
  "Self": "Self",
};

const PREC = {
  ASSIGNMENT: 1,
  LAMBDA: 2,
  EQUALITY: 3,
  ADD: 4,
  MULT: 5,
  CALL: 7,
  POSTFIX: 8,
};

module.exports = grammar({
  name: 'fin',

  word: $ => $.identifier,

  extras: $ => [/\s+/, $.line_comment, $.block_comment],

  conflicts: $ => [
    // A grant run inside a `%Ellipsis%` block could be read as more grants of
    // the list or as grant-statements; both spans parse the same tokens.
    [$.attribute_block, $._statement],
    // ','-separated lists: reduce-after-repeat needs GLR (tree-sitter's
    // unaffected-rule default would otherwise pick one).
    [$.parameter_list],
    // `#[use(Fmt)]`-style attrib arg: one identifier could read as a path or
    // a type; GLR holds both, both are the same token run.
    [$.dotted_path, $.type_identifier],
  ],

  rules: {
    // `attribute_grant` is reachable at the top level through `_statement`;
    // listing it twice caused an unresolved reduce/reduce.
    source_file: $ => repeat(choice(
      $.import_statement,
      $.function_definition,
      $.struct_definition,
      $.define_declaration,
      $.attribute_block,
      $._statement,
    )),

    import_statement: $ => choice(
      // Case 1: import "file.fin";
      seq('import', $.string, ';'),
      // Case 2/3: import lib.mod[::ns]; / import lib as alias;
      seq('import', $.dotted_path, optional(seq(KW.as, $.identifier)), ';'),
      // Case 4/5: import { A, B } from "file.fin" / from lib.mod;
      seq('import', '{', optional(commaSep1($.identifier)), '}',
          KW.from, choice($.string, $.dotted_path), ';'),
      // Case 6: import * from m;
      seq('import', '*', KW.from, $.dotted_path, ';'),
    ),

    function_definition: $ => seq(
      KW.fun,
      field('name', $.identifier),
      optional($.type_parameters),
      '(',
      optional(seq($.parameter_list, optional(seq(',', '...')))),
      ')',
      field('return_type', $.type_annotation),
      field('body', $.block),
    ),

    parameter_list: $ => commaSep1($.parameter),

    parameter: $ => choice(
      seq(field('name', $.identifier), ':',
          field('type', choice($._type, $.type_annotation))),
      seq('...', field('name', $.identifier), ':',
          field('type', choice($._type, $.type_annotation))),
    ),

    type_parameters: $ => seq('<', commaSep1($.identifier), '>'),

    struct_definition: $ => seq(
      KW.struct,
      field('name', $.identifier),
      optional($.type_parameters),
      $.field_list,
    ),

    field_list: $ => seq(
      '{',
      repeat(seq($.field_declaration, optional(','))),
      '}',
    ),

    field_declaration: $ => seq(
      field('name', $.identifier),
      field('type', $.type_annotation),
    ),

    define_declaration: $ => seq(
      '@', KW.define,
      field('name', $.identifier),
      '(',
      optional(seq($.parameter_list, optional(seq(',', '...')))),
      ')',
      optional($.type_annotation),
      ';',
    ),

    attribute_grant: $ => seq(
      '#', '[', $.attr_id,
      optional(choice($.string, seq('(', $.attr_args, ')'))),
      ']',
    ),
    attr_id: $ => choice($.identifier, KW.class, KW.type, KW.implements),
    attr_args: $ => repeat1(choice(
      $.dotted_path, $._type, KW.any,
      'enum', 'struct', 'interface',
    )),

    attribute_block: $ => seq(
      repeat1($.attribute_grant),
      '%{',
      repeat($._statement),
      '}%',
    ),

    dotted_path: $ => seq(
      $.identifier,
      repeat(choice(seq('.', $.identifier), seq('::', $.identifier))),
    ),

    block: $ => seq(
      '{',
      repeat(choice($._statement, $.function_definition)),
      '}',
    ),

    _statement: $ => choice(
      $.let_statement,
      $.for_statement,
      $.foreach_statement,
      $.while_statement,
      $.do_while_statement,
      $.if_statement,
      $.break_statement,
      $.continue_statement,
      $.return_statement,
      $.blame_statement,
      $.expression_statement,
      $.attribute_grant,
    ),

    let_statement: $ => seq(
      choice(KW.let, KW.const),
      field('name', $.identifier),
      field('type', $.type_annotation),
      optional(seq('=', field('value', $._expression))),
      ';',
    ),

    for_statement: $ => seq(
      KW.for, '(',
      optional($.for_init), ';',
      optional(field('condition', $._expression)), ';',
      optional(field('update', $._expression)),
      ')',
      field('body', $.block),
    ),

    for_init: $ => seq(
      field('name', $.identifier), ':',
      field('type', $._type), '=',
      field('start', $._expression),
    ),

    foreach_statement: $ => seq(
      KW.foreach, '(',
      $.foreach_binding_list, KW.in,
      field('iterable', $._expression),
      ')',
      field('body', $.block),
    ),

    foreach_binding_list: $ => commaSep1($.foreach_binding),
    foreach_binding: $ => seq(
      field('name', $.identifier), field('type', $.type_annotation),
    ),

    while_statement: $ => seq(
      KW.while, '(', field('condition', $._expression), ')',
      field('body', $.block),
    ),

    do_while_statement: $ => seq(
      KW.do, field('body', $.block),
      KW.while, '(', field('condition', $._expression), ')', ';',
    ),

    if_statement: $ => seq(
      KW.if, '(', field('condition', $._expression), ')',
      field('consequence', $.block),
      optional(seq(KW.else, $._else_clause)),
    ),
    _else_clause: $ => choice($.if_statement, $.block),

    break_statement: $ => seq(KW.break, ';'),
    continue_statement: $ => seq(KW.continue, ';'),

    return_statement: $ => seq(KW.return, optional($._expression), ';'),

    blame_statement: $ => choice(
      seq(KW.blame, $._expression, ';'),
      seq(KW.blame, $._expression, ',', $._expression, ';'),
    ),

    expression_statement: $ => seq($._expression, ';'),

    _expression: $ => choice(
      $.assignment_expression,
      $.binary_expression,
      $.call_expression,
      $.index_expression,
      $.member_expression,
      $.macro_call,
      $.postfix_increment,
      $.postfix_decrement,
      $.parenthesized_expression,
      $.lambda_expression,
      $.struct_literal,
      $.array_literal,
      $.identifier,
      $.integer,
      $.float,
      $.string,
      $.char_literal,
    ),

    assignment_expression: $ => prec.right(PREC.ASSIGNMENT, seq(
      field('left', $._lvalue), '=',
      field('right', $._expression),
    )),
    _lvalue: $ => choice($.identifier, $.index_expression, $.member_expression),

    binary_expression: $ => choice(
      prec.left(PREC.EQUALITY, seq(
        field('left', $._expression),
        field('operator', choice('==', '!=', '<=', '>=', '<', '>')),
        field('right', $._expression),
      )),
      prec.left(PREC.ADD, seq(
        field('left', $._expression),
        field('operator', choice('+', '-')),
        field('right', $._expression),
      )),
      prec.left(PREC.MULT, seq(
        field('left', $._expression),
        field('operator', choice('*', '/', '%')),
        field('right', $._expression),
      )),
    ),

    call_expression: $ => prec(PREC.CALL, seq(
      field('function', choice(
        $.identifier, $.member_expression, $.index_expression,
        $.parenthesized_expression,
      )),
      '(', optional(commaSep1($._expression)), ')',
    )),

    index_expression: $ => prec(PREC.CALL, seq(
      field('object', choice($.identifier, $.member_expression)),
      '[', $._expression, ']',
    )),

    member_expression: $ => prec(PREC.CALL, seq(
      field('object', choice($.identifier, $.call_expression, $.index_expression)),
      '.', field('property', $.identifier),
    )),

    macro_call: $ => prec(PREC.CALL, seq(
      field('callee', choice($.identifier, $.member_expression)),
      '!', '(', optional(commaSep1($._expression)), ')',
    )),

    postfix_increment: $ => prec(PREC.POSTFIX, seq($._lvalue, '++')),
    postfix_decrement: $ => prec(PREC.POSTFIX, seq($._lvalue, '--')),

    parenthesized_expression: $ => seq('(', $._expression, ')'),

    array_literal: $ => seq('[', repeat(seq($._expression, optional(','))), ']'),

    lambda_expression: $ => choice(
      // Case 1: anonymous fun: `fun (a: int) <int> { ... }`
      prec(PREC.LAMBDA, seq(
        KW.fun, '(', optional($.parameter_list), ')',
        field('return_type', $.type_annotation),
        field('body', $.block),
      )),
      // Case 2: `(a: int, b: int) <int> => expr-or-block`
      prec.right(PREC.LAMBDA, seq(
        '(', $.parameter_list, ')',
        field('return_type', $.type_annotation),
        '=>',
        choice($.block, $._expression),
      )),
    ),

    struct_literal: $ => seq(
      field('name', $.identifier),
      '{',
      repeat(seq($.lit_field, optional(','))),
      '}',
    ),
    lit_field: $ => seq(field('name', $.identifier), ':', field('value', $._expression)),

    _type: $ => choice(
      $.builtin_type,
      $.type_identifier,
      $.auto_type,
      $.self_type,
      $.fn_type,
      $.array_type,
    ),

    builtin_type: $ => choice("bool", "char", "double", "float", "int", "long", "noret", "string", "void"),
    type_identifier: $ => seq($.identifier, optional($.type_arguments)),
    type_arguments: $ => seq('<', field('argument', commaSep1($._type)), '>'),
    auto_type: $ => KW.auto,
    self_type: $ => KW.Self,
    fn_type: $ => seq(
      KW.fn, '(', optional(commaSep1($._type)), ')',
      choice('->', '=>'),
      $._type,
    ),
    array_type: $ => seq('[', $._type, ',', $.integer, ']'),

    type_annotation: $ => seq('<', field('type', $._type), '>'),

    integer: $ => /\d+/,
    float: $ => /\d+\.\d+/,

    string: $ => seq(
      '"',
      repeat(choice($.escape_sequence, $._string_content)),
      '"',
    ),
    escape_sequence: $ => token.immediate(prec(1, /\\./)),
    _string_content: $ => token.immediate(prec(-1, /[^\\"]+/)),

    char_literal: $ => seq(
      "'",
      choice($.escape_sequence, token.immediate(/[^'\n]/)),
      "'",
    ),

    line_comment: $ => token(seq('//', /[^\r\n]*/)),
    // Regex comment form; NESTING is acknowledged as a gap (see header) --
    // the lexer's start-condition nesting needs a recursive rule this slice skips.
    block_comment: $ => token(seq('/*', /[^*]*\*+([^/*][^*]*\*+)*/, '/')),

    identifier: $ => /[_a-zA-Z][_a-zA-Z0-9]*/,
  },
});

function commaSep1(rule) {
  return seq(rule, repeat(seq(',', rule)));
}
