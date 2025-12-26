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


# <Method name=merge_scope args=[<Compiler>, <Scope>, <List[str]>, <str>]>
# <Description>
# Merges symbols AND types from a source scope into the current scope.
# Handles mangled name resolution for imported types.
# </Description>
def merge_scope(compiler: Compiler, source_scope: Scope, targets: List[str], source_path: str = None):
    """
    Takes symbols from source_scope and puts them into compiler.current_scope.
    Also resolves and imports Types (Structs) by creating aliases.
    """
    if not targets:
        return

    for name in targets:
        found = False

        # 1. Import Values (Functions, Variables)
        if name in source_scope.symbols:
            info = source_scope.symbols[name]
            try:
                compiler.current_scope.define(name, info.llvm_value, info.fin_type)
            except Exception as e:
                compiler.errors.error(None, f"Import conflict: '{name}' is already defined.", hint=str(e))
            found = True

        # 2. Import Types (Structs, Interfaces)
        # If it's not a value, check if it's a Type defined in the source module.
        elif source_path:
            # We need to reconstruct the Mangled Name as it was defined in the source file.
            # Hack: Temporarily switch context to source_path to use get_mangled_name logic correctly.
            
            current_path_backup = compiler.current_file_path
            compiler.current_file_path = source_path
            mangled_target = compiler.get_mangled_name(name)
            compiler.current_file_path = current_path_backup
            
            # Check if this mangled type exists in the global registry
            # AND verify it actually belongs to the source module (to avoid accidental global collisions)
            
            # Check Structs/Interfaces
            if mangled_target in compiler.struct_types:
                # It exists! Register an alias in the current scope.
                # "Vector" -> "lib_math__Vector"
                compiler.current_scope.define_type_alias(name, mangled_target)
                found = True
            
            # Check Enums (If enums are mangled similarly)
            # If enums are NOT mangled (raw names), we just check enum_types
            elif name in compiler.enum_types:
                # If it's global, we don't strictly need an alias, but good for consistency
                found = True

        if not found:
            compiler.errors.error(
                None, 
                f"Could not resolve '{name}' in imported module.", 
                hint=f"Ensure '{name}' is public and defined in '{source_path}'."
            )

# -------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# <Method name=guess_type args=[<Compiler>, <Any>]>
# <Description>
# Infers the LLVM Type corresponding to a Python literal value.
# Used primarily when compiling Literal nodes to determine the constant type.
# Handles:
# - Integers (i32, i64, i128 based on magnitude)
# - Floats (float)
# - Strings (i8*)
# - Booleans (i1)
# </Description>
def guess_type(compiler: Compiler, value: Any) -> ir.Type:
    if isinstance(value, int):
        # Determine width based on value magnitude
        if -(2**31) <= value < 2**31:
            return ir.IntType(32)
        elif -(2**63) <= value < 2**63:
            return ir.IntType(64)
        elif -(2**127) <= value < 2**127:
            return ir.IntType(128)
        else:
            raise CompilerException(
                f"Integer literal {value} is too large for supported integer types (max i128)."
            )

    elif isinstance(value, float):
        return ir.FloatType()

    elif isinstance(value, str):
        # Strings are pointers to char arrays (i8*)
        return ir.IntType(8).as_pointer()

    elif isinstance(value, bool):
        return ir.IntType(1)

    else:
        raise CompilerException(
            f"Cannot guess LLVM type for Python value '{value}' of type {type(value)}."
        )

# ---------------------------------------------------------------------------
# <Method name=create_global_string args=[<Compiler>, <str>]>
# <Description>
# Interns a string literal as a global constant in the LLVM module.
# 1. Checks the cache to avoid duplicating identical strings.
# 2. Creates a global array constant [N x i8] with the string data + null terminator.
# 3. Returns a pointer (i8*) to the start of the array using a Constant GEP.
# </Description>
def create_global_string(compiler: Compiler, val: str) -> ir.Value:
    # 1. Check Cache
    if val in compiler.global_strings:
        return compiler.global_strings[val]

    # 2. Encode String
    # Convert to UTF-8 bytes and add null terminator
    bytes_ = bytearray(val.encode("utf8")) + b"\00"
    str_ty = ir.ArrayType(ir.IntType(8), len(bytes_))

    # 3. Create Global Variable
    # Use a unique name to prevent collisions in LLVM IR
    uniq = uuid.uuid4().hex[:8]
    name = f".str_{uniq}"

    gvar = ir.GlobalVariable(compiler.module, str_ty, name=name)
    gvar.linkage = "internal"
    gvar.global_constant = True
    gvar.initializer = ir.Constant(str_ty, bytes_)

    # 4. Create Pointer (i8*)
    # Use Constant Expression GEP. This works everywhere (even in __init__ or globals).
    # It does not require compiler.builder to be active.
    zero = ir.Constant(ir.IntType(32), 0)
    str_ptr = gvar.gep([zero, zero]) 

    # 5. Cache and Return
    compiler.global_strings[val] = str_ptr
    return str_ptr

# -------------------------------------------------------------------------
# <Method name=get_mangled_name args=[<Compiler>, <str>]>
# <Description>
# Generates a unique name for a symbol based on the current file path.
# Prevents name collisions between modules.
# Logic:
# 1. 'main' is never mangled.
# 2. Symbols in the entrypoint file are NOT mangled (optional, for cleaner IR).
# 3. Others are prefixed with the relative path: "modules/math.fin" -> "modules_math__func"
# </Description>
def get_mangled_name(compiler: Compiler, name: str) -> str:
    # 1. Don't mangle 'main' (Entry Point)
    if name == "main": 
        return "main"
    
    # 2. Don't mangle externs (C functions)
    # We check if the name exists in the global scope as an external function
    # (This relies on externs being declared before use, which is standard)
    if name in compiler.global_scope.symbols:
        sym = compiler.global_scope.resolve(name)
        if isinstance(sym, ir.Function) and sym.linkage == 'external':
            return name

    # 3. Mangle based on file path
    # If we are in the root file (entrypoint), we might choose not to mangle 
    # to keep the output cleaner, OR we mangle to avoid conflict with C libs.
    # Let's mangle everything except main to be safe.
    
    if not compiler.current_file_path:
        return name # Fallback if path is missing (e.g. REPL)

    # Calculate relative path from project root
    try:
        rel_path = os.path.relpath(compiler.current_file_path, compiler.module_loader.root_dir)
    except ValueError:
        # If paths are on different drives (Windows), relpath fails. Use basename.
        rel_path = os.path.basename(compiler.current_file_path)

    # Sanitize path: "src/utils/math.fin" -> "src_utils_math"
    # Remove extension
    rel_path = os.path.splitext(rel_path)[0]
    
    # Replace non-alphanumeric characters with underscore
    safe_path = re.sub(r'[^a-zA-Z0-9_]', '_', rel_path)
    
    # Prevent double underscores if path ends with one
    if safe_path.endswith('_'):
        safe_path = safe_path[:-1]

    return f"{safe_path}__{name}"

# ---------------------------------------------------------------------------
# <Method name=get_mono_mangled_name args=[<str>, <List[Any]>]>
# <Description>
# Generates unique name for Mono instantiation: Box<int> -> Box_int
# </Description>
def get_mono_mangled_name(self, base_name:str, type_args:List[Any]) -> str:
    """Generates unique name for Mono instantiation: Box<int> -> Box_int"""
    # Simple mangling: join type names
    # We need to convert type_args (AST nodes) to strings
    flat_args = []
    for arg in type_args:
        if isinstance(arg, str): flat_args.append(arg)
        elif hasattr(arg, 'name'): flat_args.append(arg.name) # TypeAnnotation/Struct
        else: flat_args.append(str(arg).replace(" ", "").replace("<", "_").replace(">", ""))
        
    suffix = "_".join(flat_args)
    return f"{base_name}_{suffix}"

# -------------------------------------------------------------------------
# <Method name=classify_mode args=[<Compiler>, <Node>]>
# <Description>
# Decides the compilation strategy.
# Returns: 'MONO', 'ERASED', or 'STANDARD'.
# </Description>
def classify_mode(compiler: Compiler, ast_node: Node) -> str:
    """
    Decides the compilation strategy.
    Returns: 'MONO', 'ERASED', or 'STANDARD'.
    """
    params = getattr(ast_node, 'generic_params', None) or \
             getattr(ast_node, 'type_parameters', None)
    
    if not params:
        return 'STANDARD'
        
    # Check constraints
    for param in params:
        # param is GenericParam object
        if param.constraint:
            # Handle complex constraints (e.g. T: List<int>) by converting to string
            # or checking base name.
            c_str = str(param.constraint)
            
            # If ANY param is marked for erasure, the whole function is compiled as ERASED.
            # (Mixing Mono and Erased in one function is complex, defaulting to Erased is safer)
            if c_str in ERASURE_MARKERS:
                return 'ERASED'
                
    # If generics exist but no erasure markers, default to Monomorphization
    return 'MONO'

# -------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# <Method name=box_value args=[<Compiler>, <ir.Value>, <FinType>]>
# <Description>
# Converts a concrete LLVM value into a generic i8* (void*).
# Used for Type Erasure (assigning int to T).
#
# Strategy:
# 1. Value Types (Int, Float, Struct): Allocate heap memory (malloc), store value, return pointer.
# 2. Reference Types (String, Pointer): Just bitcast to i8*.
# </Description>
def box_value(compiler: Compiler, llvm_val: ir.Value, fin_type: FinType) -> ir.Value:
    void_ptr_ty = ir.IntType(8).as_pointer()

    # --- Strategy 1: Value Types (Allocate & Store) ---
    # Primitives (except string) and Structs need to be boxed on the heap.
    if (isinstance(fin_type, PrimitiveType) and fin_type.name != "string") or \
       isinstance(fin_type, StructType):
        
        # 1. Calculate Size
        # We need the size of the VALUE type.
        # If llvm_val is a pointer, we need the size of the element it points to.
        
        value_type = llvm_val.type
        
        # Handle Pointer to Struct/Collection
        if isinstance(llvm_val.type, ir.PointerType):
            value_type = llvm_val.type.pointee
        
        if isinstance(llvm_val.type, ir.PointerType):
            # If we are boxing a Struct, llvm_val is likely Struct*.
            # We want size of Struct.
            value_type = llvm_val.type.pointee
        
        if compiler.data_layout_obj:
            size_int = value_type.get_abi_size(compiler.data_layout_obj)
            size_arg = ir.Constant(ir.IntType(64), size_int)
        else:
            null_ptr = ir.Constant(value_type.as_pointer(), None)
            one = ir.Constant(ir.IntType(32), 1)
            gep_ptr = compiler.builder.gep(null_ptr, [one], name="box_sizeof_gep")
            size_arg = compiler.builder.ptrtoint(gep_ptr, ir.IntType(64), name="box_sizeof_int")

        # 2. Malloc
        try:
            malloc_fn = compiler.module.get_global("malloc")
        except KeyError:
            malloc_ty = ir.FunctionType(ir.IntType(8).as_pointer(), [ir.IntType(64)])
            malloc_fn = ir.Function(compiler.module, malloc_ty, name="malloc")

        raw_ptr = compiler.builder.call(malloc_fn, [size_arg], name="box_malloc")
        
        # 3. Store Value
        # Cast i8* -> T*
        typed_ptr = compiler.builder.bitcast(raw_ptr, value_type.as_pointer(), name="box_typed_ptr")
        
        # [FIX] Handle Pointer vs Value
        if isinstance(llvm_val.type, ir.PointerType) and llvm_val.type.pointee == value_type:
            # llvm_val is T*. We need to copy *llvm_val to *typed_ptr.
            # Load and Store (Memcpy equivalent)
            val = compiler.builder.load(llvm_val, name="box_load")
            compiler.builder.store(val, typed_ptr)
        else:
            # llvm_val is T. Store directly.
            compiler.builder.store(llvm_val, typed_ptr)
        
        return raw_ptr

    # --- Strategy 2: Reference Types (Bitcast) ---
    
    # Strings are already i8* (or similar)
    elif isinstance(fin_type, PrimitiveType) and fin_type.name == "string":
        if llvm_val.type != void_ptr_ty:
            return compiler.builder.bitcast(llvm_val, void_ptr_ty, name="box_str_cast")
        return llvm_val 

    # Pointers and other Generics
    else:
        if llvm_val.type == void_ptr_ty:
            return llvm_val
        return compiler.builder.bitcast(llvm_val, void_ptr_ty, name="box_ptr_cast")
# ---------------------------------------------------------------------------
# <Method name=unbox_value args=[<Compiler>, <ir.Value>, <FinType>]>
# <Description>
# Converts a generic i8* back to a concrete LLVM value.
# Used when accessing a generic field (T) as a concrete type (int).
#
# Strategy:
# 1. Reference Types: Bitcast i8* -> T*.
# 2. Value Types: Bitcast i8* -> T*, then LOAD T.
# </Description>
def unbox_value(compiler: Compiler, void_ptr: ir.Value, target_fin_type: FinType) -> ir.Value:
    target_llvm_type = compiler.fin_type_to_llvm(target_fin_type)

    # Safety Check
    if not isinstance(void_ptr.type, ir.PointerType):
        raise CompilerException(f"Internal Error: Cannot unbox non-pointer type: {void_ptr.type}")

    # --- Strategy 1: Reference Types (Direct Bitcast) ---
    # Strings, Pointers, and Generic Params are just re-interpreted.
    if isinstance(target_fin_type, PointerType) or \
       (isinstance(target_fin_type, PrimitiveType) and target_fin_type.name == "string") or \
       isinstance(target_fin_type, GenericParamType):
        
        if void_ptr.type == target_llvm_type:
            return void_ptr
        
        return compiler.builder.bitcast(void_ptr, target_llvm_type, name="unbox_ref_cast")

    # --- Strategy 2: Value Types (Cast Pointer & Load) ---
    # Primitives (int, float), Structs, and Collections.
    # These were malloc'd, so void_ptr is the address of the data.
    else:
        # 1. Cast i8* -> T*
        typed_ptr = compiler.builder.bitcast(void_ptr, target_llvm_type.as_pointer(), name="unbox_val_ptr")
        
        # 2. Load T
        return compiler.builder.load(typed_ptr, name="unbox_val_load")

# ---------------------------------------------------------------------------
# -------------------------------------------------------------------------
# <Method name=_emit_runtime_check_zero args=[<Compiler>, <ir.Value>, <str>, <Node>]>
# <Description>
# Injects a safety check for zero values (Division/Modulo).
# Optimizations:
# 1. Compile-Time: If value is a non-zero Constant, emit NOTHING (Zero cost).
# 2. Compile-Time: If value is zero Constant, raise Compile Error.
# 3. Runtime: If value is dynamic, emit check with "Unlikely" branch weights.
# </Description>
def _emit_runtime_check_zero(self, compiler: Compiler, value_llvm: ir.Value, error_msg: str, node: Node = None):
    # Only check integers
    if not isinstance(value_llvm.type, ir.IntType):
        return

    # --- OPTIMIZATION 1: Compile-Time Constant Folding ---
    # If the value is a literal constant (e.g. 5, 100), we check it now.
    if isinstance(value_llvm, ir.Constant):
        # Get the actual python value
        const_val = value_llvm.constant
        
        if const_val == 0:
            # We caught a division by zero at compile time!
            compiler.errors.error(node, "Division by zero detected at compile-time.", hint="Change the divisor to a non-zero value.")
            return
        else:
            # It is a constant and it is NOT zero.
            # We don't need a runtime check. Do nothing.
            return

    # --- OPTIMIZATION 2: Runtime Check with Branch Prediction ---
    # If we are here, the value is dynamic (variable). We must check it.
    
    # Create blocks
    panic_block = compiler.function.append_basic_block("panic_check")
    safe_block = compiler.function.append_basic_block("safe_cont")
    
    # Compare with 0
    zero = ir.Constant(value_llvm.type, 0)
    is_zero = compiler.builder.icmp_signed("==", value_llvm, zero, name="is_zero")
    
    # Branch with Weights
    # We tell LLVM: "It is 10,000 times more likely to go to safe_block than panic_block"
    # This allows the CPU to speculatively execute the safe path without stalling.
    compiler.builder.cbranch(is_zero, panic_block, safe_block, weights=[1, 10000])
    
    # Get panic function
    try:
        panic_fn = compiler.module.get_global("__panic")
    except KeyError:
        # If builtins.fin wasn't loaded, we can't panic safely.
        # Fallback: Just trap/abort
        # compiler.builder.asm(ir.FunctionType(ir.VoidType(), []), "ud2", "", [], True)
        # Better: Raise compiler error that builtins are missing
        raise CompilerException("Runtime Error: '__panic' function not found. Ensure builtins are loaded.")
    
    # -- Panic Block --
    compiler.builder.position_at_end(panic_block)
    msg_ptr = compiler.create_global_string(error_msg)
    compiler.builder.call(panic_fn, [msg_ptr])
    compiler.builder.unreachable()
    
    # -- Safe Block --
    compiler.builder.position_at_end(safe_block)

# -------------------------------------------------------------------------

# -------------------------------------------------------------------------
# Scope Management
# -------------------------------------------------------------------------
def enter_scope(self, is_loop_scope: bool = False, loop_cond_block=None, loop_end_block=None):
    """
    Pushes a new scope onto the stack.
    """
    self.current_scope = Scope(
        parent=self.current_scope,
        is_loop_scope=is_loop_scope,
        loop_cond_block=loop_cond_block,
        loop_end_block=loop_end_block
    )

def exit_scope(self):
    """
    Pops the current scope from the stack.
    """
    if self.current_scope.parent is None:
        # We are at the global scope. Should not exit further.
        # This might happen during error recovery or end of compilation.
        print("Warning: Attempting to exit global scope or program compilation finished.")
        
        # Safety fallback to ensure we stay at global
        if self.current_scope is not self.global_scope:
            self.current_scope = self.global_scope
        return

    self.current_scope = self.current_scope.parent

def _get_scope_depth(self) -> int:
    """
    Returns the current nesting depth (0 = Global).
    Useful for debugging scope issues.
    """
    depth = 0
    s = self.current_scope
    while s.parent: # Stop at global (which has parent=None)
        depth += 1
        s = s.parent
    return depth

def is_any_type(compiler: Compiler, llvm_type: ir.Type) -> bool:
    """Checks if a type is the 'Any' struct."""
    try:
        real_any = compiler.convert_type("any")
        return llvm_type == real_any
    except:
        return False
# -------------------------------------------------------------------------
# <Method name=pack_any args=[<Compiler>, <ir.Value>, <FinType>]>
def pack_any(compiler: Compiler, val: ir.Value, val_fin_type: FinType) -> ir.Value:
    """Boxes a value into the 'Any' struct."""
    # 1. Box value to i8*
    boxed_ptr = compiler.box_value(val, val_fin_type)
    
    # 2. Get Type ID
    type_id = val_fin_type.type_id
    type_id_val = ir.Constant(ir.IntType(64), type_id)
    
    # 3. Get 'Any' Struct Type
    any_ty = compiler.convert_type("any")
    
    # 4. Create Struct
    any_val = ir.Constant(any_ty, ir.Undefined)
    any_val = compiler.builder.insert_value(any_val, boxed_ptr, 0)
    any_val = compiler.builder.insert_value(any_val, type_id_val, 1)
    
    return any_val