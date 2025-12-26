from .essentials import *

# <Method name=_create_collection_from_array_literal args=[<Compiler>, <ir.Value>, <ir.Type>]>
# <Description>
# Converts a Static Array Value ([N x T]) into a Collection ({T*, N}).
# 1. Calculates exact element size using LLVM GEP arithmetic (handles padding correctly).
# 2. Allocates Heap Memory.
# 3. Copies elements from the static array to the heap.
# 4. Returns the Collection struct.
# </Description>
def create_collection_from_array_literal(compiler: Compiler, array_val: ir.Value, element_type: ir.Type) -> ir.Value:
    # 1. Validate Input
    if not isinstance(array_val.type, ir.ArrayType):
         compiler.errors.error(None, f"Internal Error: Expected ArrayType for collection init, got {array_val.type}")
         return array_val # Fallback
    
    count = array_val.type.count
    llvm_elem_type = array_val.type.element
    
    # 2. Calculate Size in Bytes (The Robust Way)
    # We use LLVM instructions to calculate sizeof(T) to handle Struct padding correctly.
    # Logic: sizeof(T) = ptrtoint( gep(T* null, 1) )
    
    if compiler.data_layout_obj:
        # Preferred: Use DataLayout if available (Compile-time constant)
        elem_size_int = llvm_elem_type.get_abi_size(compiler.data_layout_obj)
        elem_size = ir.Constant(ir.IntType(64), elem_size_int)
    else:
        # Fallback: Generate IR to calculate size (Runtime/Linktime constant)
        null_ptr = ir.Constant(llvm_elem_type.as_pointer(), None)
        one = ir.Constant(ir.IntType(32), 1)
        # GEP to index 1 gives the address offset equal to the size of the type
        gep_ptr = compiler.builder.gep(null_ptr, [one], name="sizeof_gep")
        elem_size = compiler.builder.ptrtoint(gep_ptr, ir.IntType(64), name="sizeof_int")
    
    # Total Size = Count * ElementSize
    count_val = ir.Constant(ir.IntType(64), count)
    total_size = compiler.builder.mul(count_val, elem_size, name="coll_size")
    
    # 3. Malloc
    try:
        malloc_fn = compiler.module.get_global("malloc")
    except KeyError:
        # Should be declared in __init__, but safety check
        malloc_ty = ir.FunctionType(ir.IntType(8).as_pointer(), [ir.IntType(64)])
        malloc_fn = ir.Function(compiler.module, malloc_ty, name="malloc")

    raw_ptr = compiler.builder.call(malloc_fn, [total_size], name="coll_malloc")
    
    # 4. Cast to Element Pointer (T*)
    data_ptr = compiler.builder.bitcast(raw_ptr, llvm_elem_type.as_pointer(), name="coll_data_ptr")
    
    # 5. Store Data (Copy Loop)
    # We extract from the array value and store into the heap pointer.
    # Note: For very large arrays, memcpy is better, but extract/store is safer for 
    # handling SSA values without needing an intermediate stack slot.
    
    for i in range(count):
        # Extract value from the [N x T] array
        val = compiler.builder.extract_value(array_val, i)
        
        # Calculate address in heap: data_ptr + i
        idx = ir.Constant(ir.IntType(32), i)
        dest_gep = compiler.builder.gep(data_ptr, [idx])
        
        # Store
        compiler.builder.store(val, dest_gep)

    # 6. Create Collection Struct { T*, i32 }
    # We use the specific struct type for this collection
    coll_type = ir.LiteralStructType([
        llvm_elem_type.as_pointer(), 
        ir.IntType(32)
    ])
    
    coll_val = ir.Constant(coll_type, ir.Undefined)
    coll_val = compiler.builder.insert_value(coll_val, data_ptr, 0)
    coll_val = compiler.builder.insert_value(coll_val, ir.Constant(ir.IntType(32), count), 1)
    
    return coll_val

# ---------------------------------------------------------------------------
# <Method name=compile_array_literal args=[<Compiler>, <ArrayLiteralNode>, <Optional[ir.Type]>]>
# <Description>
# Compiles an array literal `[e1, e2, ...]`.
# Handles:
# 1. Global Context: Enforces constants, returns ir.Constant.
# 2. Local Context: Uses 'insertvalue' to build array from runtime values.
# 3. Type Coercion: Matches elements to target type.
# 4. Type Inference: Infers array type from first element if target is None.
# </Description>
# def compile_array_literal(compiler: Compiler, ast: ArrayLiteralNode, target_array_type: Optional[ir.Type] = None) -> ir.Value:
#     elements = ast.elements
    
#     # 1. Compile Elements
#     # Note: If in global scope, compiler.compile() must return ir.Constant or fail.
#     compiled_elements = []
#     for elem in elements:
#         compiled_elements.append(compiler.compile(elem))

#     # 2. Infer Target Type (if not provided)
#     if target_array_type is None:
#         if not compiled_elements:
#             compiler.errors.error(ast, "Cannot infer type of empty array literal. Please specify type explicitly.")
#             # Return dummy to prevent crash
#             return ir.Constant(ir.ArrayType(ir.IntType(8), 0), [])
            
#         # Infer from first element
#         first_type = compiled_elements[0].type
#         target_array_type = ir.ArrayType(first_type, len(elements))

#     # 3. Validate Target Type
#     if not isinstance(target_array_type, ir.ArrayType):
#          compiler.errors.error(ast, f"Internal Error: Target type for array literal is not an array: {target_array_type}")
#          return compiled_elements[0] if compiled_elements else ir.Constant(ir.IntType(32), 0)

#     if len(elements) != target_array_type.count:
#         compiler.errors.error(ast, f"Array length mismatch. Expected {target_array_type.count}, got {len(elements)}.")

#     element_type = target_array_type.element
    
#     # 4. Global Context (Constants Only)
#     if compiler.builder is None:
#         final_consts = []
#         for i, val in enumerate(compiled_elements):
#             if not isinstance(val, ir.Constant):
#                 compiler.errors.error(ast.elements[i], "Global array elements must be compile-time constants.")
            
#             # Basic Coercion for Constants (Limited)
#             if val.type != element_type:
#                  # We can't emit instructions here. Types must match or be compatible constants.
#                  # LLVM python binding might handle some, but usually strict.
#                  if isinstance(element_type, ir.IntType) and isinstance(val.type, ir.IntType):
#                      # Python-side check? ir.Constant doesn't support cast methods easily without builder.
#                      # We rely on the user providing correct constants or simple mismatches.
#                      pass
#                  else:
#                      compiler.errors.error(ast.elements[i], f"Type mismatch in global array. Expected {element_type}, got {val.type}")
            
#             final_consts.append(val)
            
#         return ir.Constant(target_array_type, final_consts)

#     # 5. Local Context (Runtime Values)
#     # Start with an Undefined array value
#     array_val = ir.Constant(target_array_type, ir.Undefined)
    
#     for i, val in enumerate(compiled_elements):
#         # --- Coercion Logic ---
#         if val.type != element_type:
#             # Int -> Float
#             if isinstance(element_type, ir.FloatType) and isinstance(val.type, ir.IntType):
#                 val = compiler.builder.sitofp(val, element_type)
            
#             # Float -> Int
#             elif isinstance(element_type, ir.IntType) and isinstance(val.type, ir.FloatType):
#                 val = compiler.builder.fptosi(val, element_type)
            
#             # Int Width
#             elif isinstance(element_type, ir.IntType) and isinstance(val.type, ir.IntType):
#                 if val.type.width < element_type.width:
#                     val = compiler.builder.sext(val, element_type)
#                 elif val.type.width > element_type.width:
#                     val = compiler.builder.trunc(val, element_type)
            
#             # Pointers
#             elif isinstance(element_type, ir.PointerType) and isinstance(val.type, ir.PointerType):
#                 val = compiler.builder.bitcast(val, element_type)
            
#             # Check failure
#             if val.type != element_type:
#                  compiler.errors.error(ast.elements[i], f"Type mismatch for array element {i}. Expected {element_type}, got {val.type}")
        
#         # Insert value into the aggregate array
#         # insertvalue returns a NEW array value (SSA form)
#         array_val = compiler.builder.insert_value(array_val, val, i, name=f"arr_init_{i}")
        
#     return array_val
