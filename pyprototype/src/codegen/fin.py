# -*- coding: utf-8 -*-
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
""" Fin Language Compiler - Code Generation Module """
import ctypes
from ctypes.util import find_library
from src.preprocessor.macros import substitute
from src.utils.helpers import resolve_c_library, parse_code, parse_file,run_experimental_mode
from src.utils.module_loader import ModuleLoader
from .essentials import *
from llvmlite import binding
from .compiletime.errors import ErrorHandler
## --- NOTES ---
# NOTE: Removed ImportC -> No longer supported

# --- Production
# Access
from .prod.access import compile_qualified_access
# Literal
from .prod.literals import compile_literal
# Variable & Operators
from .prod.vars import compile_variable_declaration,compile_parameter
from .prod.ops import compile_assignment, compile_additive, compile_comparison, compile_logical, compile_multiplicative, compile_postfix, compile_unary
# Function
from .prod.funcs import compile_function_declaration, compile_return, compile_lambda, compile_define
# Pointer
from .prod.memory import compile_address_of, compile_dereference, compile_as_ptr, compile_sizeof, compile_new, compile_delete
# Array
from .prod.arrays import compile_array_index, compile_array_literal
# Error Handling
from .prod.flow import compile_try_catch, compile_blame, compile_foreach
# Metaprogramming
from .prod.metaprogramming import compile_special_call, compile_special_declaration
# Macros
from .prod.macros import compile_macro_declaration, compile_macro_call
# --- Codegen ---
# Module
from .modules import compile_module_access, compile_import
# Types
from .types import compile_type_conv, compile_typeof
# Flow
from .flow import compile_if, compile_while, compile_for, compile_control_statement
# Structs
from .structs import compile_struct, compile_struct_instantiation, compile_member_access, compile_struct_method_call
# Enums
from .enums import compile_enum_declaration, compile_enum_access_ast

# Error Handling
import traceback
from .compiletime.errors import ErrorHandler

# -------------------------------------------
# Compiler Components Import
from .helpers import (enter_scope, exit_scope, get_mangled_name, classify_mode, 
get_mono_mangled_name, box_value, unbox_value, is_any_type,
merge_scope, _emit_runtime_check_zero, pack_any,)
from .variables import create_variable_mut, create_variable_immut, get_variable, set_variable, guess_type, create_global_string
from .functions import create_function, compile_function_call, instantiate_and_compile_generic
from .types import (convert_type, ast_to_fin_type, ast_to_fin_type_pattern, 
fin_type_to_llvm, get_arg_fin_type, compile_typeof, compile_type_conv, 
infer_fin_type_from_llvm, match_generic_types, _substitute_type, 
_substitute_ast_types, fin_type_to_ast)
from .structs import (compile_struct, compile_member_access,
compile_struct_method, compile_operator,emit_operator_call,
compile_struct_instantiation,lookup_field_type_ast,
get_generic_params_of_struct, compile_struct_field_access,
allocate_and_init_struct, compile_actual_method_call)
from .interfaces import (compile_interface,
pack_interface, get_interface_type)
from .arrays import create_collection_from_array_literal
from .modules import (compile_and_import_file,
compile_import,compile_module_access)
# -------------------------------------------

def _debug_getattr(self, name):
    if name == 'pointee':
        print(f"\n[FATAL DEBUG] Accessed .pointee on IdentifiedStructType: {self}")
        print("Stack Trace of the crash:")
        traceback.print_stack()
        raise AttributeError(f"'IdentifiedStructType' object {self} has no attribute 'pointee'")
    raise AttributeError(f"'{type(self).__name__}' object has no attribute '{name}'")

ir.IdentifiedStructType.__getattr__ = _debug_getattr

def safe_pointee(obj):
    """Safely get .pointee, raising a clear error if missing."""
    # If it's a Value, get its type first
    if hasattr(obj, 'type') and not isinstance(obj, ir.Type):
        obj = obj.type
        
    if hasattr(obj, 'pointee'):
        return obj.pointee
    
    # If we are here, it's a crash. Let's describe why.
    raise Exception(f"[Internal Error] Expected a PointerType, but got '{obj}' (Type: {type(obj)}). This object does not have a 'pointee' attribute.")

# --- DEBUG HELPER ---
def debug_type(val, label):
    try:
        t = val.type
        print(f"[DEBUG] {label}: Value={val} | Type={t} | TypeType={type(t)}")
        if isinstance(t, ir.PointerType):
            print(f"        -> Pointee: {t.pointee}")
    except:
        print(f"[DEBUG] {label}: Could not inspect type of {val}")

class FinCompiler:
    # =========================================================================
    # CORE METHODS (Signatures)
    # =========================================================================

    enter_scope = enter_scope

    exit_scope = exit_scope
    
    create_function = create_function

    # --- Helper Accessors ---
    get_mangled_name = get_mangled_name

    get_mono_mangled_name = get_mono_mangled_name

    classify_mode = classify_mode
    
    pack_any = pack_any

    # --- Type Helpers (src/codegen/types.py) ---
    convert_type = convert_type

    ast_to_fin_type = ast_to_fin_type

    ast_to_fin_type_pattern = ast_to_fin_type_pattern

    fin_type_to_llvm = fin_type_to_llvm

    infer_fin_type_from_llvm = infer_fin_type_from_llvm
    
    _substitute_type = _substitute_type
    
    _substitute_ast_types = _substitute_ast_types
    

    match_generic_types = match_generic_types

    get_arg_fin_type = get_arg_fin_type

    ast_to_fin_type_pattern = ast_to_fin_type_pattern
    
    is_any_type = is_any_type

    # --- Variable Helpers (src/codegen/helpers.py) ---
    create_variable_mut = create_variable_mut
    create_variable_immut = create_variable_immut
    get_variable = get_variable
    set_variable = set_variable
    guess_type = guess_type

    # --- Memory Helpers (src/codegen/helpers.py) ---
    box_value = box_value
    unbox_value = unbox_value

    create_global_string = create_global_string

    # --- Struct Helpers (src/codegen/structs.py) ---
    compile_struct = compile_struct
    compile_struct_instantiation = compile_struct_instantiation
    compile_member_access = compile_member_access
    compile_struct_method = compile_struct_method
    compile_operator = compile_operator
    emit_operator_call = emit_operator_call
    compile_struct_method_call = compile_struct_method_call
    compile_struct_field_access = compile_struct_field_access
    allocate_and_init_struct = allocate_and_init_struct
    compile_actual_method_call = compile_actual_method_call
    
    # --- Interface Helpers (src/codegen/interfaces.py) ---
    compile_interface = compile_interface
    pack_interface = pack_interface
    lookup_field_type_ast = lookup_field_type_ast
    get_generic_params_of_struct = get_generic_params_of_struct
    get_interface_type = get_interface_type
    
    # --- Function Helpers (src/codegen/functions.py) ---
    compile_function_declaration = compile_function_declaration
    compile_function_call = compile_function_call
    instantiate_and_compile_generic = instantiate_and_compile_generic   

    # --- Array Helpers (src/codegen/arrays.py) ---
    create_collection_from_array_literal = create_collection_from_array_literal
    
    # --- Module Helpers (src/codegen/modules.py) ---
    compile_and_import_file = compile_and_import_file
    compile_import = compile_import
    compile_module_access = compile_module_access

    
    # --- General Helpers --- (src/codegen/helpers.py) ---
    merge_scope = merge_scope
    get_mono_mangled_name = get_mono_mangled_name
    _emit_runtime_check_zero = _emit_runtime_check_zero

    def fin_type_to_ast(self, fin_type):
        return fin_type_to_ast(fin_type)
    
    def __init__(self, source_code:str,file_name:str,opt=None, codemodel=None, is_jit=False, module_loader=None, initial_file_path=None):
        # Initialize ErrorHandler
        self.errors = ErrorHandler(source_code, file_name)
        # Initialize binding
        binding.initialize_native_target()
        binding.initialize_native_asmprinter()

        # ------------------ Declare module ---------------------
        self.module = ir.Module(name="fin_module")
        
        #--------- Runner check -------
        try:
            # Get target triple
            self.target_triple = binding.get_default_triple()
            self.target = binding.Target.from_triple(self.target_triple)
            if is_jit:
                self.target_machine = self.target.create_target_machine()
            else:
                self.target_machine = self.target.create_target_machine(
                    reloc="static",
                    codemodel="default" if not codemodel else codemodel,
                    opt=0 if not opt else opt,
                )

            self.data_layout_obj = self.target_machine.target_data

            self.module.triple = self.target_triple
            self.module.data_layout = str(self.data_layout_obj)

        except RuntimeError as e:
            self.errors.error(None,f"Fatal Error: Failed to initialize LLVM target information: {e}")
            print(
                "[INFO] Please ensure LLVM is correctly installed and configured for your system."
            )

            self.target_machine = None
            self.data_layout_obj = None
            raise
        #-------------------------------
        
        # Setup necessary variables 
        self.builder = None # The builder constantly changes: IMPORTANT
        self.modes = {}
        # ---------------- Scopes & Execution State ------------------
        self.global_scope = Scope(parent=None)
        self.current_scope = self.global_scope
        self.block_count = 0
        self.current_file_path = initial_file_path

        # =========================================================================
        # Type System Registries
        # =========================================================================
        
        # ------------ Structs & Interfaces ------------
        self.struct_types: Dict[str, ir.Type] = {}
        self.struct_field_indices: Dict[str, Dict[str, int]] = {}
        self.struct_field_defaults: Dict[str, Dict[str, Node]] = {} # Maps 'MangledStructName' -> { 'field_name': DefaultValueAST }
        self.struct_field_visibility: Dict[str, Dict[str, Visibility]] = {} # Stores { 'StructName': { 'field_name': 'public' } }
        self.struct_field_types_registry: Dict[str, Dict[str, str]] = {} # { 'Box': {'val': 'T'} }
        self.struct_generic_params_registry: Dict[str, List[str]] = {} # { 'Box': ['T'] }
        self.struct_parents_registry: Dict[str, List[Any]] = {} # Registry to track inheritance (Child -> [Parents])
        self.struct_origins: Dict[str, str]   = {} # Maps 'StructName' -> '/abs/path/to/defining_file.fin'
        self.struct_methods: Dict[str, List[FunctionDeclaration]] = {}
        self.struct_operators: Dict[str, Dict[str, str]] = {}
        self.instantiated_structs: Dict[str, ir.Type] = {} # 'Vector_int': ir.Type
        self.current_struct_name: str = None # For 'Self' resolution
        self.current_struct_type: ir.Type = None # For 'Self' resolution
        self.inheritance_map: Dict[str, List[str]] = {} # Child -> [Parents]
        self.interfaces: Set[str] = {}
        # ---------------------------------
        # --------------- Enums ------------
        self.enum_types: Dict[str, ir.Type] = {}
        self.enum_members: Dict[str, List[str]] = {}
        # ---------------------------------
        # =========================================================================
        # Monomorphization & Generics
        # =========================================================================
        self.modes: Dict[str, str] = {}
        self.struct_templates: Dict[str, StructDeclaration] = {} # 'Vector': StructDeclarationAST
        self.function_templates: Dict[str, FunctionDeclaration] = {} # 'swap': FunctionDeclarationAST
        # Cache for instantiated generics to prevent re-compilation
        # Key: "Box_int", Value: LLVM Type
        self.mono_struct_cache: Dict[str, ir.Type] = {}
        # Key: "swap_int", Value: LLVM Function
        self.mono_function_cache: Dict[str, ir.Function] = {}
        # =========================================================================
        # Functions & Context
        # =========================================================================
        self.function: Optional[ir.Function] = None
        self.function_registry: Dict[str, FunctionDeclaration] = {}
        self.function_visibility: Dict[str, str] = {}
        self.function_origins: Dict[str, str] = {}
        self.main_function: Optional[ir.Function] = None
        self.current_function: Optional[ir.FunctionType] = None
        # ---------- Strings -------------
        self.global_strings: Dict[str, ir.GlobalVariable] = {}
        # --------------------------------
        # =========================================================================
        # Modules & Imports
        # =========================================================================
        self.imported_libs: List[str]  = []
        self.loaded_modules: Dict[str, Dict[str, Any]] = {}
        self.module_aliases: Dict[str, str] = {}
        self.module_loader: ModuleLoaderType = None
        self.module_struct_field_types: Dict[str, Dict[str, Dict[str, str]]] = {}
        self.active_module_scopes: Dict[str, Scope] = {}
        # Module-specific registries (for cross-module lookups)
        self.module_enum_types: Dict[str, Dict[str, ir.Type]] = {}
        self.module_enum_members: Dict[str, Dict[str, ir.Constant]] = {}
        self.module_struct_types: Dict[str, Dict[str, ir.Type]] = {}
        self.module_struct_fields: Dict[str, Dict[str, Dict[str, int]]] = {}
        self.module_struct_defaults: Dict[str, Dict[str, Dict[str, Node]]] = {}
        self.module_struct_visibility: Dict[str, Dict[str, Dict[str, str]]] = {}
        self.module_function_visibility: Dict[str, Dict[str, str]] = {}
        # -------------- Module loader ------------------
        if module_loader is None:
            self.errors.error(None,"Compiler requires a ModuleLoader instance.")
        self.module_loader = module_loader
        
        # ------------- Macros ------------
        self.macros = {}
        # --------------------------------


        # --- RUNTIME ERROR HANDLING SETUP ---
        #panic_fmt = "\n\033[1;31mFin Panicked:\033[0m %s\n"
        
        # ------------------------------------
        # =========================================================================
        # Compiler API LIBRARIES
        # =========================================================================
        self.attributes_lib = AttributeLibrary(self)
        self.intrinsics_lib = IntrinsicLibrary(self)    
        self.main_function = None

    def load_library(self, lib_path:str):
        with open(lib_path, 'r', encoding='utf-8') as f:
            lib_code = f.read()
            lib_name = lib_path.split('/')[-1]
            parsed = parse_code(lib_code, lib_name)
            self.compile(parsed.statements)

    def compile(self, ast: Node):
        if ast is None:
            return
        
        # Variable Declaration
        if isinstance(ast, VariableDeclaration):
            return compile_variable_declaration(self, ast)
        elif isinstance(ast, Parameter):
            return compile_parameter(self, ast)
        elif isinstance(ast, Assignment):
            return compile_assignment(self, ast)
        # Functions
        elif isinstance(ast, FunctionDeclaration):
            return compile_function_declaration(self, ast)
        elif isinstance(ast, FunctionCall):
            return compile_function_call(self, ast)
        elif isinstance(ast, ReturnStatement):
            return compile_return(self, ast)
        # Pointers & Memory
        elif isinstance(ast, NewExpressionNode):
            return compile_new(self, ast)
        elif isinstance(ast, DeleteStatementNode):
            return compile_delete(self, ast)
        elif isinstance(ast, AddressOfNode):
            return compile_address_of(self, ast)
        elif isinstance(ast, DereferenceNode):
            return compile_dereference(self, ast)
        elif isinstance(ast, AsPtrNode):
            return compile_as_ptr(self, ast)
        elif isinstance(ast, SizeofNode):
            return compile_sizeof(self, ast)
        # Arrays
        elif isinstance(ast, ArrayIndexNode):
            return compile_array_index(self, ast)
        elif isinstance(ast, ArrayLiteralNode):
            return compile_array_literal(self, ast)
        # Modules & Definitions
        elif isinstance(ast, ImportModule):
            return compile_import(self, ast)
        elif isinstance(ast, ModuleAccess):
            return compile_module_access(self, ast)
        elif isinstance(ast, DefineDeclaration):
            return compile_define(self, ast)
        # Type(s)
        elif isinstance(ast, TypeConv):
            return compile_type_conv(self, ast)
        elif isinstance(ast, TypeOf):
            return compile_typeof(self, ast)
        # Flow
        elif isinstance(ast, IfStatement):
            return compile_if(self, ast)
        elif isinstance(ast, WhileLoop):
            return compile_while(self, ast)
        elif isinstance(ast, ForLoop):
            return compile_for(self, ast)
        elif isinstance(ast, ControlStatement):
            return compile_control_statement(self, ast)
        elif isinstance(ast, ForeachLoop):
            return compile_foreach(self, ast)
        # Error Handling
        elif isinstance(ast, TryCatchNode):
            return compile_try_catch(self, ast)
        elif isinstance(ast, BlameNode):
            return compile_blame(self, ast)
        # Metaprogramming
        elif isinstance(ast, SpecialCallNode):
            return compile_special_call(self, ast)
        elif isinstance(ast, SpecialDeclaration):
            return compile_special_declaration(self, ast)
        # Literal
        elif isinstance(ast, Literal):
            return compile_literal(self, ast)
        # Macros
        elif isinstance(ast, MacroDeclaration):
            return compile_macro_declaration(self, ast)
        elif isinstance(ast, MacroCall):
            return compile_macro_call(self, ast)
        # Lambda
        elif isinstance(ast, LambdaNode):
            return compile_lambda(self, ast)
        # Structs
        elif isinstance(ast, StructDeclaration):
            return compile_struct(self, ast)
        elif isinstance(ast, StructInstantiation):
            return compile_struct_instantiation(self, ast)
        elif isinstance(ast, MemberAccess):
            return compile_member_access(self, ast)
        elif isinstance(ast, StructMethodCall):
            return compile_struct_method_call(self, ast)
        # Qualified Access
        elif isinstance(ast, QualifiedAccess):
            return compile_qualified_access(self, ast)
        # Enums
        elif isinstance(ast, EnumDeclaration):
            return compile_enum_declaration(self, ast)
        elif isinstance(ast, EnumAccess):
            return compile_enum_access_ast(self, ast)
        # Operators
        elif isinstance(ast, AdditiveOperator):
            return compile_additive(self, ast)
        elif isinstance(ast, MultiplicativeOperator):
            return compile_multiplicative(self, ast)
        elif isinstance(ast, ComparisonOperator):
            return compile_comparison(self, ast)
        elif isinstance(ast, LogicalOperator):
            return compile_logical(self, ast)
        elif isinstance(ast, UnaryOperator):
            return compile_unary(self, ast)
        elif isinstance(ast, PostfixOperator):
            return compile_postfix(self, ast)
        # Programic
        elif isinstance(ast, Program):
            # --- PASS 0: SCOUTING (Forward Declarations) ---
            # Register Structs, Interfaces, and Function Prototypes
            # so they are available before they are fully defined.
            for node in ast.statements:
                if isinstance(node, StructDeclaration):
                    mangled_name = self.get_mangled_name(node.name)
                    if mangled_name not in self.struct_types:
                        # Create Opaque Type
                        struct_ty = ir.global_context.get_identified_type(mangled_name)
                        self.struct_types[mangled_name] = struct_ty
                
                elif isinstance(node, InterfaceDeclaration):
                    mangled_name = self.get_mangled_name(node.name)
                    if mangled_name not in self.struct_types:
                        # Create Fat Pointer Type
                        interface_ty = ir.LiteralStructType([
                            ir.IntType(8).as_pointer(),
                            ir.IntType(8).as_pointer()
                        ])
                        self.struct_types[mangled_name] = interface_ty
                        self.interfaces.add(mangled_name)

                elif isinstance(node, FunctionDeclaration):
                    # Register Function Prototype
                    compile_function_declaration(self, node, prototype_only=True)

            # --- PASS 1: COMPILATION (Bodies) ---
            for node in ast.statements:
                self.compile(node)
        elif isinstance(ast, list):
            for node in ast:
                self.compile(node)
        elif isinstance(ast, str):
            var_name = ast
            resolved_symbol = self.current_scope.resolve(var_name)
            if isinstance(resolved_symbol, (ir.AllocaInstr, ir.GlobalVariable)):
                return self.builder.load(resolved_symbol, name=var_name + "_val")
            return self.get_variable(ast)
        else:
            self.errors.error(ast,f"Unsupported AST node type: {type(ast)}")
        return self.module
    def shutdown(self):
        binding.shutdown()
    def runwithjit(self, entry_function_name="main"):
        llvm_module = binding.parse_assembly(str(self.module))
        llvm_module.verify()

        target_machine = binding.Target.from_default_triple().create_target_machine()
        engine = binding.create_mcjit_compiler(llvm_module, target_machine)

        engine.finalize_object()
        engine.run_static_constructors()

        if self.main_function is None:
            raise Exception("No 'main' function found to JIT.")

        func_ptr = engine.get_function_address(self.main_function.name)
        func = ctypes.CFUNCTYPE(None)(func_ptr)
        func()