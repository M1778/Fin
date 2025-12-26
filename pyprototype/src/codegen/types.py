from platform import node
from .essentials import *
from copy import deepcopy

# <Method name=convert_type args=[<Compiler>, <Union[str, Node]>]>
# <Description>
# Main entry point for converting AST Type Nodes into LLVM Types.
# Handles:
# 1. Primitives (int, float, void, etc.)
# 2. Type Erasure (Generic Params T -> i8*)
# 3. Imports (Resolving Type Aliases like 'Vector' -> 'lib__Vector')
# 4. Monomorphization (Instantiating Box<int> -> Box_int)
# 5. Arrays (Static [T, N] and Dynamic [T])
# 6. Pointers (&T)
# </Description>
def convert_type(compiler: Compiler, type_node: Union[str, Node]) -> ir.Type:
    # Case -1:
    # This is required for Smart Casting, which passes resolved FinTypes (e.g. IntType)
    # instead of AST nodes.
    if isinstance(type_node, FinType):
        return fin_type_to_llvm(compiler, type_node)
    # Case 0:
    if isinstance(type_node, str):
        # 1. Check Generic Parameters
        if compiler.current_scope.is_type_parameter(type_node):
            # Differentiate behavior based on Constraint!
            constraint = compiler.current_scope.get_type_constraint(type_node)
            constraint_str = str(constraint) if constraint else ""

            # Case A: Castable2
            if constraint_str == "Castable2":
                # Fat Pointer: { i8* data, i64 type_id }
                return ir.LiteralStructType([
                    ir.IntType(8).as_pointer(),
                    ir.IntType(64)
                ])
            
            # Case B: 'Castable' / Default -> Raw Pointer i8*
            return ir.IntType(8).as_pointer()
    # =========================================================================
    # CASE 1: String Identifiers (e.g. "int", "T", "Vector")
    # =========================================================================
    if isinstance(type_node, str):
        # 1. Check Generic Parameters (Type Erasure)
        # If 'T' is defined in the current scope, it compiles to void* (i8*)
        if compiler.current_scope.is_type_parameter(type_node):
            return ir.IntType(8).as_pointer()

        # 2. Primitives
        if type_node == "int": return ir.IntType(32)
        if type_node == "long": return ir.IntType(64)
        if type_node == "float": return ir.FloatType()
        if type_node == "double": return ir.DoubleType()
        if type_node == "bool": return ir.IntType(1)
        if type_node == "string": return ir.IntType(8).as_pointer()
        if type_node == "char": return ir.IntType(8)
        if type_node == "noret" or type_node == "void": return ir.VoidType()

        if type_node == "any":
            # We look for the struct named "Any". 
            # It might be mangled (stdlib_builtins__Any) or aliased.
            # We try to resolve it via the scope or global registry.
            
            # Try resolving alias first (if imported as { Any })
            alias = compiler.current_scope.resolve_type_alias("Any")
            if alias and alias in compiler.struct_types:
                return compiler.struct_types[alias]
            
            # Try finding it in global structs (hacky search for bootstrapping)
            for name, ty in compiler.struct_types.items():
                if name.endswith("__Any") or name == "Any":
                    return ty
            
            raise Exception("Fatal: 'struct Any' not found. Ensure stdlib/builtins.fin is loaded.")

        # Enum
        if type_node in compiler.enum_types:
            return compiler.enum_types[type_node]
        
        # 3. 'Self' Type
        if type_node == "Self":
            if compiler.current_struct_type:
                return compiler.current_struct_type
            raise compiler.errors.error(type_node,"'Self' type used outside of a struct definition context.", hint="Ensure 'Self' is only used within struct methods.")
        
        # Check Compiler Intrinsics
        if type_node.startswith("compiler."):
            intrinsic_type = compiler.intrinsics_lib.resolve_type(type_node)
            if intrinsic_type:
                return intrinsic_type
            raise compiler.errors.error(type_node, f"Unknown compiler type: {type_node}")

        # 4. Check Type Aliases (Imports)
        # This handles `import { Vector }`. The scope maps "Vector" -> "lib_math__Vector".
        alias_target = compiler.current_scope.resolve_type_alias(type_node)
        if alias_target:
            if alias_target in compiler.struct_types:
                return compiler.struct_types[alias_target]
            # If alias exists but type not found, it might be a template waiting for instantiation
            # or an error. For standard types, it should be in struct_types.

        # 5. Local Mangled Name
        mangled = compiler.get_mangled_name(type_node)
        if mangled in compiler.struct_types:
            return compiler.struct_types[mangled]
        
        # 6. Fallback: Unmangled/Global Name
        if type_node in compiler.struct_types:
            return compiler.struct_types[type_node]
        
        # 7. Check for Un-instantiated Templates (Error)
        if type_node in compiler.struct_templates:
            raise compiler.errors.error(type_node, f"Generic struct '{type_node}' requires type arguments (e.g. {type_node}<int>).")
        # Handle 'any' Built-in Type (String)
        if type_node == "any":
            return ir.LiteralStructType([
                ir.IntType(8).as_pointer(),
                ir.IntType(64)
            ])
        raise compiler.errors.error(type_node, f"Unknown type: {type_node}")
    # CASE 1.5: Function Types (e.g. function < [int, int], int >)
    elif isinstance(type_node, FunctionTypeNode):
        ret_ty = convert_type(compiler, type_node.return_type)
        arg_tys = [convert_type(compiler, t) for t in type_node.arg_types]
        
        # Create Function Pointer Type: ret (args)*
        func_ty = ir.FunctionType(ret_ty, arg_tys)
        return func_ty.as_pointer()
    # =========================================================================
    # CASE 2: Generic Types (e.g. Box<int>)
    # =========================================================================
    elif isinstance(type_node, GenericTypeNode):
        base_name = type_node.base_name
        
        # Resolve Alias for Base Name (e.g. Vector -> lib__Vector)
        alias_target = compiler.current_scope.resolve_type_alias(base_name)
        
        # Determine the "Real" name to look up in modes/templates
        # If alias exists, use that. Otherwise use base_name.
        # Note: Templates are usually stored by their Short Name in the defining module.
        # If imported, we might need logic to find the template AST from the other module.
        lookup_name = base_name
        
        # Check Mode (MONO vs ERASED)
        # We check 'modes' using the short name (local) or we might need a global lookup.
        mode = compiler.modes.get(base_name)
        
        # If mode not found locally, check if it's an imported struct
        if not mode and alias_target:
            # It's imported. We assume it's compiled (ERASED/STANDARD) 
            # unless we implement cross-module templates.
            # For now, assume imported generics are ERASED or already compiled.
            if alias_target in compiler.struct_types:
                return compiler.struct_types[alias_target]

        if not mode:
             # Try mangled lookup
             mangled = compiler.get_mangled_name(base_name)
             if mangled in compiler.struct_types: return compiler.struct_types[mangled]
             raise compiler.errors.error(type_node, f"Struct '{base_name}' not defined.")

        # --- PATH A: Monomorphization ---
        if mode == 'MONO':
            # 1. Generate Unique Name (Box_int)
            inst_name = compiler.get_mono_mangled_name(base_name, type_node.type_args)
            
            # 2. Check Cache
            if inst_name in compiler.mono_struct_cache:
                return compiler.mono_struct_cache[inst_name]
            
            # print(f"[DEBUG MONO] Instantiating '{inst_name}'")
            
            # 3. Instantiate
            if base_name not in compiler.struct_templates:
                raise compiler.errors.error(type_node, f"Template AST for '{base_name}' not found.")
                
            template_ast = compiler.struct_templates[base_name]
            
            # Create bindings: {'T': 'int'}
            bindings = {}
            for i, param in enumerate(template_ast.generic_params):
                if i < len(type_node.type_args):
                    # param is GenericParam object, we need its name
                    p_name = param.name 
                    bindings[p_name] = type_node.type_args[i]
            
            # Deep Copy & Substitute
            concrete_ast = deepcopy(template_ast)
            concrete_ast.name = inst_name
            concrete_ast.generic_params = [] # Concrete now
            
            compiler._substitute_ast_types(concrete_ast, bindings)
            
            # Compile
            compiler.compile_struct(concrete_ast)
            
            # Cache
            # compile_struct registers using get_mangled_name(inst_name)
            mangled_inst = compiler.get_mangled_name(inst_name)
            struct_ty = compiler.struct_types[mangled_inst]
            compiler.mono_struct_cache[inst_name] = struct_ty
            return struct_ty

        # --- PATH B: Type Erasure ---
        elif mode == 'ERASED':
            # Return the base struct (Box). The generic args are ignored in LLVM type.
            mangled = compiler.get_mangled_name(base_name)
            return compiler.struct_types[mangled]

    # =========================================================================
    # CASE 3: Arrays (Static ONLY)
    # =========================================================================
    elif isinstance(type_node, ArrayTypeNode):
        llvm_element_type = convert_type(compiler, type_node.element_type)

        if type_node.size_expr:
            # [T, N] -> [N x T]
            if not isinstance(type_node.size_expr, Literal) or not isinstance(type_node.size_expr.value, int):
                raise compiler.errors.error(type_node, f"Array size must be a constant integer literal.")
            size = type_node.size_expr.value
            return ir.ArrayType(llvm_element_type, size)
        else:
            # [T] -> Error (Must be explicit or inferred at variable decl)
            # We return None or raise Error? 
            # Raising error is safer. VariableDeclaration handles inference BEFORE calling convert_type.
            raise compiler.errors.error(type_node, "Static array requires explicit size (e.g. <[T, 10]>).")

    # =========================================================================
    # CASE 4: Pointers (&T)
    # =========================================================================
    elif isinstance(type_node, PointerTypeNode):
        pointee = convert_type(compiler, type_node.pointee_type)
        return pointee.as_pointer()

    # =========================================================================
    # CASE 5: Module Access (std.Vector)
    # =========================================================================
    elif isinstance(type_node, ModuleAccess):
        # Resolve alias
        alias = type_node.alias
        name = type_node.name
        
        if alias not in compiler.module_aliases:
            raise compiler.errors.error(type_node, f"Module '{alias}' not imported.")

        path = compiler.module_aliases[alias]
        
        # Check Structs
        if path in compiler.module_struct_types and name in compiler.module_struct_types[path]:
            return compiler.module_struct_types[path][name]
            
        # Check Enums
        if path in compiler.module_enum_types and name in compiler.module_enum_types[path]:
            return compiler.module_enum_types[path][name]
            
        raise compiler.errors.error(type_node, f"Module '{alias}' has no type '{name}'.")

    # =========================================================================
    # CASE 6: Type Annotations (int(64))
    # =========================================================================
    elif isinstance(type_node, TypeAnnotation):
        base, bits = type_node.base, type_node.bits
        if base == "int":
            return ir.IntType(bits)
        if base == "float":
            if bits == 32: return ir.FloatType()
            if bits == 64: return ir.DoubleType()
        raise compiler.errors.error(type_node, f"Unsupported type annotation: {base}({bits})")
    # =========================================================================
    # CASE 7: Function Types (fn(int) <int>)
    # =========================================================================
    elif isinstance(type_node, FunctionTypeNode):
        # Convert Return Type
        ret_ty = convert_type(compiler, type_node.return_type)
        
        # Convert Argument Types
        arg_tys = [convert_type(compiler, t) for t in type_node.arg_types]
        
        # Create Function Type
        func_ty = ir.FunctionType(ret_ty, arg_tys)
        
        # In LLVM, functions are globals, so variables hold pointers to them.
        return func_ty.as_pointer()
    # Fallback
    return legacy_convert_type(compiler, type_node)
# <Method name=_substitute_type args=[<Compiler>, <Union[str, Node]>, <Dict>]>
# <Description>
# Helper to replace 'T' with 'int' (or other types) in AST type nodes.
# Returns a NEW node/string with the substitution applied (does not mutate).
# </Description>
def _substitute_type(compiler: Compiler, type_node: Union[str, Node], bindings: Dict[str, Any]) -> Union[str, Node]:
    if isinstance(type_node, str):
        if type_node in bindings:
            return bindings[type_node] 
        return type_node
    
    elif isinstance(type_node, GenericTypeNode):
        new_args = [_substitute_type(compiler, arg, bindings) for arg in type_node.type_args]
        return GenericTypeNode(type_node.base_name, new_args)
    
    # Add other type nodes if needed (Pointer, Array)
    elif isinstance(type_node, PointerTypeNode):
        new_pointee = _substitute_type(compiler, type_node.pointee_type, bindings)
        return PointerTypeNode(new_pointee)
        
    return type_node

# <Method name=_substitute_ast_types args=[<Compiler>, <Node>, <Dict>]>
# <Description>
# Recursively replaces Generic Types (T) with Concrete Types (int) in an AST tree.
# MUTATES the node in-place. Used for Monomorphization (Template Instantiation).
# Handles strings, lists, and nested nodes.
# </Description>
def _substitute_ast_types(compiler: Compiler, node: Node, bindings: Dict[str, Any]):
    if hasattr(node, '__dict__'):
        for key, value in node.__dict__.items():
            # 1. Direct String Replacement (e.g. var_type = "T")
            if isinstance(value, str) and value in bindings:
                setattr(node, key, bindings[value])
            
            # 2. List Replacement (e.g. type_args = ["T", "U"])
            elif isinstance(value, list):
                new_list = []
                for item in value:
                    # If item is a string to be replaced
                    if isinstance(item, str) and item in bindings:
                        new_list.append(bindings[item])
                    else:
                        # Recurse if it's a node (mutates in place)
                        if isinstance(item, Node):
                            _substitute_ast_types(compiler, item, bindings)
                        new_list.append(item)
                # Update the list in the object
                setattr(node, key, new_list)

            # 3. Recurse into Child Nodes
            elif isinstance(value, Node):
                _substitute_ast_types(compiler, value, bindings)
# ---------------------------------------------------------------------------
# <Method name=ast_to_fin_type args=[<Compiler>, <Union[str, Node]>]>
# <Description>
# Converts an AST Type Node (or string identifier) into a High-Level 'FinType' object.
# Used for:
# 1. Semantic Analysis (Type Checking).
# 2. Type Erasure (Unboxing logic).
# 3. Reflection (typeof).
#
# Handles:
# - Primitives & Void
# - Generics (T)
# - Structs (including Imported/Aliased ones)
# - Arrays (Mapped to Collection struct)
# - Pointers
# - Module Access (std.Vector)
# </Description>
def ast_to_fin_type(compiler: Compiler, node: Union[str, Node]) -> FinType:
    # =========================================================================
    # CASE 1: String Identifiers (e.g. "int", "T", "Vector")
    # =========================================================================
    if isinstance(node, str):
        # 1. Check Generic Parameters
        # If 'T' is defined in the current scope, return GenericParamType
        if compiler.current_scope.is_type_parameter(node):
            return GenericParamType(node)
        
        # 2. Primitives
        if node == "int": return ir.IntType(32)
        if node == "long": return ir.IntType(64)
        if node == "float": return ir.FloatType()
        if node == "double": return ir.DoubleType()
        if node == "bool": return ir.IntType(1)
        if node == "string": return ir.IntType(8).as_pointer()
        if node == "char": return ir.IntType(8)
        if node == "noret" or node == "void": return ir.VoidType()
        # Enum
        if node in compiler.enum_types:
            # Enums are backed by Int, but we can represent them as IntType for now
            # or create a specific EnumType if we want stricter checking later.
            return IntType
        # 3. 'Self' Type
        if node == "Self":
            if compiler.current_struct_name:
                # Return StructType with the MANGLED name to ensure uniqueness/equality checks work
                return StructType(compiler.current_struct_name)
            
            # If we are in a trait/interface context, Self might be valid too, 
            # but usually handled by current_struct_name logic if set.
            raise compiler.errors.error(node, "'Self' type used outside of a struct definition context.")

        # 4. Check Type Aliases (Imports)
        # If "Vector" is an alias for "lib_math__Vector", we must return the real name
        # so that type equality checks pass.
        alias_target = compiler.current_scope.resolve_type_alias(node)
        if alias_target:
            return StructType(alias_target)

        # 5. Default: Struct Type (Local or Global)
        # We assume it's a struct. We use the raw name here.
        # If it's a local struct, convert_type will mangle it later.
        # For FinType equality, we ideally want the mangled name if possible.
        mangled = compiler.get_mangled_name(node)
        if mangled in compiler.struct_types:
            return StructType(mangled)
            
        return StructType(node)

    # =========================================================================
    # CASE 2: Generic Types (e.g. Box<int>)
    # =========================================================================
    elif isinstance(node, GenericTypeNode):
        base_name = node.base_name
        
        # Resolve Alias for Base Name
        alias_target = compiler.current_scope.resolve_type_alias(base_name)
        real_base_name = alias_target if alias_target else base_name
        
        # If local, try mangling to get canonical name
        if not alias_target:
            mangled = compiler.get_mangled_name(base_name)
            if mangled in compiler.struct_types:
                real_base_name = mangled

        # Recursively convert arguments
        args = [ast_to_fin_type(compiler, a) for a in node.type_args]
        return StructType(real_base_name, args)

    # =========================================================================
    # CASE 3: Pointers (&T)
    # =========================================================================
    elif isinstance(node, PointerTypeNode):
        return PointerType(ast_to_fin_type(compiler, node.pointee_type))

    # =========================================================================
    # CASE 4: Arrays / Collections ([T])
    # =========================================================================
    elif isinstance(node, ArrayTypeNode):
        elem_type = ast_to_fin_type(compiler, node.element_type)
        
        if node.size_expr:
            # Static Array [T, N]
            # FinType doesn't strictly distinguish Static vs Dynamic in the class hierarchy yet,
            # but we can represent it as a specific Struct or a new ArrayFinType.
            # For now, let's map it to a "StaticArray" struct concept or just Collection for simplicity
            # if we don't need strict static analysis yet.
            # Ideally: return StaticArrayType(elem_type, size)
            # Fallback: Treat as Collection for high-level logic, or specific struct.
            return StructType("StaticArray", [elem_type]) 
        else:
            # Dynamic Collection [T] -> Collection<T>
            return StructType("Collection", [elem_type])

    # =========================================================================
    # CASE 5: Module Access (std.Vector)
    # =========================================================================
    elif isinstance(node, ModuleAccess):
        alias = node.alias
        name = node.name
        
        if alias in compiler.module_aliases:
            path = compiler.module_aliases[alias]
            # We need to find the mangled name of 'name' inside 'path'.
            # We can look into module_struct_types keys.
            # This is expensive (search). 
            # Alternative: Construct the mangled name if we know the mangling scheme.
            # Scheme: path_name
            
            # Try to find it in the module registry
            if path in compiler.module_struct_types:
                # Keys are mangled. We need to find one that ends with __{name}
                for mangled_key in compiler.module_struct_types[path]:
                    if mangled_key.endswith(f"__{name}"): # Simple heuristic
                        return StructType(mangled_key)
            
            # Fallback: Return raw "Alias.Name" (might fail strict equality checks)
            return StructType(f"{alias}.{name}")
            
        return StructType(f"{alias}.{name}")

    # =========================================================================
    # CASE 6: Type Annotations (int(64))
    # =========================================================================
    elif isinstance(node, TypeAnnotation):
        return PrimitiveType(node.base, node.bits)

    # =========================================================================
    # FALLBACK
    # =========================================================================
    # If we can't determine the type, return Unknown.
    # This prevents crashes but might lead to "Type mismatch" later.
    # print(f"[WARNING] ast_to_fin_type returned unknown for: {node} (Type: {type(node)})")
    return PrimitiveType("unknown")
# ---------------------------------------------------------------------------
# <Method name=legacy_convert_type args=[<Compiler>, <Union[str, Node]>]>
# <Description>
# Legacy convert_type function retained for backward compatibility.
# Converts AST Type Nodes into LLVM Types.
# Handles:
# 1. Primitives (int, float, void, etc.)
# 2. Type Erasure (Generic Params T -> i8*)
# 3. Imports (Resolving Type Aliases like 'Vector' -> 'lib__Vector')
# 4. Monomorphization (Instantiating Box<int> -> Box_int)
# 5. Arrays (Static [T, N] and Dynamic [T])
# 6. Pointers (&T)
# </Description>
def legacy_convert_type(compiler:Compiler, type_name_or_node: Union[str, Node]) -> ir.Type:
    if isinstance(type_name_or_node, ir.Type):
        return type_name_or_node

    type_name_str_for_param_check = None
    if isinstance(type_name_or_node, str):
        type_name_str_for_param_check = type_name_or_node
    elif isinstance(type_name_or_node, TypeParameterNode):
        type_name_str_for_param_check = type_name_or_node.name
    
    if type_name_str_for_param_check:

        if hasattr(compiler.current_scope, "get_bound_type"):
            bound_llvm_type = compiler.current_scope.get_bound_type(
                type_name_str_for_param_check
            )
            if bound_llvm_type:

                return bound_llvm_type

        if hasattr(
            compiler.current_scope, "is_type_parameter"
        ) and compiler.current_scope.is_type_parameter(type_name_str_for_param_check):
            if isinstance(type_name_or_node, str):
                raise CompilerException(
                    "Internal Error: convert_type called with non-string type_name_or_node "
                    "for a type parameter. This should not happen."
                )
            else:
                compiler.errors.error(
                    type_name_or_node,
                    f"Type parameter '{type_name_str_for_param_check}' used in a context "
                    "where it has not been bound to a concrete type.",
                    hint="Ensure that generic templates are instantiated with concrete types before use."
                )

    if type_name_str_for_param_check == "Self":
        if compiler.current_struct_type:
            return compiler.current_struct_type
        else:
            if isinstance(type_name_or_node, str):
                raise CompilerException("'Self' type used outside of a struct definition context.")
            else:
                compiler.errors.error(
                    type_name_or_node,
                    "'Self' type used outside of a struct definition context.",
                    hint="Ensure 'Self' is only used within struct methods."
                )
    
    # --- NEW: Handle Generic Types (Vector<int>) ---
    if isinstance(type_name_or_node, GenericTypeNode):
        base_name = type_name_or_node.base_name
        type_args = [compiler.convert_type(t) for t in type_name_or_node.type_args]
        
        # Generate a unique name for this instantiation: Vector_i32
        arg_names = "_".join([str(t).replace('"', '').replace(' ', '_').replace('*', 'p') for t in type_args])
        inst_name = f"{base_name}_{arg_names}"
        
        # Check if already instantiated
        if inst_name in compiler.instantiated_structs:
            return compiler.instantiated_structs[inst_name]
        
        # Instantiate it!
        if base_name not in compiler.struct_templates:
            if isinstance(type_name_or_node, str):
                raise CompilerException(f"Generic struct '{base_name}' not defined.")
            else:
                compiler.errors.error(
                    type_name_or_node,
                    f"Generic struct '{base_name}' not defined."
                )
        
        template_ast = compiler.struct_templates[base_name]
        
        if len(type_args) != len(template_ast.generic_params):
            if isinstance(type_name_or_node, str):
                raise CompilerException(f"Struct '{base_name}' expects {len(template_ast.generic_params)} type args, got {len(type_args)}.")
            else:
                compiler.errors.error(
                    type_name_or_node,
                    f"Struct '{base_name}' expects {len(template_ast.generic_params)} type args, got {len(type_args)}."
                )
            
        # Create a mapping: T -> i32
        bindings = dict(zip(template_ast.generic_params, type_args))
        
        # Create a concrete AST by substituting T with i32
        # We need a deep copy to avoid modifying the template
        concrete_ast = deepcopy(template_ast)
        concrete_ast.name = inst_name
        concrete_ast.generic_params = [] # It's concrete now
        
        # Substitute types in members
        for member in concrete_ast.members:
            member.var_type = compiler._substitute_type(member.var_type, bindings)
            
        # Compile the concrete struct
        compiler.compile_struct(concrete_ast)
        
        # Register
        struct_ty = compiler.struct_types[inst_name]
        compiler.instantiated_structs[inst_name] = struct_ty
        return struct_ty
    
    elif isinstance(type_name_or_node, ArrayTypeNode):
        ast_node = type_name_or_node
        llvm_element_type = compiler.convert_type(ast_node.element_type)

        if not isinstance(llvm_element_type, ir.Type):
            if isinstance(type_name_or_node, str):
                raise CompilerException(f"Element type '{ast_node.element_type}' did not resolve to LLVM type.")
            else:
                compiler.errors.error(
                    type_name_or_node,
                    f"Element type '{ast_node.element_type}' did not resolve to LLVM type."
                )
            
        if ast_node.size_expr:
            # Case 1: Fixed Size Array [int, 10] -> [10 x i32]
            if not isinstance(ast_node.size_expr, Literal) or not isinstance(ast_node.size_expr.value, int):
                if isinstance(type_name_or_node, str):
                    raise CompilerException(f"Array size must be a constant integer literal.")
                else:
                    compiler.errors.error(
                        type_name_or_node,
                        f"Array size must be a constant integer literal."
                    )
            size = ast_node.size_expr.value
            return ir.ArrayType(llvm_element_type, size)
        else:
            # Case 2: Slice / Dynamic View [int] -> { T*, i32 }
            # We create an anonymous struct type for the slice
            # Structure: { pointer, length }
            
            # We use i8* for generic slices (Type Erasure compatibility)
            if llvm_element_type == ir.IntType(8).as_pointer():
                    # If element is generic (i8*), slice is { i8**, i32 }
                    # Wait, if T is i8*, then T* is i8**.
                    pass

            slice_struct = ir.LiteralStructType([
                llvm_element_type.as_pointer(), # Data Pointer
                ir.IntType(32)                  # Length
            ])
            return slice_struct
    if isinstance(type_name_or_node, PointerTypeNode):
        ast_node = type_name_or_node
        llvm_pointee_type = compiler.convert_type(ast_node.pointee_type)
        return llvm_pointee_type.as_pointer()
    if isinstance(type_name_or_node, str) and "." in type_name_or_node:
        parts = type_name_or_node.split(".")
        if len(parts) == 2:
            mod_name, type_name = parts
            
            if mod_name in compiler.module_aliases:
                path = compiler.module_aliases[mod_name]
                
                # Check Structs
                if type_name in compiler.module_struct_types.get(path, {}):
                    return compiler.module_struct_types[path][type_name]
                
                # Check Enums
                if type_name in compiler.module_enum_types.get(path, {}):
                    return compiler.module_enum_types[path][type_name]
        
        raise CompilerException(f"Unknown type: {type_name_or_node}")

    type_name_str = None
    if isinstance(type_name_or_node, str):
        type_name_str = type_name_or_node
    elif isinstance(type_name_or_node, TypeAnnotation):
        base, bits = type_name_or_node.base, type_name_or_node.bits
        if base == "int":
            if bits in (8, 16, 32, 64, 128):
                return ir.IntType(bits)
            else:
                compiler.errors.error(type_name_or_node, f"Unsupported integer width: {bits}")
        if base == "float":
            if bits == 32:
                return ir.FloatType()
            if bits == 64:
                return ir.DoubleType()
            else:
                compiler.errors.error(type_name_or_node, f"Unsupported float width: {bits}")

        compiler.errors.error(type_name_or_node, f"Unknown parameterized type: {base}({bits})")
    elif isinstance(type_name_or_node, ModuleAccess):
        alias, name = type_name_or_node.alias, type_name_or_node.name
        if alias not in compiler.module_aliases:
            compiler.errors.error(type_name_or_node, f"Module '{alias}' not imported.")
        path = compiler.module_aliases[alias]
        m_enums = compiler.module_enum_types.get(path, {})
        if name in m_enums:
            return m_enums[name]
        m_structs = compiler.module_struct_types.get(path, {})
        if name in m_structs:
            return m_structs[name].as_pointer()
        compiler.errors.error(type_name_or_node, f"Module '{alias}' has no type '{name}'.")
    
    elif isinstance(type_name_or_node, TypeAnnotation):
        base, bits = type_name_or_node.base, type_name_or_node.bits
        if base == "int":
            if bits in (8, 16, 32, 64):
                return ir.IntType(bits)
            else:
                compiler.errors.error(type_name_or_node, f"Unsupported integer width: {bits}")
        if base == "float":
            if bits == 32:
                return ir.FloatType()
            if bits == 64:
                return ir.DoubleType()
            else:
                compiler.errors.error(type_name_or_node, f"Unsupported float width: {bits}")
        compiler.errors.error(type_name_or_node, f"Unknown base type '{base}' in TypeAnnotation")
    if type_name_str is None:
        raise CompilerException(
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
        raise CompilerException("'auto' type must be inferred, not passed to convert_type.")
    if type_name_str in compiler.identified_types:
        return compiler.identified_types[type_name_str]
    if type_name_str in compiler.enum_types:
        return compiler.enum_types[type_name_str]
    if type_name_str in compiler.struct_types:
        return compiler.struct_types[type_name_str]

    raise CompilerException(f"Unknown concrete type name: '{type_name_str}'")

# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# <Method name=_infer_fin_type_from_llvm args=[<Compiler>, <ir.Type>]>
# <Description>
# Guesses the High-Level 'FinType' from a raw LLVM type.
# Used when we have an expression (like a function call result) but no AST type info.
# Handles:
# - Primitives (Int, Float, Bool, Char)
# - Pointers
# - Structs (Mangled names)
# - Interfaces & Slices (Literal Structs)
# </Description>
def infer_fin_type_from_llvm(compiler: Compiler, llvm_type: ir.Type) -> FinType:
    # 1. Integers & Booleans
    if isinstance(llvm_type, ir.IntType):
        if llvm_type.width == 1: return BoolType
        if llvm_type.width == 8: return PrimitiveType("char", 8)
        return IntType # Default to i32/int

    # 2. Floats
    if isinstance(llvm_type, ir.FloatType): return FloatType
    if isinstance(llvm_type, ir.DoubleType): return PrimitiveType("double", 64)

    # 3. Pointers
    if isinstance(llvm_type, ir.PointerType):
        # We can't easily know what it points to recursively without infinite loops
        # or guessing. For now, return Pointer(Void) which is safe for most checks.
        return PointerType(VoidType)
    
    # 4. Named Structs (Opaque)
    if isinstance(llvm_type, ir.IdentifiedStructType):
        # llvm_type.name is mangled (e.g. "lib_fin__MyStruct")
        # We return a StructType with this name.
        return StructType(llvm_type.name)

    # 5. Literal Structs (Interfaces & Slices)
    if isinstance(llvm_type, ir.LiteralStructType):
        # Check for Interface: {i8*, i8*}
        if len(llvm_type.elements) == 2 and \
           llvm_type.elements[0] == ir.IntType(8).as_pointer() and \
           llvm_type.elements[1] == ir.IntType(8).as_pointer():
            # It's an Interface, but we don't know WHICH one.
            # Return a generic Interface marker or Unknown.
            return StructType("Interface") # Placeholder
        
        # Check for Slice: {T*, i32}
        if len(llvm_type.elements) == 2 and \
           isinstance(llvm_type.elements[0], ir.PointerType) and \
           llvm_type.elements[1] == ir.IntType(32):
            
            # Infer element type from pointer
            ptr_type = llvm_type.elements[0]
            elem_llvm_type = ptr_type.pointee
            elem_fin_type = infer_fin_type_from_llvm(compiler, elem_llvm_type)
            
            return StructType("Collection", [elem_fin_type])

    return VoidType

# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# <Method name=match_generic_types args=[<Compiler>, <FinType>, <FinType>, <Dict>]>
# <Description>
# Recursively matches a Concrete Type against a Generic Pattern to infer 'T'.
# Populates the 'bindings' dictionary.
# Returns True if match successful, False otherwise.
#
# Example:
#   Concrete: Point<int>
#   Pattern:  Point<T>
#   Result:   bindings['T'] = int
# </Description>
def match_generic_types(
    compiler: Compiler, 
    concrete_type: FinType, 
    generic_type: FinType, 
    bindings: Dict[str, FinType]
) -> bool:
    
    # Case 1: The pattern is a Generic Parameter (e.g. 'T')
    if isinstance(generic_type, GenericParamType):
        name = generic_type.name
        
        if name in bindings:
            # [IMPROVEMENT] Consistency Check
            # If T was already bound to 'int', but now matches 'float', fail.
            # Note: We rely on FinType.__eq__ (defined in types.py)
            existing_type = bindings[name]
            
            # Allow exact matches
            if existing_type == concrete_type:
                return True
            
            # Allow compatible types? (e.g. int vs int32). For now, strict equality.
            # If names match, we assume they are the same.
            if str(existing_type) == str(concrete_type):
                return True
                
            return False
        else:
            # Bind it
            bindings[name] = concrete_type
            return True

    # Case 2: Pointers (&Point<int> vs &Point<T>)
    if isinstance(concrete_type, PointerType) and isinstance(generic_type, PointerType):
        return match_generic_types(compiler, concrete_type.pointee, generic_type.pointee, bindings)

    # Case 3: Structs (Point<int> vs Point<T>)
    # Note: Arrays/Collections are mapped to StructType("Collection", ...), so this handles them too.
    if isinstance(concrete_type, StructType) and isinstance(generic_type, StructType):
        c_name = concrete_type.name
        g_name = generic_type.name
        
        # [IMPROVEMENT] Robust Name Matching
        # Handle mangled vs unmangled names (e.g. "lib_math__Vector" vs "Vector")
        c_clean = c_name.split("__")[-1]
        g_clean = g_name.split("__")[-1]
        
        if c_clean != g_clean:
            return False
        
        # Check Generic Args count
        if len(concrete_type.generic_args) != len(generic_type.generic_args):
            return False
            
        # Recurse into arguments
        for c_arg, g_arg in zip(concrete_type.generic_args, generic_type.generic_args):
            if not match_generic_types(compiler, c_arg, g_arg, bindings):
                return False
                
        return True

    # Case 4: Primitives (int vs int)
    # If we reached here, neither is a GenericParam. They must match exactly.
    if isinstance(concrete_type, PrimitiveType) and isinstance(generic_type, PrimitiveType):
        return concrete_type.name == generic_type.name

    # Fallback
    return False

# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# <Method name=ast_to_fin_type_pattern args=[<Compiler>, <Union[str, Node]>, <List[Any]>]>
# <Description>
# Converts an AST Type Node into a FinType, treating specific names as Generic Parameters.
# Used during Template Matching to create a "Pattern" (e.g. Point<T>) to match against concrete types.
# </Description>
def ast_to_fin_type_pattern(compiler: Compiler, node: Union[str, Node], generic_params_list: List[Any]) -> FinType:
    # 1. Extract Generic Names
    # generic_params_list might contain strings or GenericParam objects
    gen_names = set()
    if generic_params_list:
        for p in generic_params_list:
            name = p.name if hasattr(p, 'name') else p
            gen_names.add(name)

    # 2. Handle Strings
    if isinstance(node, str):
        # If it matches a generic param, return GenericParamType
        if node in gen_names:
            return GenericParamType(node)
        
        # Standard Primitives
        if node == "int": return ir.IntType(32)
        if node == "long": return ir.IntType(64)
        if node == "float": return ir.FloatType()
        if node == "double": return ir.DoubleType()
        if node == "bool": return ir.IntType(1)
        if node == "string": return ir.IntType(8).as_pointer()
        if node == "char": return ir.IntType(8)
        if node == "noret" or node == "void": return ir.VoidType()
        
        # Structs
        return StructType(node)

    # 3. Handle Generic Types (Box<T>)
    elif isinstance(node, GenericTypeNode):
        base = node.base_name
        # Recurse into arguments with the same generic list
        args = [ast_to_fin_type_pattern(compiler, a, generic_params_list) for a in node.type_args]
        return StructType(base, args)
        
    # 4. Handle Pointers (&T)
    elif isinstance(node, PointerTypeNode):
        pointee = ast_to_fin_type_pattern(compiler, node.pointee_type, generic_params_list)
        return PointerType(pointee)

    # 5. Handle Arrays / Collections ([T])
    elif isinstance(node, ArrayTypeNode):
        elem = ast_to_fin_type_pattern(compiler, node.element_type, generic_params_list)
        # Map to Collection struct pattern
        return StructType("Collection", [elem])
    
    # 6. Handle Type Annotations
    elif isinstance(node, TypeAnnotation):
        return PrimitiveType(node.base, node.bits)

    # 7. Handle Module Access
    elif isinstance(node, ModuleAccess):
        return StructType(f"{node.alias}.{node.name}")
        
    # Fallback to standard conversion (e.g. for Literals used as types?)
    return compiler.ast_to_fin_type(node)

# ---------------------------------------------------------------------------
# <Method name=get_arg_fin_type args=[<Compiler>, <Node>, <ir.Value>]>
# <Description>
# Determines the High-Level Type (FinType) of an argument expression.
# Used during Function Call Type Inference to match arguments against templates.
# </Description>
def get_arg_fin_type(compiler: Compiler, ast_node: Node, compiled_val: Optional[ir.Value]) -> FinType:
    # 1. Variable Name (Best Source)
    if isinstance(ast_node, str):
        fin_type = compiler.current_scope.resolve_type(ast_node)
        if fin_type: return fin_type

    # 2. Address Of (&expr)
    if isinstance(ast_node, AddressOfNode):
        # Recurse to find the type of the operand
        inner_type = get_arg_fin_type(compiler, ast_node.expression, None)
        if inner_type:
            return PointerType(inner_type)

    # 3. Member Access (obj.field)
    if isinstance(ast_node, MemberAccess):
        # Resolve the object type first
        obj_type = get_arg_fin_type(compiler, ast_node.struct_name, None)
        
        # Unwrap pointer if needed
        if isinstance(obj_type, PointerType):
            obj_type = obj_type.pointee
            
        if isinstance(obj_type, StructType):
            # Look up field type in registry
            # This returns the AST node for the field type (e.g. "int" or "T")
            field_type_ast = compiler.lookup_field_type_ast(obj_type.name, ast_node.member_name)
            
            if field_type_ast:
                # If the field type is a Generic Parameter (e.g. "T"), we need to resolve it
                # using the concrete generic args of the struct instance.
                # Example: obj is Box<int>, field is 'val' (type T). Result should be int.
                
                # Convert AST to FinType pattern
                # We don't have the generic params list handy here easily without looking up the struct def again.
                # But we can try to convert it directly.
                field_fin_type = compiler.ast_to_fin_type(field_type_ast)
                
                if isinstance(field_fin_type, GenericParamType):
                    # Find index of T in struct definition
                    gen_params = compiler.get_generic_params_of_struct(obj_type.name)
                    if field_fin_type.name in gen_params:
                        idx = gen_params.index(field_fin_type.name)
                        if idx < len(obj_type.generic_args):
                            return obj_type.generic_args[idx]
                
                return field_fin_type

    # 4. Array Index (arr[i])
    if isinstance(ast_node, ArrayIndexNode):
        arr_type = get_arg_fin_type(compiler, ast_node.array_expr, None)
        
        # Unwrap pointer
        if isinstance(arr_type, PointerType):
            arr_type = arr_type.pointee
            
        # Check for Collection/Slice pattern
        if isinstance(arr_type, StructType) and arr_type.name == "Collection":
            if arr_type.generic_args:
                return arr_type.generic_args[0]
        
        # Check for Static Array (if we had a specific type for it)
        # For now, fallback to inference

    # 5. Literals
    if isinstance(ast_node, Literal):
        val = ast_node.value
        if isinstance(val, int): return IntType
        if isinstance(val, float): return FloatType
        if isinstance(val, bool): return BoolType
        if isinstance(val, str): return StringType
    
    # 6. Array Literals
    if isinstance(ast_node, ArrayLiteralNode):
        if ast_node.elements:
            # Infer element type from the first element
            # We pass None for compiled_val because we are analyzing AST
            elem_type = get_arg_fin_type(compiler, ast_node.elements[0], None)
            return StructType("Collection", [elem_type])
        # Empty array? Default to Collection<void> or handle contextually
        return StructType("Collection", [VoidType])
    # 7. Struct Instantiation (Point{...})
    if isinstance(ast_node, StructInstantiation):
        return compiler.ast_to_fin_type(ast_node.struct_name)
    
    # 8. Function Call (Return Type)
    if isinstance(ast_node, FunctionCall):
        # We need to find the function and get its return type
        # This is hard without re-resolving. 
        # But we have compiled_val! 
        # If compiled_val is available, _infer is usually okay for Structs, 
        # but bad for Collections.
        pass

    # 6. Fallback: Infer from LLVM Type
    if compiled_val is not None:
        return compiler.infer_fin_type_from_llvm(compiled_val.type)
        
    return VoidType
# <Method name=fin_type_to_ast args=[<FinType>]>
# <Description>
# Converts a High-Level 'FinType' object back into an AST Node/String for substitution.
# Used during Template Instantiation to reconstruct type nodes.
# </Description>
def fin_type_to_ast(fin_type: FinType) -> Union[str, Node]:
    """Converts a FinType back to an AST Node/String for substitution."""
    if isinstance(fin_type, PrimitiveType):
        return fin_type.name
    
    elif isinstance(fin_type, StructType):
        if not fin_type.generic_args:
            return fin_type.name
        
        args = [fin_type_to_ast(a) for a in fin_type.generic_args]
        return GenericTypeNode(fin_type.name, args)
    
    elif isinstance(fin_type, PointerType):
        return PointerTypeNode(fin_type_to_ast(fin_type.pointee))
    
    elif isinstance(fin_type, GenericParamType):
        return fin_type.name
        
    return str(fin_type)
# ---------------------------------------------------------------------------
# <Method name=fin_type_to_llvm args=[<Compiler>, <FinType>]>
# <Description>
# Converts a High-Level 'FinType' object back into an LLVM Type.
# Used during variable creation, function signatures, and casting.
# </Description>
def fin_type_to_llvm(compiler: Compiler, fin_type: FinType) -> ir.Type:
    """Resolves a FinType object to an LLVM Type."""
    
    # 1. Primitives
    if isinstance(fin_type, PrimitiveType):
        if fin_type.name == "int": return ir.IntType(32)
        if fin_type.name == "long": return ir.IntType(64)
        if fin_type.name == "float": return ir.FloatType()
        if fin_type.name == "double": return ir.DoubleType()
        if fin_type.name == "bool": return ir.IntType(1)
        if fin_type.name == "string": return ir.IntType(8).as_pointer()
        if fin_type.name == "char": return ir.IntType(8)
        if fin_type.name == "noret" or fin_type.name == "void": return ir.VoidType()

    # 2. Pointers
    elif isinstance(fin_type, PointerType):
        pointee = fin_type_to_llvm(compiler, fin_type.pointee)
        return pointee.as_pointer()
    # 2b. AnyType (Boxed Generic)
    elif isinstance(fin_type, AnyType):
        # { i8* data, i64 type_id }
        return ir.LiteralStructType([
            ir.IntType(8).as_pointer(),
            ir.IntType(64)
        ])
    
    # 3. Structs / Interfaces / Collections
    elif isinstance(fin_type, StructType):
        name = fin_type.name
        
        # [CASE A] Collection (Dynamic Array Slice)
        if name == "Collection":
            # It's a slice! { T*, i32 }
            if not fin_type.generic_args:
                # Default to void* if no generic arg provided (shouldn't happen in valid AST)
                elem_llvm_type = ir.IntType(8).as_pointer()
            else:
                elem_fin_type = fin_type.generic_args[0]
                
                # If element is generic (T), it compiles to i8*
                if isinstance(elem_fin_type, GenericParamType):
                    elem_llvm_type = ir.IntType(8).as_pointer()
                else:
                    elem_llvm_type = fin_type_to_llvm(compiler, elem_fin_type)

            return ir.LiteralStructType([
                elem_llvm_type.as_pointer(), # Data Pointer
                ir.IntType(32)               # Length
            ])

        # [CASE B] Standard Structs & Interfaces
        # We need to find the LLVM type definition.
        
        # 1. Check if the name is already a valid key (Mangled or Global)
        if name in compiler.struct_types:
            return compiler.struct_types[name]

        # 2. Try Mangling (Local Struct)
        mangled_name = compiler.get_mangled_name(name)
        if mangled_name in compiler.struct_types:
            return compiler.struct_types[mangled_name]

        # 3. Try Resolving Alias (Imported Struct)
        # The FinType might hold the Short Name "Vector", but it's aliased to "lib_std__Vector"
        alias_target = compiler.current_scope.resolve_type_alias(name)
        if alias_target and alias_target in compiler.struct_types:
            return compiler.struct_types[alias_target]

        # 4. Check if it's an Interface (Fat Pointer)
        # If it wasn't in struct_types, maybe it's an interface not yet fully registered?
        # (Unlikely if compilation order is correct, but good for debugging)
        if mangled_name in compiler.interfaces:
             return compiler.struct_types[mangled_name]

        # 5. Enums
        if name in compiler.enum_types:
            return compiler.enum_types[name]
        raise CompilerException(f"Struct or Interface '{name}' not found during LLVM conversion.")

    # 4. Generic Parameters (Type Erasure)
    elif isinstance(fin_type, GenericParamType):
        # In Type Erasure, all generics are void* (i8*)
        return ir.IntType(8).as_pointer()
        
    raise CompilerException(f"Unknown FinType: {fin_type}")

# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# <Method name=compile_type_conv args=[<Compiler>, <TypeConv>]>
# <Description>
# Compiles explicit type conversions: `std_conv<Target>(Expr)`.
# Handles:
# 1. Unboxing (Generic -> Concrete)
# 2. Boxing (Concrete -> Generic)
# 3. Upcasting (Child* -> Parent*)
# 4. Slice Construction (Ptr -> {Ptr, Len})
# 5. Primitive Conversions (Int/Float/Ptr)
# </Description>
def compile_type_conv(compiler: Compiler, ast: TypeConv) -> ir.Value:
    val = compiler.compile(ast.expr)
    src_ty = val.type
    tgt_ty = compiler.convert_type(ast.target_type)
    
    # Get FinType versions for metadata checks
    tgt_fin_ty = compiler.ast_to_fin_type(ast.target_type)

    if src_ty == tgt_ty:
        return val

    # --- 1. UNBOXING (Generic i8* -> Concrete Type) ---
    if src_ty == ir.IntType(8).as_pointer() and tgt_ty != src_ty:
        return compiler.unbox_value(val, tgt_fin_ty)

    # --- 2. BOXING (Concrete Type -> Generic i8*) ---
    if tgt_ty == ir.IntType(8).as_pointer() and src_ty != tgt_ty:
        src_fin_ty = compiler.infer_fin_type_from_llvm(src_ty)
        return compiler.box_value(val, src_fin_ty)

    # --- 3. STRUCT INHERITANCE (Upcasting: Child* -> Parent*) ---
    if isinstance(src_ty, ir.PointerType) and isinstance(tgt_ty, ir.PointerType):
        src_pointee = src_ty.pointee
        tgt_pointee = tgt_ty.pointee
        
        if isinstance(src_pointee, ir.IdentifiedStructType) and \
           isinstance(tgt_pointee, ir.IdentifiedStructType):
            
            # Extract unmangled names
            child_name = src_pointee.name.split("__")[-1]
            parent_name = tgt_pointee.name.split("__")[-1]
            
            # Check inheritance
            if compiler._is_parent_of(parent_name, child_name):
                return compiler.builder.bitcast(val, tgt_ty, name="upcast")

    # --- 4. SLICE HANDLING (i8* or T* -> Slice {T*, i32}) ---
    if isinstance(tgt_ty, ir.LiteralStructType) and len(tgt_ty.elements) == 2:
        # Casting a pointer to a slice creates a slice with length 0 (Unsafe/Raw)
        if isinstance(src_ty, ir.PointerType):
            # Create a new slice struct (Undefined base)
            slice_val = ir.Constant(tgt_ty, ir.Undefined)
            
            # Cast pointer to element type
            ptr_part = compiler.builder.bitcast(val, tgt_ty.elements[0])
            
            # Insert pointer
            slice_val = compiler.builder.insert_value(slice_val, ptr_part, 0)
            
            # Insert length 0
            slice_val = compiler.builder.insert_value(slice_val, ir.Constant(ir.IntType(32), 0), 1)
            
            return slice_val

    # --- 5. PRIMITIVE NUMERIC CONVERSIONS ---
    
    # Int -> Int (Sign Extend / Truncate)
    if isinstance(src_ty, ir.IntType) and isinstance(tgt_ty, ir.IntType):
        if src_ty.width < tgt_ty.width:
            return compiler.builder.sext(val, tgt_ty, name="conv_sext")
        else:
            return compiler.builder.trunc(val, tgt_ty, name="conv_trunc")

    # Int <-> Float
    if isinstance(src_ty, ir.IntType) and isinstance(tgt_ty, ir.FloatType):
        return compiler.builder.sitofp(val, tgt_ty, name="conv_sitofp")
    if isinstance(src_ty, ir.FloatType) and isinstance(tgt_ty, ir.IntType):
        return compiler.builder.fptosi(val, tgt_ty, name="conv_fptosi")

    # Float <-> Float (Extend / Truncate)
    if isinstance(src_ty, ir.FloatType) and isinstance(tgt_ty, ir.FloatType):
        if src_ty == ir.FloatType() and tgt_ty == ir.DoubleType():
            return compiler.builder.fpext(val, tgt_ty, name="conv_fpext")
        else:
            return compiler.builder.fptrunc(val, tgt_ty, name="conv_fptrunc")

    # --- 6. INT TO POINTER (inttoptr) ---
    if isinstance(src_ty, ir.IntType) and isinstance(tgt_ty, ir.PointerType):
        return compiler.builder.inttoptr(val, tgt_ty, name="conv_inttoptr")

    # --- 7. POINTER TO INT (ptrtoint) ---
    if isinstance(src_ty, ir.PointerType) and isinstance(tgt_ty, ir.IntType):
        return compiler.builder.ptrtoint(val, tgt_ty, name="conv_ptrtoint")

    # --- 8. POINTER BITCAST (Fallback) ---
    if isinstance(src_ty, ir.PointerType) and isinstance(tgt_ty, ir.PointerType):
        return compiler.builder.bitcast(val, tgt_ty, name="conv_bitcast")

    compiler.errors.error(ast, f"Advanced TypeConv: Cannot convert from {src_ty} to {tgt_ty}")
    return val # Dummy return

# ---------------------------------------------------------------------------
# <Method name=compile_typeof args=[<Compiler>, <TypeOf>]>
# <Description>
# Compiles 'typeof(expr)'. Returns a 64-bit integer Type ID (Hash).
# Logic:
# 1. Check if argument is a Variable Name -> Return its declared type.
# 2. Check if argument is a Type Name (e.g. int, MyStruct) -> Return that type's ID.
# 3. Fallback: Compile the expression and infer type from LLVM result.
# </Description>
# <Method name=compile_typeof args=[<Compiler>, <TypeOf>]>
def compile_typeof(compiler: Compiler, ast: TypeOf) -> ir.Value:
    """
    Compiles 'typeof(expr)'.
    - If expr is a Type Name (int, MyStruct): Returns static Type ID.
    - If expr is a Variable: Returns its Type ID.
      - If Variable is 'any': Returns the RUNTIME Type ID stored in the struct.
    - If expr is an Expression: Compiles it and returns result Type ID.
    """
    expr = ast.expr
    fin_type = None
    
    # Case 1: Argument is a String (Identifier)
    if isinstance(expr, str):
        # A. Is it a Variable in Scope?
        # We check variables first to allow shadowing (e.g. let int = 5; typeof(int))
        fin_type = compiler.current_scope.resolve_type(expr)
        
        
        if fin_type:
            # [RUNTIME CHECK] Handle 'any' variable
            # If the variable is 'any', we must load the dynamic Type ID from the struct.
            if isinstance(fin_type, AnyType):
                val_ptr = compiler.current_scope.resolve(expr)
                
                # val_ptr is 'any*' (alloca). Struct is { i8* data, i64 type_id }
                # We need index 1.
                
                # Handle indirection (any** -> any*)
                if isinstance(val_ptr.type, ir.PointerType) and isinstance(val_ptr.type.pointee, ir.PointerType):
                    val_ptr = compiler.builder.load(val_ptr, name="any_ptr_load")

                zero = ir.Constant(ir.IntType(32), 0)
                one = ir.Constant(ir.IntType(32), 1)
                
                # GEP to type_id
                id_ptr = compiler.builder.gep(val_ptr, [zero, one], name="any_typeid_ptr")
                return compiler.builder.load(id_ptr, name="any_typeid")
        
        # B. If not a variable, is it a Type Name?
        else:
            try:
                candidate = compiler.ast_to_fin_type(expr)
                
                # Check if it's a valid type (not "unknown")
                is_valid = True
                if isinstance(candidate, PrimitiveType) and candidate.name == "unknown":
                    is_valid = False
                
                if is_valid:
                    fin_type = candidate
            except:
                pass

    # Case 2: Argument is a Type Node (e.g. typeof(Box<int>))
    # The parser might pass GenericTypeNode etc. if they are in the expression grammar
    elif isinstance(expr, (GenericTypeNode, ArrayTypeNode, PointerTypeNode, TypeAnnotation, ModuleAccess)):
        fin_type = compiler.ast_to_fin_type(expr)

    # Case 3: Argument is an Expression (e.g. typeof(x + y))
    if fin_type is None:
        # Compile expression to infer type
        val = compiler.compile(expr)
        fin_type = compiler.infer_fin_type_from_llvm(val.type)
        
        # [RUNTIME CHECK] Handle 'any' expression result
        # If the expression returns an 'any' struct {i8*, i64}
        if isinstance(fin_type, AnyType):
             return compiler.builder.extract_value(val, 1, name="any_typeid_extracted")

    if fin_type is None:
        compiler.errors.error(ast, f"Could not determine type for typeof({expr})")
        return ir.Constant(ir.IntType(64), 0)
    elif isinstance(fin_type, ir.Type):
        # This is so freaking wrong but whatever bro
        fin_type = compiler.infer_fin_type_from_llvm(fin_type)
    # Static Type ID
    
    type_id = fin_type.type_id
    return ir.Constant(ir.IntType(64), type_id)
