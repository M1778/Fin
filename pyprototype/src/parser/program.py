from src.ast2.nodes import *


# ==============================================================================
#                                 PROGRAM STRUCTURE
# ==============================================================================

def p_program(p):
    "program : statements"
    p[0] = Program(p[1])


def p_statements_single(p):
    """statements : statement"""
    p[0] = [p[1]]


def p_statements_multiple(p):
    "statements : statements statement"
    p[0] = p[1] + [p[2]]


def p_statement(p):
    """statement : macro_declaration
    | variable_declaration
    | function_declaration
    | struct_declaration
    | delete_statement
    | enum_declaration
    | special_programming
    | if_statement
    | loop_statement
    | control_statement
    | return_statement
    | expression_statement
    | import_c_statement
    | import_statement
    | extern_statement
    | empty"""
    p[0] = p[1]


def p_block(p):
    """block : LBRACE statements RBRACE"""
    p[0] = p[2]
