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