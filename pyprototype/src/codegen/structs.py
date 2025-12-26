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


# --------------------------------------------------------------------------- M1778, https://github.com/M1778M/
# <Method name=compile_struct args=[<Compiler>, <AstNode<StructDeclaration>>]>
# <Description>
# Compiles a 'struct' definition. 
#
# Logic Flow:
# 1. Classify Mode: Checks if it should be Monomorphized (Template) or Erased.
# 2. Path A (MONO): Saves the AST for later instantiation. Returns immediately.
# 3. Path B (ERASED/STD): 
#    - Enters a new scope.
#    - Registers Generic Parameters (T -> i8*) if Erased.
#    - Registers Parent Structs for inheritance.
#    - Creates the LLVM Opaque Type.
#    - Pass 1: Defines Memory Layout (Fields), handling inheritance flattening.
#    - Pass 2: Compiles Behavior (Methods, Constructors, Operators).
# </Description>
def compile_struct(compiler: Compiler, ast: StructDeclaration):
    name = ast.name
    
    
    # 1. Classify Mode (MONO, ERASED, STANDARD)
    # We use the internal helper _classify_mode which returns a string.
    mode = compiler.classify_mode(ast)
    compiler.modes[name] = mode

    # --- PATH A: Monomorphization ---
    if mode == 'MONO':
        # print(f"[DEBUG STRUCT] Saving MONO template for '{name}'")
        compiler.struct_templates[name] = ast
        return

    # --- PATH B: Standard / Type Erasure ---
    # print(f"[DEBUG STRUCT] Compiling {mode} struct '{name}'")
    
    compiler.enter_scope()

    # 1. Register Generic Params (Type Erasure: T = void*)
    if mode == 'ERASED' and ast.generic_params:
        param_names = []
        for param in ast.generic_params:
            compiler.current_scope.define_type_parameter(param.name, param.constraint)
            param_names.append(param.name)
        compiler.struct_generic_params_registry[name] = param_names

    # 2. Register Parents
    if ast.parents:
        compiler.struct_parents_registry[name] = ast.parents

    mangled_name = compiler.get_mangled_name(name)
    
    # Check if already defined (or Opaque from Scouting Pass)
    if mangled_name in compiler.struct_types:
        struct_ty = compiler.struct_types[mangled_name]
        if not struct_ty.is_opaque:
             compiler.exit_scope()
             compiler.errors.error(ast, f"Struct '{name}' (internal: {mangled_name}) already declared.")
             return
    else:
        struct_ty = ir.global_context.get_identified_type(mangled_name)
        compiler.struct_types[mangled_name] = struct_ty 
    
    # Save Context
    previous_struct_name = compiler.current_struct_name
    previous_struct_type = compiler.current_struct_type
    previous_struct_ast_name = getattr(compiler, 'current_struct_ast_name', None)
    
    compiler.current_struct_name = mangled_name
    compiler.current_struct_type = struct_ty
    compiler.current_struct_ast_name = name

    # --- PASS 1: Define Memory Layout ---
    final_member_types = []
    final_field_indices = {}
    final_field_defaults = {}
    final_field_visibility = {}
    field_types_map = {} 
    current_index_offset = 0

    # A. Process Inheritance
    if ast.parents:
        for parent_node in ast.parents:
            parent_llvm_type = compiler.convert_type(parent_node)
            
            # Skip Interfaces (Fat Pointers)
            if isinstance(parent_llvm_type, ir.LiteralStructType):
                # Verify Implementation
                parent_name = parent_node
                bindings = {}
                if isinstance(parent_node, GenericTypeNode):
                    parent_name = parent_node.base_name
                    if parent_name in compiler.struct_generic_params_registry:
                        param_names = compiler.struct_generic_params_registry[parent_name]
                        if len(param_names) == len(parent_node.type_args):
                            for pname, parg in zip(param_names, parent_node.type_args):
                                bindings[pname] = str(parg)
                
                if not isinstance(parent_name, str): parent_name = str(parent_name)
                _verify_interface_implementation(compiler, name, parent_name, ast.methods, ast, bindings)
                continue

            if not isinstance(parent_llvm_type, ir.IdentifiedStructType):
                compiler.exit_scope()
                compiler.errors.error(ast, f"Parent '{parent_node}' is not a struct type.")
                return
            
            parent_mangled = parent_llvm_type.name
            p_indices = compiler.struct_field_indices.get(parent_mangled)
            
            if p_indices:
                sorted_fields = sorted(p_indices.items(), key=lambda item: item[1])
                for field_name, _ in sorted_fields:
                    final_field_indices[field_name] = current_index_offset
                    final_member_types.append(parent_llvm_type.elements[p_indices[field_name]])
                    current_index_offset += 1

    # B. Process Own Members
    for member in ast.members:
        if member.identifier in final_field_indices:
             compiler.exit_scope()
             compiler.errors.error(member, f"Field '{member.identifier}' redefines inherited field.")
             return
        
        member_llvm_type = compiler.convert_type(member.var_type)
        
        final_member_types.append(member_llvm_type)
        final_field_indices[member.identifier] = current_index_offset
        current_index_offset += 1
        
        if member.default_value is not None:
            final_field_defaults[member.identifier] = member.default_value
        final_field_visibility[member.identifier] = member.visibility

        if isinstance(member.var_type, str):
            field_types_map[member.identifier] = member.var_type
        elif hasattr(member.var_type, 'name'):
            field_types_map[member.identifier] = member.var_type.name
        else:
            field_types_map[member.identifier] = "unknown"

    struct_ty.set_body(*final_member_types)

    compiler.struct_field_indices[mangled_name] = final_field_indices
    compiler.struct_field_defaults[mangled_name] = final_field_defaults
    compiler.struct_field_visibility[mangled_name] = final_field_visibility
    compiler.struct_origins[mangled_name] = compiler.current_file_path
    compiler.struct_methods[name] = ast.methods
    compiler.struct_field_types_registry[name] = field_types_map

    # --- PASS 2: Compile Behavior ---
    compiler.struct_operators[mangled_name] = {}
    for op_decl in ast.operators:
        compiler.compile_operator(name, mangled_name, struct_ty, op_decl)

    if ast.constructor:
        compiler.compile_constructor(name, struct_ty, ast.constructor)

    if ast.destructor:
        compiler.compile_destructor(name, struct_ty, ast.destructor)

    inherited_methods = []
    if ast.parents:
        for parent_node in ast.parents:
            parent_name = parent_node if isinstance(parent_node, str) else getattr(parent_node, 'base_name', str(parent_node))
            if parent_name in compiler.struct_methods:
                for pm in compiler.struct_methods[parent_name]:
                    if not any(m.name == pm.name for m in ast.methods):
                        inherited_methods.append(pm)
    
    compiler.struct_methods[name] = inherited_methods + ast.methods

    for method in ast.methods:
        compiler.compile_struct_method(mangled_name, struct_ty, method)
        
    # Restore Context
    compiler.current_struct_name = previous_struct_name
    compiler.current_struct_type = previous_struct_type
    compiler.current_struct_ast_name = previous_struct_ast_name
    
    compiler.exit_scope()
    
# --------------------------------------------------------------------------- M1778,
# <Method name=compile_struct_method args=[<Compiler>, <str>, <ir.IdentifiedStructType>, <AstNode<FunctionDeclaration>>]>
# <Description>
# Compiles a 'struct' definition. 
# Logic Flow:
# 1. Enters a new scope for the method.
# 2. Registers method-level generic parameters if any.
# 3. Constructs the LLVM function signature, handling 'self' for instance methods.
# 4. Creates the LLVM function and entry block.
# 5. Binds function arguments to local variables in the scope.
# 6. Compiles the method body statements.
# 7. Ensures proper return handling, adding implicit returns if necessary.
# 8. Restores previous compiler state and exits the method scope.
# </Description>
def compile_struct_method(compiler: Compiler,
    struct_name: str,
    struct_llvm_type: ir.IdentifiedStructType,
    method_ast: FunctionDeclaration,
    ):
    """ Compiles a method defined within a struct."""
    
    # Enter new scope for method (Also helps with method-level generics)
    compiler.enter_scope() # magic happens here
    
    if DEBUG: print(f"[DEBUG STRUCT METHOD] Compiling method '{method_ast.name}' for struct '{struct_name}'") # Debug
    
    if method_ast.type_parameters:
        for param in method_ast.type_parameters:
            compiler.current_scope.define_type_parameter(param.name, param.constraint)
            
    mangled_fn_name = f"{struct_name}_{method_ast.name}"
    if method_ast.is_static:
        mangled_fn_name = f"{struct_name}_static_{method_ast.name}"
    
    # Convert types (T -> i8*)
    llvm_ret_type = compiler.convert_type(method_ast.return_type)
    llvm_param_types = []
    
    ast_params_to_process = []
    
    if not method_ast.is_static:
        # Instance method: First param is Struct* (self)
        llvm_param_types.append(struct_llvm_type.as_pointer())
        
        if method_ast.params and method_ast.params[0].identifier == "self":
            ast_params_to_process = method_ast.params[1:] # Skip 'self'
        else:
            ast_params_to_process = method_ast.params
    else:
        ast_params_to_process = method_ast.params
        
    for param_node in ast_params_to_process:
        llvm_param_types.append(compiler.convert_type(param_node.var_type))
    
    # Create LLVM Function Type
    llvm_fn_type = ir.FunctionType(llvm_ret_type, llvm_param_types)
    method_llvm_func = ir.Function(compiler.module, llvm_fn_type, name=mangled_fn_name)
    
    # Save state
    perv_function = compiler.current_function
    perv_builder = compiler.builder
    compiler.function = method_llvm_func
    
    # Create entry block
    entry_block = method_llvm_func.append_basic_block(name='entry')
    compiler.builder = ir.IRBuilder(entry_block) # New builder for this function
    
    # Bind arguments to names in scope
    arg_offset = 0
    
    if not method_ast.is_static:
        # Instance method: First arg is 'self'
        self_arg = method_llvm_func.args[0]
        self_arg.name = "self"
        
        self_alloca = compiler.builder.alloca(self_arg.type, name="self_ptr")
        compiler.builder.store(self_arg, self_alloca)
        
        # Define 'self' in scope with HighLevel Type Info
        # reconstruct the StructType from the name
        # (Assuming struct_name is mangled, we might want the AST name, 
        # but for now let's use what we have. The unboxer handles mangled names too.)
        self_fin_type = StructType(struct_name)
        compiler.current_scope.define("self", self_alloca,self_fin_type) # Define 'self'
        arg_offset = 1 # Adjust offset for parameters
    
    for i, param_ast_node in enumerate(ast_params_to_process): # bind params
        llvm_arg_index = i + arg_offset # Adjust for 'self' if present
        llvm_arg = method_llvm_func.args[llvm_arg_index] # Get LLVM Argument
        param_name = param_ast_node.identifier # Get parameter name
        llvm_arg.name = param_name # Name the argument in LLVM
        
        # Create alloca for parameter
        param_alloca = compiler.builder.alloca(llvm_arg.type, name=f"{param_name}_ptr")
        compiler.builder.store(llvm_arg, param_alloca)
        
        # Define parameter in scope with Type Info
        param_fin_type = compiler.ast_to_fin_type(param_ast_node.var_type) # Convert AST type to FinType
        compiler.current_scope.define(param_name, param_alloca, param_fin_type)
        
    # Compile function body
    if method_ast.body:
        for stmt_node in method_ast.body:
            compiler.compile(stmt_node) # Compile each statement in body
    
    # If no return at end of void function, add one
    if not compiler.builder.block.is_terminated:
        # Case 1: Void Method -> Implicit return
        if isinstance(llvm_ret_type, ir.VoidType):
            compiler.builder.ret_void()
            
        # Case 2: Instance Method returning Struct Pointer (Fluent Interface)
        # e.g. fun set(...) <&MyStruct>
        elif not method_ast.is_static and llvm_ret_type == struct_llvm_type.as_pointer():
            # Implicit return of 'self' pointer
            self_ptr = compiler.current_scope.resolve("self")
            # self is Struct** (alloca), load to get Struct*
            self_val = compiler.builder.load(self_ptr)
            compiler.builder.ret(self_val)

        # Case 3: Non-Void Method -> ERROR!
        else:
            compiler.errors.error(
                method_ast, 
                f"Method '{method_ast.name}' is missing a return statement.", 
                hint=f"Expected return type: {method_ast.return_type}"
            )
            compiler.builder.unreachable()
    
    # Restore previous state ( just realized i named them pervs)
    compiler.function = perv_function
    compiler.builder = perv_builder
    
    # Exit method scope
    compiler.exit_scope()

# --------------------------------------------------------------------------- M1778,
# <Method name=compile_struct_instantiation args=[<Compiler>, <AstNode<StructInstantiation>>]>
# <Description>
# Compiles a 'struct' instantiation expression.
# Logic Flow:
# 1. Resolves the struct type and metadata (field indices, defaults).
# 2. Allocates memory for the struct instance.
# 3. Initializes fields using provided assignments or default values.
# </Description>
def compile_struct_instantiation(compiler: Compiler,node:StructInstantiation):
    """ Compiles a 'struct' instantiation expression."""
    # SpecialCase: Resolve 'Self' before anything else
    if node.struct_name == "Self":
        if not compiler.current_struct_name:
            compiler.errors.error(
                node,
                "'Self' used outside of a struct context.",
                hint="The 'Self' keyword can only be used within struct methods.")
            return ir.VoidType()

        struct_type = compiler.struct_types[compiler.current_struct_name]
        mangled_name = compiler.current_struct_name
        field_indices = compiler.struct_field_indices[mangled_name]
        defaults_map = compiler.struct_field_defaults.get(mangled_name, {})
        # Allocate and initialize struct
        return compiler.allocate_and_init_struct(struct_type, mangled_name, field_indices, defaults_map, node)
    
    # SpecialCase: Handle 'super' instantiation / super{...}
    if node.struct_name == "super":
        if not compiler.current_struct_name:
            compiler.errors.error(
                node,
                "'super' used outside of a struct context.",
                hint="The 'super' keyword can only be used within struct methods.")
            return ir.VoidType()
        
        current_ast_name = getattr(compiler, 'current_struct_ast_name', None) # Get current AST struct name
        parent_name = None # Parent struct name to instantiate
        
        if current_ast_name and current_ast_name in compiler.struct_parents_registry: # If current struct has parents
            parents = compiler.struct_parents_registry[current_ast_name] # Get parent names
            if parents:
                parent_node = parents[0] # Get first parent (for now)
                parent_name = parent_node if isinstance(parent_node, str) else getattr(parent_node, 'base_name', str(parent_node))
                
        if not parent_name:
            compiler.errors.error(
                node,
                "Unable to resolve 'super' struct name.",
                hint="Ensure the current struct has a valid parent struct to instantiate.")
            return ir.VoidType()
    
        # Setup for parent struct instantiation
        mangled_parent = compiler.get_mangled_name(parent_name)
        struct_type = compiler.struct_types[mangled_parent]
        field_indices = compiler.struct_field_indices[mangled_parent]
        defaults_map = compiler.struct_field_defaults.get(mangled_parent, {})
        
        self_ptr_addr = compiler.current_scope.resolve("self")
        if not self_ptr_addr:
            compiler.errors.error(
                node,
                "'self' not found in current scope for 'super' instantiation.",
                hint="Ensure 'super' is used within an instance method of a struct.")
            return ir.VoidType()
        
        # Direct Bitcase (No load) for some reason
        # Cast 'self' to parent struct type pointer
        parent_ptr = compiler.builder.bitcast(self_ptr_addr, struct_type.as_pointer(), name="super_init_cast")
        
        # Reuse init logic manually (no allocation)
        provided_assignments = {} # field_name -> value
        if node.field_assignments and node.field_assignments != [None]: # If assignments provided
            for assignment in node.field_assignments: # AstNode<FieldAssignment>
                provided_assignments[assignment.identifier] = assignment.value # Store AST node
            
        for field_name, idx in field_indices.items(): # Iterate fields
            zero = ir.Constant(ir.IntType(32), 0) # for GEP
            idx_val = ir.Constant(ir.IntType(32), idx) # field index
            fld_ptr = compiler.builder.gep(parent_ptr, [zero, idx_val], inbounds=True) # Get field pointer
            
            value_to_store = None
            if field_name in provided_assignments:
                value_to_store = compiler.compile(provided_assignments[field_name])
            elif field_name in defaults_map:
                value_to_store = compiler.compile(defaults_map[field_name])
            else:
                field_type = struct_type.elements[idx]
                value_to_store = ir.Constant(field_type, None) # Default zero/init value
            
            compiler.builder.store(value_to_store, fld_ptr) # Store the value
        return parent_ptr
    

    # The real deal starts here
    # 1. Resolve Struct Type and Metadata
    if isinstance(node.struct_name, ModuleAccess):
        alias, struct_name = node.struct_name.alias, node.struct_name.name # Get alias and name
        
        if alias not in compiler.module_aliases: # Alias not found
            compiler.errors.error(
                node,
                f"Module alias '{alias}' not found.",
                hint="Ensure the module is imported and the alias is correct.")
            return ir.VoidType()

        module_path = compiler.module_aliases[alias] # Get module path from alias
        
        if struct_name not in compiler.module_struct_types.get(module_path, {}):
            compiler.errors.error(
                node,
                f"Struct '{struct_name}' not found in module '{alias}'.",
                hint="Ensure the struct is defined in the specified module.")
            return ir.VoidType()
        
        struct_type = compiler.module_struct_types[module_path][struct_name]
        mangled_name = struct_type.name
        field_indices = compiler.module_struct_fields[module_path][mangled_name]
        defaults_map = compiler.module_struct_defaults.get(module_path, {}).get(mangled_name, {})
    elif isinstance(node.struct_name, GenericTypeNode):
        struct_type = compiler.convert_type(node.struct_name) # Get LLVM Type
        mangled_name = struct_type.name
        struct_name = mangled_name
        
        if mangled_name in compiler.struct_field_indices:
            field_indices = compiler.struct_field_indices[mangled_name]
            defaults_map = compiler.struct_field_defaults.get(mangled_name, {})
        else:
            compiler.errors.error(
                node,
                f"Metadata for generic struct '{mangled_name}' not found.",
                hint="Ensure the struct is defined and compiled before instantiation.")
            return ir.VoidType()
    else:
        # Normal Struct (Standard)
        struct_name = node.struct_name
        
        # Resolve mangled name
        mangled_name = compiler.get_mangled_name(struct_name)
        
        if mangled_name in compiler.struct_types:
            struct_type = compiler.struct_types[mangled_name]
        elif struct_name in compiler.struct_types:
            struct_type = compiler.struct_types[struct_name]
            mangled_name = struct_name
            """ Basically we try both mangled and unmangled lookups here to be safe.
                This handles cases where the user might provide either form. 
                However, ideally we should standardize on one form for clarity. 
                For now, this dual lookup increases robustness. 
                for example, if struct_name is 'MyStruct' and mangled is 'MyStruct$1' 
                we check both to find the correct type. 
            """
        else:
            compiler.errors.error(
                node,
                f"Struct '{struct_name}' is not defined.",
                hint="Ensure the struct is defined before instantiation.")
            return ir.VoidType()
        
        field_indices = compiler.struct_field_indices[mangled_name]
        defaults_map = compiler.struct_field_defaults.get(mangled_name, {})
    
    # 2. Allocate and Initialize Struct Instance
    return compiler.allocate_and_init_struct(struct_type, mangled_name, field_indices, defaults_map, node)

# --------------------------------------------------------------------------- M1778,
# <Method name=allocate_and_init_struct args=[<Compiler>, <ir.IdentifiedStructType>, <str>, <dict>, <dict>, <AstNode<StructInstantiation>>]>
# <Description>
# Allocates memory for a struct instance and initializes its fields.
# Logic Flow:
# 1. Allocates stack memory for the struct instance.
# 2. Processes field assignments from the AST node.
# 3. Iterates over all fields, calculating pointers and determining values to store.
# 4. Handles type erasure and boxing for generic fields.
# </Description>
def allocate_and_init_struct(compiler: Compiler,
    struct_type: ir.IdentifiedStructType,
    mangled_name: str,
    field_indices: dict,
    defaults_map: dict,
    node: StructInstantiation,
    ):
    """ Allocates memory for a struct instance and initializes its fields."""
    struct_name = mangled_name # For clarity
    
    # 1. Allocate (Stack) Memory for Struct Instance
    struct_ptr = compiler.builder.alloca(struct_type, name=f"{struct_name}_inst")
    if DEBUG: print(f"[DEBUG STRUCT INSTANTIATION] Allocated memory for struct '{struct_name}'") # Debug
    
    # 2. Process Field Assignments
    provided_assignments = {} # field_name -> value
    if node.field_assignments and node.field_assignments != [None]: # If assignments provided
        for assignment in node.field_assignments: # AstNode<FieldAssignment>
            provided_assignments[assignment.identifier] = assignment.value # Store AST node
    
    # 3. Iterate ALL fields
    for field_name, idx in field_indices.items(): # Iterate fields
        # Calculate pointer to this field
        zero = ir.Constant(ir.IntType(32), 0) # for GEP
        idx_val = ir.Constant(ir.IntType(32), idx) # field index
        fld_ptr = compiler.builder.gep(struct_ptr, [zero, idx_val], inbounds=True) # Get field pointer
        
        value_to_store = None
        
        # Case A: Explicitly Assigned
        if field_name in provided_assignments:
            value_to_store = compiler.compile(provided_assignments[field_name])
            
        # Case B: Default Value
        elif field_name in defaults_map:
            value_to_store = compiler.compile(defaults_map[field_name])

        # Case C: No Assignment or Default (Zero Init)
        else:
            field_type = struct_type.elements[idx]
            value_to_store = ir.Constant(field_type, None) # Default zero/init value
        
        # ------ TYPE ERASURE & BOXING LOGIC ------- / super important
        expected_type = struct_type.elements[idx]
        # Check if the field is a Generic Slot (i8*) but the value is NOT i8*
        # (e.g., assigning an int to a generic field like [int to T])
        is_generic_slot = (expected_type == ir.IntType(8).as_pointer())
        is_value_generic = (value_to_store.type == ir.IntType(8).as_pointer())
        
        if is_generic_slot and not is_value_generic:
            # Box the value into i8*
            # Infer the FinType so box_value knows it should malloc or bitcast
            value_fin_type = compiler.infer_fin_type_from_llvm(value_to_store.type)
            value_to_store = compiler.box_value(value_to_store, value_fin_type)
        
        # ----- Standard Coercion Logic -----
        # Handle Int (i32) -> Bool (i1)
        if isinstance(expected_type, ir.IntType) and expected_type.width == 1 and \
            isinstance(value_to_store.type, ir.IntType) and value_to_store.type.width > 1:
            value_to_store = compiler.builder.trunc(value_to_store, expected_type, name="bool_trunc")
            
        # Handle char (i8) vs string literal (i8*) mismatch
        if isinstance(expected_type, ir.IntType) and expected_type.width == 8:
            if isinstance(value_to_store.type, ir.PointerType) and value_to_store.type.pointee == ir.IntType(8):
                    value_to_store = compiler.builder.load(value_to_store, name=f"{field_name}_char_load")

        # Handle Int -> Float coercion
        if value_to_store.type != expected_type:
            if isinstance(expected_type, ir.FloatType) and isinstance(value_to_store.type, ir.IntType):
                value_to_store = compiler.builder.sitofp(value_to_store, expected_type, name="default_conv")
            elif isinstance(expected_type, ir.PointerType) and isinstance(value_to_store.type, ir.PointerType):
                    value_to_store = compiler.builder.bitcast(value_to_store, expected_type)
            else:
                compiler.errors.error(
                    node,
                    f"Type mismatch for field '{field_name}' in struct '{struct_name}'. "
                    f"Expected {expected_type}, got {value_to_store.type}",
                    hint="Ensure the assigned value matches the field's type."
                )
        
        compiler.builder.store(value_to_store, fld_ptr)
    if DEBUG: print(f"[DEBUG STRUCT INSTANTIATION] Initialized fields for struct '{struct_name}'") # Debug  
    return struct_ptr

# --------------------------------------------------------------------------- M1778,
# <Method name=compile_member_access args=[<Compiler>, <AstNode<MemberAccess>>]>
# <Description>
# Compiles 'obj.member'. Handles:
# 1. 'super.__init' resolution.
# 2. Generic Constraints (T.val where T: Struct).
# 3. Array/Slice '.length' access.
# 4. Standard struct field access (GEP + Load).
# Logic Flow:
# 1. Resolves the left-hand side (LHS) expression to get the struct instance.
# 2. Handles special cases like 'super' access and generic constraints.
# 3. For array/slice types, retrieves the length field.
# 4. For standard structs, calculates the field pointer using GEP and loads the value.
# </Description>
def compile_member_access(compiler: Compiler, node: MemberAccess, want_pointer: bool = False):
    """ Compiles 'obj.member' access expressions."""
    
    # 1. Handle 'super' access
    if isinstance(node.struct_name, SuperNode):
        if not compiler.current_struct_name:
            compiler.errors.error(
                node,
                "'super' used outside of a struct context.",
                hint="The 'super' keyword can only be used within struct methods.")
            return ir.VoidType()

        # First: Identify Parent
        current_ast_name = getattr(compiler, 'current_struct_ast_name', None) # Get current AST struct name
        parent_name = None # Parent struct name to access
        if current_ast_name and current_ast_name in compiler.struct_parents_registry:# If current struct has parents
            parents = compiler.struct_parents_registry[current_ast_name] # Get parent names
            if parents:# If parents exist
                parent_node = parents[0] # Get first parent (for now)
                parent_name = parent_node if isinstance(parent_node, str) else getattr(parent_node, 'base_name', str(parent_node)) # Normalize name
        
        if not parent_name: # Could not resolve parent
            compiler.errors.error(
                node,
                "Unable to resolve 'super' struct name.",
                hint="Ensure the current struct has a valid parent struct to access.")
            return ir.VoidType()
        # Second: if it's a Field (Inherited fields are in Child's registry)
        # We use the Child's name (current_struct_name) because fields are flattened.
        mangled_child = compiler.current_struct_name # Get mangled child name
        if mangled_child in compiler.struct_field_indices and \
            node.member_name in compiler.struct_field_indices[mangled_child]: # If member is a field
                # It is a field treat 'super.x' exactly like 'self.x'
                self_ptr = compiler.current_scope.resolve("self") # Get 'self' pointer
                if not self_ptr: # 'self' not found
                    compiler.errors.error(
                        node,
                        "'self' not found in current scope for 'super' member access.",
                        hint="Ensure 'super' is used within an instance method of a struct.")
                    return ir.VoidType()
                
                # Handle Indirection: In methods, 'self' is stored in an alloca (Struct**)
                if isinstance(self_ptr.type, ir.PointerType) and \
                   isinstance(self_ptr.type.pointee, ir.PointerType): # if Struct**
                    self_ptr = compiler.builder.load(self_ptr, name="super_field_self_load") # Load to get Struct*
                # Use the standard helper with the Child's layout
                return compiler.compile_struct_field_access(self_ptr, mangled_child, node.member_name, node) # Access field
        # Third: Check if it's a Method on the Parent (Return Function Pointer)
        mangled_parent = compiler.get_mangled_name(parent_name) # Get mangled parent name
        
        # SpecialCase: __init access (super constructor)
        if node.member_name == "__init": # Accessing super constructor
            ctor_name = f"{mangled_parent}__init" # Constructor mangled name
            try:# Try to get constructor function
                return compiler.module.get_global(ctor_name)# Return the constructor function
            except KeyError:# Constructor not found
                return ir.Constant(ir.IntType(8).as_pointer(), None)# Return null pointer for constructor
        
        # Check Methods
        if parent_name in compiler.struct_methods:
            methods = compiler.struct_methods[parent_name]
            for m in methods:
                if m.name == node.member_name:
                    # Found method, return function pointer
                    func_name = f"{mangled_parent}_{node.member_name}"
                    try:
                        return compiler.module.get_global(func_name)
                    except KeyError:
                        compiler.errors.error(
                            node,
                            f"Method '{node.member_name}' not found in struct '{parent_name}'.",
                            hint="Ensure the method is defined in the parent struct.")
                        return ir.VoidType()
        # If we reach here, member not found in parent
        compiler.errors.error(
            node,
            f"Member '{node.member_name}' not found in parent struct '{parent_name}'.",
            hint="Ensure the member is defined in the parent struct.")
        return ir.VoidType()
    
    # 2. Resolve Left-Hand Side (Struct Instance)
    lhs_val = None
    lhs_fin_type = None
    
    if isinstance(node.struct_name, str):
        # Case A: Variable Name (e.g. "x.length", "gorn.pay")
        
        # Check module/enum first (For ModuleAccess and EnumAccess)
        if node.struct_name in compiler.module_aliases: # if it's a module alias
            path = compiler.module_aliases[node.struct_name] # Get module path
            namespace = compiler.loaded_modules.get(path, {}) # Get module namespace
            if node.member_name in namespace:# If member exists in module 
                val = namespace[node.member_name] # Get the value
                if isinstance(val, (ir.GlobalVariable, ir.AllocaInstr)): # If it's a variable
                    return compiler.builder.load(val, name=f"{node.struct_name}_{node.member_name}") # Load and return
                return val # Return the value directly (could be function or constant)
            # Else, not found in module
            compiler.errors.error(
                node,
                f"Member '{node.member_name}' not found in module '{node.struct_name}'.",
                hint="Ensure the member is defined in the specified module.")
            return ir.VoidType()
        
        # Enum Access
        if node.struct_name in compiler.enum_members: # if it's an enum
            members = compiler.enum_members[node.struct_name] # Get enum members
            if node.member_name in members: # If member exists
                return members[node.member_name] # Return enum member value
            # Else, not found in enum
            compiler.errors.error(
                node,
                f"Member '{node.member_name}' not found in enum '{node.struct_name}'.",
                hint="Ensure the member is defined in the specified enum.")
            return ir.VoidType()
        
        # Resolve variable from scope
        lhs_val = compiler.current_scope.resolve(node.struct_name) # Get variable pointer
        lhs_fin_type = compiler.current_scope.resolve_type(node.struct_name) # Get variable FinType
        
        if not lhs_val:
            compiler.errors.error(
                node,
                f"Variable '{node.struct_name}' not found in current scope.",
                hint="Ensure the variable is defined before accessing its members.")
            return ir.VoidType()
    else:
        # Case B: Expression (e.g. "get_vec().x" or "std_conv(...).length")
        lhs_val = compiler.compile(node.struct_name) # Compile expression
        lhs_fin_type = compiler.infer_fin_type_from_llvm(lhs_val.type) # Infer FinType
        
    # 3. Handle Generic Constraints (T.val where T: Struct)
    if isinstance(lhs_fin_type, GenericParamType):
        constraint = compiler.current_scope.get_type_constraint(lhs_fin_type.name) # Get constraint
        if constraint:
            # We have a constraint (e.g. T: StructName)
            # lhs_val is i8* (boxed generic). We cast it to ConstraintType*
            
            constraint_type = compiler.convert_type(constraint) # Get LLVM Type of constraint
            
            # Handle Indirection: If lhs_val is i8** (boxed generic pointer), load once
            val_to_cast = lhs_val # Default
            if isinstance(lhs_val.type, ir.PointerType) and \
                isinstance(lhs_val.type.pointee, ir.PointerType): # if i8** 
                val_to_cast = compiler.builder.load(lhs_val, name="generic_load") # Load to get i8*
            
            # Cast i8* to ConstraintType*
            lhs_val = compiler.builder.bitcast(val_to_cast, constraint_type.as_pointer(), name="constraint_cast") # Bitcast
            
            # Update type info to match the constraint so field access works
            lhs_fin_type = compiler.ast_to_fin_type(constraint) # Update FinType to constraint type
            
            # Fall through to standard struct access logic below
    
    # 4. Handle Array Length (Collection Length)
    # Check if lhs_val is a pointer to { T*, len }
    check_type = lhs_val.type
    if isinstance(check_type, ir.PointerType): # If pointer type
        check_type = check_type.pointee # Dereference once
    
    if isinstance(check_type, ir.LiteralStructType) and \
        len(check_type.elements) == 2 and check_type.elements[1] == ir.IntType(32): # If struct with 2 elements
            # It's probably a collection (we call arrays collections )
            if node.member_name == "length": # Accessing .length
                zero = ir.Constant(ir.IntType(32), 0)
                one = ir.Constant(ir.IntType(32), 1)
                
                # If lhs_val is a pointer (alloca), GEP to length
                if isinstance(lhs_val.type, ir.PointerType): # If pointer type
                    len_ptr = compiler.builder.gep(lhs_val, [zero, one], inbounds=True) # Get length pointer
                    return compiler.builder.load(len_ptr, name="array_len") # Load and return length
                else:
                    # If lhs_val is a value (loaded struct), extract value
                    return compiler.builder.extract_value(lhs_val, 1, name="array_len") # Extract length value
    # Static array
    elif isinstance(check_type, ir.ArrayType):
        if node.member_name == "length": # Accessing .length
            # Length is constant known at compile time
            return ir.Constant(ir.IntType(32), check_type.count)
    
    # 5. Handle Standard Struct Field Access
    if isinstance(lhs_val.type, ir.PointerType):
        # If lhs_val is a pointer to struct (Struct*)
        struct_name_str = str(node.struct_name)
        return compiler.compile_struct_field_access(lhs_val, struct_name_str, node.member_name, lhs_fin_type, node,want_pointer)

    compiler.errors.error(
        node,
        f"Cannot access member '{node.member_name}' on non-struct type.",
        hint="Ensure the left-hand side is a struct instance.")
    return ir.VoidType() 

# --------------------------------------------------------------------------- M1778,
# <Method name=compile_struct_method_call args=[<Compiler>, <AstNode<StructMethodCall>>]>
# <Description>
# Compiles struct method calls, handling both 'super' calls and standard calls.
# Logic Flow:
# 1. For 'super.method()', resolves the parent struct and method, prepares 'self', and calls the method.
# 2. For standard calls, resolves the object (variable or expression).
# 3. Determines if the call is via an interface (dynamic dispatch) or standard struct
# 4. For interface calls, extracts the vtable and data pointer, retrieves the method, and invokes it.
# 5. For standard struct calls, delegates to the static dispatch method compiler.
# </Description>
def compile_struct_method_call(compiler: Compiler, node: StructMethodCall):
    # SPECIAL: Handle 'super.method()' call
    if isinstance(node.struct_name, SuperNode): # Calling super method
        if not compiler.current_struct_name: # Not in struct context
            compiler.errors.error(
                node,
                "'super' used outside of a struct context.",
                hint="The 'super' keyword can only be used within struct methods.")
            return ir.VoidType()
        
        # 1. Find Parent
        current_ast_name = getattr(compiler, 'current_struct_ast_name', None) # Get current AST struct name
        parent_name = None # Parent struct name to access
        if current_ast_name and current_ast_name in compiler.struct_parents_registry: # If current struct has parents
            parents = compiler.struct_parents_registry[current_ast_name] # Get parent names
            if parents: # If parents exist
                parent_node = parents[0] # Get first parent (for now)
                parent_name = parent_node if isinstance(parent_node, str) else getattr(parent_node, 'base_name', str(parent_node)) # Normalize name
        
        if not parent_name: # Could not resolve parent
            compiler.errors.error(
                node,
                "Unable to resolve 'super' struct name.",
                hint="Ensure the current struct has a valid parent struct to access.")
            return ir.VoidType()

        # 2. Resolve Parent Method
        mangled_parent = compiler.get_mangled_name(parent_name) # Get mangled parent name
        method_full_name = f"{mangled_parent}_{node.method_name}" # Full mangled method name
        
        try:
            func_to_call = compiler.module.get_global(method_full_name) # Get method function
        except KeyError: # Method not found
            compiler.errors.error(
                node,
                f"Method '{node.method_name}' not found in parent struct '{parent_name}'.",
                hint="Ensure the method is defined in the parent struct.")
            return ir.VoidType()
        
        # 3. Prepare 'self' (Cast Child* to Parent*)
        self_ptr = compiler.current_scope.resolve("self") # Get 'self' pointer
        if not self_ptr: # 'self' not found
            compiler.errors.error(
                node,
                "'self' not found in current scope for 'super' method call.",
                hint="Ensure 'super' is used within an instance method of a struct.")
            return ir.VoidType()
        
        # Load if it's a pointer-to-pointer (alloca)
        if isinstance(self_ptr.type, ir.PointerType) and isinstance(self_ptr.type.pointee, ir.PointerType): # if Struct**
            self_ptr = compiler.builder.load(self_ptr, name="super_method_self_load") # Load to get Struct*
        
        parent_ty = compiler.struct_types[mangled_parent] # Get parent struct type
        self_as_parent = compiler.builder.bitcast(self_ptr, parent_ty.as_pointer(), name="super_method_cast") # Cast to Parent*
        
        # 4. Compile Args
        args = [compiler.compile(arg) for arg in node.params] # Compile arguments
        final_args = [self_as_parent] + args # Prepend 'self'
        
        return compiler.builder.call(func_to_call, final_args) # Call method
    
    # 1. Resolve the Object (Variable or Expression)
    obj_ptr = None
    fin_type= None
    
    if isinstance(node.struct_name, str):
        # Case A: Variable Name
        obj_ptr = compiler.current_scope.resolve(node.struct_name) # Get variable pointer
        fin_type = compiler.current_scope.resolve_type(node.struct_name) # Get variable FinType
        
        # Case B: Module Access
        if not obj_ptr and node.struct_name in compiler.module_aliases:
            path = compiler.module_aliases[node.struct_name] # Get module path
            namespace = compiler.loaded_modules.get(path, {}) # Get module namespace
            if node.method_name in namespace:# If method exists in module 
                func = namespace[node.method_name] # Get the function
                args = [compiler.compile(arg) for arg in node.params] # Compile arguments
                return compiler.builder.call(func, args) # Call function
            compiler.errors.error(
                node,
                f"Method '{node.method_name}' not found in module '{node.struct_name}'.",
                hint="Ensure the method is defined in the specified module.")
            return ir.VoidType()
        
        if not obj_ptr:
            # Case C: Expression (e.g. get_obj().method())
            obj_ptr = compiler.compile(node.struct_name) # Compile expression
            fin_type = compiler.infer_fin_type_from_llvm(obj_ptr.type) # Infer FinType
        
        if not obj_ptr: # Still not found
            compiler.errors.error(
                node,
                f"Couldn't resolve object '{node.struct_name}' for method call.",
                hint="Ensure the object is defined before calling its methods.")
            return ir.VoidType()
        
        # 2. Determine Dispatch Mode
        is_interface = False # Flag for interface dispatch
        
        # Check Via FinType (the most reliable way)
        if fin_type and isinstance(fin_type, StructType): # If FinType is StructType
            mangled_name = compiler.get_mangled_name(fin_type.name) # Get mangled name
            if mangled_name in compiler.interfaces: # If it's an interface
                is_interface = True
        
        # Fallback: Check LLVM Type Structure ({i8*, i8*})
        if not is_interface:
            check_type = obj_ptr.type # Get LLVM type
            if isinstance(check_type, ir.PointerType): # If pointer type
                check_type = check_type.pointee # Dereference once

            if isinstance(check_type, ir.LiteralStructType) and \
                len(check_type.elements) == 2:
                    # Check if elements are pointers
                    e0 = check_type.elements[0]
                    e1 = check_type.elements[1]
                    if isinstance(e0, ir.PointerType) and isinstance(e1, ir.PointerType):
                        is_interface = True # It's an interface dispatch
            
        # ----- PATH A: Interface (Dynamic Dispatch) -----
        if is_interface:
            # 1. Load the Fat Pointer (if it's an address/alloca)
            fat_ptr = obj_ptr # Default
            if isinstance(obj_ptr.type, ir.PointerType): # If pointer type
                fat_ptr = compiler.builder.load(obj_ptr, name="iface_load") # Load fat pointer

            # 2. Extract Data and VTable
            data_ptr = compiler.builder.extract_value(fat_ptr, 0, name="iface_data")
            vtable_ptr = compiler.builder.extract_value(fat_ptr, 1, name="iface_vtable")
            
            # 3. Identify Interface Name
            if not fin_type or not isinstance(fin_type, StructType):
                compiler.errors.error(
                    node,
                    f"Cannot dispatch method '{node.method_name}': Interface type info lost.",
                    hint="Ensure the object's type is known at compile time.")
                return ir.VoidType()
            
            interface_name = fin_type.name # Get interface name
            if interface_name not in compiler.struct_methods:
                # Try mangled lookup
                mangled = compiler.get_mangled_name(interface_name) # Get mangled name
                if mangled in compiler.struct_methods:
                    interface_name = mangled
                else:
                    compiler.errors.error(
                        node,
                        f"Interface '{interface_name}' not found in registry.",
                        hint="Ensure the interface is defined before use.")
                    return ir.VoidType()
            
            # 4. Find Method Index in Interface
            methods = compiler.struct_methods[interface_name] # Get interface methods
            method_idx = -1 # Initialize index
            method_def = None # Initialize method definition
            for i, m in enumerate(methods): # Iterate methods
                if m.name == node.method_name: # Found method
                    method_idx = i # Store index
                    method_def = m # Store method definition
                    break # Exit loop
            if method_idx == -1: # Method not found
                compiler.errors.error(
                    node,
                    f"Method '{node.method_name}' not found in interface '{interface_name}'.",
                    hint="Ensure the method is defined in the interface.")
                return ir.VoidType()
            
            # 5. Load Function Pointer from VTable
            vtable_array = compiler.builder.bitcast(vtable_ptr, ir.IntType(8).as_pointer().as_pointer()) # i8** for GEP
            
            func_ptr_ptr = compiler.builder.gep(
                vtable_array,
                [ir.Constant(ir.IntType(32), method_idx)],
            ) # Get function pointer address
            func_ptr_i8 = compiler.builder.load(func_ptr_ptr, name="impl_func_i8") # Load i8* function pointer
            
            # 6. Cast function pointer
            ret_ty = compiler.convert_type(method_def.return_type) # Return type
            arg_tys = [ir.IntType(8).as_pointer()] # self arg is i8* (data pointer)
            for p in method_def.params: # Other params
                arg_tys.append(compiler.convert_type(p.var_type)) # Convert param types
            
            func_ty = ir.FunctionType(ret_ty, arg_tys) # Construct function type
            func_ptr = compiler.builder.bitcast(func_ptr_i8, func_ty.as_pointer())
            
            # 7. Compile Arguments
            args = [compiler.compile(arg) for arg in node.params]
            final_args = [data_ptr] + args # Prepend data pointer as 'self'
            return compiler.builder.call(func_ptr, final_args) # Call method
        
        # ----- PATH B: Standard Struct (Static Dispatch) -----
        return compiler.compile_actual_method_call(node, obj_ptr) # Standard dispatch

# --------------------------------------------------------------------------- M1778,
# <Method name=compile_struct_field_access args=[<Compiler>, <ir.PointerType>, <str>, <str>, <FinType|None>, <AstNode|None]>
# <Description>
# Verifies that a struct implements all methods required by an interface.
# Raises CompileError if any method is missing or has a mismatched signature.
# Logic Flow:
# 1. Resolves the interface methods from the compiler registry.
# 2. Maps the struct's implemented methods for O(1) lookup.
# 3. For each required method in the interface:
#    A. Checks if the method exists in the struct.
#    B. Compares return types and parameter types for compatibility.
# </Description>
def compile_struct_field_access(compiler: Compiler, struct_ptr: ir.Value, struct_name_str: str, field_name: str, fin_type: FinType=None, node: AstNode=None, want_pointer: bool = False):
    print(f"DEBUG: struct_ptr.type = {struct_ptr.type}, struct_name_str = {struct_name_str}, field_name = {field_name}, want_pointer = {want_pointer}") # Debug
    # 1. Handle Auto-Dereferencing (Pointer to Struct)
    if isinstance(struct_ptr.type, ir.PointerType) and \
       isinstance(struct_ptr.type.pointee, ir.PointerType):
           struct_ptr = compiler.builder.load(struct_ptr, name="deref_struct")
    
    # 2. Get LLVM Type
    struct_llvm_type = struct_ptr.type.pointee
    if not isinstance(struct_llvm_type, ir.IdentifiedStructType):
        compiler.errors.error(
            node,
            f"Cannot access field '{field_name}': Not a struct type.",
            hint="Ensure the left-hand side is a valid struct instance.")
        return ir.Constant(ir.IntType(32), 0) # Dummy
    
    mangled_name = struct_llvm_type.name
    
    # 3. Find Field Index
    indices = None
    if mangled_name in compiler.struct_field_indices:
        indices = compiler.struct_field_indices[mangled_name]
    else:
        for path, registry in compiler.module_struct_fields.items():
            if mangled_name in registry:
                indices = registry[mangled_name]
                break
    
    if indices is None or field_name not in indices:
        compiler.errors.error(
            node,
            f"Cannot access field '{field_name}': Not a valid field.",
            hint="Ensure the field name is correct and the struct is properly defined."
        )
        return ir.Constant(ir.IntType(32), 0)

    idx = indices[field_name]
    
    # 4. GEP (Get Element Pointer)
    zero = ir.Constant(ir.IntType(32), 0)
    idx_val = ir.Constant(ir.IntType(32), idx)
    field_ptr = compiler.builder.gep(struct_ptr, [zero, idx_val], inbounds=True, name=f"field_{field_name}")
    
    # [FIX] Return Pointer if requested (L-Value)
    if want_pointer:
        return field_ptr

    # 5. Load (R-Value)
    loaded_val = compiler.builder.load(field_ptr, name=f"val_{field_name}")

    # --- Unboxing Logic (Only for R-Values) ---
    if fin_type and hasattr(fin_type, 'name'):
        field_def_type = compiler.lookup_field_type_ast(fin_type.name, field_name)
        generic_params = compiler.get_generic_params_of_struct(fin_type.name)
        
        is_generic_field = isinstance(field_def_type, str) and field_def_type in generic_params
        
        if is_generic_field:
            try:
                param_index = generic_params.index(field_def_type)
                concrete_type = fin_type.generic_args[param_index]
                return compiler.unbox_value(loaded_val, concrete_type)
            except Exception:
                pass
            
    return loaded_val
# --------------------------------------------------------------------------- M1778,
# <Method name=compile_constructor args=[<Compiler>, <str>, <ir.Type>, <AstNode<ConstructorDeclaration>>]>
# <Description>
# Compiles a struct constructor declaration into an LLVM function.
# The constructor initializes the struct instance with default values
# and processes constructor parameters.
# Logic Flow:
# 1. Mangles the constructor function name.
# 2. Defines the function type and creates the LLVM function.
# 3. Allocates space for 'self' and initializes fields with defaults.
# 4. Processes constructor parameters, storing them in the scope.
# 5. Compiles the constructor body statements.
# 6. Returns the 'self' pointer at the end.
# </Description>
def compile_constructor(compiler: Compiler, struct_name: str, struct_llvm_type: ir.Type, node: ConstructorDeclaration):
    """
    Compiles a constructor into a function named 'StructName__init'.
    Returns a pointer to the allocated struct.
    """
    # Mangle name: MyStruct__init
    mangled_fn_name = f"{struct_name}__init" # Constructor function name
    
    # Return type: Pointer to the Struct
    llvm_ret_type = struct_llvm_type.as_pointer() # Return type
    
    # Parameter Types
    param_types = [compiler.convert_type(p.var_type) for p in node.params] # Convert param types
    
    fn_ty = ir.FunctionType(llvm_ret_type, param_types) # Function type
    fn = ir.Function(compiler.module, fn_ty, name=mangled_fn_name) # Create function
    
    # Save current state
    prev_fn = compiler.function # Save previous function
    prev_builder = compiler.builder # Save previous builder
    compiler.function = fn # Set current function

    compiler.enter_scope() # New scope for constructor

    bb = fn.append_basic_block("entry") # Entry block
    compiler.builder = ir.IRBuilder(bb) # New builder
    
    # 1. Allocate 'self' (The struct instance being created)
    self_ptr = compiler.builder.alloca(struct_llvm_type, name="self") # Allocate struct
    compiler.current_scope.define("self", self_ptr) # Define 'self' in scope
    
    # 2. Initialize 'self' with Default Values
    # We reuse the logic from struct instantiation to ensure consistency
    mangled_struct_name = struct_llvm_type.name # Get mangled struct name
    field_indices = compiler.struct_field_indices[mangled_struct_name] # Field indices
    defaults_map = compiler.struct_field_defaults.get(mangled_struct_name, {})  # Default values
    
    for field_name, idx in field_indices.items(): # For each field
        # Calculate pointer to field
        zero = ir.Constant(ir.IntType(32), 0) # for GEP
        idx_val = ir.Constant(ir.IntType(32), idx) # field index
        fld_ptr = compiler.builder.gep(self_ptr, [zero, idx_val], inbounds=True) # Field pointer
        
        # Apply default if exists
        if field_name in defaults_map: # If default value exists
            val = compiler.compile(defaults_map[field_name]) # Compile default value
            # Add coercion logic here if needed (int->float etc) 
            if val.type != struct_llvm_type.elements[idx]:# Type mismatch
                    if isinstance(struct_llvm_type.elements[idx], ir.FloatType) and \
                        isinstance(val.type, ir.IntType): # Int to Float
                        val = compiler.builder.sitofp(val, struct_llvm_type.elements[idx]) # Convert
            compiler.builder.store(val, fld_ptr) # Store default value
        else:
            # Zero init
            zero_val = ir.Constant(struct_llvm_type.elements[idx], None) # Zero value
            compiler.builder.store(zero_val, fld_ptr) # Store zero value

    # 3. Process Constructor Arguments
    for i, param in enumerate(node.params): # For each parameter
        arg_val = fn.args[i] # Get argument value
        arg_val.name = param.identifier # Name the argument
        
        # Store argument in stack for mutability
        arg_ptr = compiler.builder.alloca(arg_val.type, name=param.identifier) # Allocate space
        compiler.builder.store(arg_val, arg_ptr) # Store argument value
        
        # Define in scope
        compiler.current_scope.define(param.identifier, arg_ptr) # Define param in scope
        
    # 4. Compile Constructor Body
    for stmt in node.body: # For each statement
        compiler.compile(stmt) # Compile statement
        
    # 5. Return 'self'
    # Note: If there is a 'return ...' in the body, compile(ReturnStatement) 
    # handles it. But constructors implicitly return self at the end.
    if not compiler.builder.block.is_terminated: # If not already returned
        compiler.builder.ret(self_ptr) # Return self pointer
    
    compiler.exit_scope() # Exit constructor scope
    
    # Restore state
    compiler.function = prev_fn # Restore previous function
    compiler.builder = prev_builder # Restore previous builder

# --------------------------------------------------------------------------- M1778,
# <Method name=compile_operator args=[<Compiler>, <str>, <str>, <ir.Type>, <AstNode<OperatorDeclaration>>]>
# <Description>
# Compiles an operator overload for a struct.
# The operator function is named using the pattern 'StructName__op_OperatorName'.
# Logic Flow:
# 1. Maps the operator symbol to a name suffix.
# 2. Defines the function type and creates the LLVM function.
# 3. Allocates and binds 'self' and parameters in the function scope.
# 4. Compiles the operator body statements.
# </Description>
def compile_operator(compiler: Compiler, struct_name: str, mangled_struct_name: str, struct_llvm_type: ir.Type, op_ast: OperatorDeclaration):
    # Map symbol to name
    op_map = CONSTANTS.OPERATOR_SYMBOL_MAP # Sample: '+' -> 'add'
    
    if op_ast.operator not in op_map:
        compiler.errors.error(
            op_ast,
            f"Unsupported operator '{op_ast.operator}' in struct '{struct_name}'.",
            hint="Ensure the operator is one of the supported operators."
        )
        return
    
    op_suffix = op_map[op_ast.operator]
    mangled_fn_name = f"{mangled_struct_name}__op_{op_suffix}"
    
    compiler.struct_operators[mangled_struct_name][op_ast.operator] = mangled_fn_name
    
    compiler.enter_scope()
    
    if op_ast.generic_params:
        for param in op_ast.generic_params:
            compiler.current_scope.define_type_parameter(param.name, param.constraint)
    
    llvm_ret_type = compiler.convert_type(op_ast.return_type)
    
    llvm_param_types = [struct_llvm_type.as_pointer()]
    for p in op_ast.params:
        llvm_param_types.append(compiler.convert_type(p.var_type))
        
    fn_ty = ir.FunctionType(llvm_ret_type, llvm_param_types)
    fn = ir.Function(compiler.module, fn_ty, name=mangled_fn_name)
    
    prev_fn = compiler.function
    prev_builder = compiler.builder
    compiler.function = fn

    bb = fn.append_basic_block("entry")
    compiler.builder = ir.IRBuilder(bb)
    
    # Bind Self
    self_arg = fn.args[0]
    self_arg.name = "self"
    self_ptr = compiler.builder.alloca(self_arg.type, name="self_ptr")
    compiler.builder.store(self_arg, self_ptr)
    
    # Use the real struct name for FinType
    self_fin_type = StructType(struct_name) 
    compiler.current_scope.define("self", self_ptr, self_fin_type)
    
    # Bind Args
    for i, param in enumerate(op_ast.params):
        arg_val = fn.args[i+1]
        arg_val.name = param.identifier
        p_ptr = compiler.builder.alloca(arg_val.type, name=param.identifier)
        compiler.builder.store(arg_val, p_ptr)

        param_fin_type = compiler.ast_to_fin_type(param.var_type)
        compiler.current_scope.define(param.identifier, p_ptr, param_fin_type)

    for stmt in op_ast.body:
        compiler.compile(stmt)
        
    if not compiler.builder.block.is_terminated:
            if isinstance(llvm_ret_type, ir.VoidType):
                compiler.builder.ret_void()
    
    compiler.function = prev_fn
    compiler.builder = prev_builder

    compiler.exit_scope() 

# --------------------------------------------------------------------------- M1778,
# <Method name=emit_operator_call args=[<Compiler>, <str>, <str>, <Assume<ir.Value>>, <Optional<Assume<ir.Value>>]>
# <Description>
# Emits a call to a struct operator overload function.
# Handles spilling 'self' to stack if needed and boxing 'other' for generics.
# Logic Flow:
# 1. Retrieves the mangled operator function name and function from the module.
# 2. Prepares 'self' argument:
#    A. If 'self' is a value, spills it to a temporary alloca to get a pointer.
# 3. Prepares 'other' argument for binary operators:
#    A. If the operator expects a generic (i8*), boxes 'other' if it's a concrete type.
# </Description>
def emit_operator_call(compiler: Compiler, struct_name: str, op: str, left_val: ir.Value, right_val: ir.Value=None):
    """
    Helper to invoke an operator overload.
    Handles:
    1. Spilling 'self' (left_val) to stack if it's a value.
    2. Boxing 'other' (right_val) if the operator expects a generic (i8*).
    """
    fn_name = compiler.struct_operators[struct_name][op] # Get mangled function name
    fn = compiler.module.get_global(fn_name) # Get function
    
    # 1. Prepare 'self' (Arg 0)
    # Operator functions always expect a pointer to the struct.
    self_arg = left_val # Default
    if not isinstance(left_val.type, ir.PointerType):
        # It's a value (loaded from stack). We must spill it back to a temp alloca.
        temp_ptr = compiler.builder.alloca(left_val.type, name="op_self_spill") # Allocate temp
        compiler.builder.store(left_val, temp_ptr) # Store value
        self_arg = temp_ptr # Use pointer as arg
    
    args = [self_arg] # Initialize args with self

    # 2. Prepare 'other' (Arg 1) - Binary Operators only
    if right_val is not None and len(fn.args) > 1: # If binary operator
        expected_type = fn.args[1].type # Expected type of other arg
        other_arg = right_val # Default
        
        # Boxing for Generics
        # If operator expects i8* (T) but we have a concrete type, BOX IT.
        if expected_type == ir.IntType(8).as_pointer() and \
            other_arg.type != expected_type: # if T (i8*) expected
            fin_type = compiler._infer_fin_type_from_llvm(other_arg.type) # Infer FinType
            other_arg = compiler.box_value(other_arg, fin_type) # Box to i8*
        
        # Pointer Casting (e.g. Child* -> Parent*)
        elif isinstance(expected_type, ir.PointerType) and \
            isinstance(other_arg.type, ir.PointerType): # if both are pointers
            if other_arg.type != expected_type: # Types differ
                other_arg = compiler.builder.bitcast(other_arg, expected_type) # Cast to expected type
        
        args.append(other_arg)

    return compiler.builder.call(fn, args, name="op_call")

# --------------------------------------------------------------------------- M1778,
# <Method name=assign_struct_field arg=[<Compiler>, ir.PointerType>, <str>, <str>, <ir.Value>, <AstNode|None]>>
# <Description>
# Compiles an assignment to a struct field.
# </Description>
def assign_struct_field(compiler: Compiler, struct_ptr: ir.Value, struct_name: str, field_name: str, value: ir.Value, node=None):
    """
    Assigns a value to a struct field.
    Handles:
    1. Auto-dereferencing (Struct** -> Struct*)
    2. Field Lookup (Local & Imported)
    3. Type Erasure (Auto-Boxing T -> i8*)
    4. Coercion (Int -> Float, Ptr -> Ptr)
    """
    
    # 1. Ensure we have a pointer
    if not isinstance(struct_ptr.type, ir.PointerType):
        msg = f"Cannot assign field '{field_name}': Target is not a pointer. Got: {struct_ptr.type}"
        if node: compiler.errors.error(node, msg)
        else: raise Exception(msg)

    # 2. Handle Struct** (Dereference if needed)
    # e.g. 'let x = new Struct' -> x is Struct** (alloca holding pointer)
    if isinstance(struct_ptr.type.pointee, ir.PointerType):
        struct_ptr = compiler.builder.load(struct_ptr, name="deref_assign_struct")

    # 3. Get Struct Type
    struct_llvm_type = struct_ptr.type.pointee
    
    if not isinstance(struct_llvm_type, ir.IdentifiedStructType):
        msg = f"Cannot assign field '{field_name}': Target is not a struct. Got pointer to: {struct_llvm_type}"
        if node: compiler.errors.error(node, msg)
        else: raise Exception(msg)

    mangled_name = struct_llvm_type.name

    # 4. Find Indices
    indices = None
    if mangled_name in compiler.struct_field_indices:
        indices = compiler.struct_field_indices[mangled_name]
    else:
        for path, registry in compiler.module_struct_fields.items():
            if mangled_name in registry:
                indices = registry[mangled_name]
                break
    
    if indices is None:
        # Try unmangled name fallback (rare)
        if struct_name in compiler.struct_field_indices:
             indices = compiler.struct_field_indices[struct_name]
        else:
             msg = f"Struct definition for '{mangled_name}' not found."
             if node: compiler.errors.error(node, msg)
             else: raise Exception(msg)

    if field_name not in indices:
        msg = f"Struct '{mangled_name}' has no field '{field_name}'."
        if node: compiler.errors.error(node, msg)
        else: raise Exception(msg)

    idx = indices[field_name]

    # 5. Calculate Field Pointer
    zero = ir.Constant(ir.IntType(32), 0)
    field_ptr = compiler.builder.gep(
        struct_ptr, [zero, ir.Constant(ir.IntType(32), idx)], inbounds=True, name=f"field_{field_name}_ptr"
    )

    # 6. Type Checking, Boxing & Coercion
    expected_type = struct_llvm_type.elements[idx]
    val_to_store = value

    # [FIX] Auto-Boxing for Generics
    # If field is i8* (Generic) but value is Concrete
    if expected_type == ir.IntType(8).as_pointer() and val_to_store.type != expected_type:
        # Infer type and box
        fin_type = compiler._infer_fin_type_from_llvm(val_to_store.type)
        val_to_store = compiler.box_value(val_to_store, fin_type)

    # [FIX] Standard Coercion
    if val_to_store.type != expected_type:
        # Int -> Float
        if isinstance(expected_type, ir.FloatType) and isinstance(val_to_store.type, ir.IntType):
            val_to_store = compiler.builder.sitofp(val_to_store, expected_type)
        
        # Ptr -> Ptr (Bitcast)
        elif isinstance(expected_type, ir.PointerType) and isinstance(val_to_store.type, ir.PointerType):
            val_to_store = compiler.builder.bitcast(val_to_store, expected_type)
        
        # Int -> Int (Width)
        elif isinstance(expected_type, ir.IntType) and isinstance(val_to_store.type, ir.IntType):
            if val_to_store.type.width < expected_type.width:
                val_to_store = compiler.builder.sext(val_to_store, expected_type)
            elif val_to_store.type.width > expected_type.width:
                val_to_store = compiler.builder.trunc(val_to_store, expected_type)

    # Final Safety Check
    if val_to_store.type != expected_type:
        msg = f"Type mismatch assigning '{field_name}'. Expected {expected_type}, got {val_to_store.type}"
        if node: compiler.errors.error(node, msg)
        else: raise Exception(msg)

    compiler.builder.store(val_to_store, field_ptr)

# --------------------------------------------------------------------------- M1778,
# <Method name=_compile_actual_method_call args=[<Compiler>, <StructMethodCall>, <ir.Value>]>
# <Description>
# Compiles a static dispatch method call (Direct call to Struct_Method).
# Handles:
# 1. Auto-dereferencing 'self'.
# 2. Argument Compilation.
# 3. Auto-Boxing (for Generic arguments).
# 4. Interface Packing (passing Struct to Interface arg).
# 5. Type Coercion.
# </Description>
def compile_actual_method_call(compiler: Compiler, ast: StructMethodCall, struct_ptr: ir.Value):
    # 1. Handle Pointer-to-Pointer (Struct**)
    # e.g. 'self' inside a method is an alloca (Struct**), we need Struct*
    if isinstance(struct_ptr.type, ir.PointerType) and \
       isinstance(struct_ptr.type.pointee, ir.PointerType):
        struct_ptr = compiler.builder.load(struct_ptr, name="deref_self")

    # 2. Verify we have a Struct Pointer (Struct*)
    if not isinstance(struct_ptr.type, ir.PointerType) or \
       not isinstance(struct_ptr.type.pointee, ir.IdentifiedStructType):
            compiler.errors.error(ast, f"Method call '{ast.method_name}' expects a struct pointer, got {struct_ptr.type}")

    # 3. Get Struct Name
    struct_llvm_type = struct_ptr.type.pointee
    struct_type_name = struct_llvm_type.name # Mangled name

    # 4. Find Method
    method_full = f"{struct_type_name}_{ast.method_name}"

    try:
        method_func = compiler.module.get_global(method_full)
    except KeyError:
        # Try to unmangle for error message
        clean_name = struct_type_name.split("__")[-1]
        compiler.errors.error(ast, f"Method '{ast.method_name}' not found on struct '{clean_name}'")
        return # Stop

    # 5. Prepare Arguments
    fn_ty = method_func.function_type
    
    # Start with 'self'
    final_args = [struct_ptr] 
    
    # Compile User Arguments
    # Note: LLVM args start at 1 (0 is self)
    for i, param_node in enumerate(ast.params):
        val = compiler.compile(param_node)
        
        # Check against expected type signature
        # Arg index in LLVM is i + 1 (because of self)
        llvm_arg_idx = i + 1
        
        if llvm_arg_idx < len(fn_ty.args):
            expected_type = fn_ty.args[llvm_arg_idx]
            
            # [FIX] Auto-Boxing for Type Erasure
            # Method expects T (i8*) but we have Concrete Type
            if expected_type == ir.IntType(8).as_pointer() and val.type != expected_type:
                fin_type = compiler._infer_fin_type_from_llvm(val.type)
                val = compiler.box_value(val, fin_type)

            # [FIX] Interface Packing
            # Method expects Interface {i8*, i8*} but we have Struct*
            is_interface_expected = (isinstance(expected_type, ir.LiteralStructType) and 
                                     len(expected_type.elements) == 2)
            
            if is_interface_expected:
                is_struct_arg = isinstance(val.type, ir.PointerType) and \
                                isinstance(val.type.pointee, ir.IdentifiedStructType)
                if is_struct_arg:
                    val = compiler._pack_interface(val, val.type, expected_type)

            # [FIX] Standard Coercion
            if val.type != expected_type:
                # Int -> Float
                if isinstance(expected_type, ir.FloatType) and isinstance(val.type, ir.IntType):
                    val = compiler.builder.sitofp(val, expected_type)
                
                # Ptr -> Ptr
                elif isinstance(expected_type, ir.PointerType) and isinstance(val.type, ir.PointerType):
                    val = compiler.builder.bitcast(val, expected_type)
                
                # Int -> Int (Width)
                elif isinstance(expected_type, ir.IntType) and isinstance(val.type, ir.IntType):
                    if val.type.width < expected_type.width:
                        val = compiler.builder.sext(val, expected_type)
                    elif val.type.width > expected_type.width:
                        val = compiler.builder.trunc(val, expected_type)

            if val.type != expected_type:
                compiler.errors.error(ast, f"In method '{ast.method_name}' of struct '{ast.struct_name}': Cannot convert argument {i+1} from '{val.type}' to expected parameter type '{expected_type}'.")

        # Handle VarArgs (Float Promotion)
        elif fn_ty.var_arg:
            if isinstance(val.type, ir.FloatType):
                val = compiler.builder.fpext(val, ir.DoubleType())

        final_args.append(val)

    return compiler.builder.call(
        method_func, final_args, name=f"{ast.struct_name}_{ast.method_name}_call"
    )

# --------------------------------------------------------------------------- M1778,
    
# <Method name=_lookup_field_type_ast args=[<Compiler>, str, str]>
# <Description>
# Finds the AST type node (e.g., "T", "int", or GenericParam) for a specific field.
# Used to determine if a field requires unboxing (Type Erasure).
# Checks:
# 1. Local Registry (Mangled & Unmangled).
# 2. Imported Modules (Mangled & Unmangled).
# </Description>
def lookup_field_type_ast(compiler: Compiler, struct_name: str, field_name: str) -> Optional[Union[str, Any]]:
    # Pre-calculate unmangled name if applicable
    unmangled = None
    if "__" in struct_name:
        unmangled = struct_name.split("__")[-1]

    # 1. Check Local Registry (Fast Path)
    # Check exact name
    if struct_name in compiler.struct_field_types_registry:
        return compiler.struct_field_types_registry[struct_name].get(field_name)
    
    # Check unmangled name (e.g. "Box" instead of "lib_fin__Box")
    if unmangled and unmangled in compiler.struct_field_types_registry:
        return compiler.struct_field_types_registry[unmangled].get(field_name)

    # 2. Check Imported Modules (Slow Path)
    # We iterate through loaded modules to find where this struct is defined.
    if hasattr(compiler, 'module_struct_field_types'):
        for path, registry in compiler.module_struct_field_types.items():
            # Check exact name in this module
            if struct_name in registry:
                return registry[struct_name].get(field_name)
            
            # Check unmangled name in this module
            if unmangled and unmangled in registry:
                return registry[unmangled].get(field_name)

    return None

# --------------------------------------------------------------------------- M1778,
# <Method name=get_generic_params_of_struct args=[<Compiler>, <str>]>
# <Description>
# Returns list of generic param names ['T', 'U'] for a struct.
# </Description>
def get_generic_params_of_struct(compiler:Compiler, struct_name:str):
    """Returns list of generic param names ['T', 'U'] for a struct."""
    if struct_name in compiler.struct_generic_params_registry:
        return compiler.struct_generic_params_registry[struct_name]
    return []

# --------------------------------------------------------------------------- M1778,
# <Method name=_is_parent_of args=[<Compiler>, <str>, <str>]>
# <Description>
# Recursively checks if 'parent_name' is in 'child_name's inheritance tree.
# Used for TypeConv (Upcasting) and 'super' checks.
# </Description>
def _is_parent_of(compiler: Compiler, parent_name: str, child_name: str) -> bool:
    if child_name not in compiler.struct_parents_registry:
        return False
    
    parents = compiler.struct_parents_registry[child_name]
    for p_node in parents:
        # Resolve parent name from AST node (could be string or GenericTypeNode)
        p_name = p_node if isinstance(p_node, str) else getattr(p_node, 'base_name', str(p_node))
        
        if p_name == parent_name:
            return True
        
        # Recursive check for multi-level inheritance
        if _is_parent_of(compiler, parent_name, p_name):
            return True
            
    return False

# --------------------------------------------------------------------------- M1778,
# <Method name=compile_destructor args=[<Compiler>, <str>, <ir.Type>, <DestructorDeclaration>]>
# <Description>
# Compiles the destructor ('delete') for a struct.
# Named 'StructName__del'.
# </Description>
def compile_destructor(compiler: Compiler, struct_name: str, struct_llvm_type: ir.Type, dtor_ast: DestructorDeclaration):
    mangled_fn_name = f"{struct_name}__del"
    
    # Destructor: void(Struct*)
    fn_ty = ir.FunctionType(ir.VoidType(), [struct_llvm_type.as_pointer()])
    fn = ir.Function(compiler.module, fn_ty, name=mangled_fn_name)
    
    prev_fn = compiler.function
    prev_builder = compiler.builder
    compiler.function = fn
    
    bb = fn.append_basic_block("entry")
    compiler.builder = ir.IRBuilder(bb)
    
    compiler.enter_scope()
    
    # Bind Self
    self_arg = fn.args[0]
    self_arg.name = "self"
    self_ptr = compiler.builder.alloca(self_arg.type, name="self_ptr")
    compiler.builder.store(self_arg, self_ptr)
    
    self_fin_type = StructType(struct_name)
    compiler.current_scope.define("self", self_ptr, self_fin_type)
    
    # Compile Body
    for stmt in dtor_ast.body:
        compiler.compile(stmt)
        
    compiler.builder.ret_void()
    
    compiler.exit_scope()
    
    compiler.function = prev_fn
    compiler.builder = prev_builder











# <UNIMPLEMENTED IMPLEMENTING>
# def _compile_destructor(compiler, struct_name, struct_llvm_type, dtor_ast): ...
# </UNIMPLEMENTED IMPLEMENTING>

# <REWRITE_REQUIRED> - M1778
# ----------------------------- LOADED HELPER FUNCTIONS
def _verify_interface_implementation(compiler, struct_name, interface_name, struct_methods, struct_ast_node, generic_bindings=None):
    """
    Verifies that 'struct_name' implements all methods required by 'interface_name'.
    Raises a CompileError if a method is missing or has a mismatched signature.
    """
    # 1. Resolve Interface Methods
    # Interface names might be mangled in the registry, but we usually have the clean name here.
    if interface_name not in compiler.struct_methods:
        # Try mangled lookup
        mangled = compiler.get_mangled_name(interface_name)
        if mangled in compiler.struct_methods:
            interface_name = mangled
        else:
            # If we can't find the interface definition, we can't verify. 
            # (convert_type likely already caught this, but safety first)
            return

    required_methods = compiler.struct_methods[interface_name]
    
    # 2. Map Implemented Methods for O(1) lookup
    implemented_map = {m.name: m for m in struct_methods}

    # 3. Check each requirement
    for req in required_methods:
        # A. Check Existence
        if req.name not in implemented_map:
            compiler.errors.error(
                struct_ast_node,
                f"Struct '{struct_name}' does not implement method '{req.name}' required by interface '{interface_name}'.",
                hint=f"Missing implementation: {req}"
            )
        
        impl = implemented_map[req.name]
        
        # B. Check Signature (Return Type & Params)
        # We convert AST types to FinTypes to compare them semantically.
        
        # Handle Generic Substitution (e.g. Interface<T> where T=int)
        # We need to substitute T in the requirement with the concrete type.
        req_ret_ast = req.return_type
        if generic_bindings:
            req_ret_ast = _substitute_type_ref(req_ret_ast, generic_bindings)

        req_ret = compiler.ast_to_fin_type(req_ret_ast)
        impl_ret = compiler.ast_to_fin_type(impl.return_type)
        
        if not _are_types_compatible(req_ret, impl_ret, struct_name):
             compiler.errors.error(
                impl,
                f"Method '{req.name}' return type mismatch.",
                hint=f"Interface expects '{req_ret}', struct provides '{impl_ret}'."
            )

        # Check Parameter Count
        if len(req.params) != len(impl.params):
             compiler.errors.error(
                impl,
                f"Method '{req.name}' parameter count mismatch.",
                hint=f"Expected {len(req.params)} parameters, got {len(impl.params)}."
            )
            
        # Check Parameter Types
        for i, (p_req, p_impl) in enumerate(zip(req.params, impl.params)):
            # Skip 'self' check (it's always compatible implicitly)
            if i == 0 and p_req.identifier == "self": continue

            p_req_type_ast = p_req.var_type
            if generic_bindings:
                p_req_type_ast = _substitute_type_ref(p_req_type_ast, generic_bindings)

            t_req = compiler.ast_to_fin_type(p_req_type_ast)
            t_impl = compiler.ast_to_fin_type(p_impl.var_type)
            
            if not _are_types_compatible(t_req, t_impl, struct_name):
                compiler.errors.error(
                    impl,
                    f"Method '{req.name}' parameter '{p_impl.identifier}' type mismatch.",
                    hint=f"Expected '{t_req}', got '{t_impl}'."
                )

def _are_types_compatible(t1, t2, current_struct_name):
    """
    Checks if interface type t1 is compatible with implementation type t2.
    Handles 'Self' polymorphism.
    """
    s1, s2 = str(t1), str(t2)
    
    if s1 == s2: return True
    
    # Interface 'Self' matches the Struct's Name
    if s1 == "Self" and s2 == current_struct_name: return True
    
    # Interface 'Self' matches Struct 'Self' (if used explicitly)
    if s1 == "Self" and s2 == "Self": return True
    
    return False

def _substitute_type_ref(node, bindings):
    """
    Helper to substitute generics in a Type Node without mutating the original.
    Returns a new Node or string.
    """
    if isinstance(node, str):
        return bindings.get(node, node)
    elif isinstance(node, GenericTypeNode):
        new_args = [_substitute_type_ref(a, bindings) for a in node.type_args]
        return GenericTypeNode(node.base_name, new_args)
    # Add other type nodes if necessary (Pointer, Array)
    return node


    

# </REWRITE_REQUIRED> - M1778
# =============================================================================