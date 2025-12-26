from .essentials import *
from src.semantics.scope import Scope
from src.utils.helpers import parse_file
import os

# ---------------------------------------------------------------------------
# <Method name=_compile_and_import_file args=[<Compiler>, str, <AstNode>, List[str], str]>
# <Description>
# Core logic for the Import System with Circular Dependency Support.
# </Description>
def compile_and_import_file(compiler: Compiler, abs_path: str, node: AstNode = None, targets: List[str] = None, alias: str = None):
    # 1. Cycle Detection (Strict)
    if abs_path in compiler.module_loader.visiting:
        # Check if we can recover using Opaque Types (Scouting Pass)
        if abs_path in getattr(compiler, 'active_module_scopes', {}):
            imported_scope = compiler.active_module_scopes[abs_path]
            compiler.merge_scope(imported_scope, targets, alias)
            if alias:
                compiler.module_aliases[alias] = abs_path
                compiler.loaded_modules[abs_path] = imported_scope.symbols
            return
        
        # Fatal Cycle
        cycle_chain = " -> ".join([os.path.basename(p) for p in compiler.module_loader.visiting] + [os.path.basename(abs_path)])
        compiler.errors.error(
            node, 
            f"Circular dependency detected: {cycle_chain}",
            hint="Fin does not support circular imports that require immediate value resolution. Try breaking the cycle or using pointers."
        )
        return
    
    # 2. Cache Check
    if abs_path in compiler.module_loader.cache:
        imported_scope = compiler.module_loader.cache[abs_path]
        compiler._merge_scope(imported_scope, targets, alias)
        if alias:
            compiler.module_aliases[alias] = abs_path
            compiler.loaded_modules[abs_path] = imported_scope.symbols
        return

    # 3. Mark Visiting
    compiler.module_loader.visiting.add(abs_path)
    
    # 4. Context Switch
    prev_path = compiler.current_file_path
    prev_scope = compiler.current_scope
    
    compiler.current_file_path = abs_path
    
    # 5. Parse File
    try:
        module_ast = parse_file(abs_path)
    except Exception as e:
        # If parsing fails, report it relative to the import statement
        compiler.errors.error(node, f"Failed to parse imported module '{os.path.basename(abs_path)}'", hint=str(e))
        return
    
    # 6. Create Module Scope & Register as Active
    module_scope = Scope(parent=compiler.global_scope)
    compiler.current_scope = module_scope
    
    if not hasattr(compiler, 'active_module_scopes'): compiler.active_module_scopes = {}
    compiler.active_module_scopes[abs_path] = module_scope
    
    if module_ast and module_ast.statements:
        # --- PASS 0: SCOUTING (Forward Declarations) ---
        for stmt in module_ast.statements:
            if isinstance(stmt, StructDeclaration):
                mangled_name = compiler.get_mangled_name(stmt.name)
                if mangled_name not in compiler.struct_types:
                    struct_ty = ir.global_context.get_identified_type(mangled_name)
                    compiler.struct_types[mangled_name] = struct_ty
            
            elif isinstance(stmt, InterfaceDeclaration):
                mangled_name = compiler.get_mangled_name(stmt.name)
                if mangled_name not in compiler.struct_types:
                    interface_ty = ir.LiteralStructType([
                        ir.IntType(8).as_pointer(),
                        ir.IntType(8).as_pointer()
                    ])
                    compiler.struct_types[mangled_name] = interface_ty
                    compiler.interfaces.add(mangled_name)

            # Register Function Prototypes
            elif isinstance(stmt, FunctionDeclaration):
                # Import locally to avoid circular dependency at top level
                from .prod.funcs import compile_function_declaration
                compile_function_declaration(compiler, stmt, prototype_only=True)

        # --- PASS 1: COMPILATION ---
        for stmt in module_ast.statements:
            compiler.compile(stmt)
        
    # 8. Restore Context
    compiler.current_scope = prev_scope
    compiler.current_file_path = prev_path
    
    compiler.module_loader.visiting.remove(abs_path)
    if abs_path in compiler.active_module_scopes:
        del compiler.active_module_scopes[abs_path]
    
    # 9. Cache Results
    compiler.module_loader.cache[abs_path] = module_scope
    
    # 10. Register Module Metadata
    if alias:
        compiler.module_aliases[alias] = abs_path
        compiler.loaded_modules[abs_path] = module_scope.symbols
    
    compiler._merge_scope(module_scope, targets, alias)
    
    # 11. Snapshot Registries
    if not hasattr(compiler, 'module_struct_field_types'): compiler.module_struct_field_types = {}
    if not hasattr(compiler, 'module_struct_visibility'): compiler.module_struct_visibility = {}
    if not hasattr(compiler, 'module_struct_fields'): compiler.module_struct_fields = {}
    if not hasattr(compiler, 'module_function_visibility'): compiler.module_function_visibility = {}
    if not hasattr(compiler, 'module_enum_members'): compiler.module_enum_members = {}
    if not hasattr(compiler, 'module_enum_types'): compiler.module_enum_types = {}
    if not hasattr(compiler, 'module_struct_types'): compiler.module_struct_types = {}

    compiler.module_struct_field_types[abs_path] = compiler.struct_field_types_registry.copy()
    compiler.module_struct_visibility[abs_path] = compiler.struct_field_visibility.copy()
    compiler.module_struct_fields[abs_path] = compiler.struct_field_indices.copy()
    compiler.module_struct_types[abs_path] = compiler.struct_types.copy()
    
    compiler.module_enum_members[abs_path] = compiler.enum_members.copy()
    compiler.module_enum_types[abs_path] = compiler.enum_types.copy()
    
    func_vis_map = {}
    for mangled, vis in compiler.function_visibility.items():
        origin = compiler.function_origins.get(mangled)
        if origin == abs_path:
            func_vis_map[mangled] = vis
            
    compiler.module_function_visibility[abs_path] = func_vis_map

# ---------------------------------------------------------------------------
# <Method name=compile_import args=[<Compiler>, <ImportModule>]>
# <Description>
# Handles the 'import' statement AST node.
# </Description>
def compile_import(compiler: Compiler, node: ImportModule):
    current_context = compiler.current_file_path if compiler.current_file_path else compiler.module_loader.entrypoint_file

    # --- Calculate Default Alias ---
    effective_alias = node.alias
    if not effective_alias and not node.targets:
        if node.is_package:
            effective_alias = node.source.split('.')[-1]
        else:
            filename = os.path.basename(node.source)
            effective_alias = os.path.splitext(filename)[0]
    # 1. Handle Package Specific Exports
    if node.is_package and node.targets:
        try:
            symbol_map = compiler.module_loader.get_package_exports(node.source)
        except Exception as e:
            compiler.errors.error(node, str(e))
            return

        files_to_load = {}
        
        for target in node.targets:
            if target in symbol_map:
                fpath = symbol_map[target]
                if fpath not in files_to_load: files_to_load[fpath] = []
                files_to_load[fpath].append(target)
            else:
                try:
                    main_path = compiler.module_loader.resolve_import(node, current_context)
                    if main_path not in files_to_load: files_to_load[main_path] = []
                    files_to_load[main_path].append(target)
                except Exception as e:
                    compiler.errors.error(node, f"Could not resolve target '{target}' in package '{node.source}'", hint=str(e))
        
        for fpath, symbols in files_to_load.items():
            # [FIX] Pass 'node' for error reporting
            compiler.compile_and_import_file(compiler, fpath, node, symbols, None)
        return

    # 2. Standard Resolution
    try:
        abs_path = compiler.module_loader.resolve_import(node, current_context)
    except Exception as e:
        compiler.errors.error(node, f"Import resolution failed for '{node.source}'", hint=str(e))
        return

    # [FIX] Pass 'node' for error reporting
    compiler.compile_and_import_file(compiler, abs_path, node, node.targets, effective_alias)

# ---------------------------------------------------------------------------
# <Method name=compile_module_access args=[<Compiler>, <ModuleAccess>]>
# <Description>
# Compiles 'mod.symbol' or 'mod.Enum.Variant'.
# </Description>
def compile_module_access(compiler: Compiler, ast: ModuleAccess):
    # Case 1: Nested Module Access
    if isinstance(ast.alias, ModuleAccess):
        inner = ast.alias
        root_alias = inner.alias
        middle_name = inner.name
        final_name = ast.name

        if root_alias in compiler.module_aliases:
            path = compiler.module_aliases[root_alias]
            
            # A. Enum Member
            if path in compiler.module_enum_members:
                module_enums = compiler.module_enum_members[path]
                if middle_name in module_enums:
                    enum_variants = module_enums[middle_name]
                    if final_name in enum_variants:
                        return enum_variants[final_name]
                    else:
                        compiler.errors.error(ast, f"Enum '{middle_name}' has no member '{final_name}' in module '{root_alias}'.")
            
            # B. Static Method
            if path in compiler.module_struct_types:
                module_structs = compiler.module_struct_types[path]
                if middle_name in module_structs:
                    struct_llvm_type = module_structs[middle_name]
                    mangled_struct_name = struct_llvm_type.name
                    static_method_name = f"{mangled_struct_name}_static_{final_name}"
                    
                    try:
                        return compiler.module.get_global(static_method_name)
                    except KeyError:
                        compiler.errors.error(ast, f"Struct '{middle_name}' has no static method '{final_name}' in module '{root_alias}'.")

            compiler.errors.error(ast, f"Could not resolve '{middle_name}' in module '{root_alias}'.")
        
        compiler.errors.error(ast, f"Module '{root_alias}' not imported.")

    # Case 2: Simple Dot Access
    alias = ast.alias
    name = ast.name

    # A. Check Module
    if alias in compiler.module_aliases:
        path = compiler.module_aliases[alias]
        namespace = compiler.loaded_modules[path]

        if name in namespace:
            val = namespace[name]
            if isinstance(val, (ir.GlobalVariable, ir.AllocaInstr)):
                return compiler.builder.load(val, name=f"{alias}_{name}")
            return val
        
        if path in compiler.module_enum_types and name in compiler.module_enum_types[path]:
             compiler.errors.error(ast, f"'{alias}.{name}' is a type, not a value.", hint="Did you mean to access a member of this Enum?")

        compiler.errors.error(ast, f"Module '{alias}' has no symbol '{name}'.")
    
    # B. Check Local Enum
    if alias in compiler.enum_members:
        members = compiler.enum_members[alias]
        if name in members:
            return members[name]
        compiler.errors.error(ast, f"Enum '{alias}' has no member '{name}'.")

    compiler.errors.error(ast, f"Unknown identifier or module '{alias}'.")
# ---------------------------------------------------------------------------
# <Method name=compile_import_c args=[<Compiler>, <ImportC>]>
# </Description>
def compile_import_c(compiler: Compiler, ast: ImportC):
    lib_name = ast.path_or_name.strip('"').rstrip('"').strip("'").rstrip("'")
    compiler.imported_libs.append(lib_name)