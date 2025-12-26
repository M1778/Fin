from .essentials import *
# ---------------------------------------------------------------------------
# <Method name=compile_qualified_access args=[<Compiler>, <QualifiedAccess>]>
# <Description>
# Compiles 'Qualifier.Name'.
# Handles:
# 1. Enum Access (MyEnum.Variant)
# 2. Module Access (my_mod.Func)
# 3. Struct Static Access (MyStruct.StaticFunc)
# </Description>
def compile_qualified_access(compiler: Compiler, ast: QualifiedAccess) -> ir.Value:
    lhs = ast.left
    name = ast.name

    # Ensure LHS is a string (Identifier)
    if not isinstance(lhs, str):
        compiler.errors.error(ast, f"Invalid qualifier type: {type(lhs).__name__}")
        return ir.Constant(ir.IntType(32), 0)

    # Case 1: Enum Access
    if lhs in compiler.enum_types:
        # Delegate to enums.py
        return compiler.compile_enum_access(compiler, ast)

    # Case 2: Module Access
    if lhs in compiler.module_aliases:
        # Delegate to modules.py
        # We create a synthetic ModuleAccess node to reuse the robust logic there
        mod_node = ModuleAccess(alias=lhs, name=name)
        # Copy location info for error reporting
        mod_node.lineno = getattr(ast, 'lineno', 0)
        mod_node.col_offset = getattr(ast, 'col_offset', 0)
        
        return compiler.compile_module_access(compiler, mod_node)

    # Case 3: Struct Static Access (e.g. MyStruct.New)
    # Check if LHS is a known Struct
    mangled_struct = compiler.get_mangled_name(lhs)
    if mangled_struct in compiler.struct_types or lhs in compiler.struct_types:
        # Use the correct name key
        struct_key = mangled_struct if mangled_struct in compiler.struct_types else lhs
        
        # Construct Static Method Name: Struct_static_Method
        static_func_name = f"{struct_key}_static_{name}"
        
        try:
            return compiler.module.get_global(static_func_name)
        except KeyError:
            compiler.errors.error(ast, f"Struct '{lhs}' has no static member '{name}'.")
            return ir.Constant(ir.IntType(8).as_pointer(), None)

    # Failure
    compiler.errors.error(ast, f"Unknown qualifier '{lhs}'. Expected Enum, Module, or Struct.")
    return ir.Constant(ir.IntType(32), 0)