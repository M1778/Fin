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
        "title": "std::stdio - Standard I/O, Stream & File",
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
        "title": "std::fs - Filesystem Operations",
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
        "title": "std::path - Path Manipulation",
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
        "title": "std::collection - Dynamic Array Collection<T>",
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
        "title": "std::hashmap - Open-Addressing Hash Table",
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
        "title": "std::env - Environment Variables & Process",
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
        "title": "std::time - Clocks & Sleep",
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
        "title": "std::random - PRNG & Distributions",
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
        "title": "std::math - Mathematical Functions & Constants",
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
        "title": "std::strings - String Manipulation",
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
        "title": "std::types - Numeric Constraints & number2str",
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
        "title": "std::typing - Tagged Union Result<T, U>",
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
        "title": "std::enums - Enum Reflection & Discriminants",
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
        "title": "std::error - Error Base Class",
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
        "title": "std::operators - Operator Overloading",
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
        "title": "std::memory - Dynamic Heap & Arena Patterns",
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
        "title": "std::stdptr - Reference-Counted Pointer rptr<T>",
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
    const systemTheme = window.matchMedia('(prefers-color-scheme: dark)');
    const themeToggleBtn = document.getElementById('theme-toggle-btn');
    let manualTheme = null;
    try {
      const storedTheme = localStorage.getItem('fin-docs-theme');
      if (storedTheme === 'dark' || storedTheme === 'light') manualTheme = storedTheme;
    } catch {
      // Storage can be unavailable in private windows or file previews.
    }

    function applyTheme(theme) {
      document.documentElement.setAttribute('data-theme', theme);
      themeToggleBtn?.setAttribute('aria-label', `Switch to ${theme === 'dark' ? 'light' : 'dark'} theme`);
    }

    applyTheme(manualTheme || (systemTheme.matches ? 'dark' : 'light'));
    systemTheme.addEventListener('change', (event) => {
      if (!manualTheme) applyTheme(event.matches ? 'dark' : 'light');
    });

    themeToggleBtn?.addEventListener('click', () => {
      manualTheme = document.documentElement.getAttribute('data-theme') === 'dark' ? 'light' : 'dark';
      applyTheme(manualTheme);
      try {
        localStorage.setItem('fin-docs-theme', manualTheme);
      } catch {
        // The manual choice still lasts for this page when storage is blocked.
      }
    });
  }

  // --- Toast Notification Helper ---
  let toastTimeout;
  function showToast(message) {
    let toast = document.getElementById('fin-toast');
    if (!toast) {
      toast = document.createElement('div');
      toast.id = 'fin-toast';
      toast.className = 'fin-toast';
      document.body.appendChild(toast);
    }
    toast.setAttribute('role', 'status');
    toast.setAttribute('aria-live', 'polite');
    toast.setAttribute('aria-atomic', 'true');
    toast.textContent = message;
    toast.classList.add('show');
    clearTimeout(toastTimeout);
    toastTimeout = setTimeout(() => {
      toast.classList.remove('show');
    }, message.startsWith('Could not copy') ? 6000 : 2400);
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

  // --- Copy to Clipboard & Feedback ---
  function initCodeCopy() {
    document.querySelectorAll('.copy-btn').forEach(copyBtn => {
      const block = copyBtn.closest('.code-block, .code-showcase-window');
      if (!block) return;
      const originalHTML = copyBtn.innerHTML;
      const originalLabel = copyBtn.getAttribute('aria-label');
      let feedbackTimeout;
      let copying = false;

      function restoreButton() {
        copyBtn.classList.remove('copied');
        copyBtn.innerHTML = originalHTML;
        if (originalLabel === null) copyBtn.removeAttribute('aria-label');
        else copyBtn.setAttribute('aria-label', originalLabel);
      }

      copyBtn.addEventListener('click', async () => {
        const activePane = block.querySelector('.code-tab-pane.active') || block;
        const codeEl = activePane.querySelector('pre code') || block.querySelector('pre code');
        if (!codeEl || copying) return;
        copying = true;
        try {
          await navigator.clipboard.writeText(codeEl.innerText || codeEl.textContent);
          clearTimeout(feedbackTimeout);
          copyBtn.classList.add('copied');
          copyBtn.textContent = 'Copied!';
          copyBtn.setAttribute('aria-label', 'Copied!');
          showToast('Code copied to clipboard!');
          showCopySparkles(copyBtn);
          feedbackTimeout = setTimeout(restoreButton, 2000);
        } catch {
          clearTimeout(feedbackTimeout);
          restoreButton();
          showToast('Could not copy. Please select and copy the code.');
        } finally {
          copying = false;
        }
      });
    });
  }

  // --- Interactive Code Tabs ---
  function initCodeTabs() {
    const tabLists = new Set(Array.from(document.querySelectorAll('.code-tab-btn'), btn => btn.parentElement));
    tabLists.forEach(tabList => {
      const buttons = Array.from(tabList.querySelectorAll('.code-tab-btn'));
      tabList.setAttribute('role', 'tablist');

      function selectTab(selected) {
        buttons.forEach(btn => {
          const active = btn === selected;
          const targetId = btn.getAttribute('data-tab');
          const pane = document.getElementById(targetId);
          if (!btn.id) btn.id = `${targetId}-tab`;
          btn.setAttribute('role', 'tab');
          btn.setAttribute('aria-controls', targetId);
          btn.setAttribute('aria-selected', String(active));
          btn.tabIndex = active ? 0 : -1;
          btn.classList.toggle('active', active);
          if (pane) {
            pane.setAttribute('role', 'tabpanel');
            pane.setAttribute('aria-labelledby', btn.id);
            pane.tabIndex = 0;
            pane.hidden = !active;
            pane.classList.toggle('active', active);
          }
        });
      }

      selectTab(buttons.find(btn => btn.classList.contains('active')) || buttons[0]);
      buttons.forEach((btn, index) => {
        btn.addEventListener('click', () => selectTab(btn));
        btn.addEventListener('keydown', (event) => {
          let next;
          if (event.key === 'ArrowRight' || event.key === 'ArrowDown') next = (index + 1) % buttons.length;
          else if (event.key === 'ArrowLeft' || event.key === 'ArrowUp') next = (index - 1 + buttons.length) % buttons.length;
          else if (event.key === 'Home') next = 0;
          else if (event.key === 'End') next = buttons.length - 1;
          else return;
          event.preventDefault();
          selectTab(buttons[next]);
          buttons[next].focus();
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
      const links = Array.from(document.querySelectorAll('.stdlib-module-link'));
      const emptyMessage = document.getElementById('stdlib-filter-empty');
      function filterModules() {
        const query = filterInput.value.toLowerCase().trim();
        let visibleCount = 0;
        links.forEach(link => {
          const visible = !query || link.textContent.toLowerCase().includes(query);
          const item = link.closest('li') || link;
          item.hidden = !visible;
          if (visible) visibleCount++;
        });
        if (emptyMessage) emptyMessage.hidden = visibleCount > 0;
      }
      filterInput.addEventListener('input', filterModules);
      filterModules();
    }
  }

  // --- Search System ---
  function initSearch() {
    const searchBackdrop = document.getElementById('search-modal-backdrop');
    const searchTriggerBtn = document.getElementById('search-trigger-btn');
    const searchCloseBtn = document.getElementById('search-close-btn');
    const searchInput = document.getElementById('search-input');
    const searchResults = document.getElementById('search-results');
    if (!searchBackdrop || !searchInput || !searchResults) return;

    let selectedIndex = 0;
    let previousFocus;
    let previousOverflow;
    searchInput.setAttribute('aria-controls', searchResults.id);

    fetch('search-index.json')
      .then(res => res.ok ? res.json() : null)
      .then(data => {
        if (Array.isArray(data) && data.length > 0) {
          searchIndex = data;
          if (searchBackdrop.open) renderResults(searchInput.value);
        }
      })
      .catch(() => {
        // The embedded index also works offline and via file://.
      });

    function openSearch() {
      if (searchBackdrop.open) {
        searchInput.focus();
        return;
      }
      previousFocus = document.activeElement;
      previousOverflow = document.body.style.overflow;
      searchInput.value = '';
      renderResults('');
      searchBackdrop.showModal();
      document.body.style.overflow = 'hidden';
      searchInput.focus();
    }

    function closeSearch() {
      if (searchBackdrop.open) searchBackdrop.close();
    }

    searchTriggerBtn?.addEventListener('click', openSearch);
    searchCloseBtn?.addEventListener('click', closeSearch);
    searchBackdrop.addEventListener('cancel', (event) => {
      event.preventDefault();
      closeSearch();
    });
    searchBackdrop.addEventListener('close', () => {
      document.body.style.overflow = previousOverflow;
      if (previousFocus?.isConnected) previousFocus.focus({ preventScroll: true });
    });
    searchBackdrop.addEventListener('click', (event) => {
      if (event.target === searchBackdrop) closeSearch();
    });
    searchResults.addEventListener('click', (event) => {
      if (event.target.closest('.search-result-item')) closeSearch();
    });

    window.addEventListener('keydown', (event) => {
      if (event.defaultPrevented || event.isComposing) return;
      const editable = event.target.closest('input, textarea, select, [contenteditable]:not([contenteditable="false"])');
      if (((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 'k') ||
          (event.key === '/' && !event.metaKey && !event.ctrlKey && !event.altKey && !editable)) {
        event.preventDefault();
        openSearch();
      }
    });

    function renderResults(query) {
      const q = query.trim().toLowerCase();
      selectedIndex = 0;
      const matches = q ? searchIndex.filter(item => {
        return item.title.toLowerCase().includes(q) ||
          item.snippet.toLowerCase().includes(q) ||
          item.category.toLowerCase().includes(q) ||
          (item.tags && item.tags.some(tag => tag.toLowerCase().includes(q)));
      }) : searchIndex.slice(0, 6);

      if (matches.length === 0) {
        searchResults.innerHTML = `
          <div class="search-empty">
            No documentation matching <strong>"${escapeHtml(query)}"</strong> found.
          </div>
        `;
        return;
      }

      searchResults.innerHTML = matches.map((item, idx) => `
        <a href="${escapeHtml(item.url)}" class="search-result-item ${idx === 0 ? 'selected' : ''}" data-index="${idx}">
          <div class="search-result-top">
            <span class="search-result-title">${highlightMatch(item.title, q)}</span>
            <span class="search-result-badge">${escapeHtml(item.category)}</span>
          </div>
          <p class="search-result-snippet">${highlightMatch(item.snippet, q)}</p>
        </a>
      `).join('');
    }

    function highlightMatch(text, query) {
      if (!query) return escapeHtml(text);
      const escapedQuery = query.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
      const regex = new RegExp(`(${escapedQuery})`, 'gi');
      return escapeHtml(text).replace(regex, '<mark class="search-highlight">$1</mark>');
    }

    searchInput.addEventListener('input', () => renderResults(searchInput.value));
    searchInput.addEventListener('keydown', (event) => {
      if (event.isComposing) return;
      const items = searchResults.querySelectorAll('.search-result-item');
      if (!items.length) return;
      if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
        event.preventDefault();
        items[selectedIndex].classList.remove('selected');
        selectedIndex = (selectedIndex + (event.key === 'ArrowDown' ? 1 : -1) + items.length) % items.length;
        items[selectedIndex].classList.add('selected');
        items[selectedIndex].scrollIntoView({ block: 'nearest' });
      } else if (event.key === 'Enter') {
        event.preventDefault();
        // Follow the real anchor, including its page and fragment, exactly once.
        items[selectedIndex].click();
      }
    });
  }

  // --- Scrollspy & TOC Synchronization ---
  function initScrollspy() {
    const sectionSelector = '.doc-section[id], .module-doc-section[id]';
    const sections = Array.from(document.querySelectorAll(sectionSelector));
    if (!sections.length) return;
    // Match anchor clearance as well as the header so initial deep links stay active.
    const topOffset = Math.max(112, parseFloat(getComputedStyle(document.documentElement).scrollPaddingTop) || 0);

    function sectionForHash(hash) {
      let id;
      try { id = decodeURIComponent(hash.slice(1)); } catch { return null; }
      const target = document.getElementById(id) || document.getElementById(id.replace(/-heading$/, ''));
      return target?.closest(sectionSelector);
    }

    const links = Array.from(document.querySelectorAll('.nav-item-link, .stdlib-module-link, .toc-link')).map(link => {
      const url = new URL(link.href, window.location.href);
      const samePage = url.origin === window.location.origin && url.pathname === window.location.pathname;
      return { link, section: samePage ? sectionForHash(url.hash) : null };
    });
    let activeId;
    function setActive(section) {
      if (!section || activeId === section.id) return;
      activeId = section.id;
      links.forEach(({ link, section: linkedSection }) => {
        const active = linkedSection?.id === activeId;
        link.classList.toggle('active', active);
        if (active) link.setAttribute('aria-current', 'location');
        else link.removeAttribute('aria-current');
      });
    }

    function updateFromPosition() {
      let current = sections[0];
      for (const section of sections) {
        if (section.getBoundingClientRect().top <= topOffset + 1) current = section;
        else break;
      }
      setActive(current);
    }

    function updateFromHash() {
      const section = sectionForHash(window.location.hash);
      if (section) setActive(section);
      else updateFromPosition();
    }

    updateFromHash();
    window.addEventListener('hashchange', updateFromHash);
    if (!('IntersectionObserver' in window)) return;

    let observer;
    function observeSections() {
      observer?.disconnect();
      // A narrow reading line below the fixed header works even for very tall sections.
      const bottomMargin = Math.max(0, window.innerHeight - topOffset - 2);
      observer = new IntersectionObserver(updateFromPosition, {
        rootMargin: `-${topOffset}px 0px -${bottomMargin}px 0px`,
        threshold: 0
      });
      sections.forEach(section => observer.observe(section));
    }
    observeSections();
    window.addEventListener('resize', observeSections);
  }

  // --- Collapsible Navigation Categories ---
  function initNavCollapsing() {
    let savedStates = {};
    try {
      const saved = JSON.parse(localStorage.getItem('fin-docs-nav-state') || '{}');
      if (saved && typeof saved === 'object' && !Array.isArray(saved)) savedStates = saved;
    } catch {
      // Ignore unavailable storage and corrupt state without breaking navigation.
    }

    document.querySelectorAll('.nav-section-header').forEach((btn, idx) => {
      const section = btn.closest('.nav-section');
      if (!section) return;
      const items = section.querySelector('.nav-section-items');
      const sectionKey = `section_${idx}`;
      if (items) {
        if (!items.id) items.id = `nav-section-items-${idx}`;
        btn.setAttribute('aria-controls', items.id);
      }

      function setCollapsed(collapsed) {
        section.classList.toggle('collapsed', collapsed);
        btn.setAttribute('aria-expanded', String(!collapsed));
        if (items) items.hidden = collapsed;
      }

      setCollapsed(savedStates[sectionKey] === true);
      btn.addEventListener('click', () => {
        savedStates[sectionKey] = !section.classList.contains('collapsed');
        setCollapsed(savedStates[sectionKey]);
        try {
          localStorage.setItem('fin-docs-nav-state', JSON.stringify(savedStates));
        } catch {
          // Collapsing remains usable when the preference cannot be saved.
        }
      });
    });
  }

  // --- Mobile Drawer Menu ---
  function initMobileMenu() {
    const mobileMenuBtn = document.getElementById('mobile-menu-btn');
    const sidebarNav = document.getElementById('sidebar-nav') || document.getElementById('stdlib-sidebar');
    const sidebarOverlay = document.getElementById('sidebar-overlay');
    if (!mobileMenuBtn || !sidebarNav || !sidebarOverlay) return;
    const mobileViewport = window.matchMedia('(max-width: 1023px)');
    let isOpen = false;
    let previousFocus;
    let previousOverflow;
    mobileMenuBtn.setAttribute('aria-controls', sidebarNav.id);
    mobileMenuBtn.setAttribute('aria-expanded', 'false');
    sidebarNav.inert = mobileViewport.matches;

    function focusableItems() {
      return Array.from(sidebarNav.querySelectorAll('a[href], button, input, select, textarea, [tabindex]'))
        .filter(el => el.tabIndex >= 0 && !el.disabled && el.getClientRects().length && getComputedStyle(el).visibility !== 'hidden');
    }

    function openMenu() {
      if (isOpen || !mobileViewport.matches) return;
      previousFocus = document.activeElement;
      previousOverflow = document.body.style.overflow;
      isOpen = true;
      sidebarNav.inert = false;
      sidebarNav.classList.add('drawer-open');
      sidebarOverlay.classList.add('active');
      mobileMenuBtn.setAttribute('aria-expanded', 'true');
      document.body.style.overflow = 'hidden';
      (focusableItems()[0] || mobileMenuBtn).focus({ preventScroll: true });
    }

    function closeMenu(restoreFocus = true) {
      if (isOpen) {
        isOpen = false;
        document.body.style.overflow = previousOverflow;
        if (restoreFocus) {
          const target = previousFocus?.isConnected && previousFocus !== document.body ? previousFocus : mobileMenuBtn;
          target.focus({ preventScroll: true });
        }
      }
      sidebarNav.classList.remove('drawer-open');
      sidebarOverlay.classList.remove('active');
      sidebarNav.inert = mobileViewport.matches;
      mobileMenuBtn.setAttribute('aria-expanded', 'false');
    }

    mobileMenuBtn.addEventListener('click', () => isOpen ? closeMenu() : openMenu());
    sidebarOverlay.addEventListener('click', () => closeMenu());
    sidebarNav.addEventListener('click', (event) => {
      if (event.target.closest('a[href]')) closeMenu();
    });
    window.addEventListener('keydown', (event) => {
      if (!isOpen || document.getElementById('search-modal-backdrop')?.open) return;
      if (event.key === 'Escape') {
        event.preventDefault();
        closeMenu();
      } else if (event.key === 'Tab') {
        const items = [mobileMenuBtn, ...focusableItems()];
        const index = items.indexOf(document.activeElement);
        const next = (index + (event.shiftKey ? -1 : 1) + items.length) % items.length;
        event.preventDefault();
        items[next].focus();
      }
    });
    mobileViewport.addEventListener('change', () => {
      const focusWasInside = sidebarNav.contains(document.activeElement);
      closeMenu(false);
      if (mobileViewport.matches && focusWasInside) mobileMenuBtn.focus({ preventScroll: true });
    });
  }

  // --- Small, Interruptible Motion ---
  const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)');
  const runningAnimations = new Set();

  function animate(element, keyframes, options, cleanup = () => {}) {
    if (reducedMotion.matches || !element.animate) {
      cleanup();
      return null;
    }
    const animation = element.animate(keyframes, options);
    runningAnimations.add(animation);
    const finish = () => {
      runningAnimations.delete(animation);
      cleanup();
    };
    animation.addEventListener('finish', finish, { once: true });
    animation.addEventListener('cancel', finish, { once: true });
    return animation;
  }

  reducedMotion.addEventListener('change', () => {
    if (!reducedMotion.matches) return;
    runningAnimations.forEach(animation => animation.cancel());
    document.querySelectorAll('.copy-sparkle').forEach(sparkle => sparkle.remove());
  });

  function showCopySparkles(button) {
    if (reducedMotion.matches || !button.animate) return;
    const rect = button.getBoundingClientRect();
    for (let i = 0; i < 6; i++) {
      const sparkle = document.createElement('span');
      sparkle.className = 'copy-sparkle';
      sparkle.setAttribute('aria-hidden', 'true');
      sparkle.textContent = ['♥', '✦', '♡'][i % 3];
      sparkle.style.left = `${rect.left + rect.width / 2}px`;
      sparkle.style.top = `${rect.top + rect.height / 2}px`;
      document.body.appendChild(sparkle);
      const angle = (i / 6) * Math.PI * 2;
      const x = Math.cos(angle) * 48;
      const y = Math.sin(angle) * 32 - 28;
      animate(sparkle, [
        { opacity: 1, transform: 'translate(-50%, -50%) scale(.65)' },
        { opacity: 0, transform: `translate(calc(-50% + ${x}px), calc(-50% + ${y}px)) scale(1)` }
      ], { duration: 560, easing: 'cubic-bezier(.16, 1, .3, 1)' }, () => sparkle.remove());
    }
  }

  function initGentleMotion() {
    document.querySelectorAll('[data-logo-placeholder]').forEach(logo => {
      const face = logo.querySelector('.mascot-face');
      if (!face) return;
      const originalFace = face.textContent;
      let wink;
      function restoreFace() {
        wink?.cancel();
        face.textContent = originalFace;
      }
      logo.addEventListener('pointerenter', () => {
        if (reducedMotion.matches) return;
        restoreFace();
        face.textContent = originalFace.includes('^') ? originalFace.replace('^', '-') : '-ᴗ^';
        wink = animate(logo, [
          { transform: 'rotate(0deg)' },
          { transform: 'rotate(-7deg)', offset: 0.45 },
          { transform: 'rotate(0deg)' }
        ], { duration: 240, easing: 'ease-out' });
      });
      logo.addEventListener('pointerleave', restoreFace);
      logo.addEventListener('pointercancel', restoreFace);
      reducedMotion.addEventListener('change', restoreFace);
    });

    const greet = document.getElementById('mascot-greet');
    if (greet) {
      greet.hidden = false;
      const message = document.getElementById('mascot-message');
      const logo = document.querySelector('.companion [data-logo-placeholder]');
      const face = logo.querySelector('.mascot-face');
      const originalFace = face.textContent;
      let resetFace;
      greet.addEventListener('click', () => {
        clearTimeout(resetFace);
        message.textContent = 'Hello, friend! You bring the ideas. I’ll bring the pink.';
        face.textContent = '♥ᴗ♥';
        showCopySparkles(logo);
        animate(logo, [
          { transform: 'translateY(0) rotate(0deg)' },
          { transform: 'translateY(-7px) rotate(-8deg)', offset: 0.3 },
          { transform: 'translateY(-3px) rotate(6deg)', offset: 0.65 },
          { transform: 'translateY(0) rotate(0deg)' }
        ], { duration: 420, easing: 'ease-out' });
        resetFace = setTimeout(() => { face.textContent = originalFace; }, 1600);
      });
    }

    document.querySelectorAll('.start-link, .mascot-greet').forEach((element, index) => {
      animate(element, [
        { opacity: 0.65, transform: 'translateY(5px)' },
        { opacity: 1, transform: 'translateY(0)' }
      ], { duration: 260, delay: index * 45, easing: 'ease-out' });
    });

    if (!('IntersectionObserver' in window)) return;
    const observer = new IntersectionObserver(entries => {
      entries.forEach(entry => {
        if (!entry.isIntersecting) return;
        observer.unobserve(entry.target);
        // Never hide text: only a light arrival for sections newly entering below the fold.
        if (entry.boundingClientRect.top < 112) return;
        animate(entry.target, [
          { opacity: 0.85, transform: 'translateY(8px)' },
          { opacity: 1, transform: 'translateY(0)' }
        ], { duration: 240, easing: 'cubic-bezier(.16, 1, .3, 1)' });
      });
    }, { threshold: 0 });
    document.querySelectorAll('.doc-section, .module-doc-section').forEach(section => observer.observe(section));
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
    initGentleMotion();
  });

})();

