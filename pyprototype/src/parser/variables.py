from src.ast2.nodes import *

# ==============================================================================
#                                 VARIABLES & TYPES
# ==============================================================================

def p_variable_declaration(p):
    """variable_declaration : mutable_declaration
    | immutable_declaration
    | declared_not_assigned_declaration"""
    p[0] = p[1]


def p_declared_not_assigned_declaration(p):
    """declared_not_assigned_declaration : LET IDENTIFIER LT type GT SEMICOLON
    | BEZ IDENTIFIER LT type GT SEMICOLON
    | CONST IDENTIFIER LT type GT SEMICOLON
    | BETON IDENTIFIER LT type GT SEMICOLON"""
    p[0] = VariableDeclaration(
        is_mutable=True, identifier=p[2], var_type=p[4], value=None
    )


def p_mutable_declaration(p):
    """mutable_declaration : LET IDENTIFIER LT type GT EQUAL expression SEMICOLON
    | BEZ IDENTIFIER LT type GT EQUAL expression SEMICOLON"""
    p[0] = VariableDeclaration(
        is_mutable=True, identifier=p[2], var_type=p[4], value=p[7]
    )


def p_immutable_declaration(p):
    """immutable_declaration : CONST IDENTIFIER LT type GT EQUAL expression SEMICOLON
    | BETON IDENTIFIER LT type GT EQUAL expression SEMICOLON"""
    p[0] = VariableDeclaration(
        is_mutable=False, identifier=p[2], var_type=p[4], value=p[7]
    )


def p_type(p):
    """type : base_type
    | pointer_type
    | array_type"""
    p[0] = p[1]


def p_base_type(p):
    """base_type : IDENTIFIER
    | IDENTIFIER COLONCOLON IDENTIFIER
    | AUTO
    | IDENTIFIER LPAREN INTEGER RPAREN
    | LPAREN type RPAREN"""

    if (
        p.slice[1].type == "LPAREN"
        and p.slice[len(p) - 1].type == "RPAREN"
        and len(p) == 4
    ):

        p[0] = p[2]
    elif len(p) == 2:
        if p[1] == "auto":
            p[0] = "auto"
        else:
            p[0] = p[1]
    elif len(p) == 4 and p.slice[2].type == "COLONCOLON":
        p[0] = ModuleAccess(p[1], p[3])
    elif (
        len(p) == 5 and p.slice[1].type == "IDENTIFIER" and p.slice[2].type == "LPAREN"
    ):
        base = p[1]
        bits = p[3]
        p[0] = TypeAnnotation(base, bits)


def p_pointer_type(p):
    """pointer_type : AMPERSAND type"""
    p[0] = PointerTypeNode(p[2])


def p_array_type(p):
    """array_type : LBRACKET type RBRACKET
    | LBRACKET type COMMA expression RBRACKET"""

    if len(p) == 4:
        p[0] = ArrayTypeNode(element_type=p[2], size_expr=None)
    elif len(p) == 6:

        p[0] = ArrayTypeNode(element_type=p[2], size_expr=p[4])


def p_type_param(p):
    """type : IDENTIFIER LPAREN INTEGER RPAREN"""

    base = p[1]
    bits = int(p[3])
    p[0] = TypeAnnotation(base, bits)


def p_generic_param_list_decl_opt(p):
    """generic_param_list_decl_opt : LT generic_param_list_items GT
    | empty"""
    if len(p) == 4:
        p[0] = p[2]
    else:
        p[0] = []


def p_generic_param_list_items(p):
    """generic_param_list_items : IDENTIFIER
    | generic_param_list_items COMMA IDENTIFIER"""
    if len(p) == 2:
        p[0] = [p[1]]
    else:
        p[0] = p[1] + [p[3]]

