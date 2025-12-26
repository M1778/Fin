from llvmlite import ir, binding
import platform
import ctypes
import os
import subprocess
from ..semantics.scope import Scope

class LLVMBaseMixin:
    def __init__(self, opt=None, codemodel=None, is_jit=False):
        binding.initialize()
        binding.initialize_native_target()
        binding.initialize_native_asmprinter()

        self.module = ir.Module(name="popo_module")
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

        self.builder = None
        self.function = None
        
        # Scopes
        self.global_scope = Scope(parent=None)
        self.current_scope = self.global_scope
        
        # Registries
        self.struct_types = {}
        self.struct_field_indices = {}
        self.struct_methods = {}
        self.enum_types = {}
        self.enum_members = {}
        self.global_strings = {}
        self.imported_libs = []
        self.macros = {}
        
        # Initialize printf (Standard Library)
        printf_ty = ir.FunctionType(ir.IntType(32), [ir.IntType(8).as_pointer()], var_arg=True)
        printf = ir.Function(self.module, printf_ty, name="printf")
        self.global_scope.define("printf", printf)

    def generate_ir(self):
        return str(self.module)

    def generate_object_code(self, output_filename="output.o"):
        print(f"--- Generating Object Code: {output_filename} ---")
        llvm_module_parsed = binding.parse_assembly(str(self.module))
        with open(output_filename, "wb") as f:
            f.write(self.target_machine.emit_object(llvm_module_parsed))

    def runwithjit(self, entry_function_name="main"):
        llvm_module = binding.parse_assembly(str(self.module))
        target_machine = binding.Target.from_default_triple().create_target_machine()
        engine = binding.create_mcjit_compiler(llvm_module, target_machine)
        engine.finalize_object()
        engine.run_static_constructors()

        # Load C libraries
        for lib in self.imported_libs:
            binding.load_library_permanently(lib)

        func_ptr = engine.get_function_address(entry_function_name)
        func = ctypes.CFUNCTYPE(None)(func_ptr)
        func()

    def shutdown(self):
        binding.shutdown()