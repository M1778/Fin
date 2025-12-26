from .essentials import *

# <Method name=create_vtable args=[<Compiler>, <str>, <str>]>
# <Description>
# Creates a VTable Struct Type for the given Interface.
# Handles method resolution including inheritance lookup.
# </Description>
def create_vtable(compiler: Compiler, concrete_struct_name: str, interface_name: str) -> ir.Value:
    """
    Generates a Global Constant VTable for (Struct -> Interface).
    Returns a pointer to the VTable (i8*).
    """
    # 1. Calculate Mangled Names for the VTable Symbol
    mangled_struct = compiler.get_mangled_name(concrete_struct_name)
    mangled_iface = compiler.get_mangled_name(interface_name)

    vtable_global_name = f"vtable_{mangled_struct}_for_{mangled_iface}"
    
    # Check if already generated
    try:
        gvar = compiler.module.get_global(vtable_global_name)
        return compiler.builder.bitcast(gvar, ir.IntType(8).as_pointer())
    except KeyError:
        pass

    # 2. Resolve Interface Definition
    # struct_methods registry uses Unmangled Names usually.
    if interface_name in compiler.struct_methods:
        iface_methods = compiler.struct_methods[interface_name]
    elif mangled_iface in compiler.struct_methods:
        iface_methods = compiler.struct_methods[mangled_iface]
    else:
        # Try stripping module prefix if present
        clean_name = interface_name.split(".")[-1]
        if clean_name in compiler.struct_methods:
            iface_methods = compiler.struct_methods[clean_name]
        else:
            compiler.errors.error(None, f"Interface '{interface_name}' definition not found during VTable generation.")
            return ir.Constant(ir.IntType(8).as_pointer(), None)

    # 3. Find matching Concrete Methods (Recursive Lookup)
    func_ptrs = []
    for method in iface_methods:
        impl_func = _find_implementation(compiler, concrete_struct_name, method.name)
        
        if not impl_func:
            compiler.errors.error(None, f"Struct '{concrete_struct_name}' does not implement method '{method.name}' required by '{interface_name}'.")
            # Fill with null to avoid crash, but compilation is failed
            void_ptr = ir.Constant(ir.IntType(8).as_pointer(), None)
        else:
            # Cast function pointer to i8* for storage in VTable
            void_ptr = ir.Constant.bitcast(impl_func, ir.IntType(8).as_pointer())
        
        func_ptrs.append(void_ptr)

    # 4. Create VTable Array
    if not func_ptrs:
        # Empty interface? Create dummy 1-element array
        vtable_array_ty = ir.ArrayType(ir.IntType(8).as_pointer(), 1)
        vtable_const = ir.Constant(vtable_array_ty, [ir.Constant(ir.IntType(8).as_pointer(), None)])
    else:
        vtable_array_ty = ir.ArrayType(ir.IntType(8).as_pointer(), len(func_ptrs))
        vtable_const = ir.Constant(vtable_array_ty, func_ptrs)
    
    # 5. Store in Global
    gvar = ir.GlobalVariable(compiler.module, vtable_array_ty, name=vtable_global_name)
    gvar.global_constant = True
    gvar.initializer = vtable_const
    
    # Return pointer to start (i8*)
    return compiler.builder.bitcast(gvar, ir.IntType(8).as_pointer())

# ---------------------------------------------------------------------------
from .essentials import *

# <Method name=pack_interface args=[<Compiler>, <ir.Value>, <ir.Type>, <ir.Type>]>
# <Description>
# Converts a Struct* (or Value) into an Interface Fat Pointer {data*, vtable*}.
# 1. Casts the struct pointer to i8* (Data).
# 2. Resolves the mangled names of the Struct and Interface from their LLVM types.
# 3. Generates or retrieves the VTable for this specific pair.
# 4. Constructs the Fat Pointer struct.
# </Description>
def pack_interface(compiler: Compiler, struct_val: ir.Value, struct_type: ir.Type, interface_type: ir.Type) -> ir.Value:
    # 1. Get Data Pointer (i8*)
    data_ptr = struct_val
    
    # If it's a Value (loaded struct), spill to stack to get a pointer
    if not isinstance(struct_val.type, ir.PointerType):
        temp = compiler.builder.alloca(struct_val.type, name="pack_spill")
        compiler.builder.store(struct_val, temp)
        data_ptr = temp
    
    # Bitcast to void*
    data_ptr_i8 = compiler.builder.bitcast(data_ptr, ir.IntType(8).as_pointer(), name="pack_data_cast")

    # 2. Resolve Mangled Names from LLVM Types
    # We need to know EXACTLY which struct and which interface we are dealing with.
    
    # Unwrap pointer if necessary
    actual_struct_type = struct_type
    if isinstance(actual_struct_type, ir.PointerType): 
        actual_struct_type = actual_struct_type.pointee
        
    struct_mangled_name = getattr(actual_struct_type, 'name', None)
    
    # Find Interface Name by searching the registry (Reverse Lookup)
    # This is necessary because LiteralStructType doesn't have a name.
    interface_mangled_name = None
    for name, ty in compiler.struct_types.items():
        if ty == interface_type:
            interface_mangled_name = name
            break
    
    if not struct_mangled_name or not interface_mangled_name:
         compiler.errors.error(None, f"Internal Error: Could not resolve type names for interface packing.\nStruct: {struct_type}\nInterface: {interface_type}")
         return ir.Constant(interface_type, ir.Undefined)

    # 3. Get VTable Pointer
    # We use the mangled names directly to avoid re-mangling issues across modules.
    vtable_ptr = _create_vtable_from_mangled(compiler, struct_mangled_name, interface_mangled_name)

    # 4. Create Fat Pointer { i8*, i8* }
    fat_ptr = ir.Constant(interface_type, ir.Undefined)
    fat_ptr = compiler.builder.insert_value(fat_ptr, data_ptr_i8, 0)
    fat_ptr = compiler.builder.insert_value(fat_ptr, vtable_ptr, 1)
    
    return fat_ptr

# ---------------------------------------------------------------------------
# <Method name=compile_interface args=[<Compiler>, <InterfaceDeclaration>]>
# <Description>
# Compiles an 'interface' definition.
# 1. Enters a new scope for generics.
# 2. Registers the interface as a Fat Pointer type {i8*, i8*} (Data + VTable).
# 3. Processes members (fields) to populate metadata registries (indices, visibility).
#    Note: Interfaces don't have a memory layout for fields like structs, but we track them
#    for semantic checking.
# 4. Registers methods for VTable generation.
# </Description>
def compile_interface(compiler: Compiler, ast: InterfaceDeclaration):
    name = ast.name
    compiler.enter_scope()
    
    # 1. Register Generic Params
    if ast.generic_params:
        param_names = []
        for param in ast.generic_params:
            # param is GenericParam object
            compiler.current_scope.define_type_parameter(param.name, param.constraint)
            param_names.append(param.name)
        compiler.struct_generic_params_registry[name] = param_names

    mangled_name = compiler.get_mangled_name(name)
    
    if mangled_name in compiler.struct_types:
         compiler.exit_scope()
         compiler.errors.error(ast, f"Interface '{name}' (internal: {mangled_name}) already declared.")
         return
    
    # 2. Register as Fat Pointer Type: { i8* data, i8* vtable }
    # This type is fixed and immutable.
    interface_ty = ir.LiteralStructType([
        ir.IntType(8).as_pointer(), # data (void*)
        ir.IntType(8).as_pointer()  # vtable (void*)
    ])
    
    compiler.struct_types[mangled_name] = interface_ty
    compiler.interfaces.add(mangled_name)
    
    # 3. Process Members (Metadata only)
    final_field_indices = {}
    final_field_defaults = {}
    final_field_visibility = {}
    field_types_map = {}
    current_index_offset = 0
    
    # Check if AST node has 'members' attribute
    members = getattr(ast, 'members', [])
    
    for member in members:
        # We convert types to ensure they exist/are valid
        compiler.convert_type(member.var_type)
        
        # Register metadata
        final_field_indices[member.identifier] = current_index_offset
        current_index_offset += 1
        
        final_field_visibility[member.identifier] = member.visibility
        
        if isinstance(member.var_type, str):
            field_types_map[member.identifier] = member.var_type
        elif hasattr(member.var_type, 'name'):
            field_types_map[member.identifier] = member.var_type.name
        else:
            field_types_map[member.identifier] = "unknown"

    # Note: We do NOT call set_body() because LiteralStructType is immutable.
    
    # 4. Register Metadata
    compiler.struct_field_indices[mangled_name] = final_field_indices
    compiler.struct_field_defaults[mangled_name] = final_field_defaults
    compiler.struct_field_visibility[mangled_name] = final_field_visibility
    compiler.struct_field_types_registry[name] = field_types_map
    
    # Register Methods (Abstract)
    compiler.struct_methods[name] = ast.methods
    
    compiler.exit_scope()

# ---------------------------------------------------------------------------
# <Method name=get_interface_type args=[<Compiler>, <str>]>
# <Description>
# Returns the LLVM Struct type for a Fat Pointer: { i8* data, i8* vtable }.
# </Description>
def get_interface_type(self, interface_name):
    """Returns the LLVM Struct type for a Fat Pointer: { i8* data, i8* vtable }"""
    # We use a literal struct for all interfaces to keep it simple
    # Element 0: Data Pointer (void*)
    # Element 1: VTable Pointer (void*) - We cast this to function ptrs later
    return ir.LiteralStructType([ir.IntType(8).as_pointer(), ir.IntType(8).as_pointer()])

# ---------------------------------------------------------------------------












# <InnerHelperFunctions>
# <Method name=_create_vtable_from_mangled args=[<Compiler>, <str>, <str>]>
# <Description>
# Helper to generate a VTable when we already have the fully mangled names.
# This is critical for cross-module interface implementation.
# </Description>
def _create_vtable_from_mangled(compiler: Compiler, mangled_struct: str, mangled_iface: str) -> ir.Value:
    vtable_global_name = f"vtable_{mangled_struct}_for_{mangled_iface}"
    
    # 1. Check Cache (Global Variable)
    try:
        gvar = compiler.module.get_global(vtable_global_name)
        return compiler.builder.bitcast(gvar, ir.IntType(8).as_pointer())
    except KeyError:
        pass

    # 2. Find Interface Definition (to get method list)
    # The struct_methods registry keys are usually UNMANGLED.
    # We try to unmangle the interface name to find the definition.
    
    unmangled_iface = mangled_iface.split("__")[-1] # Simple unmangle strategy
    
    if unmangled_iface in compiler.struct_methods:
        iface_methods = compiler.struct_methods[unmangled_iface]
    elif mangled_iface in compiler.struct_methods:
        iface_methods = compiler.struct_methods[mangled_iface]
    else:
        compiler.errors.error(None, f"Interface definition for '{unmangled_iface}' ({mangled_iface}) not found.")
        return ir.Constant(ir.IntType(8).as_pointer(), None)

    # 3. Find Implementation Methods
    func_ptrs = []
    for method in iface_methods:
        # The implementation function name is always: MangledStruct_MethodName
        impl_name = f"{mangled_struct}_{method.name}"
        
        try:
            impl_func = compiler.module.get_global(impl_name)
            # Cast to i8*
            void_ptr = ir.Constant.bitcast(impl_func, ir.IntType(8).as_pointer())
            func_ptrs.append(void_ptr)
        except KeyError:
             # Try to find it in the inheritance chain?
             # Since we only have the mangled name here, looking up parents is hard 
             # unless we have a reverse lookup for AST nodes.
             # For now, we assume the struct implements it directly or we fail.
             # (Future improvement: Use struct_parents_registry with unmangled name)
             compiler.errors.error(None, f"Struct '{mangled_struct}' missing implementation for interface method '{method.name}'.")
             return ir.Constant(ir.IntType(8).as_pointer(), None)

    # 4. Create VTable Array
    if not func_ptrs:
        vtable_array_ty = ir.ArrayType(ir.IntType(8).as_pointer(), 1)
        vtable_const = ir.Constant(vtable_array_ty, [ir.Constant(ir.IntType(8).as_pointer(), None)])
    else:
        vtable_array_ty = ir.ArrayType(ir.IntType(8).as_pointer(), len(func_ptrs))
        vtable_const = ir.Constant(vtable_array_ty, func_ptrs)
    
    # 5. Store in Global
    gvar = ir.GlobalVariable(compiler.module, vtable_array_ty, name=vtable_global_name)
    gvar.global_constant = True
    gvar.initializer = vtable_const
    
    return compiler.builder.bitcast(gvar, ir.IntType(8).as_pointer())

# ---------------------------------------------------------------------------
def _find_implementation(compiler: Compiler, struct_name: str, method_name: str) -> Optional[ir.Function]:
    """
    Recursively searches for a method implementation in the struct or its parents.
    """
    # 1. Check current struct
    mangled_struct = compiler.get_mangled_name(struct_name)
    impl_name = f"{mangled_struct}_{method_name}"
    
    try:
        return compiler.module.get_global(impl_name)
    except KeyError:
        pass
    
    # 2. Check Parents
    if struct_name in compiler.struct_parents_registry:
        parents = compiler.struct_parents_registry[struct_name]
        for parent in parents:
            # Resolve parent name (handle GenericTypeNode or string)
            parent_name = parent if isinstance(parent, str) else getattr(parent, 'base_name', str(parent))
            
            # Recurse
            found = _find_implementation(compiler, parent_name, method_name)
            if found:
                return found

    return None