from llvmlite import ir
from ..ast2.nodes import *

def convert_type(type_name_or_node):
    if isinstance(type_name_or_node, ir.Type):
        return type_name_or_node

    type_name_str_for_param_check = None
    if isinstance(type_name_or_node, str):
        type_name_str_for_param_check = type_name_or_node
    elif isinstance(type_name_or_node, TypeParameterNode):
        type_name_str_for_param_check = type_name_or_node.name

    if type_name_str_for_param_check:

        if hasattr(self.current_scope, "get_bound_type"):
            bound_llvm_type = self.current_scope.get_bound_type(
                type_name_str_for_param_check
            )
            if bound_llvm_type:

                return bound_llvm_type

        if hasattr(
            self.current_scope, "is_type_parameter"
        ) and self.current_scope.is_type_parameter(type_name_str_for_param_check):
            raise Exception(
                f"Internal Error: convert_type called for unbound type parameter '{type_name_str_for_param_check}'. "
                "Generic templates should not be fully type-converted until instantiation."
            )

    if isinstance(type_name_or_node, ArrayTypeNode):
        ast_node = type_name_or_node
        llvm_element_type = self.convert_type(ast_node.element_type)

        if not isinstance(llvm_element_type, ir.Type):
            raise Exception(
                f"Element type '{ast_node.element_type}' of array did not resolve to LLVM type. Got: {llvm_element_type}"
            )

        if ast_node.size_expr:
            if not isinstance(ast_node.size_expr, Literal) or not isinstance(
                ast_node.size_expr.value, int
            ):
                raise Exception(
                    f"Array size in type annotation must be a constant integer literal, got {ast_node.size_expr}"
                )
            size = ast_node.size_expr.value
            if size < 0:
                raise Exception(f"Array size must be non-negative, got {size}")
            return ir.ArrayType(llvm_element_type, size)
        else:

            raise Exception(
                f"Array type annotation '{ast_node}' within a larger type (or used directly without an initializer context) "
                "must have an explicit size. Unsized arrays like <[int]> are only allowed as the outermost type "
                "of a variable being initialized, e.g., 'let x <[int]> = [1,2,3];'."
            )

    if isinstance(type_name_or_node, PointerTypeNode):
        ast_node = type_name_or_node
        llvm_pointee_type = self.convert_type(ast_node.pointee_type)
        return llvm_pointee_type.as_pointer()

    type_name_str = None
    if isinstance(type_name_or_node, str):
        type_name_str = type_name_or_node
    elif isinstance(type_name_or_node, TypeAnnotation):
        base, bits = type_name_or_node.base, type_name_or_node.bits
        if base == "int":
            if bits in (8, 16, 32, 64, 128):
                return ir.IntType(bits)
            else:
                raise Exception(f"Unsupported integer width: {bits}")
        if base == "float":
            if bits == 32:
                return ir.FloatType()
            if bits == 64:
                return ir.DoubleType()
            else:
                raise Exception(f"Unsupported float width: {bits}")

        raise Exception(f"Unknown parameterized type: {base}({bits})")

    elif isinstance(type_name_or_node, ModuleAccess):
        alias, name = type_name_or_node.alias, type_name_or_node.name
        if alias not in self.module_aliases:
            raise Exception(f"Module '{alias}' not imported.")
        path = self.module_aliases[alias]
        m_enums = self.module_enum_types.get(path, {})
        if name in m_enums:
            return m_enums[name]
        m_structs = self.module_struct_types.get(path, {})
        if name in m_structs:
            return m_structs[name].as_pointer()
        raise Exception(f"Module '{alias}' has no type '{name}'.")
    elif isinstance(type_name_or_node, TypeAnnotation):
        base, bits = type_name_or_node.base, type_name_or_node.bits
        if base == "int":
            if bits in (8, 16, 32, 64):
                return ir.IntType(bits)
            else:
                raise Exception(f"Unsupported integer width: {bits}")
        if base == "float":
            if bits == 32:
                return ir.FloatType()
            if bits == 64:
                return ir.DoubleType()
            else:
                raise Exception(f"Unsupported float width: {bits}")
        raise Exception(f"Unknown base type '{base}' in TypeAnnotation")
    if type_name_str is None:
        raise Exception(
            f"Unknown type representation passed to convert_type: {type_name_or_node} (type: {type(type_name_or_node)})"
        )

    if type_name_str == "int":
        return ir.IntType(32)
    if type_name_str == "float":
        return ir.FloatType()
    if type_name_str == "double":
        return ir.DoubleType()
    if type_name_str == "bool":
        return ir.IntType(1)
    if type_name_str == "string":
        return ir.IntType(8).as_pointer()
    if type_name_str == "noret":
        return ir.VoidType()
    if type_name_str == "auto":
        raise Exception("'auto' type must be inferred, not passed to convert_type.")
    if type_name_str in self.identified_types:
        return self.identified_types[type_name_str]
    if type_name_str in self.enum_types:
        return self.enum_types[type_name_str]
    if type_name_str in self.struct_types:
        return self.struct_types[type_name_str]

    raise Exception(f"Unknown concrete type name: '{type_name_str}'")
