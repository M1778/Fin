from src.ast2.nodes import *




# ==============================================================================
#                                 EXPRESSIONS
# ==============================================================================

def p_expression_statement(p):
    "expression_statement : expression SEMICOLON"
    p[0] = p[1]


def p_expression(p):
    """expression : assignment_expression"""
    p[0] = p[1]


def p_assignment_expression(p):
    """assignment_expression : conditional_expression
    | unary assignment_operator assignment_expression
    """

    if len(p) == 2:
        p[0] = p[1]
    else:

        p[0] = Assignment(p[1], p[2], p[3])


def p_assignment_operator(p):
    """assignment_operator : EQUAL
    | PLUSEQUAL
    | MINUSEQUAL
    | MULTEQUAL
    | DIVEQUAL"""
    p[0] = p[1]


def p_conditional_expression(p):
    "conditional_expression : logical_or"
    p[0] = p[1]


def p_logical_or(p):
    """logical_or : logical_and
    | logical_or OR logical_and"""
    if len(p) == 2:
        p[0] = p[1]
    else:
        p[0] = LogicalOperator("||", p[1], p[3])


def p_logical_and(p):
    """logical_and : equality
    | logical_and AND equality"""
    if len(p) == 2:
        p[0] = p[1]
    else:
        p[0] = LogicalOperator("&&", p[1], p[3])


def p_equality(p):
    """equality : comparison
    | equality EQEQ comparison
    | equality NOTEQ comparison"""
    if len(p) == 2:
        p[0] = p[1]
    else:
        p[0] = ComparisonOperator(p[2], p[1], p[3])


def p_comparison(p):
    """comparison : additive
    | comparison LT additive
    | comparison GT additive
    | comparison LTEQ additive
    | comparison GTEQ additive"""
    if len(p) == 2:
        p[0] = p[1]
    else:
        p[0] = ComparisonOperator(p[2], p[1], p[3])


def p_additive(p):
    """additive : multiplicative
    | additive PLUS multiplicative
    | additive MINUS multiplicative"""
    if len(p) == 2:
        p[0] = p[1]
    else:
        p[0] = AdditiveOperator(p[2], p[1], p[3])


def p_multiplicative(p):
    """multiplicative : unary
    | multiplicative MULT unary
    | multiplicative DIV unary
    | multiplicative MOD unary"""
    if len(p) == 2:
        p[0] = p[1]
    else:
        p[0] = MultiplicativeOperator(p[2], p[1], p[3])


def p_unary(p):
    """unary : PLUS unary
    | MINUS unary %prec UMINUS
    | AMPERSAND unary %prec ADDRESSOF_PREC
    | MULT unary %prec DEREFERENCE_PREC
    | NOT unary
    | postfix"""
    if len(p) == 3:
        op_token_type = p.slice[1].type
        op_value = p[1]

        if op_token_type == "PLUS":
            p[0] = p[2]
        elif op_token_type == "MINUS":
            p[0] = UnaryOperator(op_value, p[2])
        elif op_token_type == "AMPERSAND":
            p[0] = AddressOfNode(p[2])
        elif op_token_type == "MULT":
            p[0] = DereferenceNode(p[2])
        elif op_token_type == "NOT":

            p[0] = UnaryOperator(op_value, p[2])
        else:
            raise SyntaxError(f"Unknown unary operator token type: {op_token_type}")
    else:
        p[0] = p[1]


def p_postfix(p):
    """
    postfix : primary
            | postfix postfix_suffix
    """
    if len(p) == 2:
        p[0] = p[1]
    else:
        left = p[1]
        kind, data = p[2]

        if kind == "inc" or kind == "dec":
            p[0] = PostfixOperator(data, left)
        elif kind == "call":
            args = data
            p[0] = FunctionCall(left, args)
        elif kind == "method":
            method_name, args = data
            p[0] = StructMethodCall(left, method_name, args)
        elif kind == "field":
            p[0] = MemberAccess(left, data)
        elif kind == "qual":

            if isinstance(left, str):
                p[0] = ModuleAccess(left, data)
            elif isinstance(left, ModuleAccess):

                new_alias = f"{left.alias}::{left.name}" if left.name else left.alias
                p[0] = ModuleAccess(new_alias, data)

            else:
                raise SyntaxError(f"Invalid left-hand side for COLONCOLON: {left}")
        elif kind == "index":
            index_expr = data
            p[0] = ArrayIndexNode(left, index_expr)
        else:
            raise SyntaxError(f"Unknown postfix kind: {kind}")


def p_postfix_suffix(p):
    """
    postfix_suffix : INCREMENT
                   | DECREMENT
                   | LPAREN arguments RPAREN
                   | DOT IDENTIFIER LPAREN arguments RPAREN
                   | DOT IDENTIFIER
                   | COLONCOLON IDENTIFIER
                   | LBRACKET expression RBRACKET"""
    tok = p.slice[1].type
    if tok == "INCREMENT":
        p[0] = ("inc", "++")
    elif tok == "DECREMENT":
        p[0] = ("dec", "--")
    elif tok == "LPAREN":
        p[0] = ("call", p[2])
    elif tok == "DOT":
        if len(p) == 3:
            p[0] = ("field", p[2])
        else:
            p[0] = ("method", (p[2], p[4]))
    elif tok == "COLONCOLON":
        p[0] = ("qual", p[2])
    elif tok == "LBRACKET":
        p[0] = ("index", p[2])

    else:

        raise SyntaxError(
            f"Unknown postfix_suffix starting with token {tok} value {p[1]}"
        )

