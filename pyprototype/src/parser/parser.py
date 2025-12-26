from ..lib import yacc
from ..lexer import lexer, tokens, keywords, find_column
from ..ast2.nodes import *
from .packages.errors import *

# ==============================================================================
#                                 SETUP
# ==============================================================================
global parser
diagnostic_engine = None
last_token = None 
second_last_token = None 

original_lexer_token = lexer.token
def tracked_token():
    global last_token, second_last_token
    tok = original_lexer_token()
    if tok:
        tok.prev = last_token
        if last_token:
            tok.prev_prev = second_last_token
        else:
            tok.prev_prev = None
        second_last_token = last_token
        last_token = tok
    return tok
lexer.token = tracked_token

def attach_loc(node, p, index=1):
    """
    Attaches location info (lineno, col_offset) to an AST node.
    Robustly handles missing metadata.
    """
    if node is None or not hasattr(node, '__dict__'):
        return node
    
    lineno = p.lineno(index)
    lexpos = p.lexpos(index)

    # 1. Try linespan (PLY 3.4+) for better accuracy
    if (lineno == 0 or lineno is None) and hasattr(p, 'linespan'):
        try:
            lineno = p.linespan(index)[0]
            lexpos = p.lexspan(index)[0]
        except: pass

    # 2. Fallback: Use the start of the entire rule (index 0)
    if lineno == 0 or lineno is None:
        lineno = p.lineno(0)
        lexpos = p.lexpos(0)

    node.lineno = lineno if lineno else 0
    node.lexpos = lexpos if lexpos else 0
    
    if node.lexpos:
        node.col_offset = find_column(p.lexer.lexdata, node.lexpos)
    else:
        node.col_offset = 0
        
    return node

# ==============================================================================
#                                 PROGRAM
# ==============================================================================

def p_program(p):
    "program : statements"
    p[0] = Program(p[1])

def p_statements(p):
    """statements : statements statement
                  | statement
                  | empty"""
    if len(p) == 3:
        if p[2] is None: p[0] = p[1]
        else: p[0] = p[1] + [p[2]]
    elif len(p) == 2:
        if p[1] is None: p[0] = []
        else: p[0] = [p[1]]

def p_statement(p):
    """statement : define_declaration
    | macro_declaration
    | variable_declaration
    | function_declaration
    | struct_declaration
    | interface_declaration
    | delete_statement
    | enum_declaration
    | macro_return_statement
    | special_declaration
    | if_statement
    | loop_statement
    | try_catch_statement
    | blame_statement
    | control_statement
    | return_statement
    | expression_statement
    | import_statement
    | SEMICOLON
    """
    if len(p) == 2 and p.slice[1].type == 'SEMICOLON':
        p[0] = None # Ignore empty statements
    else:
        p[0] = p[1]

def p_block(p):
    """block : LBRACE statements RBRACE"""
    p[0] = p[2]

# ==============================================================================
#                                 IMPORTS
# ==============================================================================
def p_import_statement(p):
    """import_statement : IMPORT import_source SEMICOLON
                        | IMPORT import_source AS IDENTIFIER SEMICOLON
                        | IMPORT LBRACE import_targets RBRACE FROM import_source SEMICOLON"""
    if len(p) == 4:
        source, is_pkg = p[2]
        p[0] = attach_loc(ImportModule(source=source, is_package=is_pkg, alias=None), p, 1)
    elif len(p) == 6:
        source, is_pkg = p[2]
        alias = p[4]
        p[0] = attach_loc(ImportModule(source=source, is_package=is_pkg, alias=alias), p, 1)
    else:
        targets = p[3]
        source, is_pkg = p[6]
        p[0] = attach_loc(ImportModule(source=source, is_package=is_pkg, targets=targets), p, 1)

def p_import_source(p):
    """import_source : STRING_LITERAL
                     | dotted_path"""
    if p.slice[1].type == 'STRING_LITERAL':
        p[0] = (p[1], False)
    else:
        p[0] = (p[1], True)

def p_dotted_path(p):
    """dotted_path : IDENTIFIER
                   | dotted_path DOT IDENTIFIER"""
    if len(p) == 2:
        p[0] = p[1]
    else:
        p[0] = f"{p[1]}.{p[3]}"

def p_import_targets(p):
    """import_targets : IDENTIFIER
                      | import_targets COMMA IDENTIFIER"""
    if len(p) == 2: p[0] = [p[1]]
    else: p[0] = p[1] + [p[3]]

# ==============================================================================
#                                 OOP & STRUCTS
# ==============================================================================

def p_struct_declaration(p):
    """struct_declaration : attributes_opt visibility_opt STRUCT IDENTIFIER generic_param_list_decl_opt inheritance_opt LBRACE struct_body RBRACE"""
    p[0] = attach_loc(StructDeclaration(
        name=p[4], 
        members=p[8]["members"], 
        methods=p[8]["methods"],
        constructor=p[8]["constructor"],
        destructor=p[8]["destructor"],
        operators=p[8]["operators"],
        visibility=p[2],
        generic_params=p[5],
        parents=p[6],
        attributes=p[1]
    ), p, 2)

def p_struct_body(p):
    """struct_body : struct_content_list
                   | empty"""
    if len(p) == 2 and p[1] is not None:
        members = [x for x in p[1] if isinstance(x, StructMember)]
        methods = [x for x in p[1] if isinstance(x, FunctionDeclaration)]
        constructors = [x for x in p[1] if isinstance(x, ConstructorDeclaration)]
        destructors = [x for x in p[1] if isinstance(x, DestructorDeclaration)]
        operators = [x for x in p[1] if isinstance(x, OperatorDeclaration)]
        
        ctor = constructors[0] if constructors else None
        dtor = destructors[0] if destructors else None
        
        p[0] = {"members": members, "methods": methods, "constructor": ctor, "destructor": dtor, "operators": operators}
    else:
        p[0] = {"members": [], "methods": [], "constructor": None, "destructor": None, "operators": []}

def p_struct_content_list(p):
    """struct_content_list : struct_content_list struct_content
                           | struct_content"""
    if len(p) == 3: p[0] = p[1] + [p[2]]
    else: p[0] = [p[1]]

def p_struct_content(p):
    """struct_content : struct_member_with_comma
                      | function_declaration
                      | constructor_declaration
                      | destructor_declaration
                      | operator_declaration"""
    p[0] = p[1]

def p_struct_member_with_comma(p):
    """struct_member_with_comma : struct_member COMMA
                                | struct_member"""
    p[0] = p[1]
    
def p_struct_member(p):
    """struct_member : visibility_opt IDENTIFIER LT type GT
                     | visibility_opt IDENTIFIER LT type GT EQUAL expression"""
    if len(p) == 6:
        p[0] = attach_loc(StructMember(p[2], p[4], visibility=p[1], default_value=None), p, 2)
    else:
        # [FIX] Use p[7] for the expression, not p[6] (which is '=')
        p[0] = attach_loc(StructMember(p[2], p[4], visibility=p[1], default_value=p[7]), p, 2)

def p_constructor_declaration(p):
    """constructor_declaration : STRUCT LPAREN params RPAREN LBRACE statements RBRACE
                               | STRUCT LBRACE statements RBRACE"""
    if len(p) == 8:
        p[0] = attach_loc(ConstructorDeclaration(params=p[3], body=p[6], visibility="public"), p, 1)
    else:
        p[0] = attach_loc(ConstructorDeclaration(params=[], body=p[3], visibility="public"), p, 1)

def p_destructor_declaration(p):
    """destructor_declaration : DELETE LPAREN RPAREN LBRACE statements RBRACE
                              | DELETE LBRACE statements RBRACE"""
    if len(p) == 7:
        p[0] = attach_loc(DestructorDeclaration(body=p[5]), p, 1)
    else:
        p[0] = attach_loc(DestructorDeclaration(body=p[3]), p, 1)

def p_operator_declaration(p):
    """operator_declaration : visibility_opt OPERATOR generic_param_list_decl_opt operator_symbol LPAREN params RPAREN return_type LBRACE statements RBRACE"""
    p[0] = attach_loc(OperatorDeclaration(
        operator=p[4],
        params=p[6],
        return_type=p[8],
        body=p[10],
        visibility=p[1],
        generic_params=p[3]
    ), p, 2)

def p_operator_symbol(p):
    """operator_symbol : PLUS
                       | MINUS
                       | MULT
                       | DIV
                       | MOD
                       | EQEQ
                       | NOTEQ
                       | LT
                       | GT
                       | LTEQ
                       | GTEQ
                       | AND
                       | OR
                       | NOT"""
    p[0] = p[1]

def p_inheritance_opt(p):
    """inheritance_opt : COLON LT type_list GT
                       | empty"""
    if len(p) == 5: p[0] = p[3]
    else: p[0] = []

def p_type_list(p):
    """type_list : type
                 | type_list COMMA type"""
    if len(p) == 2: p[0] = [p[1]]
    else: p[0] = p[1] + [p[3]]

def p_interface_declaration(p):
    """interface_declaration : visibility_opt INTERFACE IDENTIFIER generic_param_list_decl_opt LBRACE interface_body RBRACE"""
    p[0] = attach_loc(InterfaceDeclaration(
        name=p[3],
        methods=p[6]["methods"],
        visibility=p[1],
        generic_params=p[4]
    ), p, 2)
def p_interface_body(p):
    """interface_body : interface_content_list
                      | empty"""
    if len(p) == 2 and p[1] is not None:
        members = [x for x in p[1] if isinstance(x, StructMember)]
        methods = [x for x in p[1] if isinstance(x, FunctionDeclaration)]
        p[0] = {"members": members, "methods": methods}
    else:
        p[0] = {"members": [], "methods": []}

def p_interface_content_list(p):
    """interface_content_list : interface_content_list interface_content
                              | interface_content"""
    if len(p) == 3: p[0] = p[1] + [p[2]]
    else: p[0] = [p[1]]

def p_interface_content(p):
    """interface_content : struct_member SEMICOLON
                         | function_declaration"""
    # Interfaces use SEMICOLON for members: `pub x <T>;`
    p[0] = p[1]
# ==============================================================================
#                                 FUNCTIONS
# ==============================================================================
def p_define_declaration(p):
    """define_declaration : AT DEFINE IDENTIFIER LPAREN extern_params_content RPAREN return_type SEMICOLON
                          | attribute_list AT DEFINE IDENTIFIER LPAREN extern_params_content RPAREN return_type SEMICOLON"""
    
    # Determine if attributes are present
    has_attrs = (p.slice[1].type == 'attribute_list')
    
    if has_attrs:
        attrs = p[1]
        base_idx = 1 # Shift indices by 1
    else:
        attrs = []
        base_idx = 0
    
    # Indices relative to base_idx:
    # [1] @
    # [2] DEFINE
    # [3] Name
    # [5] Params
    # [7] Return Type
    
    param_list, is_vararg = p[base_idx + 5]
    
    p[0] = attach_loc(DefineDeclaration(
        name=p[base_idx + 3],
        params=param_list,
        return_type=p[base_idx + 7],
        is_vararg=is_vararg,
        attributes=attrs
    ), p, base_idx + 2)
    
def p_extern_params_content(p):
    """extern_params_content : extern_param_list COMMA ELLIPSIS
                             | extern_param_list
                             | empty"""
    # Returns: (param_list, is_vararg)
    if len(p) == 4: 
        p[0] = (p[1], True) # Has varargs (...)
    elif len(p) == 2:
        if p[1] is None: 
            p[0] = ([], False) # Empty
        else: 
            p[0] = (p[1], False) # No varargs
    else: 
        p[0] = ([], False)

def p_extern_param_list_single(p):
    """extern_param_list : extern_param"""
    p[0] = [p[1]]

def p_extern_param_list_multiple(p):
    """extern_param_list : extern_param_list COMMA extern_param"""
    p[0] = p[1] + [p[3]]

def p_extern_param(p):
    """extern_param : IDENTIFIER COLON LT type GT"""
    # We use the standard Parameter node
    p[0] = attach_loc(Parameter(identifier=p[1], var_type=p[4], default_value=None), p, 1)

def p_function_declaration(p):
    """function_declaration : attributes_opt visibility_opt FUN IDENTIFIER generic_param_list_decl_opt LPAREN params RPAREN return_type LBRACE statements RBRACE
                            | attributes_opt visibility_opt STATIC FUN IDENTIFIER generic_param_list_decl_opt LPAREN params RPAREN return_type LBRACE statements RBRACE
                            | attributes_opt visibility_opt FUN IDENTIFIER generic_param_list_decl_opt LPAREN params RPAREN return_type SEMICOLON
                            | attributes_opt visibility_opt STATIC FUN IDENTIFIER generic_param_list_decl_opt LPAREN params RPAREN return_type SEMICOLON"""
    
    # Indices are shifted by 1 because of attributes_opt at p[1]
    attrs = p[1]
    visibility = p[2]
    
    # Determine Static vs Instance
    # p[3] is either FUN or STATIC
    is_static_decl = (p.slice[3].type == "STATIC")
    
    if is_static_decl:
        name_idx = 5
        generic_params_idx = 6
        params_lparen_idx = 7
    else:
        name_idx = 4
        generic_params_idx = 5
        params_lparen_idx = 6

    func_name = p[name_idx]
    type_params_list = p[generic_params_idx]
    params_tuple = p[params_lparen_idx + 1]
    actual_params = params_tuple[0]
    is_vararg = params_tuple[1]
    return_type = p[params_lparen_idx + 3]
    
    
    
    # Determine Body vs Abstract (Semicolon)
    token_after_ret = p.slice[params_lparen_idx + 4]
    
    body_stmts = None
    if token_after_ret.type == 'LBRACE':
        body_stmts = p[params_lparen_idx + 5]
    else:
        body_stmts = None

    # Use index 3 (FUN/STATIC) for location
    p[0] = attach_loc(FunctionDeclaration(
        name=func_name,
        params=actual_params,
        return_type=return_type,
        body=body_stmts,
        is_static=is_static_decl,
        type_parameters=type_params_list,
        visibility=visibility,
        is_vararg=is_vararg,
        attributes=attrs # [NEW] Pass attributes
    ), p, 3)
  

def p_fn_type(p):
    """fn_type : FN_TYPE LPAREN type_list_opt RPAREN return_type"""
    # Syntax: fn(int, int) <int>
    p[0] = attach_loc(FunctionTypeNode(arg_types=p[3], return_type=p[5]), p, 1)

def p_type_list_opt(p):
    """type_list_opt : type_list
                     | empty"""
    p[0] = p[1] if p[1] else []

def p_lambda_expression(p):
    """lambda_expression : LPAREN params RPAREN return_type ARROW block"""
    # Case 1: (x:int) <int> => { ... }
    if len(p) == 7:
        p[0] = attach_loc(LambdaNode(params=p[2], return_type=p[4], body=p[6]), p, 3) # Loc at ARROW? or LPAREN(1)
    # Case 2: fun(x:int) <int> => { ... }
    else:
        p[0] = attach_loc(LambdaNode(params=p[3], return_type=p[5], body=p[7]), p, 1)
        
def p_params(p):
    """params : param_list COMMA ELLIPSIS
              | param_list
              | ELLIPSIS
              | empty"""
    if len(p) == 4: # param_list , ...
        p[0] = (p[1], True)
    elif len(p) == 2:
        if p[1] is None: 
            p[0] = ([], False)
        elif p[1] == '...': 
            p[0] = ([], True)
        else: 
            p[0] = (p[1], False)
    else:
        p[0] = ([], False)

def p_param_list_single(p):
    "param_list : param"
    p[0] = [p[1]]

def p_param_list_multiple(p):
    "param_list : param_list COMMA param"
    p[0] = p[1] + [p[3]]

def p_param(p):
    """param : IDENTIFIER COLON LT type GT
             | IDENTIFIER COLON LT type GT EQUAL expression
             | ELLIPSIS IDENTIFIER COLON LT type GT"""
    # Case 1: Standard (x: <int>)
    if len(p) == 6 and p.slice[1].type == 'IDENTIFIER':
        p[0] = attach_loc(Parameter(identifier=p[1], var_type=p[4], default_value=None, is_vararg=False), p, 1)
    
    # Case 2: Default (x: <int> = 10)
    elif len(p) == 8:
        p[0] = attach_loc(Parameter(identifier=p[1], var_type=p[4], default_value=p[7], is_vararg=False), p, 1)
    
    # Case 3: Vararg (...x: <[int]>)
    elif len(p) == 7 and p.slice[1].type == 'ELLIPSIS':
        p[0] = attach_loc(Parameter(identifier=p[2], var_type=p[5], default_value=None, is_vararg=True), p, 2)
    
    
def p_return_type(p):
    """return_type : LT type GT
    | NORET"""
    if len(p) == 4: p[0] = p[2]
    else: p[0] = p[1]

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
    p[0] = attach_loc(VariableDeclaration(is_mutable=True, identifier=p[2], var_type=p[4], value=None), p, 1)

def p_mutable_declaration(p):
    """mutable_declaration : LET IDENTIFIER LT type GT EQUAL expression SEMICOLON
    | BEZ IDENTIFIER LT type GT EQUAL expression SEMICOLON"""
    p[0] = attach_loc(VariableDeclaration(is_mutable=True, identifier=p[2], var_type=p[4], value=p[7]), p, 1)

def p_immutable_declaration(p):
    """immutable_declaration : CONST IDENTIFIER LT type GT EQUAL expression SEMICOLON
    | BETON IDENTIFIER LT type GT EQUAL expression SEMICOLON"""
    p[0] = attach_loc(VariableDeclaration(is_mutable=False, identifier=p[2], var_type=p[4], value=p[7]), p, 1)

def p_type(p):
    """type : base_type
    | pointer_type
    | array_type
    | fn_type"""
    p[0] = p[1]

def p_primitive_type(p):
    """primitive_type : TYPE_INT
                      | TYPE_FLOAT
                      | TYPE_BOOL
                      | TYPE_STRING
                      | TYPE_CHAR
                      | TYPE_VOID
                      | TYPE_LONG
                      | TYPE_DOUBLE"""
    p[0] = p[1] # Returns string "int", "float", etc.

def p_base_type(p):
    """base_type : dotted_path
                 | dotted_generic_type_usage
                 | AUTO
                 | LPAREN type RPAREN
                 | type_annotation
                 | primitive_type"""
    
    # Handle Primitives
    if p.slice[1].type in ['TYPE_INT', 'TYPE_LONG', 'TYPE_FLOAT', 'TYPE_DOUBLE', 'TYPE_BOOL', 'TYPE_STRING', 'TYPE_CHAR', 'TYPE_VOID']:
        # Return the string value (e.g. "int") so ast_to_fin_type can handle it
        p[0] = p[1]
        
    elif len(p) == 2:
        if p[1] == "auto": p[0] = "auto"
        elif p[1] == "Self": p[0] = "Self"
        else: p[0] = p[1]
    elif p.slice[1].type == "LPAREN":
        p[0] = p[2]

def p_type_annotation(p):
    """type_annotation : IDENTIFIER LPAREN INTEGER RPAREN"""
    base = p[1]
    bits = int(p[3])
    p[0] = attach_loc(TypeAnnotation(base, bits), p, 1)

def p_generic_param_decl(p):
    """generic_param_decl : IDENTIFIER
                          | IDENTIFIER COLON type"""
    if len(p) == 2:
        p[0] = GenericParam(name=p[1], constraint=None)
    else:
        p[0] = GenericParam(name=p[1], constraint=p[3])

# def p_generic_type_usage(p):
#     """generic_type_usage : IDENTIFIER LT type_list GT"""
#     p[0] = attach_loc(GenericTypeNode(base_name=p[1], type_args=p[3]), p, 1)

def p_dotted_generic_type_usage(p):
    """dotted_generic_type_usage : dotted_path LT type_list GT"""
    p[0] = attach_loc(GenericTypeNode(base_name=p[1], type_args=p[3]), p, 1)

def p_pointer_type(p):
    """pointer_type : AMPERSAND type"""
    p[0] = attach_loc(PointerTypeNode(p[2]), p, 1)

def p_array_type(p):
    """array_type : LBRACKET type RBRACKET
    | LBRACKET type COMMA expression RBRACKET"""
    if len(p) == 4: p[0] = attach_loc(ArrayTypeNode(element_type=p[2], size_expr=None), p, 1)
    elif len(p) == 6: p[0] = attach_loc(ArrayTypeNode(element_type=p[2], size_expr=p[4]), p, 1)


def p_generic_param_list_decl_opt(p):
    """generic_param_list_decl_opt : LT generic_param_list_items GT
    | empty"""
    if len(p) == 4: p[0] = p[2]
    else: p[0] = []

def p_generic_param_list_items(p):
    """generic_param_list_items : generic_param_decl
                                | generic_param_list_items COMMA generic_param_decl"""
    if len(p) == 2: 
        p[0] = [p[1]]
    else: 
        p[0] = p[1] + [p[3]]

# ==============================================================================
#                                 ENUMS
# ==============================================================================

def p_enum_declaration(p):
    "enum_declaration : ENUM IDENTIFIER LBRACE enum_values RBRACE"
    p[0] = attach_loc(EnumDeclaration(name=p[2], values=p[4]), p, 1)

def p_enum_values_single(p):
    "enum_values : enum_value"
    p[0] = [p[1]]

def p_enum_values_multiple(p):
    "enum_values : enum_values COMMA enum_value"
    p[0] = p[1] + [p[3]]

def p_enum_value(p):
    """enum_value : IDENTIFIER
    | IDENTIFIER EQUAL expression"""
    if len(p) == 2: p[0] = (p[1], None)
    else: p[0] = (p[1], p[3])

# ==============================================================================
#                                 CONTROL FLOW & EXCEPTION
# ==============================================================================

def p_if_statement(p):
    """if_statement : IF LPAREN expression RPAREN block else_clause_opt"""
    p[0] = attach_loc(IfStatement(condition=p[3], body=p[5], elifs=p[6]["elifs"], else_body=p[6]["else"]), p, 1)

def p_else_clause_opt(p):
    """else_clause_opt : else_if_list else_block_opt
    | else_block_opt"""
    if len(p) == 3: p[0] = {"elifs": p[1], "else": p[2]}
    else: p[0] = {"elifs": [], "else": p[1]}

def p_else_if_list(p):
    """else_if_list : ELSEIF LPAREN expression RPAREN block
    | else_if_list ELSEIF LPAREN expression RPAREN block"""
    if len(p) == 6: p[0] = [(p[3], p[5])]
    else: p[0] = p[1] + [(p[4], p[6])]

def p_else_block_opt(p):
    """else_block_opt : ELSE block
    | empty"""
    if len(p) == 3: p[0] = p[2]
    else: p[0] = None

def p_loop_statement(p):
    """loop_statement : while_loop
    | for_loop
    | foreach_loop"""
    p[0] = p[1]

def p_while_loop(p):
    "while_loop : WHILE LPAREN expression RPAREN block"
    p[0] = attach_loc(WhileLoop(p[3], p[5]), p, 1)

def p_for_loop(p):
    "for_loop : FOR LPAREN variable_declaration expression SEMICOLON expression RPAREN block"
    p[0] = attach_loc(ForLoop(init=p[3], condition=p[4], increment=p[6], body=p[8]), p, 1)

def p_foreach_loop(p):
    "foreach_loop : FOREACH IDENTIFIER LT type GT IN expression block"
    p[0] = attach_loc(ForeachLoop(p[2], p[4], p[7], p[8]), p, 1)

def p_control_statement(p):
    """control_statement : BREAK SEMICOLON
    | CONTINUE SEMICOLON"""
    p[0] = attach_loc(ControlStatement(p[1]), p, 1)

def p_return_statement(p):
    """return_statement : RETURN expression SEMICOLON
    | RETURN SEMICOLON"""
    if len(p) == 4: p[0] = attach_loc(ReturnStatement(p[2]), p, 1)
    else: p[0] = attach_loc(ReturnStatement(None), p, 1)

def p_try_catch_statement(p):
    """try_catch_statement : TRY block CATCH LPAREN IDENTIFIER catch_type_opt RPAREN block"""
    p[0] = attach_loc(TryCatchNode(try_body=p[2], catch_var=p[5], catch_type=p[6], catch_body=p[8]), p, 1)
    
def p_catch_type_opt(p):
    """catch_type_opt : AS type
                      | empty"""
    if len(p) == 3: p[0] = p[2]
    else: p[0] = None

def p_blame_statement(p):
    """blame_statement : BLAME expression SEMICOLON"""
    p[0] = attach_loc(BlameNode(p[2]), p, 1)

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
    | unary assignment_operator assignment_expression"""
    if len(p) == 2: 
        p[0] = p[1]
    else: 
        # [FIX] Use index 1 (unary/LHS) for location, not 2 (operator)
        p[0] = attach_loc(Assignment(p[1], p[2], p[3]), p, 1)

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
    if len(p) == 2: p[0] = p[1]
    else: p[0] = attach_loc(LogicalOperator("||", p[1], p[3]), p, 2)

def p_logical_and(p):
    """logical_and : equality
    | logical_and AND equality"""
    if len(p) == 2: p[0] = p[1]
    else: p[0] = attach_loc(LogicalOperator("&&", p[1], p[3]), p, 2)

def p_equality(p):
    """equality : comparison
    | equality EQEQ comparison
    | equality NOTEQ comparison"""
    if len(p) == 2: p[0] = p[1]
    else: p[0] = attach_loc(ComparisonOperator(p[2], p[1], p[3]), p, 2)

def p_comparison(p):
    """comparison : additive
    | comparison LT additive
    | comparison GT additive
    | comparison LTEQ additive
    | comparison GTEQ additive"""
    if len(p) == 2: p[0] = p[1]
    else: p[0] = attach_loc(ComparisonOperator(p[2], p[1], p[3]), p, 2)

def p_additive(p):
    """additive : multiplicative
    | additive PLUS multiplicative
    | additive MINUS multiplicative"""
    if len(p) == 2: p[0] = p[1]
    else: p[0] = attach_loc(AdditiveOperator(p[2], p[1], p[3]), p, 2)

def p_multiplicative(p):
    """multiplicative : unary
    | multiplicative MULT unary
    | multiplicative DIV unary
    | multiplicative MOD unary"""
    if len(p) == 2: p[0] = p[1]
    else: p[0] = attach_loc(MultiplicativeOperator(p[2], p[1], p[3]), p, 2)

def p_unary(p):
    """unary : PLUS unary
    | MINUS unary %prec UMINUS
    | AMPERSAND unary %prec ADDRESSOF_PREC
    | MULT unary %prec DEREFERENCE_PREC
    | NOT unary
    | postfix"""
    if len(p) == 3:
        op_value = p[1]
        if p.slice[1].type == "PLUS": p[0] = p[2]
        elif p.slice[1].type == "MINUS": p[0] = attach_loc(UnaryOperator(op_value, p[2]), p, 1)
        elif p.slice[1].type == "AMPERSAND": p[0] = attach_loc(AddressOfNode(p[2]), p, 1)
        elif p.slice[1].type == "MULT": p[0] = attach_loc(DereferenceNode(p[2]), p, 1)
        elif p.slice[1].type == "NOT": p[0] = attach_loc(UnaryOperator(op_value, p[2]), p, 1)
        else: raise SyntaxError(f"Unknown unary operator: {p.slice[1].type}")
    else:
        p[0] = p[1]

def p_postfix(p):
    """postfix : primary
            | postfix postfix_suffix"""
    if len(p) == 2: 
        p[0] = p[1]
    else:
        left = p[1]
        kind, data = p[2]
        
        node = None
        if kind == "inc" or kind == "dec": 
            node = PostfixOperator(data, left)
        elif kind == "spread": # [NEW]
            node = PostfixOperator(data, left)
        elif kind == "call": 
            node = FunctionCall(left, data)
        elif kind == "method": 
            node = StructMethodCall(left, data[0], data[1])
        elif kind == "field": 
            node = MemberAccess(left, data)
        elif kind == "index": 
            node = ArrayIndexNode(left, data)
        else: 
            raise SyntaxError(f"Unknown postfix kind: {kind}")
        
        attach_loc(node, p, 1)
        if node.lineno == 0: attach_loc(node, p, 2)
        p[0] = node

    
def p_postfix_suffix(p):
    """postfix_suffix : INCREMENT
                   | DECREMENT
                   | LPAREN arguments RPAREN
                   | DOT IDENTIFIER LPAREN arguments RPAREN
                   | DOT IDENTIFIER
                   | LBRACKET expression RBRACKET
                   | ELLIPSIS"""
    tok = p.slice[1].type
    if tok == "INCREMENT": p[0] = ("inc", "++")
    elif tok == "DECREMENT": p[0] = ("dec", "--")
    elif tok == "LPAREN": p[0] = ("call", p[2])
    elif tok == "DOT":
        if len(p) == 3: p[0] = ("field", p[2])
        else: p[0] = ("method", (p[2], p[4]))
    elif tok == "LBRACKET": p[0] = ("index", p[2])
    elif tok == "ELLIPSIS": p[0] = ("spread", "...") # [NEW]

  

def p_turbofish(p):
    """turbofish : IDENTIFIER DOUBLE_COLON LT type_list GT"""
    # Returns a GenericTypeNode, which is convenient for StructInstantiation
    # For FunctionCall, we will extract the name and args from this node.
    p[0] = attach_loc(GenericTypeNode(base_name=p[1], type_args=p[4]), p, 1)

def p_primary(p):
    """primary : literal
    | IDENTIFIER
    | LPAREN expression RPAREN
    | IDENTIFIER LBRACE field_assignments RBRACE
    | turbofish LBRACE field_assignments RBRACE
    | turbofish LPAREN arguments RPAREN
    | typeof_expression
    | array_literal
    | new_heap_allocation_expression
    | sizeof_expression
    | as_ptr_expression
    | SUPER
    | SUPER LBRACE field_assignments RBRACE
    | AT IDENTIFIER LPAREN arguments RPAREN
    | SELF_TYPE LBRACE field_assignments RBRACE
    | lambda_expression"""
    
    # Case: Single token
    if len(p) == 2:
        if p.slice[1].type == 'SUPER':
            p[0] = SuperNode()
        elif p.slice[1].type == 'NEW':
            p[0] = p[1]
        else:
            p[0] = p[1]
        p[0] = attach_loc(p[0], p, 1)

    # Case: ( expr )
    elif p.slice[1].type == "LPAREN":
        p[0] = p[2]
        p[0] = attach_loc(p[0], p, 1)
    # [FIX] Turbofish Struct Instantiation: Vector::<int>{...}
    # p[1] is GenericTypeNode from turbofish rule
    elif p.slice[1].type == "turbofish" and p.slice[2].type == "LBRACE":
        p[0] = StructInstantiation(p[1], p[3])
        p[0] = attach_loc(p[0], p, 1)

    # [FIX] Turbofish Function Call: foo::<int>(...)
    elif p.slice[1].type == "turbofish" and p.slice[2].type == "LPAREN":
        # p[1] is GenericTypeNode(base_name="foo", type_args=[int])
        generic_node = p[1]
        p[0] = FunctionCall(
            call_name=generic_node.base_name, 
            params=p[3], 
            generic_args=generic_node.type_args
        )
        p[0] = attach_loc(p[0], p, 1)

     # Case: Struct Instantiation (Name { ... })
    elif p.slice[1].type == "IDENTIFIER" and p.slice[2].type == "LBRACE":
        p[0] = StructInstantiation(p[1], p[3])
        p[0] = attach_loc(p[0], p, 1)
        
    # DEAD: Case: Generic Struct Instantiation (struct Box<int> { ... })
    # elif len(p) == 6 and p.slice[1].type == 'STRUCT':
    #     p[0] = StructInstantiation(p[2], p[4])
    #     p[0] = attach_loc(p[0], p, 1)
        
    # Case: Super Struct Init (super { ... })
    elif len(p) == 5 and p.slice[1].type == 'SUPER':
        p[0] = StructInstantiation(struct_name="super", field_assignments=p[3])
        p[0] = attach_loc(p[0], p, 1)
        
    # Case: Self Struct Init (Self { ... })
    elif len(p) == 5 and p.slice[1].type == 'SELF_TYPE':
        p[0] = StructInstantiation(struct_name="Self", field_assignments=p[3])
        p[0] = attach_loc(p[0], p, 1)

    # Case: Special Call (@name(...))
    elif len(p) == 6 and p.slice[1].type == 'AT':
        p[0] = attach_loc(SpecialCallNode(name=p[2], args=p[4]), p, 1)

def p_new_heap_allocation_expression(p):
    """new_heap_allocation_expression : NEW type
                                      | NEW type LPAREN arguments RPAREN
                                      | NEW type LBRACE field_assignments RBRACE"""
    if len(p) == 3:
        p[0] = attach_loc(NewExpressionNode(alloc_type_ast=p[2]), p, 1)
    elif p.slice[3].type == 'LPAREN':
        p[0] = attach_loc(NewExpressionNode(alloc_type_ast=p[2], init_args=p[4]), p, 1)
    elif p.slice[3].type == 'LBRACE':
        p[0] = attach_loc(NewExpressionNode(alloc_type_ast=p[2], init_fields=p[4]), p, 1)

def p_field_assignments_single(p):
    """field_assignments : field_assignment
    | empty"""
    p[0] = [p[1]] if p[1] else []

def p_field_assignments_multiple(p):
    "field_assignments : field_assignments COMMA field_assignment"
    p[0] = p[1] + [p[3]]

def p_field_assignment(p):
    "field_assignment : IDENTIFIER COLON expression"
    p[0] = attach_loc(FieldAssignment(p[1], p[3]), p, 1)

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
               | CHAR_LITERAL
               | NULL"""
    if p.slice[1].type == 'NULL':
        p[0] = Literal(None)
    else:
        p[0] = Literal(p[1])

# ==============================================================================
#                                 ATTRIBUTES
# ==============================================================================

def p_attribute(p):
    """attribute : HASH LBRACKET IDENTIFIER EQUAL literal RBRACKET
                 | HASH LBRACKET IDENTIFIER RBRACKET
                 | HASH LBRACKET IDENTIFIER LPAREN arguments RPAREN RBRACKET"""
    # Case 1: #[key = value]
    if len(p) == 7:
        p[0] = attach_loc(Attribute(name=p[3], value=p[5].value), p, 1)
    
    # Case 2: #[flag]
    elif len(p) == 5:
        p[0] = attach_loc(Attribute(name=p[3], value=True), p, 1)
    
    # Case 3: #[key(args)]
    elif len(p) == 8:
        # p[5] is the list of arguments from the 'arguments' rule
        # We store the list of values as the attribute value
        # Note: 'arguments' returns a list of AST nodes (Expressions).
        # For attributes, we usually want literals, but expressions allow flexibility.
        p[0] = attach_loc(Attribute(name=p[3], value=p[5]), p, 1)

def p_attribute_list(p):
    """attribute_list : attribute_list attribute
                      | attribute"""
    if len(p) == 3: p[0] = p[1] + [p[2]]
    else: p[0] = [p[1]]

def p_attributes_opt(p):
    """attributes_opt : attribute_list
                      | empty"""
    p[0] = p[1] if p[1] else []


# ==============================================================================
#                            MACROS & Meta Programming
# ==============================================================================

def p_special_declaration(p):
    """special_declaration : AT SPECIAL IDENTIFIER LPAREN params RPAREN return_type LBRACE statements RBRACE
                           | AT SPECIAL IDENTIFIER LPAREN params RPAREN LBRACE statements RBRACE
                           | attribute_list AT SPECIAL IDENTIFIER LPAREN params RPAREN return_type LBRACE statements RBRACE
                           | attribute_list AT SPECIAL IDENTIFIER LPAREN params RPAREN LBRACE statements RBRACE"""
    
    # 1. Determine Attributes
    has_attrs = (p.slice[1].type == 'attribute_list')
    offset = 1 if has_attrs else 0
    attrs = p[1] if has_attrs else []
    
    # Indices relative to offset:
    # [1] @
    # [2] SPECIAL
    # [3] Name
    # [5] Params
    
    # 2. Determine Return Type presence
    # If return type exists, LBRACE is at index 8 (1-based from @).
    # If not, LBRACE is at index 7.
    
    # p[offset + 7] is the token after RPAREN
    token_after_paren = p.slice[offset + 7]
    
    if token_after_paren.type == 'LBRACE':
        # No return type
        ret_type = None
        body = p[offset + 8]
    else:
        # Has return type
        ret_type = p[offset + 7]
        body = p[offset + 9]
        
    p[0] = attach_loc(SpecialDeclaration(
        name=p[offset + 3], 
        params=p[offset + 5], 
        return_type=ret_type, 
        body=body, 
        attributes=attrs
    ), p, offset + 2)
    
        
def p_macro_declaration(p):
    """macro_declaration : AT MACRO IDENTIFIER LPAREN macro_param_list RPAREN LBRACE statements RBRACE"""
    # p[1]=@, p[2]=macro, p[3]=name, p[5]=params, p[8]=body
    p[0] = attach_loc(MacroDeclaration(
        name=p[3], 
        params=p[5], 
        body=p[8]
    ), p, 2)

def p_macro_return_statement(p):
    """macro_return_statement : AT_RETURN expression SEMICOLON"""
    p[0] = attach_loc(MacroReturnStatement(p[2]), p, 1)
    
def p_primary_macro_call(p):
    """primary : DOLLAR IDENTIFIER LPAREN arguments RPAREN"""
    p[0] = attach_loc(MacroCall(p[2], p[4] if p[4] is not None else []), p, 1)

def p_macro_param_list(p):
    """macro_param_list : macro_param
                        | macro_param_list COMMA macro_param
                        | empty"""
    if len(p) == 2:
        p[0] = [p[1]] if p[1] else []
    else:
        p[0] = p[1] + [p[3]]

    
def p_macro_param(p):
    """macro_param : IDENTIFIER
                   | IDENTIFIER COLON IDENTIFIER
                   | IDENTIFIER ELLIPSIS"""
    if len(p) == 2:
        p[0] = MacroParam(name=p[1], mode="expr", is_vararg=False)
    elif p.slice[2].type == 'COLON':
        p[0] = MacroParam(name=p[1], mode=p[3], is_vararg=False)
    elif p.slice[2].type == 'ELLIPSIS':
        p[0] = MacroParam(name=p[1], mode="ast", is_vararg=True) # Varargs usually imply AST substitution list

  

def p_reserved_kw_tconv(p):
    """reserved_kw_tconv : STD_CONV"""
    p[0] = p[1]

def p_primary_type_conv(p):
    """primary : reserved_kw_tconv LT type GT LPAREN expression RPAREN"""
    p[0] = attach_loc(TypeConv(p[3], p[6]), p, 1)

def p_as_ptr_expression(p):
    """as_ptr_expression : AS_PTR LPAREN expression RPAREN"""
    p[0] = attach_loc(AsPtrNode(expression_ast=p[3]), p, 1)

def p_sizeof_expression(p):
    """sizeof_expression : SIZEOF LPAREN sizeof_target RPAREN"""
    p[0] = attach_loc(SizeofNode(target_ast_node=p[3]), p, 1)

def p_sizeof_target(p):
    """sizeof_target : LT type GT
    | expression"""
    if len(p) == 4: p[0] = p[2]
    else: p[0] = p[1]

def p_delete_statement(p):
    """delete_statement : DELETE expression SEMICOLON"""
    p[0] = attach_loc(DeleteStatementNode(pointer_expr_ast=p[2]), p, 1)

def p_array_literal(p):
    """array_literal : LBRACKET arguments RBRACKET"""
    p[0] = attach_loc(ArrayLiteralNode(p[2]), p, 1)

def p_typeof_expression(p):
    """typeof_expression : TYPEOF LPAREN expression RPAREN
                         | TYPEOF LPAREN type RPAREN"""
    # p[3] can be an Expression Node OR a Type Node (or string "int")
    p[0] = attach_loc(TypeOf(p[3]), p, 1)

# ==============================================================================
#                                 UTILITIES
# ==============================================================================
def p_empty(p):
    "empty :"
    p[0] = None

def p_visibility_opt(p):
    """visibility_opt : PUB
                      | PRIV
                      | empty"""
    if len(p) == 2 and p[1] == 'pub': p[0] = "public"
    else: p[0] = "private"

def p_error(p):
    global diagnostic_engine
    if p:
        current_lexer = p.lexer
        current_parser = getattr(current_lexer, 'parser_instance', parser)
    else:
        current_lexer = lexer
        current_parser = parser

    if diagnostic_engine is None:
        diagnostic_engine = DiagnosticEngine(current_parser, current_lexer)
    
    msg = "Syntax error"
    hint = None
    fatal = False

    if p is None:
        msg = diagnostic_engine.analyze_unclosed_delimiters()
        hint = "Check for missing closing delimiters '}', ')', or ']'."
        diagnostic_engine.add_error(None, msg, hint, fatal=True)
        return

    ctx = diagnostic_engine.analyze_context()
    expected = []
    try:
        state = current_parser.state
        actions = current_parser.action[state]
        expected = [diagnostic_engine.get_friendly_name(k) for k in actions.keys() if k != '$end']
    except: pass

    # TRAPS
    if p.type == 'LBRACE' and hasattr(p, 'prev') and p.prev and p.prev.type == 'IDENTIFIER':
        if hasattr(p.prev, 'prev') and p.prev.prev and p.prev.prev.type == 'IDENTIFIER':
            suspect = p.prev.prev.value
            ghost_typo = diagnostic_engine.check_typo(suspect)
            if ghost_typo in ['struct', 'enum', 'fun', 'class']:
                msg = f"Unexpected identifier '{suspect}'"
                hint = f"Did you mean keyword '{Colors.BOLD}{ghost_typo}{Colors.RESET}'?"

    elif p.value in ['int', 'float', 'char', 'auto', 'bool', 'void'] and not diagnostic_engine.is_literal(p):
        if "'<'" in expected or "':'" in expected:
            msg = f"Unexpected type keyword '{p.value}'"
            hint = f"Variable types must be wrapped in angle brackets. Try: {Colors.GREEN}<{p.value}>{Colors.RESET}"

    elif p.type == 'LBRACE' and hasattr(p, 'prev') and p.prev and p.prev.type == 'RPAREN':
        msg = "Unexpected '{' after ')'"
        hint = "Did you mean to use a control flow keyword like 'if', 'while', or 'for' earlier?"

    elif p.type == 'IDENTIFIER' and hasattr(p, 'prev') and p.prev and p.prev.type == 'IDENTIFIER':
        ghost_typo = diagnostic_engine.check_typo(p.prev.value)
        if ghost_typo:
            msg = f"Unexpected identifier '{p.prev.value}'"
            hint = f"The previous word '{p.prev.value}' looks like a typo. Did you mean '{Colors.BOLD}{ghost_typo}{Colors.RESET}'?"
        else:
            msg = f"Unexpected identifier '{p.value}'"
            hint = f"Expected one of: {', '.join(expected[:4])}"

    elif p.type == 'STRING_LITERAL' and hasattr(p, 'prev') and p.prev and p.prev.type == 'IDENTIFIER':
        ghost_typo = diagnostic_engine.check_typo(p.prev.value)
        if ghost_typo in ['import']:
            msg = f"Unexpected string literal after '{p.prev.value}'"
            hint = f"Did you mean '{Colors.BOLD}{ghost_typo}{Colors.RESET}'?"

    elif (ctx == "struct definition" or ctx == "enum definition") and p.type == 'IDENTIFIER':
        if "','" in expected:
            msg = "Missing comma between members"
            hint = f"Insert a comma before '{p.value}'."
        elif "'}'" in expected:
             msg = f"Unexpected identifier '{p.value}'"
             hint = "Did you forget to close the block with '}'?"

    elif ctx == "struct definition" and p.type == 'EQUAL':
        msg = "Invalid assignment in struct"
        hint = "Struct fields are defined using ':', not '='. Example: `x: <int>`"

    elif p.type in ['INTEGER', 'FLOAT', 'IDENTIFIER'] and hasattr(p, 'prev') and p.prev and p.prev.type == 'DOLLAR':
         msg = f"Unexpected token '{p.value}' after macro call"
         hint = "Macro arguments must be enclosed in parentheses. Example: `$macro(...)`"

    elif p.type == 'IDENTIFIER':
        suggestion = diagnostic_engine.check_typo(p.value)
        if suggestion:
            msg = f"Unexpected identifier '{p.value}'"
            hint = f"Did you mean keyword '{Colors.BOLD}{suggestion}{Colors.RESET}'?"
        else:
            msg = f"Unexpected identifier '{p.value}'"
            if expected:
                hint = f"Expected: {', '.join(expected[:4])}"

    elif p.type in ['LET', 'CONST', 'IF', 'WHILE', 'RETURN', 'FUN'] and "';'" in expected:
        msg = f"Unexpected token '{p.value}'"
        hint = "Check the previous line. You might be missing a semicolon ';'."

    else:
        msg = f"Unexpected token '{p.value}'"
        if expected:
            clean_expected = ", ".join(expected[:5])
            hint = f"Expected one of: {clean_expected}..."

    diagnostic_engine.add_error(p, msg, hint, fatal=False)
    return diagnostic_engine.recover()

precedence = (
    ("right", "EQUAL", "PLUSEQUAL", "MINUSEQUAL", "MULTEQUAL", "DIVEQUAL"),
    ("left", "OR"),
    ("left", "AND"),
    ("nonassoc", "EQEQ", "NOTEQ", "LT", "GT", "LTEQ", "GTEQ"),
    ("left", "PLUS", "MINUS"),
    ("left", "MULT", "DIV", "MOD"),
    ("right", "NOT", "UMINUS", "ADDRESSOF_PREC", "DEREFERENCE_PREC"),
    ("left", "LPAREN", "LBRACKET", "DOT", "LBRACE"), 
    ("left", "INCREMENT", "DECREMENT"),
)

parser = yacc.yacc(optimize=True)