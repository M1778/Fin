/**
 * Fin Systems Programming Language - Documentation Interactivity
 * Features:
 *  - Theme switching (Light/Dark) with system fallback & persistence
 *  - Instant Search dialog (Cmd+K / Ctrl+K) with client-side indexing
 *  - Syntax Highlighting engine for Fin language constructs
 *  - Code copy-to-clipboard with visual feedback
 *  - Scrollspy for active Table of Contents & Navigation links
 *  - Collapsible sidebar categories with memory
 *  - Mobile responsive drawer menu
 */

(function () {
  'use strict';

  // --- Embedded Fallback Search Data (Ensures search works offline & via file://) ---
  const FALLBACK_SEARCH_INDEX = [
    {
        "id": "intro",
        "title": "Introduction to Fin",
        "category": "Getting Started",
        "tags": [
            "intro",
            "philosophy",
            "systems",
            "memory",
            "invariants",
            "overview"
        ],
        "snippet": "Fin is a statically-typed systems programming language with explicit memory semantics, structural prototypes, first-class enums, and zero-overhead C interop.",
        "url": "index.html#intro"
    },
    {
        "id": "installation",
        "title": "Installation & Building finc",
        "category": "Getting Started",
        "tags": [
            "install",
            "build",
            "cmake",
            "conan",
            "llvm",
            "finc",
            "cli",
            "flags",
            "prerequisites"
        ],
        "snippet": "Build finc from source using ./build.sh with CMake, Conan, Flex, Bison, and LLVM. Check installation with finc --version.",
        "url": "index.html#installation"
    },
    {
        "id": "quickstart",
        "title": "Quickstart Guide & Compiler Modes",
        "category": "Getting Started",
        "tags": [
            "quickstart",
            "workflow",
            "compiler",
            "check",
            "compile",
            "flags",
            "finc -o"
        ],
        "snippet": "Create and compile Fin programs using finc. Type-check with finc file.fin, compile objects with finc -c, or link native binaries with finc -o bin.",
        "url": "index.html#quickstart"
    },
    {
        "id": "hello-world",
        "title": "Hello, World! & Ambient printf",
        "category": "Getting Started",
        "tags": [
            "hello world",
            "printf",
            "main",
            "noret",
            "variadic",
            "define",
            "entry point"
        ],
        "snippet": "Your first Fin program using ambient printf and fun main() <noret>. Learn about angle-bracket return types and C FFI bindings.",
        "url": "index.html#hello-world"
    },
    {
        "id": "variables-types",
        "title": "Variables, Sized Integers & Inference",
        "category": "Language Tour",
        "tags": [
            "let",
            "auto",
            "const",
            "readonly",
            "types",
            "nullable",
            "denullify",
            "int",
            "uint",
            "cast"
        ],
        "snippet": "Local variable declarations with let x <int> = 100, sized types int{64}, type inference with auto, const parameters, and cast<T> conversions.",
        "url": "index.html#variables-types"
    },
    {
        "id": "control-flow",
        "title": "Control Flow & Ternary Operator",
        "category": "Language Tour",
        "tags": [
            "if",
            "else",
            "for",
            "while",
            "do while",
            "ternary",
            "scope",
            "branching"
        ],
        "snippet": "Branching with if/else, three-clause for loops, while loops, lexical scopes {}, and Fin's ternary condition : then ? otherwise.",
        "url": "index.html#control-flow"
    },
    {
        "id": "functions",
        "title": "Functions, Entry Point & Generics",
        "category": "Language Tour",
        "tags": [
            "fun",
            "noret",
            "void",
            "generics",
            "parameters",
            "const",
            "define",
            "extern"
        ],
        "snippet": "Declare functions with fun name(param: Type) <ReturnType>. Supports generic functions, const arguments, and @define extern bindings.",
        "url": "index.html#functions"
    },
    {
        "id": "structs-classes",
        "title": "Structs, Methods & Encapsulation",
        "category": "Language Tour",
        "tags": [
            "struct",
            "class",
            "self",
            "methods",
            "fields",
            "pub",
            "priv",
            "readonly",
            "static"
        ],
        "snippet": "Define data structures with struct. Methods take explicit self: &Self receiver. Default field values, pub/priv visibility, and readonly members.",
        "url": "index.html#structs-classes"
    },
    {
        "id": "interfaces",
        "title": "Interfaces & Generic Bounds",
        "category": "Language Tour",
        "tags": [
            "interface",
            "implements",
            "generics",
            "bounds",
            "constraints",
            "polymorphism"
        ],
        "snippet": "Define polymorphic contracts with pub interface. Nominal conformance on structs, external implements blocks, and generic type constraints.",
        "url": "index.html#interfaces"
    },
    {
        "id": "enums",
        "title": "Enums & Tagged Payloads",
        "category": "Language Tour",
        "tags": [
            "enum",
            "tagged union",
            "payload",
            "discriminant",
            "result",
            "getkeyid",
            "keyidof"
        ],
        "snippet": "Fieldless enums and tagged union enums with member payloads. Positional payload access via .0 and pattern constructors.",
        "url": "index.html#enums"
    },
    {
        "id": "pointers-memory",
        "title": "Arrays, Pointers & Explicit Allocation",
        "category": "Language Tour",
        "tags": [
            "pointer",
            "reference",
            "new",
            "delete",
            "array",
            "fixed",
            "dynamic",
            "length",
            "heap"
        ],
        "snippet": "Fixed arrays [T, N], dynamic arrays [T] with .length, heap allocation with new [T, n]{}, deallocation with delete, and raw references &T.",
        "url": "index.html#pointers-memory"
    },
    {
        "id": "blame",
        "title": "Unified Blame Assertions",
        "category": "Language Tour",
        "tags": [
            "blame",
            "m1778",
            "assert",
            "verification",
            "raise",
            "error",
            "unimplemented"
        ],
        "snippet": "Fin's unified verification statement: blame condition, 'message' for assertions, and blame m1778 for unimplemented paths.",
        "url": "index.html#blame"
    },
    {
        "id": "prototypes",
        "title": "Structural Prototypes {K,V}",
        "category": "Language Tour",
        "tags": [
            "prototype",
            "structural",
            "map",
            "dict",
            "hashmap",
            "collection",
            "literal"
        ],
        "snippet": "Builtin structural map type {K,V} and literal syntax {'key': value}. Structural equivalence and explicit conversion to HashMap.",
        "url": "index.html#prototypes"
    },
    {
        "id": "macros-specials",
        "title": "Macros & Compile-Time Specials",
        "category": "Language Tour",
        "tags": [
            "macro",
            "special",
            "quote",
            "bracket",
            "comptime",
            "compiler api",
            "grant"
        ],
        "snippet": "Compile-time metaprogramming: @macro returning quote AST blocks, bracket shaping (name!, name![]), @special functions, and #[use(...)] grants.",
        "url": "index.html#macros-specials"
    },
    {
        "id": "pipeline-architecture",
        "title": "Compiler Pipeline Flow",
        "category": "Compiler Reference",
        "tags": [
            "pipeline",
            "llvm",
            "ast",
            "lexer",
            "parser",
            "semantics",
            "codegen"
        ],
        "snippet": "Six-stage compiler flow from Source through Bison Parser, AST cloning, Two-Moment semantics, and LLVM lowering.",
        "url": "index.html#pipeline-architecture"
    },
    {
        "id": "compiler-cli",
        "title": "Compiler CLI Reference & Exit Codes",
        "category": "Compiler Reference",
        "tags": [
            "cli",
            "flags",
            "exit codes",
            "finc",
            "-o",
            "-c",
            "-S",
            "--emit-llvm"
        ],
        "snippet": "Standard CLI flags (-o, -c, -S, --emit-llvm) and deterministic exit codes (0 = Success, 1 = Syntax, 2 = Semantic, 3 = Refusal).",
        "url": "index.html#compiler-cli"
    },
    {
        "id": "compiler-invariants",
        "title": "Explicit Refusal Guarantee",
        "category": "Compiler Reference",
        "tags": [
            "refusal",
            "invariant",
            "guarantee",
            "safety",
            "soundness"
        ],
        "snippet": "The compiler will NEVER silently drop code or guess intermediate representations. Any un-lowerable construct halts with an explicit refusal diagnostic.",
        "url": "index.html#compiler-invariants"
    },
    {
        "id": "module-stdio",
        "title": "std::stdio \u2014 Standard I/O, Stream & File",
        "category": "Standard Library",
        "tags": [
            "stdio",
            "printf",
            "print",
            "println",
            "eprint",
            "eprintln",
            "Printable",
            "Stream",
            "File",
            "IOResult"
        ],
        "snippet": "Terminal I/O, formatted output, the Printable interface contract, in-memory Stream byte buffers, and static File operations.",
        "url": "stdlib.html#module-stdio"
    },
    {
        "id": "module-fs",
        "title": "std::fs \u2014 Filesystem Operations",
        "category": "Standard Library",
        "tags": [
            "fs",
            "file_exists",
            "remove_file",
            "file_size",
            "read_to_string",
            "write_string",
            "append_string",
            "free_file_content"
        ],
        "snippet": "Filesystem operations: checking file existence, querying file sizes, reading entire files into strings, writing, appending, and deleting files.",
        "url": "stdlib.html#module-fs"
    },
    {
        "id": "module-path",
        "title": "std::path \u2014 Path Manipulation",
        "category": "Standard Library",
        "tags": [
            "path",
            "is_absolute",
            "is_relative",
            "join",
            "basename",
            "dirname",
            "extname",
            "free_path"
        ],
        "snippet": "Filesystem path manipulation: safe segment joining, basename and dirname extraction, extension checking, and absolute vs relative testing.",
        "url": "stdlib.html#module-path"
    },
    {
        "id": "module-collection",
        "title": "std::collection \u2014 Dynamic Array Collection<T>",
        "category": "Standard Library",
        "tags": [
            "collection",
            "array",
            "vector",
            "push",
            "pop_last",
            "get",
            "set",
            "len",
            "capacity",
            "insert",
            "remove"
        ],
        "snippet": "The primary growable dynamic array Collection<T> backed by [T] with geometric buffer doubling, capacity reservations, and bounds checking.",
        "url": "stdlib.html#module-collection"
    },
    {
        "id": "module-hashmap",
        "title": "std::hashmap \u2014 Open-Addressing Hash Table",
        "category": "Standard Library",
        "tags": [
            "hashmap",
            "map",
            "table",
            "put",
            "get",
            "remove",
            "contains_key",
            "size",
            "linear probing"
        ],
        "snippet": "Associative key-value map HashMap<K, V> using open addressing with linear probing and a 0.75 load factor threshold for automatic rehashing.",
        "url": "stdlib.html#module-hashmap"
    },
    {
        "id": "module-env",
        "title": "std::env \u2014 Environment Variables & Process",
        "category": "Standard Library",
        "tags": [
            "env",
            "get_env",
            "has_env",
            "set_env",
            "unset_env",
            "exit",
            "get_pid",
            "process"
        ],
        "snippet": "Process environment variable inspection (get_env, has_env, set_env, unset_env), process ID query, and process exit codes.",
        "url": "stdlib.html#module-env"
    },
    {
        "id": "module-time",
        "title": "std::time \u2014 Clocks & Sleep",
        "category": "Standard Library",
        "tags": [
            "time",
            "now",
            "sleep_sec",
            "sleep_ms",
            "diff_sec",
            "epoch",
            "timestamp",
            "delay"
        ],
        "snippet": "Operating system clock timestamps (Unix epoch) and synchronous execution delays in whole seconds and milliseconds.",
        "url": "stdlib.html#module-time"
    },
    {
        "id": "module-random",
        "title": "std::random \u2014 PRNG & Distributions",
        "category": "Standard Library",
        "tags": [
            "random",
            "seed",
            "seed_now",
            "rand",
            "random_int",
            "random_float",
            "random_bool"
        ],
        "snippet": "Pseudo-random number generator (PRNG): deterministic integer seeds, automatic epoch-based seeding, uniform integer ranges, floats, and booleans.",
        "url": "stdlib.html#module-random"
    },
    {
        "id": "module-math",
        "title": "std::math \u2014 Mathematical Functions & Constants",
        "category": "Standard Library",
        "tags": [
            "math",
            "PI",
            "E",
            "abs",
            "min",
            "max",
            "clamp",
            "signum",
            "gcd",
            "lcm",
            "ipow",
            "isqrt",
            "sqrt",
            "sin",
            "cos",
            "pow"
        ],
        "snippet": "Mathematical constants (PI, E, TAU), generic functions over Number (abs, min, max, clamp, signum), integer algorithms (gcd, lcm, ipow, isqrt), and C libm bindings.",
        "url": "stdlib.html#module-math"
    },
    {
        "id": "module-strings",
        "title": "std::strings \u2014 String Manipulation",
        "category": "Standard Library",
        "tags": [
            "strings",
            "len",
            "starts_with",
            "ends_with",
            "contains",
            "index_of",
            "slice",
            "concat",
            "repeat"
        ],
        "snippet": "String query, validation, slicing, and manipulation routines: length, prefix/suffix inspection, substring search, slicing, concatenation, and repetition.",
        "url": "stdlib.html#module-strings"
    },
    {
        "id": "module-types",
        "title": "std::types \u2014 Numeric Constraints & number2str",
        "category": "Standard Library",
        "tags": [
            "types",
            "Number",
            "Integer",
            "Float",
            "Signed",
            "Unsigned",
            "number2str"
        ],
        "snippet": "Core numeric constraint sets (Number, Integer, Float, Signed, Unsigned) for generic function bounds, and integer-to-string formatting.",
        "url": "stdlib.html#module-types"
    },
    {
        "id": "module-typing",
        "title": "std::typing \u2014 Tagged Union Result<T, U>",
        "category": "Standard Library",
        "tags": [
            "typing",
            "Result",
            "IResult",
            "Ok",
            "Err",
            "payload",
            "error handling"
        ],
        "snippet": "The tagged union Result<T, U> and polymorphic IResult interface for robust, exception-free error handling.",
        "url": "stdlib.html#module-typing"
    },
    {
        "id": "module-enums",
        "title": "std::enums \u2014 Enum Reflection & Discriminants",
        "category": "Standard Library",
        "tags": [
            "enums",
            "EnumType",
            "getkeyid",
            "keyidof",
            "reflection",
            "discriminant"
        ],
        "snippet": "Reflection and inspection of Fin enum values, runtime discriminants, and compile-time member identifiers.",
        "url": "stdlib.html#module-enums"
    },
    {
        "id": "module-error",
        "title": "std::error \u2014 Error Base Class",
        "category": "Standard Library",
        "tags": [
            "error",
            "Error",
            "ErrorType",
            "code",
            "message",
            "describe"
        ],
        "snippet": "Base Error class and standard error categorizations for structured failure modeling across libraries.",
        "url": "stdlib.html#module-error"
    },
    {
        "id": "module-operators",
        "title": "std::operators \u2014 Operator Overloading",
        "category": "Standard Library",
        "tags": [
            "operators",
            "IndexAssign",
            "Add",
            "Sub",
            "Mul",
            "Div",
            "Equals",
            "overload"
        ],
        "snippet": "Interfaces for user-defined operator overloading on structs: arithmetic (Add, Sub, Mul, Div), comparison (Equals), and index assignment (IndexAssign).",
        "url": "stdlib.html#module-operators"
    },
    {
        "id": "module-memory",
        "title": "std::memory \u2014 Dynamic Heap & Arena Patterns",
        "category": "Standard Library",
        "tags": [
            "memory",
            "new",
            "delete",
            "heap",
            "allocation",
            "arena",
            "deallocation"
        ],
        "snippet": "Explicit memory allocation mechanisms: native heap dynamic arrays new [T, n]{}, delete arr;, and user-defined arena allocators.",
        "url": "stdlib.html#module-memory"
    },
    {
        "id": "module-stdptr",
        "title": "std::stdptr \u2014 Reference-Counted Pointer rptr<T>",
        "category": "Standard Library",
        "tags": [
            "stdptr",
            "rptr",
            "OwnershipError",
            "retain",
            "release",
            "reference counting",
            "smart pointer"
        ],
        "snippet": "Smart pointer rptr<T> providing sound reference-counted memory sharing with explicit retain/release lifecycle tracking.",
        "url": "stdlib.html#module-stdptr"
    }
];

  let searchIndex = FALLBACK_SEARCH_INDEX;

  // --- Theme Management ---
  function initTheme() {
    const storedTheme = localStorage.getItem('fin-docs-theme');
    const systemPrefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
    const initialTheme = storedTheme || (systemPrefersDark ? 'dark' : 'light');

    document.documentElement.setAttribute('data-theme', initialTheme);

    const themeToggleBtn = document.getElementById('theme-toggle-btn');
    if (themeToggleBtn) {
      themeToggleBtn.addEventListener('click', () => {
        const current = document.documentElement.getAttribute('data-theme') || 'light';
        const next = current === 'dark' ? 'light' : 'dark';
        document.documentElement.setAttribute('data-theme', next);
        localStorage.setItem('fin-docs-theme', next);
      });
    }
  }

  // --- Toast Notification Helper ---
  function showToast(message) {
    let toast = document.getElementById('fin-toast');
    if (!toast) {
      toast = document.createElement('div');
      toast.id = 'fin-toast';
      toast.className = 'fin-toast';
      document.body.appendChild(toast);
    }
    toast.innerHTML = `
      <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
        <polyline points="20 6 9 17 4 12"></polyline>
      </svg>
      <span>${escapeHtml(message)}</span>
    `;
    toast.classList.add('show');
    setTimeout(() => {
      toast.classList.remove('show');
    }, 2400);
  }

  // --- Syntax Highlighter for Fin ---
  function highlightFinCode() {
    const codeBlocks = document.querySelectorAll('pre code.language-fin, pre code.language-c, pre code.language-rust, pre code.language-bash');
    codeBlocks.forEach(codeEl => {
      let code = codeEl.textContent;
      const tokens = [];

      // 1. Strings (including interpolation)
      let temp = code.replace(/("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')/g, (match) => {
        const id = `\uE000STR${tokens.length}\uE001`;
        const formatted = escapeHtml(match).replace(/(\\?\{[^}]+\})/g, '<span class="tok-meta">$1</span>');
        tokens.push({ id, html: `<span class="tok-str">${formatted}</span>` });
        return id;
      });

      // 2. Comments (single-line & multi-line)
      temp = temp.replace(/(\/\/[^\n]*|\/\*[\s\S]*?\*\/)/g, (match) => {
        const id = `\uE000COM${tokens.length}\uE001`;
        tokens.push({ id, html: `<span class="tok-com">${escapeHtml(match)}</span>` });
        return id;
      });

      // 3. Attributes / Decorators (#[...])
      temp = temp.replace(/(#\[[^\]\n]+\])/g, (match) => {
        const id = `\uE000ATTR${tokens.length}\uE001`;
        tokens.push({ id, html: `<span class="tok-attr">${escapeHtml(match)}</span>` });
        return id;
      });

      // 4. Meta directives, specials & macros (@define, @macro, quote, $var)
      temp = temp.replace(/(@[a-zA-Z_]\w*|quote|\$[a-zA-Z_]\w*)/g, (match) => {
        const id = `\uE000META${tokens.length}\uE001`;
        tokens.push({ id, html: `<span class="tok-meta">${escapeHtml(match)}</span>` });
        return id;
      });

      // Escape HTML in the remaining text
      temp = escapeHtml(temp);

      // 5. Single-pass keyword and type tokenization (PREVENTS HTML ATTRIBUTE CORRUPTION)
      const KEYWORDS = new Set([
        'fun', 'let', 'const', 'pub', 'priv', 'static', 'readonly',
        'if', 'else', 'for', 'foreach', 'while', 'do', 'return',
        'struct', 'class', 'interface', 'implements', 'enum', 'type',
        'namespace', 'import', 'export', 'new', 'delete', 'cast',
        'sizeof', 'as', 'from', 'switch', 'case', 'default', 'in', 'operator'
      ]);

      const TYPES = new Set([
        'int', 'uint', 'short', 'ushort', 'long', 'ulong', 'float', 'double',
        'bool', 'char', 'string', 'void', 'noret', 'any', 'object', 'auto',
        'Self', 'byte', 'usize', 'isize',
        'Number', 'Integer', 'Float', 'Signed', 'Unsigned',
        'Buffer', 'Stream', 'IStream', 'File', 'Collection', 'HashMap', 'Result', 'IResult', 'Error', 'Printable', 'IOError', 'IOResult', 'EnumType', 'rptr'
      ]);

      const BLAMES = new Set(['blame', 'm1778']);

      temp = temp.replace(/\b([a-zA-Z_]\w*)\b/g, (match) => {
        if (BLAMES.has(match)) return `<span class="tok-blame">${match}</span>`;
        if (KEYWORDS.has(match)) return `<span class="tok-kw">${match}</span>`;
        if (TYPES.has(match)) return `<span class="tok-type">${match}</span>`;
        return match;
      });

      // 6. Numbers (decimal, float, hex)
      temp = temp.replace(/\b(\d+(?:\.\d+)?(?:e[+-]?\d+)?|0x[0-9a-fA-F]+)\b/g, '<span class="tok-num">$1</span>');

      // 7. Restore tokens with function replacer to prevent $ corruption
      tokens.forEach(t => {
        temp = temp.replace(t.id, () => t.html);
      });

      codeEl.innerHTML = temp;
    });
  }

  function escapeHtml(str) {
    return str
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#039;');
  }

  // --- Copy to Clipboard & Tooltip ---
  function initCodeCopy() {
    const codeBlocks = document.querySelectorAll('.code-block, .code-showcase-window');
    codeBlocks.forEach(block => {
      const copyBtn = block.querySelector('.copy-btn');
      if (!copyBtn) return;

      copyBtn.addEventListener('click', async () => {
        const activePane = block.querySelector('.code-tab-pane.active') || block;
        const codeEl = activePane.querySelector('pre code') || block.querySelector('pre code');
        if (!codeEl) return;

        const textToCopy = codeEl.innerText || codeEl.textContent;
        try {
          await navigator.clipboard.writeText(textToCopy);
          copyBtn.classList.add('copied');
          copyBtn.innerHTML = `
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
              <polyline points="20 6 9 17 4 12"></polyline>
            </svg>
            <span>Copied!</span>
          `;
          showToast('Code copied to clipboard!');
          setTimeout(() => {
            copyBtn.classList.remove('copied');
            copyBtn.innerHTML = `
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect>
                <path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path>
              </svg>
              <span>Copy</span>
            `;
          }, 2000);
        } catch (err) {
          console.error('Failed to copy to clipboard', err);
        }
      });
    });

    // Hero Install Box Copy
    const heroCopyBtn = document.getElementById('hero-copy-install-btn');
    if (heroCopyBtn) {
      heroCopyBtn.addEventListener('click', async () => {
        const cmd = 'curl -fsSL https://fin-lang.org/install.sh | sh';
        try {
          await navigator.clipboard.writeText(cmd);
          heroCopyBtn.innerHTML = `
            <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5">
              <polyline points="20 6 9 17 4 12"></polyline>
            </svg>
            <span>Copied!</span>
          `;
          showToast('Copied install command!');
          setTimeout(() => {
            heroCopyBtn.innerHTML = `
              <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                <rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect>
                <path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path>
              </svg>
              <span>Copy</span>
            `;
          }, 2000);
        } catch (e) {
          console.error('Failed to copy install cmd', e);
        }
      });
    }
  }

  // --- Interactive Code Tabs ---
  function initCodeTabs() {
    const tabButtons = document.querySelectorAll('.code-tab-btn');
    tabButtons.forEach(btn => {
      btn.addEventListener('click', () => {
        const targetId = btn.getAttribute('data-tab');
        const container = btn.closest('.code-showcase-window') || document;
        
        // Deactivate siblings in this tab bar
        const siblingButtons = btn.parentElement.querySelectorAll('.code-tab-btn');
        siblingButtons.forEach(b => b.classList.remove('active'));
        btn.classList.add('active');

        // Switch pane
        const panes = container.querySelectorAll('.code-tab-pane');
        panes.forEach(pane => {
          if (pane.id === targetId) {
            pane.classList.add('active');
          } else {
            pane.classList.remove('active');
          }
        });
      });
    });
  }

  // --- Interactive Stdlib Module Filtering ---
  function initStdlibFilter() {
    const filterButtons = document.querySelectorAll('.stdlib-filter-btn');
    const cards = document.querySelectorAll('.stdlib-card');

    if (filterButtons.length > 0 && cards.length > 0) {
      filterButtons.forEach(btn => {
        btn.addEventListener('click', () => {
          const category = btn.getAttribute('data-filter');
          filterButtons.forEach(b => b.classList.remove('active'));
          btn.classList.add('active');

          cards.forEach(card => {
            const cardCat = card.getAttribute('data-category');
            if (category === 'all' || cardCat === category) {
              card.style.display = 'flex';
            } else {
              card.style.display = 'none';
            }
          });
        });
      });
    }

    // Sidebar text filter on stdlib.html
    const filterInput = document.getElementById('stdlib-search-input') || document.querySelector('.stdlib-filter-input');
    if (filterInput) {
      filterInput.addEventListener('input', (e) => {
        const query = e.target.value.toLowerCase().trim();
        const links = document.querySelectorAll('.stdlib-module-link');
        links.forEach(link => {
          const text = link.textContent.toLowerCase();
          const li = link.closest('li');
          if (li) {
            li.style.display = (!query || text.includes(query)) ? '' : 'none';
          }
        });
      });
    }
  }

  // --- Search System ---
  function initSearch() {
    const searchBackdrop = document.getElementById('search-modal-backdrop');
    const searchTriggerBtn = document.getElementById('search-trigger-btn');
    const searchCloseBtn = document.getElementById('search-close-btn');
    const searchInput = document.getElementById('search-input');
    const searchResults = document.getElementById('search-results');

    // Attempt to load search-index.json
    fetch('search-index.json')
      .then(res => res.ok ? res.json() : null)
      .then(data => {
        if (Array.isArray(data) && data.length > 0) {
          searchIndex = data;
        }
      })
      .catch(() => {
        // Fallback index is already initialized
      });

    function openSearch() {
      if (!searchBackdrop) return;
      searchBackdrop.classList.add('open');
      if (searchInput) {
        searchInput.value = '';
        renderResults('');
        setTimeout(() => searchInput.focus(), 50);
      }
    }

    function closeSearch() {
      if (!searchBackdrop) return;
      searchBackdrop.classList.remove('open');
    }

    if (searchTriggerBtn) searchTriggerBtn.addEventListener('click', openSearch);
    if (searchCloseBtn) searchCloseBtn.addEventListener('click', closeSearch);

    if (searchBackdrop) {
      searchBackdrop.addEventListener('click', (e) => {
        if (e.target === searchBackdrop) closeSearch();
      });
    }

    // Keyboard shortcut (Cmd+K / Ctrl+K / /)
    window.addEventListener('keydown', (e) => {
      if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === 'k') {
        e.preventDefault();
        openSearch();
      } else if (e.key === '/' && document.activeElement.tagName !== 'INPUT' && document.activeElement.tagName !== 'TEXTAREA') {
        e.preventDefault();
        openSearch();
      } else if (e.key === 'Escape' && searchBackdrop && searchBackdrop.classList.contains('open')) {
        closeSearch();
      }
    });

    let selectedIndex = 0;

    function renderResults(query) {
      if (!searchResults) return;
      const q = query.trim().toLowerCase();
      selectedIndex = 0;

      if (!q) {
        // Show recommended top entries
        const topHits = searchIndex.slice(0, 6);
        searchResults.innerHTML = topHits.map((item, idx) => `
          <a href="${item.url}" class="search-result-item ${idx === 0 ? 'selected' : ''}" data-index="${idx}">
            <div class="search-result-top">
              <span class="search-result-title">${escapeHtml(item.title)}</span>
              <span class="search-result-badge">${escapeHtml(item.category)}</span>
            </div>
            <p class="search-result-snippet">${escapeHtml(item.snippet)}</p>
          </a>
        `).join('');
        attachResultClickListeners();
        return;
      }

      const matches = searchIndex.filter(item => {
        const titleMatch = item.title.toLowerCase().includes(q);
        const snippetMatch = item.snippet.toLowerCase().includes(q);
        const categoryMatch = item.category.toLowerCase().includes(q);
        const tagMatch = item.tags && item.tags.some(t => t.toLowerCase().includes(q));
        return titleMatch || snippetMatch || categoryMatch || tagMatch;
      });

      if (matches.length === 0) {
        searchResults.innerHTML = `
          <div class="search-empty">
            No documentation matching <strong>"${escapeHtml(query)}"</strong> found.
          </div>
        `;
        return;
      }

      searchResults.innerHTML = matches.map((item, idx) => {
        const highlightedTitle = highlightMatch(item.title, q);
        const highlightedSnippet = highlightMatch(item.snippet, q);
        return `
          <a href="${item.url}" class="search-result-item ${idx === 0 ? 'selected' : ''}" data-index="${idx}">
            <div class="search-result-top">
              <span class="search-result-title">${highlightedTitle}</span>
              <span class="search-result-badge">${escapeHtml(item.category)}</span>
            </div>
            <p class="search-result-snippet">${highlightedSnippet}</p>
          </a>
        `;
      }).join('');

      attachResultClickListeners();
    }

    function attachResultClickListeners() {
      const items = searchResults.querySelectorAll('.search-result-item');
      items.forEach(item => {
        item.addEventListener('click', () => {
          closeSearch();
        });
      });
    }

    function highlightMatch(text, query) {
      if (!query) return escapeHtml(text);
      const escapedQuery = query.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
      const regex = new RegExp(`(${escapedQuery})`, 'gi');
      return escapeHtml(text).replace(regex, '<mark class="search-highlight">$1</mark>');
    }

    if (searchInput) {
      searchInput.addEventListener('input', (e) => {
        renderResults(e.target.value);
      });

      searchInput.addEventListener('keydown', (e) => {
        const items = searchResults.querySelectorAll('.search-result-item');
        if (items.length === 0) return;

        if (e.key === 'ArrowDown') {
          e.preventDefault();
          items[selectedIndex]?.classList.remove('selected');
          selectedIndex = (selectedIndex + 1) % items.length;
          items[selectedIndex]?.classList.add('selected');
          items[selectedIndex]?.scrollIntoView({ block: 'nearest' });
        } else if (e.key === 'ArrowUp') {
          e.preventDefault();
          items[selectedIndex]?.classList.remove('selected');
          selectedIndex = (selectedIndex - 1 + items.length) % items.length;
          items[selectedIndex]?.classList.add('selected');
          items[selectedIndex]?.scrollIntoView({ block: 'nearest' });
        } else if (e.key === 'Enter') {
          e.preventDefault();
          if (items[selectedIndex]) {
            items[selectedIndex].click();
            const href = items[selectedIndex].getAttribute('href');
            if (href) {
              window.location.hash = href;
              closeSearch();
            }
          }
        }
      });
    }
  }

  // --- Scrollspy & TOC Synchronization ---
  function initScrollspy() {
    const sections = Array.from(document.querySelectorAll('.main-content .doc-section[id]'));
    const tocLinks = Array.from(document.querySelectorAll('.toc-link'));
    const navLinks = Array.from(document.querySelectorAll('.nav-item-link'));

    if (sections.length === 0) return;

    let ticking = false;

    function updateActiveState() {
      const scrollPos = window.scrollY + 120;

      let currentSectionId = null;
      for (let i = sections.length - 1; i >= 0; i--) {
        const sec = sections[i];
        if (sec.offsetTop <= scrollPos) {
          currentSectionId = sec.getAttribute('id');
          break;
        }
      }

      if (!currentSectionId && sections.length > 0) {
        currentSectionId = sections[0].getAttribute('id');
      }

      // Update Right TOC
      tocLinks.forEach(link => {
        const href = link.getAttribute('href');
        if (href === `#${currentSectionId}` || href === `#${currentSectionId}-heading`) {
          link.classList.add('active');
        } else {
          link.classList.remove('active');
        }
      });

      // Update Left Navigation
      navLinks.forEach(link => {
        const href = link.getAttribute('href');
        if (href === `#${currentSectionId}`) {
          link.classList.add('active');
        } else {
          link.classList.remove('active');
        }
      });

      ticking = false;
    }

    window.addEventListener('scroll', () => {
      if (!ticking) {
        requestAnimationFrame(updateActiveState);
        ticking = true;
      }
    }, { passive: true });

    updateActiveState();
  }

  // --- Collapsible Navigation Categories ---
  function initNavCollapsing() {
    const sectionHeaders = document.querySelectorAll('.nav-section-header');

    // Restore saved collapse states
    const savedStates = JSON.parse(localStorage.getItem('fin-docs-nav-state') || '{}');

    sectionHeaders.forEach((btn, idx) => {
      const section = btn.closest('.nav-section');
      const sectionKey = `section_${idx}`;

      if (savedStates[sectionKey] === true) {
        section.classList.add('collapsed');
      }

      btn.addEventListener('click', () => {
        section.classList.toggle('collapsed');
        savedStates[sectionKey] = section.classList.contains('collapsed');
        localStorage.setItem('fin-docs-nav-state', JSON.stringify(savedStates));
      });
    });
  }

  // --- Mobile Drawer Menu ---
  function initMobileMenu() {
    const mobileMenuBtn = document.getElementById('mobile-menu-btn');
    const sidebarNav = document.getElementById('sidebar-nav');
    const sidebarOverlay = document.getElementById('sidebar-overlay');

    if (!mobileMenuBtn || !sidebarNav || !sidebarOverlay) return;

    function toggleMenu() {
      const isOpen = sidebarNav.classList.contains('drawer-open');
      if (isOpen) {
        closeMenu();
      } else {
        openMenu();
      }
    }

    function openMenu() {
      sidebarNav.classList.add('drawer-open');
      sidebarOverlay.classList.add('active');
      document.body.style.overflow = 'hidden';
    }

    function closeMenu() {
      sidebarNav.classList.remove('drawer-open');
      sidebarOverlay.classList.remove('active');
      document.body.style.overflow = '';
    }

    mobileMenuBtn.addEventListener('click', toggleMenu);
    sidebarOverlay.addEventListener('click', closeMenu);

    // Close when clicking any nav link
    const navLinks = sidebarNav.querySelectorAll('.nav-item-link');
    navLinks.forEach(link => {
      link.addEventListener('click', closeMenu);
    });
  }

  // --- DOM Ready Initialization ---
  document.addEventListener('DOMContentLoaded', () => {
    initTheme();
    highlightFinCode();
    initCodeCopy();
    initCodeTabs();
    initStdlibFilter();
    initSearch();
    initScrollspy();
    initNavCollapsing();
    initMobileMenu();
  });

})();

