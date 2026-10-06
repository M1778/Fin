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
from ..parser.packages.errors import Colors
from ..ast2.nodes import *

class TypeChecker:
    def __init__(self, diagnostic_engine):
        self.diag = diagnostic_engine
        self.symbol_table = {} # Simple scope for types: name -> type_str
        self.current_return_type = None

    def check(self, node):
        
        self.visit(node)
        # If fatal errors occurred, the diagnostic engine handles the exit

    def visit(self, node):
        if hasattr(node, "lineno"):
            self.current_node = node # Track for error reporting
            
        method = f'visit_{type(node).__name__}'
        visitor = getattr(self, method, self.generic_visit)
        return visitor(node)

    def generic_visit(self, node):
        if hasattr(node, '__dict__'):
            for _, value in node.__dict__.items():
                if isinstance(value, list):
                    for item in value:
                        if isinstance(item, Node): self.visit(item)
                elif isinstance(value, Node):
                    self.visit(value)

    def error(self, msg, hint=None):
        # Uses YOUR existing engine
        self.diag.add_error(self.current_node, msg, hint, fatal=False)

    # --- RULES ---

    def visit_VariableDeclaration(self, node):
        # 1. Check shadowing/redefinition
        if node.identifier in self.symbol_table:
            self.error(f"Variable '{node.identifier}' is already defined.", 
                       hint="Shadowing is not allowed in the same block.")
        
        # 2. Resolve types
        declared_type = node.type
        inferred_type = self.visit(node.value) if node.value else None

        if declared_type == "auto":
            if not inferred_type:
                self.error(f"Cannot infer type for '{node.identifier}'.", hint="Auto variables must be initialized.")
                declared_type = "unknown"
            else:
                declared_type = inferred_type

        # 3. Type Mismatch
        if inferred_type and declared_type != inferred_type:
            # Allow int->float coercion if your language supports it
            if not (declared_type == "float" and inferred_type == "int"):
                self.error(
                    f"Type mismatch: expected '{declared_type}', got '{inferred_type}'",
                    hint=f"Variable '{node.identifier}' was declared as {declared_type}."
                )

        self.symbol_table[node.identifier] = declared_type

    def visit_AdditiveOperator(self, node):
        left = self.visit(node.left)
        right = self.visit(node.right)

        if left != right:
            self.error(f"Invalid operation between '{left}' and '{right}'", 
                       hint=f"Operator '{node.operator}' requires operands of the same type.")
            return "unknown"
        
        return left

    def visit_Literal(self, node):
        if isinstance(node.value, int): return "int"
        if isinstance(node.value, float): return "float"
        if isinstance(node.value, str): return "string"
        return "unknown"

    def visit_str(self, node):
        # Identifier lookup
        return self.symbol_table.get(node, "unknown")