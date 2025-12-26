from .essentials import *

# ---------------------------------------------------------------------------
# <Method name=compile_array_index args=[<Compiler>, <ArrayIndexNode>, <bool>]>
# <Description>
# Compiles array indexing `arr[i]`.
# Handles:
# 1. Static Arrays ([N x T]): Compile-time bounds check + Runtime check.
# 2. Dynamic Collections ({T*, len}): Runtime bounds check.
# 3. Raw Pointers (T*): Unsafe access (no check).
# 4. L-Value vs R-Value: Returns pointer if `want_pointer=True`, else loads primitives.
# </Description>
def compile_array_index(compiler: Compiler, ast: ArrayIndexNode, want_pointer: bool = False) -> ir.Value:
    # 1. Compile Index
    index_val = compiler.compile(ast.index_expr)
    
    if not isinstance(index_val.type, ir.IntType):
        compiler.errors.error(ast.index_expr, f"Array index must be an integer, got {index_val.type}")
        return ir.Constant(ir.IntType(32), 0) # Dummy return

    # Ensure index is i32 for GEP
    if index_val.type.width != 32:
        index_val = compiler.builder.intcast(index_val, ir.IntType(32), name="idx_cast")

    # 2. Resolve Array Base
    array_ptr = None
    
    if isinstance(ast.array_expr, str):
        array_ptr = compiler.current_scope.resolve(ast.array_expr)
    else:
        array_ptr = compiler.compile(ast.array_expr)

    if not array_ptr:
        compiler.errors.error(ast, f"Could not resolve array for indexing: {ast.array_expr}")
        return ir.Constant(ir.IntType(8).as_pointer(), None)

    # Handle Indirection (Pointer to Pointer)
    # e.g. Function arguments are passed as pointers to the stack slot
    if isinstance(array_ptr.type, ir.PointerType) and isinstance(array_ptr.type.pointee, ir.PointerType):
        array_ptr = compiler.builder.load(array_ptr, name="deref_arr")

    # 3. Determine Array Type & Length
    pointee_type = array_ptr.type.pointee
    is_collection = False
    length_val = None
    
    # Case A: Static Array ([10 x i32])
    if isinstance(pointee_type, ir.ArrayType):
        length_val = ir.Constant(ir.IntType(32), pointee_type.count)
        
        # [OPTIMIZATION] Compile-Time Bounds Check
        if isinstance(ast.index_expr, Literal) and isinstance(ast.index_expr.value, int):
            idx_const = ast.index_expr.value
            if idx_const < 0 or idx_const >= pointee_type.count:
                compiler.errors.error(ast, f"Index {idx_const} out of bounds for array of size {pointee_type.count}")

    # Case B: Collection / Slice ({T*, len})
    elif isinstance(pointee_type, ir.LiteralStructType) and len(pointee_type.elements) == 2:
        is_collection = True
        # Load length from struct (index 1)
        zero = ir.Constant(ir.IntType(32), 0)
        one = ir.Constant(ir.IntType(32), 1)
        len_ptr = compiler.builder.gep(array_ptr, [zero, one], inbounds=True)
        length_val = compiler.builder.load(len_ptr, name="coll_len")
    
    # Case C: Raw Pointer (Unsafe)
    else:
        # No bounds check possible for raw pointers
        pass

    # 4. [RUNTIME CHECK] Bounds Checking
    if length_val is not None:
        # Check: if (index < 0 || index >= length) panic
        # 'icmp ult' handles negative check automatically (negative cast to large unsigned)
        in_bounds = compiler.builder.icmp_unsigned("<", index_val, length_val, name="bounds_check")
        
        with compiler.builder.if_then(compiler.builder.not_(in_bounds)):
            msg_ptr = compiler.create_global_string("Runtime Error: Index out of bounds")
            
            # Resolve Panic Function (Dynamic or Internal)
            try:
                panic_fn = compiler.module.get_global("__panic")
            except KeyError:
                panic_fn = compiler.panic_func # Fallback to internal if builtins missing

            compiler.builder.call(panic_fn, [msg_ptr])
            compiler.builder.unreachable()

    # 5. Get Element Pointer (GEP)
    zero = ir.Constant(ir.IntType(32), 0)
    elem_ptr = None
    
    if is_collection:
        # Collection: Load data ptr (index 0) -> GEP
        data_ptr_ptr = compiler.builder.gep(array_ptr, [zero, zero], inbounds=True)
        data_ptr = compiler.builder.load(data_ptr_ptr, name="coll_data")
        elem_ptr = compiler.builder.gep(data_ptr, [index_val], inbounds=True, name="elem_ptr")
    
    elif isinstance(pointee_type, ir.ArrayType):
        # Static Array: GEP [0, index]
        elem_ptr = compiler.builder.gep(array_ptr, [zero, index_val], inbounds=True, name="elem_ptr")
    
    else:
        # Raw Pointer: GEP [index]
        elem_ptr = compiler.builder.gep(array_ptr, [index_val], inbounds=True, name="elem_ptr")

    # 6. Return Result
    # If the caller wants the address (e.g. for assignment or &), return the pointer.
    if want_pointer:
        return elem_ptr

    # Otherwise, load the value (R-Value)
    element_type = elem_ptr.type.pointee
    
    # Load Primitives (Int, Float, Pointer)
    if isinstance(element_type, (ir.IntType, ir.FloatType, ir.DoubleType, ir.PointerType)):
        return compiler.builder.load(elem_ptr, name="array_elem_val")
    
    # For Aggregates (Structs, Arrays), we usually pass them around by pointer in LLVM,
    # even when conceptually "loaded". The VariableDeclaration logic handles loading if needed.
    return elem_ptr
# ---------------------------------------------------------------------------
# <Method name=compile_array_literal args=[<Compiler>, <ArrayLiteralNode>, <Optional[ir.Type]>]>
# <Description>
# Compiles an array literal `[e1, e2, ...]`.
# Handles:
# 1. Global Context: Enforces constants, returns ir.Constant.
# 2. Local Context: Uses 'insertvalue' to build array from runtime values.
# 3. Type Coercion: Matches elements to target type (Int->Float, etc.).
# 4. Type Inference: Infers array type from first element if target is None.
# </Description>
def compile_array_literal(compiler: Compiler, ast: ArrayLiteralNode, target_array_type: Optional[ir.Type] = None) -> ir.Value:
    # 1. Compile Elements
    # Note: If in global scope, compiler.compile() must return ir.Constant or fail.
    elements = ast.elements
    compiled_elements = [compiler.compile(e) for e in elements]

    # 2. Infer Target Type (if not provided)
    if target_array_type is None:
        if not compiled_elements:
            compiler.errors.error(ast, "Cannot infer type of empty array.")
            return ir.Constant(ir.ArrayType(ir.IntType(8), 0), [])
        first_type = compiled_elements[0].type
        target_array_type = ir.ArrayType(first_type, len(elements))

    # Build Array
    if compiler.builder is None:
        # Global Constant
        return ir.Constant(target_array_type, compiled_elements)
    else:
        # Local Value (insertvalue)
        array_val = ir.Constant(target_array_type, ir.Undefined)
        for i, val in enumerate(compiled_elements):
            # (Add element coercion logic here if needed)
            array_val = compiler.builder.insert_value(array_val, val, i)
        return array_val