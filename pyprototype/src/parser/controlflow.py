from src.ast2.nodes import *




# ==============================================================================
#                                 CONTROL FLOW
# ==============================================================================

def p_if_statement(p):
    """if_statement : IF LPAREN expression RPAREN block else_clause_opt"""

    p[0] = IfStatement(
        condition=p[3], body=p[5], elifs=p[6]["elifs"], else_body=p[6]["else"]
    )


def p_else_clause_opt(p):
    """else_clause_opt : else_if_list else_block_opt
    | else_block_opt"""
    if len(p) == 3:
        p[0] = {"elifs": p[1], "else": p[2]}
    else:
        p[0] = {"elifs": [], "else": p[1]}


def p_else_if_list(p):
    """else_if_list : ELSEIF LPAREN expression RPAREN block
    | else_if_list ELSEIF LPAREN expression RPAREN block"""
    if len(p) == 6:
        p[0] = [(p[3], p[5])]
    else:
        p[0] = p[1] + [(p[4], p[6])]


def p_else_block_opt(p):
    """else_block_opt : ELSE block
    | empty"""
    if len(p) == 3:
        p[0] = p[2]
    else:
        p[0] = None


def p_loop_statement(p):
    """loop_statement : while_loop
    | for_loop
    | foreach_loop"""
    p[0] = p[1]


def p_while_loop(p):
    "while_loop : WHILE LPAREN expression RPAREN block"
    p[0] = WhileLoop(p[3], p[5])


def p_for_loop(p):
    "for_loop : FOR LPAREN variable_declaration expression SEMICOLON expression RPAREN block"
    p[0] = ForLoop(init=p[3], condition=p[4], increment=p[6], body=p[8])


def p_foreach_loop(p):
    "foreach_loop : FOREACH IDENTIFIER LT type GT IN expression block"
    p[0] = ForeachLoop(p[2], p[4], p[7], p[8])


def p_control_statement(p):
    """control_statement : BREAK SEMICOLON
    | CONTINUE SEMICOLON"""
    p[0] = ControlStatement(p[1])


def p_return_statement(p):
    """return_statement : RETURN expression SEMICOLON
    | RETURN SEMICOLON"""
    if len(p) == 4:
        p[0] = ReturnStatement(p[2])
    else:
        p[0] = ReturnStatement(None)
