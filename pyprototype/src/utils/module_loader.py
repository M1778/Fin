import os
import json
import toml

class ModuleLoader:
    def __init__(self, entrypoint_file):
        """
        Initializes the loader with the absolute path to the main file being compiled.
        """
        self.entrypoint_file = os.path.abspath(entrypoint_file)
        self.root_dir = os.path.dirname(self.entrypoint_file)
        
        self.cache = {} # Cache for compiled module scopes
        self.visiting = set() # For circular dependency detection
        
        # Load .finn config and determine packages path
        self.finn_config = self._load_finn_config()
        envpath = self.finn_config.get("project", {}).get("envpath", ".finn")
        self.packages_path = os.path.join(self.root_dir, envpath, "packages")

    def _load_finn_config(self):
        """
        Searches up from the root directory for a 'finn.toml' file.
        """
        current_dir = self.root_dir
        while True:
            path = os.path.join(current_dir, "finn.toml")
            if os.path.exists(path):
                return toml.load(path)
            
            parent = os.path.dirname(current_dir)
            if parent == current_dir: # Reached filesystem root
                break
            current_dir = parent
        return {}

    def resolve_import(self, import_node, current_file_path):
        """
        Resolves an import node to an absolute file path.
        `current_file_path` is the absolute path of the file containing the import.
        """
        current_dir = os.path.dirname(os.path.abspath(current_file_path))
        source = import_node.source

        if import_node.is_package:
            # Handles: import pkg; import pkg.mod; import {x} from pkg.mod;
            return self._resolve_package(source)
        else:
            # Handles: import "lib"; import "./lib"; import "mod.sub";
            return self._resolve_local(source, current_dir)

    def _resolve_local(self, source, current_dir):
        """Resolves local, relative imports."""
        # Convert dot notation if it's not explicitly relative
        if not source.startswith("./") and not source.startswith("../"):
            source = source.replace(".", os.sep)
        
        path = os.path.join(current_dir, source)
        
        # Try extensions and check if it's a directory
        for ext in [".fin", ".popo", ""]:
            candidate = path + ext
            if os.path.exists(candidate) and not os.path.isdir(candidate):
                return os.path.abspath(candidate)
        
        # If the original path was a directory, check for default files
        if os.path.isdir(path):
            for default_file in ["lib.fin", "index.fin", "main.fin"]:
                candidate = os.path.join(path, default_file)
                if os.path.exists(candidate):
                    return os.path.abspath(candidate)

        raise Exception(f"Local module '{source}' not found. Searched relative to '{current_dir}'.")

    def _resolve_package(self, source):
        """Resolves package imports, including submodules like `pkg.mod`."""
        parts = source.split('.')
        pkg_name = parts[0]
        sub_modules = parts[1:]
        
        pkg_root = os.path.join(self.packages_path, pkg_name)
        
        if not os.path.exists(pkg_root):
            raise Exception(f"Package '{pkg_name}' not installed. Searched in '{self.packages_path}'.")

        # If it's a submodule (e.g., import pkg.mod.utils)
        if sub_modules:
            rel_path = os.path.join(*sub_modules)
            # We treat submodule resolution just like a local resolution inside the package
            return self._resolve_local(rel_path, pkg_root)
        
        # It's a root package import (e.g., import pkg)
        # Find the main entrypoint for the package
        
        # 1. Check package.json for a "main" or "entry" field
        pkg_json_path = os.path.join(pkg_root, "package.json")
        if os.path.exists(pkg_json_path):
            with open(pkg_json_path, 'r') as f:
                config = json.load(f)
                entry = config.get("main") or config.get("entry")
                if entry:
                    return os.path.abspath(os.path.join(pkg_root, entry))
        
        # 2. Check for special 'exports.fin'
        candidate = os.path.join(pkg_root, "exports.fin")
        if os.path.exists(candidate):
            return os.path.abspath(candidate)
            
        # 3. Fallback to standard names in the package root
        for default_file in ["lib.fin", "main.fin", "index.fin"]:
            candidate = os.path.join(pkg_root, default_file)
            if os.path.exists(candidate):
                return os.path.abspath(candidate)
                
        raise Exception(f"Could not find an entry point for package '{pkg_name}'. No package.json, exports.fin, or standard file found.")

    def get_package_exports(self, pkg_name):
        """
        Reads package.json 'modules' section for `import {x} from pkg` syntax.
        Returns a map of { symbol_name: absolute_file_path }.
        """
        pkg_dir = os.path.join(self.packages_path, pkg_name)
        pkg_json_path = os.path.join(pkg_dir, "package.json")
        
        symbol_map = {}
        
        if os.path.exists(pkg_json_path):
            try:
                with open(pkg_json_path, 'r') as f:
                    config = json.load(f)
                    if "modules" in config:
                        for file_path, symbols in config["modules"].items():
                            for sym in symbols:
                                symbol_map[sym] = os.path.abspath(os.path.join(pkg_dir, file_path))
            except json.JSONDecodeError:
                print(f"Warning: Could not parse malformed 'package.json' for '{pkg_name}'.")
                            
        return symbol_map