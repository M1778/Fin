from src.ast2.nodes import *

# ==============================================================================
#                                 MACROS
# ==============================================================================

def p_special_programming(p):
    """special_programming : AT IDENTIFIER LBRACE statements RBRACE"""
    p[0] = SpecialDeclaration(p[2], p[4])


def p_macro_declaration(p):
    """macro_declaration : AT MACRO IDENTIFIER LPAREN macro_param_list RPAREN LBRACE statements RBRACE"""
    p[0] = MacroDeclaration(name=p[3], params=p[5], body=p[8])


def p_primary_macro_call(p):
    """primary : DOLLAR IDENTIFIER LPAREN arguments RPAREN"""

    p[0] = MacroCall(p[2], p[4] if p[4] is not None else [])


def p_macro_param_list(p):
    """macro_param_list : IDENTIFIER
    | macro_param_list COMMA IDENTIFIER
    | empty"""
    if len(p) == 2:
        p[0] = [] if p[1] is None else [p[1]]
    else:
        p[0] = p[1] + [p[3]]
