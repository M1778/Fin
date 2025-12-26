# ==============================================================================
#                                 UTILITIES
# ==============================================================================

def p_empty(p):
    "empty :"
    p[0] = None


def p_error(p):
    if p:
        print(f"Syntax error at '{p.value}' (line {p.lineno})")
    else:
        print("Syntax error at EOF")


precedence = (
    ("right", "EQUAL", "PLUSEQUAL", "MINUSEQUAL", "MULTEQUAL", "DIVEQUAL"),
    ("left", "OR"),
    ("left", "AND"),
    ("nonassoc", "EQEQ", "NOTEQ", "LT", "GT", "LTEQ", "GTEQ"),
    ("left", "PLUS", "MINUS"),
    ("left", "MULT", "DIV", "MOD"),
    ("right", "NOT", "UMINUS", "ADDRESSOF_PREC", "DEREFERENCE_PREC"),
    ("left", "LPAREN", "LBRACKET", "DOT"),
    ("left", "INCREMENT", "DECREMENT"),
)
