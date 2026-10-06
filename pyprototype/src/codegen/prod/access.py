# =============================================================================
# Fin Programming Language Compiler
#
# Made with ❤️
#
# This project is genuinely built on love, dedication, and care.
# Fin exists not only as a compiler, but as a labor of passion —
# created for a lover, inspired by curiosity, perseverance, and belief
# in building something meaningful from the ground up.
#
# “What is made with love is never made in vain.”
# “Love is the reason this code exists; logic is how it survives.”
#
# -----------------------------------------------------------------------------
# Author: M1778
# Repository: https://github.com/M1778M/Fin
# Profile: https://github.com/M1778M/
#
# Socials:
#   Telegram: https://t.me/your_username_here
#   Instagram: https://instagram.com/your_username_here
#   X (Twitter): https://x.com/your_username_here
#
# -----------------------------------------------------------------------------
# Copyright (C) 2025 M1778
#
# This file is part of the Fin Programming Language Compiler.
#
# Fin is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Fin is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Fin.  If not, see <https://www.gnu.org/licenses/>.
#
# -----------------------------------------------------------------------------
# “Code fades. Love leaves a signature.”
# =============================================================================
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