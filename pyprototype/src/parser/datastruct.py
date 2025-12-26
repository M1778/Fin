from src.ast2.nodes import *

# ==============================================================================
#                                 DATA STRUCTURES
# ==============================================================================

def p_struct_declaration(p):
    "struct_declaration : STRUCT IDENTIFIER LBRACE struct_body RBRACE"
    p[0] = StructDeclaration(
        name=p[2], members=p[4]["members"], methods=p[4]["methods"]
    )


def p_struct_body(p):
    """struct_body : struct_members struct_methods
    | struct_members
    | struct_methods
    | empty"""
    if len(p) == 3:
        p[0] = {"members": p[1], "methods": p[2]}
    elif len(p) == 2 and isinstance(p[1], list):
        p[0] = {"members": p[1], "methods": []}
    elif len(p) == 2 and isinstance(p[1], dict):
        p[0] = {"members": [], "methods": p[1]["methods"]}
    elif len(p) == 2 and p[1] is None:
        p[0] = {"members": [], "methods": []}
    else:
        p[0] = {"members": [], "methods": p[1]}


def p_struct_methods(p):
    """struct_methods : struct_methods function_declaration
    | function_declaration"""
    if len(p) == 3:
        p[0] = p[1] + [p[2]]
    else:
        p[0] = [p[1]]


def p_struct_members_single(p):
    """struct_members : struct_member"""
    p[0] = [p[1]]


def p_struct_members_multiple(p):
    """struct_members : struct_members COMMA struct_member"""
    p[0] = p[1] + [p[3]]


def p_struct_members_trailing_comma(p):
    """struct_members : struct_members COMMA"""
    p[0] = p[1]


def p_struct_member(p):
    """struct_member : IDENTIFIER LT type GT"""
    p[0] = StructMember(p[1], p[3])


def p_enum_declaration(p):
    "enum_declaration : ENUM IDENTIFIER LBRACE enum_values RBRACE"
    p[0] = EnumDeclaration(name=p[2], values=p[4])


def p_enum_values_single(p):
    "enum_values : enum_value"
    p[0] = [p[1]]


def p_enum_values_multiple(p):
    "enum_values : enum_values COMMA enum_value"
    p[0] = p[1] + [p[3]]


def p_enum_value(p):
    """enum_value : IDENTIFIER
    | IDENTIFIER EQUAL expression"""
    if len(p) == 2:
        p[0] = (p[1], None)
    else:
        p[0] = (p[1], p[3])
