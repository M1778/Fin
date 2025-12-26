from src.ast2.nodes import *

# ==============================================================================
#                                 PRIMARY EXPRESSIONS
# ==============================================================================

def p_primary(p):
    """primary : literal
    | IDENTIFIER
    | LPAREN expression RPAREN
    | IDENTIFIER LBRACE field_assignments RBRACE
    | typeof_expression
    | array_literal
    | new_heap_allocation_expression
    | sizeof_expression
    | as_ptr_expression"""
    if len(p) == 2:
        p[0] = p[1]
    elif p.slice[1].type == "LPAREN":
        p[0] = p[2]
    elif p.slice[1].type == "IDENTIFIER" and p.slice[2].type == "LBRACE":
        p[0] = StructInstantiation(p[1], p[3])

    if len(p) == 2 and p.slice[1].type == "NEW":
        p[0] = p[1]


def p_reserved_kw_tconv(p):
    """reserved_kw_tconv : STD_CONV"""
    p[0] = p[1]


def p_primary_type_conv(p):
    """primary : reserved_kw_tconv LT type GT LPAREN expression RPAREN"""

    name = p[1]
    to_ty = p[3]
    expr = p[6]
    p[0] = TypeConv(to_ty, expr)


def p_as_ptr_expression(p):
    """as_ptr_expression : AS_PTR LPAREN expression RPAREN"""
    p[0] = AsPtrNode(expression_ast=p[3])


def p_sizeof_expression(p):
    """sizeof_expression : SIZEOF LPAREN sizeof_target RPAREN"""
    p[0] = SizeofNode(target_ast_node=p[3])


def p_sizeof_target(p):
    """sizeof_target : LT type GT
    | expression
    """
    if len(p) == 4:
        p[0] = p[2]
    else:
        p[0] = p[1]


def p_primary_new_angle_brackets(p):
    """primary : NEW LT type GT"""
    p[0] = NewExpressionNode(alloc_type_ast=p[3])


def p_new_heap_allocation_expression(p):
    """new_heap_allocation_expression : NEW LT type GT"""
    p[0] = NewExpressionNode(alloc_type_ast=p[3])


def p_delete_statement(p):
    """delete_statement : DELETE expression SEMICOLON"""
    p[0] = DeleteStatementNode(pointer_expr_ast=p[2])


def p_array_literal(p):
    """array_literal : LBRACKET arguments RBRACKET"""
    p[0] = ArrayLiteralNode(p[2])


def p_typeof_expression(p):
    """typeof_expression : TYPEOF LPAREN expression RPAREN"""
    p[0] = TypeOf(p[3])


def p_field_assignments_single(p):
    """field_assignments : field_assignment
    | empty"""
    p[0] = [p[1]]


def p_field_assignments_multiple(p):
    "field_assignments : field_assignments COMMA field_assignment"
    p[0] = p[1] + [p[3]]


def p_field_assignment(p):
    "field_assignment : IDENTIFIER COLON expression"
    p[0] = FieldAssignment(p[1], p[3])


def p_arguments(p):
    """arguments : expression_list
    | empty"""
    p[0] = p[1] if p[1] is not None else []


def p_expression_list_single(p):
    "expression_list : expression"
    p[0] = [p[1]]


def p_expression_list_multiple(p):
    "expression_list : expression_list COMMA expression"
    p[0] = p[1] + [p[3]]


def p_literal(p):
    """literal : INTEGER
    | FLOAT
    | STRING_LITERAL
    | CHAR_LITERAL"""
    p[0] = Literal(p[1])