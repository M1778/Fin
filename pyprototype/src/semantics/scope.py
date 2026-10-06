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
from llvmlite import ir

class SymbolInfo:
    def __init__(self, llvm_value, fin_type):
        self.llvm_value = llvm_value
        self.fin_type = fin_type

class Scope:
    def __init__(
        self,
        parent=None,
        is_loop_scope=False,
        loop_cond_block=None,
        loop_end_block=None,
    ):
        self.parent = parent
        self.symbols = {} # Maps name -> SymbolInfo
        
        # [NEW] Type Aliases: Maps "Vector" -> "lib_math__Vector"
        self.type_aliases = {} 
        
        self.type_parameters = set()
        self.type_param_constraints = {} # Maps T -> ConstraintNode

        self.is_loop_scope = is_loop_scope
        self.loop_cond_block = loop_cond_block
        self.loop_end_block = loop_end_block

    # --- Type Parameters (Generics) ---
    def define_type_parameter(self, name: str, constraint=None):
        if name in self.type_parameters:
            raise Exception(f"Type parameter '{name}' already declared in this immediate scope.")
        self.type_parameters.add(name)
        if constraint:
            self.type_param_constraints[name] = constraint

    def is_type_parameter(self, name: str) -> bool:
        if name in self.type_parameters: return True
        if self.parent: return self.parent.is_type_parameter(name)
        return False

    def get_type_constraint(self, name: str):
        if name in self.type_param_constraints: return self.type_param_constraints[name]
        if self.parent: return self.parent.get_type_constraint(name)
        return None

    # --- Type Aliases (Imports) ---
    def define_type_alias(self, alias: str, real_name: str):
        """Registers a local alias for a (potentially mangled) type name."""
        self.type_aliases[alias] = real_name

    def resolve_type_alias(self, alias: str) -> str:
        """Resolves 'Vector' to 'lib_math__Vector' if imported."""
        if alias in self.type_aliases:
            return self.type_aliases[alias]
        if self.parent:
            return self.parent.resolve_type_alias(alias)
        return None

    # --- Symbols (Variables/Functions) ---
    def define(self, name, llvm_value, fin_type=None):
        if name in self.symbols:
            raise Exception(f"Symbol '{name}' already defined in this scope.")
        self.symbols[name] = SymbolInfo(llvm_value, fin_type)

    def resolve(self, name):
        info = self._resolve_info(name)
        return info.llvm_value if info else None

    def resolve_type(self, name):
        info = self._resolve_info(name)
        return info.fin_type if info else None

    def _resolve_info(self, name):
        if name in self.symbols: return self.symbols[name]
        if self.parent: return self.parent._resolve_info(name)
        return None

    def find_loop_scope(self):
        if self.is_loop_scope: return self
        if self.parent: return self.parent.find_loop_scope()
        return None