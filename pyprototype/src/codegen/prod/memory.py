from .essentials import *
# ---------------------------------------------------------------------------
# <Method name=compile_address_of args=[<Compiler>, <AddressOfNode>]>
# <Description>
# Compiles '&expr'. Returns the memory address (L-Value) of the expression.
# Handles:
# 1. Variables (&x)
# 2. Array Elements (&arr[i]) - Preserves bounds checking!
# 3. Struct Members (&obj.field)
# 4. Dereference (&*ptr) -> ptr optimization
# </Description>
def compile_address_of(compiler: Compiler, ast: AddressOfNode) -> ir.Value:
    target = ast.expression

    # Case 1: Variable (&x)
    if isinstance(target, str):
        # Check Module Access first (e.g. &std.PI)
        if target in compiler.module_aliases:
            # This is a module alias, not a variable. 
            # We need to check if it's a variable inside the module?
            # Actually, the parser usually parses "std.PI" as ModuleAccess, not string.
            # If it comes here as string, it's a local var.
            pass

        var_ptr = compiler.current_scope.resolve(target)
        if not var_ptr:
            compiler.errors.error(ast, f"Cannot take address of unknown variable '{target}'")
            return ir.Constant(ir.IntType(8).as_pointer(), None)
        
        # Ensure it's an L-Value (Allocated memory)
        if not isinstance(var_ptr, (ir.AllocaInstr, ir.GlobalVariable)):
             compiler.errors.error(ast, f"Cannot take address of '{target}' (it is not a memory location).")
             return ir.Constant(ir.IntType(8).as_pointer(), None)
             
        return var_ptr

    # Case 2: Array Index (&arr[i])
    elif isinstance(target, ArrayIndexNode):
        # Delegate to array logic but request POINTER (L-Value)
        # This ensures we get Bounds Checking for free!
        return compiler.compile_array_index(target, want_pointer=True)

    # Case 3: Member Access (&obj.field)
    elif isinstance(target, MemberAccess):
        # Delegate to member logic but request POINTER (L-Value)
        return compiler.compile_member_access(target, want_pointer=True)

    # Case 4: Dereference (&*ptr) -> ptr
    elif isinstance(target, DereferenceNode):
        # Optimization: Address of a dereference is just the pointer itself.
        return compiler.compile(target.expression)

    # Case 5: Module Access (&mod.var)
    elif isinstance(target, ModuleAccess):
        # We need to manually handle this to avoid loading the value
        alias = target.alias
        name = target.name
        
        if alias in compiler.module_aliases:
            path = compiler.module_aliases[alias]
            namespace = compiler.loaded_modules.get(path, {})
            
            if name in namespace:
                val = namespace[name]
                if isinstance(val, (ir.GlobalVariable, ir.AllocaInstr)):
                    return val # Return the pointer directly!
                else:
                    compiler.errors.error(ast, f"Cannot take address of '{alias}.{name}' (not a variable).")
                    return ir.Constant(ir.IntType(8).as_pointer(), None)
                    
        compiler.errors.error(ast, f"Cannot resolve '{alias}.{name}' for address-of.")
        return ir.Constant(ir.IntType(8).as_pointer(), None)

    else:
        compiler.errors.error(ast, f"Cannot take address of expression type '{type(target).__name__}'.")
        return ir.Constant(ir.IntType(8).as_pointer(), None)

# ---------------------------------------------------------------------------
# <Method name=compile_dereference args=[<Compiler>, <DereferenceNode>]>
# <Description>
# Compiles '*expr'.
# 1. Compiles expression.
# 2. Checks if it's a pointer.
# 3. Emits Runtime Null Check.
# 4. Loads the value.
# </Description>
def compile_dereference(compiler: Compiler, ast: DereferenceNode) -> ir.Value:
    ptr_val = compiler.compile(ast.expression)
    
    if not isinstance(ptr_val.type, ir.PointerType):
        compiler.errors.error(ast, f"Cannot dereference non-pointer type: {ptr_val.type}")
        return ptr_val # Dummy return

    # Runtime Null Check
    # Cast pointer to int for comparison
    ptr_as_int = compiler.builder.ptrtoint(ptr_val, ir.IntType(64))
    compiler._emit_runtime_check_zero(ptr_as_int, "Segmentation fault: Dereferencing null pointer", node=ast)

    return compiler.builder.load(ptr_val, name="deref_val")

# ---------------------------------------------------------------------------
# <Method name=compile_new args=[<Compiler>, <NewExpressionNode>]>
# <Description>
# Compiles 'new T' or 'new T(...)'.
# 1. Calculates size.
# 2. Calls malloc.
# 3. Initializes memory (Constructor or Field Init).
# </Description>
def compile_new(compiler: Compiler, ast: NewExpressionNode) -> ir.Value:
    alloc_type_ast = ast.alloc_type_ast
    llvm_type_to_allocate = compiler.convert_type(alloc_type_ast)

    # 1. Calculate Size
    if compiler.data_layout_obj:
        size_int = llvm_type_to_allocate.get_abi_size(compiler.data_layout_obj)
        size_arg = ir.Constant(ir.IntType(64), size_int)
    else:
        # Runtime calculation
        null_ptr = ir.Constant(llvm_type_to_allocate.as_pointer(), None)
        one = ir.Constant(ir.IntType(32), 1)
        gep = compiler.builder.gep(null_ptr, [one])
        size_arg = compiler.builder.ptrtoint(gep, ir.IntType(64))

    # 2. Malloc
    try:
        malloc_fn = compiler.module.get_global("malloc")
    except KeyError:
        # Should be in builtins, but fallback
        malloc_ty = ir.FunctionType(ir.IntType(8).as_pointer(), [ir.IntType(64)])
        malloc_fn = ir.Function(compiler.module, malloc_ty, name="malloc")

    raw_ptr = compiler.builder.call(malloc_fn, [size_arg], name="new_malloc")
    typed_ptr = compiler.builder.bitcast(raw_ptr, llvm_type_to_allocate.as_pointer(), name="new_typed_ptr")

    # 3. Initialization
    
    # Case A: Constructor Arguments (new Box(10))
    if ast.init_args:
        # Check if it's a Struct with Constructor
        if isinstance(llvm_type_to_allocate, ir.IdentifiedStructType):
            mangled_name = llvm_type_to_allocate.name
            ctor_name = f"{mangled_name}__init"
            try:
                ctor = compiler.module.get_global(ctor_name)
                # We need to call constructor on the allocated memory?
                # WAIT: Your constructors currently ALLOCATE and return a pointer.
                # They don't initialize existing memory (inplace new).
                # This is a mismatch in the current architecture.
                
                # Current Constructor: returns MyStruct* (allocated inside)
                # 'new' keyword: allocates memory.
                
                # FIX: If using 'new Struct(...)', we should just call the constructor directly?
                # But the constructor allocates on STACK (alloca).
                # If we want Heap, we need to change how constructors work or copy stack->heap.
                
                # For now: Call constructor (Stack), then memcpy to Heap.
                # This is inefficient but safe for now.
                
                args = [compiler.compile(a) for a in ast.init_args]
                stack_instance = compiler.builder.call(ctor, args, name="temp_stack_ctor")
                
                # Load value from stack instance
                val = compiler.builder.load(stack_instance)
                # Store to heap
                compiler.builder.store(val, typed_ptr)
                
            except KeyError:
                compiler.errors.error(ast, f"Struct '{mangled_name}' has no constructor.")
        
        # Case: Primitive (new int(5))
        elif len(ast.init_args) == 1:
            init_val = compiler.compile(ast.init_args[0])
            # Coercion
            if init_val.type != llvm_type_to_allocate:
                if isinstance(llvm_type_to_allocate, ir.FloatType) and isinstance(init_val.type, ir.IntType):
                    init_val = compiler.builder.sitofp(init_val, llvm_type_to_allocate)
            compiler.builder.store(init_val, typed_ptr)
        else:
             compiler.errors.error(ast, "Primitive initialization expects exactly 1 argument.")

    # Case B: Field Init (new Box{val: 10})
    elif ast.init_fields:
        if not isinstance(llvm_type_to_allocate, ir.IdentifiedStructType):
            compiler.errors.error(ast, "Field initialization syntax {...} is only valid for structs.")
            return typed_ptr
        
        mangled_name = llvm_type_to_allocate.name
        
        # Reuse _allocate_and_init_struct logic but use our heap pointer
        # We need to extract the logic or call a helper that takes a pointer.
        # Let's manually do it here to ensure we use the heap pointer.
        
        field_indices = compiler.struct_field_indices.get(mangled_name)
        defaults = compiler.struct_field_defaults.get(mangled_name, {})
        
        if not field_indices:
             # Check imports
             for path, reg in compiler.module_struct_fields.items():
                 if mangled_name in reg:
                     field_indices = reg[mangled_name]
                     defaults = compiler.module_struct_defaults.get(path, {}).get(mangled_name, {})
                     break
        
        if not field_indices:
             compiler.errors.error(ast, f"Struct definition for '{mangled_name}' not found.")
             return typed_ptr

        provided = {fa.identifier: fa.value for fa in ast.init_fields}
        
        for field_name, idx in field_indices.items():
            zero = ir.Constant(ir.IntType(32), 0)
            idx_val = ir.Constant(ir.IntType(32), idx)
            fld_ptr = compiler.builder.gep(typed_ptr, [zero, idx_val], inbounds=True)
            
            val_to_store = None
            if field_name in provided:
                val_to_store = compiler.compile(provided[field_name])
            elif field_name in defaults:
                val_to_store = compiler.compile(defaults[field_name])
            else:
                val_to_store = ir.Constant(llvm_type_to_allocate.elements[idx], None)
            
            # [TYPE ERASURE] Auto-Boxing
            expected_type = llvm_type_to_allocate.elements[idx]
            if expected_type == ir.IntType(8).as_pointer() and val_to_store.type != expected_type:
                fin_type = compiler._infer_fin_type_from_llvm(val_to_store.type)
                val_to_store = compiler.box_value(val_to_store, fin_type)
            
            # Coercion
            if val_to_store.type != expected_type:
                 if isinstance(expected_type, ir.FloatType) and isinstance(val_to_store.type, ir.IntType):
                     val_to_store = compiler.builder.sitofp(val_to_store, expected_type)
                 elif isinstance(expected_type, ir.PointerType) and isinstance(val_to_store.type, ir.PointerType):
                     val_to_store = compiler.builder.bitcast(val_to_store, expected_type)

            compiler.builder.store(val_to_store, fld_ptr)

    # Case C: Zero Init
    else:
        zero_val = ir.Constant(llvm_type_to_allocate, None)
        compiler.builder.store(zero_val, typed_ptr)

    return typed_ptr

# ---------------------------------------------------------------------------
# <Method name=compile_delete args=[<Compiler>, <DeleteStatementNode>]>
# <Description>
# Compiles 'delete ptr'. Calls free().
# </Description>
def compile_delete(compiler: Compiler, ast: DeleteStatementNode):
    ptr_val = compiler.compile(ast.pointer_expr_ast)
    
    if not isinstance(ptr_val.type, ir.PointerType):
        compiler.errors.error(ast, f"'delete' expects a pointer, got {ptr_val.type}")
        return

    # Cast to i8*
    void_ptr = compiler.builder.bitcast(ptr_val, ir.IntType(8).as_pointer())
    
    try:
        free_fn = compiler.module.get_global("free")
    except KeyError:
        free_ty = ir.FunctionType(ir.VoidType(), [ir.IntType(8).as_pointer()])
        free_fn = ir.Function(compiler.module, free_ty, name="free")
        
    compiler.builder.call(free_fn, [void_ptr])

# ---------------------------------------------------------------------------
# <Method name=compile_sizeof args=[<Compiler>, <SizeofNode>]>
# <Description>
# Compiles 'sizeof(T)' or 'sizeof(expr)'.
# Returns the size in bytes as an i64 integer.
# Logic:
# 1. Determines the LLVM type of the target (either by resolving the type directly or compiling the expression).
# 2. Uses DataLayout (if available) for a compile-time constant.
# 3. Fallback: Uses the LLVM "GEP Null" trick to calculate size at runtime/link-time.
# </Description>
def compile_sizeof(compiler: Compiler, ast: SizeofNode) -> ir.Value:
    target = ast.target_ast_node
    llvm_type = None
    
    # 1. Determine Type
    # Check if the target is a Type Node or a String representing a type
    if isinstance(target, (str, ArrayTypeNode, PointerTypeNode, GenericTypeNode, TypeAnnotation, ModuleAccess)):
        try:
            llvm_type = compiler.convert_type(target)
        except Exception as e:
            # If convert_type fails, it might be an expression (e.g. a variable name that looks like a type)
            # Fallback to compiling as expression
            pass

    if llvm_type is None:
        # It's an Expression (e.g. sizeof(x + y))
        # We compile it to get the resulting LLVM Value, then check its type.
        # Note: This emits code for the expression!
        val = compiler.compile(target)
        llvm_type = val.type
        
    # 2. Calculate Size
    if compiler.data_layout_obj:
        # Preferred: Compile-time constant
        try:
            size = llvm_type.get_abi_size(compiler.data_layout_obj)
            return ir.Constant(ir.IntType(64), size)
        except Exception as e:
            compiler.errors.error(ast, f"Could not calculate ABI size for type {llvm_type}: {e}")
            return ir.Constant(ir.IntType(64), 0)
    else:
        # Fallback: Runtime GEP trick
        # size = ptrtoint( gep(T* null, 1) )
        null_ptr = ir.Constant(llvm_type.as_pointer(), None)
        one = ir.Constant(ir.IntType(32), 1)
        
        # GEP to index 1 calculates the offset in bytes for one element
        gep = compiler.builder.gep(null_ptr, [one], name="sizeof_gep")
        
        return compiler.builder.ptrtoint(gep, ir.IntType(64), name="sizeof_int")
# ---------------------------------------------------------------------------
# <Method name=compile_as_ptr args=[<Compiler>, <AsPtrNode>]>
# <Description>
# Compiles 'as_ptr(var)'. Returns the memory address (L-Value) of a variable.
# Functionally identical to '&var' (AddressOf), but explicit.
# Implementation: Delegates to compile_address_of.
# </Description>
def compile_as_ptr(compiler: Compiler, ast: AsPtrNode) -> ir.Value:
    # Reuse the robust logic in compile_address_of
    # We create a synthetic AddressOfNode wrapping the expression
    dummy_node = AddressOfNode(ast.expression_ast)
    
    # Copy location info for error reporting
    dummy_node.lineno = getattr(ast, 'lineno', 0)
    dummy_node.col_offset = getattr(ast, 'col_offset', 0)
    
    return compile_address_of(compiler, dummy_node)

# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# <Method name=compile_new args=[<Compiler>, <NewExpressionNode>]>
# <Description>
# Compiles 'new T' or 'new T(...)'.
# 1. Calculates size.
# 2. Calls allocator (compiler.memory.alloc).
# 3. Initializes memory (Constructor or Field Init).
# </Description>
def compile_new(compiler: Compiler, ast: NewExpressionNode) -> ir.Value:
    alloc_type_ast = ast.alloc_type_ast
    llvm_type_to_allocate = compiler.convert_type(alloc_type_ast)

    # 1. Calculate Size (Robust GEP Method)
    if compiler.data_layout_obj:
        size_int = llvm_type_to_allocate.get_abi_size(compiler.data_layout_obj)
        size_arg = ir.Constant(ir.IntType(64), size_int)
    else:
        # Runtime calculation
        null_ptr = ir.Constant(llvm_type_to_allocate.as_pointer(), None)
        one = ir.Constant(ir.IntType(32), 1)
        gep = compiler.builder.gep(null_ptr, [one])
        size_arg = compiler.builder.ptrtoint(gep, ir.IntType(64))

    # 2. Allocate Memory
    # We use a helper that calls the user-defined allocator (or malloc fallback)
    raw_ptr = _call_allocator(compiler, size_arg)
    
    typed_ptr = compiler.builder.bitcast(raw_ptr, llvm_type_to_allocate.as_pointer(), name="new_typed_ptr")

    # 3. Initialization
    
    # Case A: Constructor Arguments (new Box(10))
    if ast.init_args:
        if isinstance(llvm_type_to_allocate, ir.IdentifiedStructType):
            mangled_name = llvm_type_to_allocate.name
            ctor_name = f"{mangled_name}__init"
            
            try:
                ctor = compiler.module.get_global(ctor_name)
                
                # [FIX] Call Constructor on Heap Memory
                # Our constructors currently allocate their own stack memory and return it.
                # This is incompatible with 'new'.
                # We need to change constructors to accept an optional 'self' pointer?
                # OR: We let the constructor allocate on stack, then memcpy to heap.
                
                # Current Strategy: Stack Alloc -> Memcpy
                args = [compiler.compile(a) for a in ast.init_args]
                stack_instance_ptr = compiler.builder.call(ctor, args, name="temp_stack_ctor")
                
                # Load value from stack
                val = compiler.builder.load(stack_instance_ptr)
                # Store to heap
                compiler.builder.store(val, typed_ptr)
                
            except KeyError:
                compiler.errors.error(ast, f"Struct '{mangled_name}' has no constructor.")
        
        # Case: Primitive (new int(5))
        elif len(ast.init_args) == 1:
            init_val = compiler.compile(ast.init_args[0])
            # Coercion
            if init_val.type != llvm_type_to_allocate:
                if isinstance(llvm_type_to_allocate, ir.FloatType) and isinstance(init_val.type, ir.IntType):
                    init_val = compiler.builder.sitofp(init_val, llvm_type_to_allocate)
            compiler.builder.store(init_val, typed_ptr)
        else:
             compiler.errors.error(ast, "Primitive initialization expects exactly 1 argument.")

    # Case B: Field Init (new Box{val: 10})
    elif ast.init_fields:
        if not isinstance(llvm_type_to_allocate, ir.IdentifiedStructType):
            compiler.errors.error(ast, "Field initialization syntax {...} is only valid for structs.")
            return typed_ptr
        
        mangled_name = llvm_type_to_allocate.name
        
        # Reuse _allocate_and_init_struct logic logic manually
        field_indices = compiler.struct_field_indices.get(mangled_name)
        defaults = compiler.struct_field_defaults.get(mangled_name, {})
        
        if not field_indices:
             for path, reg in compiler.module_struct_fields.items():
                 if mangled_name in reg:
                     field_indices = reg[mangled_name]
                     defaults = compiler.module_struct_defaults.get(path, {}).get(mangled_name, {})
                     break
        
        if not field_indices:
             compiler.errors.error(ast, f"Struct definition for '{mangled_name}' not found.")
             return typed_ptr

        provided = {fa.identifier: fa.value for fa in ast.init_fields}
        
        for field_name, idx in field_indices.items():
            zero = ir.Constant(ir.IntType(32), 0)
            idx_val = ir.Constant(ir.IntType(32), idx)
            fld_ptr = compiler.builder.gep(typed_ptr, [zero, idx_val], inbounds=True)
            
            val_to_store = None
            if field_name in provided:
                val_to_store = compiler.compile(provided[field_name])
            elif field_name in defaults:
                val_to_store = compiler.compile(defaults[field_name])
            else:
                val_to_store = ir.Constant(llvm_type_to_allocate.elements[idx], None)
            
            # [TYPE ERASURE] Auto-Boxing
            expected_type = llvm_type_to_allocate.elements[idx]
            if expected_type == ir.IntType(8).as_pointer() and val_to_store.type != expected_type:
                fin_type = compiler._infer_fin_type_from_llvm(val_to_store.type)
                val_to_store = compiler.box_value(val_to_store, fin_type)
            
            # Coercion
            if val_to_store.type != expected_type:
                 if isinstance(expected_type, ir.FloatType) and isinstance(val_to_store.type, ir.IntType):
                     val_to_store = compiler.builder.sitofp(val_to_store, expected_type)
                 elif isinstance(expected_type, ir.PointerType) and isinstance(val_to_store.type, ir.PointerType):
                     val_to_store = compiler.builder.bitcast(val_to_store, expected_type)

            compiler.builder.store(val_to_store, fld_ptr)

    # Case C: Zero Init
    else:
        zero_val = ir.Constant(llvm_type_to_allocate, None)
        compiler.builder.store(zero_val, typed_ptr)

    return typed_ptr

# ---------------------------------------------------------------------------
# <Method name=compile_delete args=[<Compiler>, <DeleteStatementNode>]>
# <Description>
# Compiles 'delete ptr'. Calls deallocator.
# </Description>
def compile_delete(compiler: Compiler, ast: DeleteStatementNode):
    ptr_val = compiler.compile(ast.pointer_expr_ast)
    
    if not isinstance(ptr_val.type, ir.PointerType):
        compiler.errors.error(ast, f"'delete' expects a pointer, got {ptr_val.type}")
        return

    # Cast to i8*
    void_ptr = compiler.builder.bitcast(ptr_val, ir.IntType(8).as_pointer())
    
    # Call deallocator
    _call_deallocator(compiler, void_ptr)








# ---------------------------------------------------------------------------
# Internal Helpers for Allocator Resolution
# ---------------------------------------------------------------------------

def _call_allocator(compiler: Compiler, size_arg: ir.Value) -> ir.Value:
    """
    Calls the system allocator.
    Prioritizes 'compiler.memory.alloc' if defined, else 'malloc'.
    """
    # Try to find a user-defined allocator wrapper
    # (We assume builtins.fin defines 'malloc' or a custom function)
    try:
        malloc_fn = compiler.module.get_global("malloc")
    except KeyError:
        # Fallback declaration
        malloc_ty = ir.FunctionType(ir.IntType(8).as_pointer(), [ir.IntType(64)])
        malloc_fn = ir.Function(compiler.module, malloc_ty, name="malloc")
        
    return compiler.builder.call(malloc_fn, [size_arg], name="alloc_call")

def _call_deallocator(compiler: Compiler, ptr: ir.Value):
    try:
        free_fn = compiler.module.get_global("free")
    except KeyError:
        free_ty = ir.FunctionType(ir.VoidType(), [ir.IntType(8).as_pointer()])
        free_fn = ir.Function(compiler.module, free_ty, name="free")
        
    compiler.builder.call(free_fn, [ptr])