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
# essentials.py : Easy import for the codegen frontend parts
from __future__ import annotations
from typing import Dict, List, Set, Optional, Union, Any, Tuple
from copy import deepcopy
import enum, uuid, re, os


# --- LLVM Imports ---
from llvmlite import ir
from llvmlite.binding import ffi, targets

# --- Internal Imports ---
from src.codegen.lib.intrinsics import IntrinsicLibrary
from src.codegen.lib.attributes import AttributeLibrary
from src.semantics.types import *
from src.semantics.scope import Scope
from src.ast2.nodes import *
from .compiletime.errors import ErrorHandler
from .helpers import *

DEBUG = False

# Constants
ERASURE_MARKERS = {"Castable", "Any", "Object", "VoidPointer"}

# --- Enums ---
class Visibility(str, enum.Enum):
    PUBLIC = "public"
    PRIVATE = "private"

# --- Forward Declarations ---
# We use Any for ModuleLoader to avoid circular dependency with utils
ModuleLoaderType = Any 

class Compiler:
    """
    The Central Compiler State.
    This class holds the LLVM Module, Builder, Symbol Tables, and Configuration.
    It acts as the 'Context' passed to all generation functions.
    """
    errors: ErrorHandler
    # =========================================================================
    # 1. LLVM Core State
    # =========================================================================
    module: ir.Module
    builder: Optional[ir.IRBuilder]
    target_triple: str
    target: ffi._lib_fn_wrapper
    target_machine: Optional[targets.TargetMachine]
    data_layout_obj: Optional[targets.TargetData]
    
    # =========================================================================
    # 2. Scopes & Execution State
    # =========================================================================
    global_scope: Scope
    current_scope: Scope
    block_count: int
    current_file_path: str
    
    # =========================================================================
    # 3. Type System Registries
    # =========================================================================
    
    # Maps Mangled Name -> LLVM Type (e.g. "lib_fin__MyStruct" -> %MyStruct)
    struct_types: Dict[str, ir.Type]
    
    # Maps Mangled Name -> { FieldName: Index }
    struct_field_indices: Dict[str, Dict[str, int]]
    
    # Maps Mangled Name -> { FieldName: DefaultValueAST }
    struct_field_defaults: Dict[str, Dict[str, Node]]
    
    # Maps Mangled Name -> { FieldName: Visibility }
    struct_field_visibility: Dict[str, Dict[str, Visibility]]
    
    # Maps Mangled Name -> { FieldName: TypeString } (Used for Unboxing logic)
    struct_field_types_registry: Dict[str, Dict[str, str]]
    
    # Maps Struct Name -> [GenericParamName] (e.g. "Box" -> ["T"])
    struct_generic_params_registry: Dict[str, List[str]]
    
    # Maps Struct Name -> [ParentAST] (For inheritance checks)
    struct_parents_registry: Dict[str, List[Any]]
    
    # Maps Mangled Name -> FilePath
    struct_origins: Dict[str, str]  
    
    # Maps Interface Name -> List[FunctionDeclaration] (For VTable generation)
    struct_methods: Dict[str, List[FunctionDeclaration]]
    
    # Maps Mangled Name -> { OperatorSymbol: MangledFuncName }
    struct_operators: Dict[str, Dict[str, str]]
    
    # Set of mangled names that are Interfaces (Fat Pointers)
    interfaces: Set[str]

    # =========================================================================
    # 4. Monomorphization & Generics
    # =========================================================================
    
    # Tracks compilation mode for a name: 'MONO', 'ERASED', 'STANDARD'
    modes: Dict[str, str]
    
    # AST Templates for Monomorphization (Saved but not compiled yet)
    struct_templates: Dict[str, StructDeclaration]
    function_templates: Dict[str, FunctionDeclaration]
    
    # Cache for instantiated generics to prevent re-compilation
    # Key: "Box_int", Value: LLVM Type
    mono_struct_cache: Dict[str, ir.Type]
    # Key: "swap_int", Value: LLVM Function
    mono_function_cache: Dict[str, ir.Function]

    # =========================================================================
    # 5. Functions & Context
    # =========================================================================
    
    # The current function being compiled (for appending blocks)
    function: Optional[ir.Function]
    
    # Maps Name -> AST (Used to look up default arguments)
    function_registry: Dict[str, FunctionDeclaration]
    
    # Maps Mangled Name -> Visibility
    function_visibility: Dict[str, str]
    
    # Maps Name -> FilePath (For error reporting/visibility checks)
    function_origins: Dict[str, str]
    
    # The 'main' entry point function
    main_function: Optional[ir.Function]

    # =========================================================================
    # 6. Modules & Imports
    # =========================================================================
    imported_libs: List[str]
    loaded_modules: Dict[str, Dict[str, Any]]
    module_aliases: Dict[str, str]
    module_loader: ModuleLoaderType
    module_struct_field_types: Dict[str, Dict[str, Dict[str, str]]]
    active_module_scopes: Dict[str, Scope]
    
    # Module-specific registries (for cross-module lookups)
    module_enum_types: Dict[str, Dict[str, ir.Type]]
    module_enum_members: Dict[str, Dict[str, ir.Constant]]
    module_struct_types: Dict[str, Dict[str, ir.Type]]
    module_struct_fields: Dict[str, Dict[str, Dict[str, int]]]
    module_struct_defaults: Dict[str, Dict[str, Dict[str, Node]]]
    module_struct_visibility: Dict[str, Dict[str, Dict[str, str]]]
    module_function_visibility: Dict[str, Dict[str, str]]

    # =========================================================================
    # 7. Runtime & RTTI
    # =========================================================================
    
    # Maps TypeName -> Integer ID (Legacy RTTI)
    type_codes: Dict[str, int]
    _next_type_code: int
    
    # Pre-identified types (char, etc.)
    identified_types: Dict[str, ir.Type]
    
    # Enums
    enum_types: Dict[str, ir.Type]
    enum_members: Dict[str, Dict[str, ir.Constant]]
    
    # String Interning
    global_strings: Dict[str, ir.Value]
    
    # Panic / Exit handlers
    exit_func: ir.Function
    panic_func: ir.Function
    panic_str_const: ir.Value
    
    # Macros
    macros: Dict[str, Tuple[List[str], List[AstNode]]]
    attributes_lib = AttributeLibrary(...)
    intrinsics_lib = IntrinsicLibrary(...)

    # =========================================================================
    # CORE METHODS (Signatures)
    # =========================================================================

    def compile(self, node: Node) -> Any:
        """Main dispatch loop. Compiles an AST node into LLVM IR."""
        ...

    def enter_scope(self, is_loop_scope: bool = False, loop_cond_block=None, loop_end_block=None) -> None:
        """Pushes a new scope onto the stack."""
        ...

    def exit_scope(self) -> None:
        """Pops the current scope."""
        ...
    
    def create_function(self, name:str, ret_type:Any, arg_types:List[Any]) -> ir.Function:
        """Creates an LLVM function and adds it to the module."""
        ...

    # --- Helper Accessors ---
    def get_mangled_name(self, name: str) -> str:
        """Generates a unique name based on the current file path."""
        ...

    def get_mono_mangled_name(self, base_name: str, type_args: List[Any]) -> str:
        """Generates a unique name for a monomorphized instance (e.g. Box_int)."""
        ...

    def classify_mode(self, ast_node: Node) -> str:
        """Returns 'MONO', 'ERASED', or 'STANDARD' based on generics/constraints."""
        ...

    # --- Type Helpers (src/codegen/types.py) ---
    def convert_type(self, type_node: Union[str, Node]) -> ir.Type:
        """Converts AST type representation to LLVM Type."""
        ...

    def ast_to_fin_type(self, node: Union[str, Node]) -> FinType:
        """Converts AST to High-Level FinType (for semantic checks)."""
        ...

    def ast_to_fin_type_pattern(self, node: Union[str, Node], generic_params_list: List[Any]) -> FinType:
        """Converts AST to FinType, treating specific names as Generics (for matching)."""
        ...

    def fin_type_to_llvm(self, fin_type: FinType) -> ir.Type:
        """Converts High-Level FinType back to LLVM Type."""
        ...

    def infer_fin_type_from_llvm(self, llvm_type: ir.Type) -> FinType:
        """Guesses FinType from a raw LLVM type."""
        ...
    
    def _substitute_type(compiler: Compiler, type_node: Union[str, Node], bindings: Dict[str, Any]) -> Union[str, Node]:
        """Substitutes generic type parameters in an AST type node based on bindings."""
        ...
    
    def _substitute_ast_types(compiler: Compiler, node: Node, bindings: Dict[str, Any]):
        """Recursively substitutes generic type parameters in an AST node based on bindings."""
        ...
    
    def legacy_convert_type(compiler:Compiler, type_name_or_node: Union[str, Node]) -> ir.Type:
        """Legacy type conversion function (used in helpers)."""
        ...

    def match_generic_types(self, concrete_type: FinType, generic_type: FinType, bindings: Dict[str, Any]) -> bool:
        """
        Recursively matches a Concrete Type against a Generic Pattern to solve for T.
        Populates 'bindings' dict.
        """
        ...
    
    def get_arg_fin_type(compiler: Compiler, ast_node: Node, compiled_val: Optional[ir.Value]) -> FinType:
        """
        Determines the High-Level Type (FinType) of an argument expression.
        Used during Function Call Type Inference to match arguments against templates.
        """
        ...
    
    def ast_to_fin_type_pattern(compiler: Compiler, node: Union[str, Node], generic_params_list: List[Any]) -> FinType:
        """Converts AST to FinType, treating specific names as Generics (for matching)."""
        ...

    # --- Variable Helpers (src/codegen/helpers.py) ---
    def create_variable_mut(self, name: str, var_type_ast_or_llvm: Any, initial_value_llvm: Optional[ir.Value] = None) -> ir.AllocaInstr:
        """Allocates a mutable stack variable and registers it in scope."""
        ...

    def create_variable_immut(self, name: str, var_type_ast_or_llvm: Any, initial_value_ast_or_llvm_const: Any) -> ir.GlobalVariable:
        """Creates a global constant."""
        ...

    def get_variable(self, name_or_node_ast: Union[str, Node]) -> ir.Value:
        """Resolves a variable/expression to an LLVM Value."""
        ...

    def set_variable(self, name: str, value_llvm: ir.Value) -> ir.Value:
        """Stores a value into a variable."""
        ...

    # --- Memory Helpers (src/codegen/helpers.py) ---
    def box_value(self, llvm_val: ir.Value, fin_type: FinType) -> ir.Value:
        """Allocates memory (malloc) and stores value (Boxing). Returns i8*."""
        ...

    def unbox_value(self, void_ptr: ir.Value, target_fin_type: FinType) -> ir.Value:
        """Casts i8* back to concrete type and loads it (Unboxing)."""
        ...

    def create_global_string(self, val: str) -> ir.Value:
        """Interns a string literal as a global constant."""
        ...

    # --- Struct Helpers (src/codegen/structs.py) ---
    def compile_struct(self, ast: StructDeclaration) -> None:
        """Compiles a struct definition."""
        ...

    def compile_struct_instantiation(self, node: StructInstantiation) -> ir.Value:
        """Compiles 'new Struct' or stack instantiation."""
        ...

    def compile_member_access(self, node: MemberAccess) -> ir.Value:
        """Compiles 'obj.member'."""
        ...

    def compile_struct_method(self, struct_name: str, struct_llvm_type: ir.Type, method_ast: FunctionDeclaration) -> None:
        """Compiles a method inside a struct."""
        ...

    def compile_operator(self, struct_name: str, mangled_struct_name: str, struct_llvm_type: ir.Type, op_ast: Any) -> None:
        """Compiles an operator overload."""
        ...

    def emit_operator_call(self, struct_name: str, op: str, left_val: ir.Value, right_val: Optional[ir.Value] = None) -> ir.Value:
        """Generates a call to an operator overload."""
        ...
        
    def compile_actual_method_call(self, ast: Any, struct_ptr: ir.PointerType):
        """Compiles a method call on a struct instance."""
        ...
    
    # --- Variables (src/codegen/variables.py) ---
    def create_variable_mut(
        self,
        name: str,
        var_type_ast_or_llvm: Any,
        initial_value_llvm: Optional[ir.Value] = None) -> ir.AllocaInstr:
        """
        Helper to declare and optionally initialize a mutable local variable.
        Handles Type Erasure (FinType registration) and Type Coercion.
        """
        ...
    def create_variable_immut(
        self,
        name: str,
        var_type_ast_or_llvm: Any,
        initial_value_ast_or_llvm_const: Any) -> ir.GlobalVariable:
        """
        Declare an immutable (constant) global variable in the module.
        The initial_value_ast_or_llvm_const MUST evaluate to a compile-time constant.
        """
        ...
    
    def guess_type(self, value):...

    def create_global_string(self, val: str) -> ir.Value:
        """Interns a string literal as a global constant."""
        ...
    
    def set_variable(self, name:str, value_llvm: ir.Value) -> ir.Value:
        """Stores a value into a variable."""
        ...
    
    def get_variable(self, name_or_node_ast: Union[str, Node]) -> ir.Value:
        """Resolves a variable/expression to an LLVM Value."""
        ...
    
    def compile_array_literal(compiler: Compiler, ast: ArrayLiteralNode, target_array_type: Optional[ir.Type] = None) -> ir.Value:
        """Compiles an array literal `[e1, e2, ...]`."""
        ...
    
    # --- Interface Helpers (src/codegen/interfaces.py) ---
    def compile_interface(self, ast: InterfaceDeclaration) -> None:
        """Compiles an interface definition."""
        ...

    def compile_struct_method_call(self, ast: Any) -> ir.Value:
        """Compiles a method call (Dynamic or Static dispatch)."""
        ...

    def pack_interface(self, struct_val: ir.Value, struct_type: ir.Type, interface_type: ir.Type) -> ir.Value:
        """Converts Struct* to Interface Fat Pointer."""
        ...
    
    def lookup_field_type_ast(self, struct_name: str, field_name: str)-> Optional[str]:
        """Finds the AST type node (e.g., "T" or "int") for a specific field."""
        ...
    
    def get_generic_params_of_struct(self, struct_name: str) -> List[str]:
        """Returns list of generic param names ['T', 'U'] for a struct."""
        ...
        
    def get_interface_type(self, interface_name)->ir.Type:
        """Returns the LLVM Type for an Interface Fat Pointer."""
        ...
    
    # --- Function Helpers (src/codegen/functions.py) ---
    def compile_function_declaration(self, ast: FunctionDeclaration) -> ir.Function:
        """Compiles a global function."""
        ...

    def compile_function_call(self, ast: Any) -> ir.Value:
        """Compiles a function call."""
        ...
    
    def instantiate_and_compile_generic(
        compiler: Compiler,
        func_name_str: str,
        generic_func_ast: FunctionDeclaration,
        inferred_bindings: Dict[str, Any], # Map T -> FinType/LLVMType
        concrete_types_tuple: Tuple[Any, ...]
    ) -> ir.Function:
        """
        Instantiates and compiles a generic function for specific type arguments.
        1. Generates a unique mangled name based on concrete types.
        2. Substitutes generic parameters in the AST with concrete types.
        3. Clones the AST and substitutes 'T' with concrete types.
        4. Compiles the new concrete function.
        """
        ...
        

    # --- Array Helpers (src/codegen/arrays.py) ---
    def create_collection_from_array_literal(self, array_val: ir.Value, element_type: ir.Type) -> ir.Value:
        """Converts static array to dynamic collection."""
        ...
    
    # --- Module Helpers (src/codegen/modules.py) ---
    def compile_and_import_file(self, abs_path: str, node: AstNode = None, targets: List[str] = None, alias: str = None) -> None:
        """Compiles and imports a module from a file path."""
        ...

    def compile_import(self, node: ImportModule) -> None:
        """Handles the 'import' statement AST node."""
        ...
    
    def compile_module_access(self, node: ModuleAccess) -> ir.Value:
        """Handles 'module_name.item_name' access."""
        ...
    
    def compile_import_c(self, node: ImportC) -> None:
        """Handles 'import cmodule' statement."""
        ...
    
    # --- General Helpers --- (src/codegen/helpers.py) ---
    def merge_scope(self, source_scope: Scope, targets: Optional[List[str]], alias: Optional[str]) -> None:
        """Merges symbols from source_scope into current_scope."""
        ...
    
    def get_mono_mangled_name(self, base_name:str, type_args:List[Any]) -> str:
        """Generates a unique name for a monomorphized instance (e.g. Box_int)."""
        ...
    
    def _emit_runtime_check_zero(self, compiler: Compiler, value_llvm: ir.Value, error_msg: str, node: Node = None):
        """
        Emits a runtime check that 'value_llvm' is not zero/null.
        If it is zero, calls the panic function with 'error_msg'.
        """
        ...
    
    def _is_parent_of(compiler: Compiler, parent_name: str, child_name: str) -> bool:
        """Checks if 'parent_name' is a parent of 'child_name' in the inheritance hierarchy."""
        ...

    
    
class AstNode(Node):...





class GenericCompilationMode(enum.Enum):
    ERASED = "ERASED"
    MONO = "MONO"
    STANDARD = "STANDARD"


class CONSTANTS:
    OPERATOR_SYMBOL_MAP = {
        '+': 'add', '-': 'sub', '*': 'mul', '/': 'div', '%': 'mod',
        '==': 'eq', '!=': 'neq', '<': 'lt', '>': 'gt', '<=': 'lte', '>=': 'gte',
        '&&': 'and', '||': 'or', '!': 'not'
    }
    ...

class CompilerException(Exception):
    """Custom exception for compiler errors."""
    pass