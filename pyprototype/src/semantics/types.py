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
from ..utils.hashing import fnv1a_64


class FinType:
    """Base class for all Fin types."""
    def __repr__(self): return self.__class__.__name__
    def is_generic(self): return False
    @property
    def type_id(self) -> int:
        """Returns a deterministic 64-bit hash of the type signature."""
        # We use the string representation to generate the hash.
        # Ensure __repr__ is unique for every distinct type!
        return fnv1a_64(self.get_signature())

    def get_signature(self) -> str:
        """Returns the unique string signature for hashing."""
        return str(self)

class PrimitiveType(FinType):
    """int, float, bool, void, char"""
    def __init__(self, name, bits=0):
        self.name = name
        self.bits = bits # e.g., 32 for int32
    def __repr__(self): return self.name
    def __eq__(self, other):
        return isinstance(other, PrimitiveType) and self.name == other.name

class PointerType(FinType):
    """&T"""
    def __init__(self, pointee):
        self.pointee = pointee # Another FinType
    def __repr__(self): return f"&{self.pointee}"

class StructType(FinType):
    def __init__(self, name, generic_args=None):
        self.name = name # This should ideally be the MANGLED name for uniqueness across modules
        self.generic_args = generic_args or []
    
    def __repr__(self):
        if self.generic_args:
            args = ", ".join(str(a) for a in self.generic_args)
            return f"{self.name}<{args}>"
        return self.name
    
    # Override signature to ensure we hash the full structure
    def get_signature(self):
        return self.__repr__()

class GenericParamType(FinType):
    """The 'T' in Vector<T>"""
    def __init__(self, name):
        self.name = name
    def __repr__(self): return f"@{self.name}"
    def is_generic(self): return True

# M1778 Special Type (ANY!)
class AnyType(FinType):
    def __repr__(self): return "any"
    
    @property
    def type_id(self) -> int:
        # Fixed ID for 'any' itself, though usually we check the ID *inside* it.
        return 0x1111111111111111

# Standard Instances
IntType = PrimitiveType("int", 32)
FloatType = PrimitiveType("float", 32)
BoolType = PrimitiveType("bool", 1)
StringType = PrimitiveType("string") # Actually i8*
VoidType = PrimitiveType("void")