from .essentials import *

# ---------------------------------------------------------------------------
# <Method name=compile_enum_declaration args=[<Compiler>, <EnumDeclaration>]>
# <Description>
# Compiles an 'enum' definition.
# 1. Registers the Enum type (mapped to i32).
# 2. Calculates values for all members (auto-increment or explicit).
# 3. Stores members in the global registry for compile-time lookup.
# </Description>
def compile_enum_declaration(compiler: Compiler, ast: EnumDeclaration):
    enum_name = ast.name

    if enum_name in compiler.enum_types:
        compiler.errors.error(ast, f"Enum type '{enum_name}' already declared.")
        return

    # Enums are backed by i32 by default
    # (Future improvement: Allow specifying backing type like 'enum Color : u8')
    llvm_enum_underlying_type = ir.IntType(32)
    compiler.enum_types[enum_name] = llvm_enum_underlying_type

    member_map = {}
    next_value = 0
    
    for member_name, value_expr in ast.values:
        member_val = None
        
        if value_expr is not None:
            # Explicit Value: A = 5
            # We strictly require Literals for now to ensure compile-time constants without complex folding
            if not isinstance(value_expr, Literal) or not isinstance(value_expr.value, int):
                compiler.errors.error(
                    value_expr if isinstance(value_expr, Node) else ast,
                    f"Enum member '{enum_name}::{member_name}' value must be an integer literal."
                )
                continue
                
            assigned_value = value_expr.value
            member_val = ir.Constant(llvm_enum_underlying_type, assigned_value)
            next_value = assigned_value + 1
        else:
            # Implicit Value: B (Auto-increment)
            member_val = ir.Constant(llvm_enum_underlying_type, next_value)
            next_value += 1

        member_map[member_name] = member_val

    # Register in Compiler State
    compiler.enum_members[enum_name] = member_map

    # Note: We removed 'type_codes' logic here because we now use Hash-Based RTTI.

# ---------------------------------------------------------------------------
# <Method name=compile_enum_access_ast args=[<Compiler>, <EnumAccess>]>
# <Description>
# Compiles a specific 'EnumAccess' AST node (if your parser generates it).
# Note: Often enums are accessed via QualifiedAccess (Dot), but this handles
# the specific node if it exists.
# </Description>
def compile_enum_access_ast(compiler: Compiler, ast: EnumAccess) -> ir.Value:
    enum_name = ast.enum_name
    member_name = ast.value

    if enum_name not in compiler.enum_members:
        compiler.errors.error(ast, f"Enum '{enum_name}' is not defined.")
        return ir.Constant(ir.IntType(32), 0)

    members = compiler.enum_members[enum_name]
    
    if member_name not in members:
        compiler.errors.error(ast, f"Enum '{enum_name}' has no member '{member_name}'.",
                              hint=f"Available members: {', '.join(members.keys())}")
        return ir.Constant(ir.IntType(32), 0)

    return members[member_name]