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
    { id: "stdlib-overview", title: "17 Core Standard Library Modules Directory", category: "Standard Library", tags: ["stdlib", "modules", "stdio", "strings", "fs", "math", "random", "env", "path", "time", "collection", "hashmap", "error", "types", "typing", "enums", "operators", "stdptr", "networking"], snippet: "Interactive directory and category filters for all 17 Fin standard library modules engineered with modular memory management.", url: "#stdlib-overview" },
    { id: "pipeline-architecture", title: "The Fin Compiler Pipeline Flow & Refusal Invariants", category: "Compiler Architecture", tags: ["pipeline", "llvm", "ast", "lexer", "parser", "semantics", "codegen", "refusal", "invariants"], snippet: "Six-stage compiler flow from Source through Bison Parser, AST cloning, Two-Moment semantics, and LLVM lowering with strict refusal invariants.", url: "#pipeline-architecture" },
    { id: "intro", title: "Introduction to Fin", category: "Getting Started", tags: ["philosophy", "systems", "memory", "invariants"], snippet: "Fin is a modern systems programming language where memory management is a library, the sample corpus is the normative specification, and the compiler refuses silently guessing IR.", url: "#intro" },
    { id: "installation", title: "Installation & Build", category: "Getting Started", tags: ["build", "cmake", "conan", "llvm", "finc", "cli", "flags"], snippet: "Build finc from source using ./build.sh with CMake, Conan, Flex, Bison, and LLVM. Check installation with finc --version.", url: "#installation" },
    { id: "quickstart", title: "Quickstart Guide", category: "Getting Started", tags: ["quickstart", "workflow", "compiler", "check", "compile"], snippet: "Create and compile Fin programs using finc. Learn the difference between type-checking with finc file.fin and compiling binaries with finc -o bin.", url: "#quickstart" },
    { id: "hello-world", title: "Hello, World!", category: "Getting Started", tags: ["hello world", "printf", "main", "noret", "variadic", "define"], snippet: "Your first Fin program using @define printf and fun main() <noret>. Learn about angle-bracket return types and C FFI bindings.", url: "#hello-world" },
    { id: "variables-types", title: "Variables & Types", category: "Language Tour", tags: ["let", "auto", "const", "readonly", "types", "nullable", "denullify"], snippet: "Local variable declarations with let x <int> = 100, type inference with auto, primitive types, const parameters, and nullable T? with denullify postfix ?.", url: "#variables-types" },
    { id: "control-flow", title: "Control Flow", category: "Language Tour", tags: ["if", "else", "for", "foreach", "while", "do while", "ternary", "scope"], snippet: "Branching with if/else, three-clause for loops, foreach iterators, while and do-while loops, bare brace scopes, and Fin's ternary condition : then ? otherwise.", url: "#control-flow" },
    { id: "functions", title: "Functions & Lambdas", category: "Language Tour", tags: ["fun", "noret", "void", "generics", "lambdas", "const", "parameters", "define"], snippet: "Declare functions with fun name(param: Type) <ReturnType>. Supports generic functions, const parameters, first-class lambdas, and @define extern bindings.", url: "#functions" },
    { id: "structs-classes", title: "Structs & Methods", category: "Language Tour", tags: ["struct", "class", "self", "methods", "fields", "constructors", "pub", "priv", "operator"], snippet: "Define data structures with struct and class. Method definitions with explicit self: &Self receiver, default values, visibility labels, and operator overloading.", url: "#structs-classes" },
    { id: "interfaces", title: "Interfaces & Generics", category: "Language Tour", tags: ["interface", "implements", "generics", "bounds", "constraints"], snippet: "Define polymorphic contracts with pub interface. Nominal conformance on structs, external implements blocks, generic bounds, and constraint sets.", url: "#interfaces" },
    { id: "enums", title: "Enums & Tagged Unions", category: "Language Tour", tags: ["enum", "tagged union", "discriminant", "payload", "result", "getkeyid", "keyidof"], snippet: "Fieldless enums and tagged union enums with member payloads. 32-bit discriminants (ADR 0041), positional access enum_.0, and enum reflection.", url: "#enums" },
    { id: "pointers-memory", title: "Pointers & Memory (rptr, wptr)", category: "Language Tour", tags: ["pointer", "reference", "new", "delete", "rptr", "wptr", "ownership", "borrow"], snippet: "Raw references &T, heap dynamic allocation new [T, n]{}, and standard library smart pointers rptr<T> and wptr<T> with sound ownership and borrow tracking.", url: "#pointers-memory" },
    { id: "blame", title: "Blame Error Handling", category: "Language Tour", tags: ["blame", "m1778", "assert", "raise", "error", "unimplemented"], snippet: "Fin's unified error construct: blame condition, 'message' for assertions, blame Error(...) for raising, and blame m1778 for unimplemented paths.", url: "#blame" },
    { id: "prototypes", title: "Prototypes {K,V}", category: "Language Tour", tags: ["prototype", "structural", "map", "dict", "hashmap", "collection"], snippet: "Builtin structural map type {K,V} and literal syntax {'key': value}. Structural equivalence and explicit conversion to nominal HashMap and Collection.", url: "#prototypes" },
    { id: "macros-specials", title: "Macros & Special Functions", category: "Language Tour", tags: ["macro", "special", "quote", "bracket", "comptime", "compiler api", "grant"], snippet: "Compile-time metaprogramming: @macro returning quote AST blocks, bracket shaping (name!, name![], name!{}), @special functions, and #[use(...)] grants.", url: "#macros-specials" },
    { id: "stdlib-stdio", title: "std::stdio — Standard I/O & Files", category: "Standard Library", tags: ["stdio", "printf", "format", "print", "println", "Stream", "File", "FileIO"], snippet: "I/O primitives including printf, format!, typed print/println with Printable interface, stderr eprint, byte Stream, and FileIO static File operations.", url: "#stdlib-stdio" },
    { id: "stdlib-math", title: "std::math — Mathematical Functions", category: "Standard Library", tags: ["math", "PI", "E", "sqrt", "pow", "sin", "cos", "min", "max", "clamp", "abs", "gcd"], snippet: "Constants PI and E, generic Number helpers (min, max, clamp, abs, signum, gcd), integer ipow/isqrt, and C libm bindings (sqrt, pow, log, trig).", url: "#stdlib-math" },
    { id: "stdlib-strings", title: "std::strings — String Manipulation", category: "Standard Library", tags: ["strings", "len", "equals", "concat", "substr", "to_chars", "from_chars", "trim"], snippet: "String operations and byte utilities: len, equals (content comparison vs == pointer equality), concat, substr, to_chars, from_chars, trim, and case conversion.", url: "#stdlib-strings" },
    { id: "stdlib-path", title: "std::path — File Paths", category: "Standard Library", tags: ["path", "join", "basename", "dirname", "extname", "is_absolute", "free_path"], snippet: "Filesystem path manipulation: path joining, basename/dirname extraction, extension query, absolute path detection, and memory management.", url: "#stdlib-path" },
    { id: "stdlib-env", title: "std::env — Environment & Process", category: "Standard Library", tags: ["env", "getenv", "setenv", "unsetenv", "get_env", "has_env", "set_env", "get_pid"], snippet: "Environment inspection and process controls: get_env, has_env, set_env, unset_env, get_pid, and exit code handling via C runtime.", url: "#stdlib-env" },
    { id: "stdlib-time", title: "std::time — Clocks & Sleep", category: "Standard Library", tags: ["time", "now", "sleep_sec", "sleep_ms", "diff_sec", "clock", "timestamp"], snippet: "System clock and delay utilities: now() Unix timestamp, sleep_sec, sleep_ms (microsecond sleep), and diff_sec duration measurement.", url: "#stdlib-time" },
    { id: "stdlib-fs", title: "std::fs — File System I/O", category: "Standard Library", tags: ["fs", "file_exists", "remove_file", "file_size", "read_to_string", "write_string"], snippet: "Filesystem operations: file_exists, remove_file, file_size, read_to_string with nullable return, write_string, append_string, and buffer deallocation.", url: "#stdlib-fs" },
    { id: "stdlib-random", title: "std::random — Pseudo-Random Numbers", category: "Standard Library", tags: ["random", "seed", "seed_now", "rand", "random_int", "random_float"], snippet: "Pseudo-random number generation: seed, seed_now with system epoch, rand, uniform random_int(min, max), random_float, and random_bool.", url: "#stdlib-random" },
    { id: "stdlib-collection", title: "std::collection — Dynamic Array", category: "Standard Library", tags: ["collection", "array", "vector", "push", "pop_last", "insert", "remove", "len", "capacity"], snippet: "Dynamic array Collection<T> with geometric doubling: push, pop_last, get, set, insert, remove, is_empty, first, last, reverse, and bounds checking.", url: "#stdlib-collection" },
    { id: "stdlib-hashmap", title: "std::hashmap — Hash Table", category: "Standard Library", tags: ["hashmap", "map", "table", "get_index", "exists", "with_hasher", "linear probing"], snippet: "Real open-addressing hash table with linear probing and 0.75 load factor: exists, __get, __set, indexing operators, and custom hashers.", url: "#stdlib-hashmap" },
    { id: "stdlib-error", title: "std::error — Error Base Class", category: "Standard Library", tags: ["error", "IOError", "err_code", "message", "describe", "format"], snippet: "Standard Error base class: message, err_code, format, and describe with C snprintf formatting. Inheritable by custom library errors.", url: "#stdlib-error" },
    { id: "stdlib-types", title: "std::types — Numeric Aliases & Conversions", category: "Standard Library", tags: ["types", "i8", "u8", "usize", "isize", "byte", "Number", "Integer", "number2str"], snippet: "Sized integer aliases (i8-u64, usize, byte), constraint sets (Number, Integer, Float, Signed, Unsigned), and string/number conversions.", url: "#stdlib-types" },
    { id: "stdlib-typing", title: "std::typing — Result<T, E> & Interfaces", category: "Standard Library", tags: ["typing", "Result", "unwrap", "unwrap_or", "expect", "is_ok", "is_err"], snippet: "The Result<T, E> sum-type enum: Ok(T) and Err(E), with methods unwrap, unwrap_or, expect, select, is_ok, and is_err.", url: "#stdlib-typing" },
    { id: "stdlib-enums", title: "std::enums — Enum Reflection", category: "Standard Library", tags: ["enums", "getkeyid", "keyidof", "Enum", "EnumType", "discriminant"], snippet: "Enum reflection intrinsics: getkeyid(enum_val) reads the runtime discriminant, and keyidof(Member) resolves compile-time member ID.", url: "#stdlib-enums" },
    { id: "stdlib-operators", title: "std::operators — Operator Overloading", category: "Standard Library", tags: ["operators", "Equal", "Add", "Sub", "Mul", "Div", "Index", "IndexAssign"], snippet: "Thirty operator interfaces in std::ops: Equal, NotEqual, GreaterThan, LessThan, Add, Sub, Mul, Div, Mod, Index, IndexAssign, and bitwise contracts.", url: "#stdlib-operators" },
    { id: "stdlib-stdptr", title: "std::stdptr — Smart Pointers (rptr, wptr)", category: "Standard Library", tags: ["stdptr", "rptr", "wptr", "own", "borrow", "giveback", "release"], snippet: "Reference-counted pointer rptr<T> and weak handle wptr<T>: shared reference and borrow counters, sound aliasing refusal, and RAII-style release.", url: "#stdlib-stdptr" },
    { id: "stdlib-networking", title: "std::networking — Socket Facilities & Status", category: "Standard Library", tags: ["networking", "sockets", "tcp", "udp", "libc", "status"], snippet: "Architectural status of socket facilities in Fin: specification roadmap, libc socket bridging, and design invariants under ADR 0008.", url: "#stdlib-networking" },
    { id: "arch-invariants", title: "Architecture Invariants & ADRs", category: "Compiler Architecture", tags: ["architecture", "adr", "spec", "normative", "samples", "invariants"], snippet: "Fin's design discipline: ADR 0001 through 0042, normative sample authority, expectations (ok, error, unimplemented), and held rulings.", url: "#arch-invariants" },
    { id: "arch-refusals", title: "Compiler Refusal Invariants", category: "Compiler Architecture", tags: ["refusal", "invariant", "codegen", "soundness", "explicit error"], snippet: "Non-negotiable backend invariant: if codegen cannot lower a construct, it reports an explicit refusal naming the construct. Never drop code or emit guessed IR.", url: "#arch-refusals" },
    { id: "arch-llvm", title: "LLVM Backend Lowering", category: "Compiler Architecture", tags: ["llvm", "backend", "codegen", "tagged discriminant", "tagged union"], snippet: "Fresh LLVM backend (ADR 0002): tagged discriminant aggregates for nullables (ADR 0040), tagged unions for enums (ADR 0041), and typed pointer maps.", url: "#arch-llvm" }
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
    const codeBlocks = document.querySelectorAll('pre code.language-fin');
    codeBlocks.forEach(codeEl => {
      let code = codeEl.textContent;

      // Tokenize safely using placeholders for strings and comments
      const tokens = [];
      let temp = code;

      // 1. Strings (including string interpolation \{...\})
      temp = temp.replace(/("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')/g, (match) => {
        const id = `__STR_${tokens.length}__`;
        // highlight interpolation inside strings
        const formatted = escapeHtml(match).replace(/(\\?\{[^}]+\})/g, '<span class="tok-meta">$1</span>');
        tokens.push({ id, html: `<span class="tok-str">${formatted}</span>` });
        return id;
      });

      // 2. Comments (single-line & multi-line)
      temp = temp.replace(/(\/\/[^\n]*|\/\*[\s\S]*?\*\/)/g, (match) => {
        const id = `__COM_${tokens.length}__`;
        tokens.push({ id, html: `<span class="tok-com">${escapeHtml(match)}</span>` });
        return id;
      });

      // 3. Attributes / Decorators (#[...])
      temp = temp.replace(/(#\[[^\]\n]+\])/g, (match) => {
        const id = `__ATTR_${tokens.length}__`;
        tokens.push({ id, html: `<span class="tok-attr">${escapeHtml(match)}</span>` });
        return id;
      });

      // Escape HTML in the remaining text
      temp = escapeHtml(temp);

      // 4. Meta directives, specials & macros (@Alloc, @define, @macro, quote, $var)
      temp = temp.replace(/(@[a-zA-Z_]\w*|quote|\$[a-zA-Z_]\w*)/g, '<span class="tok-meta">$1</span>');

      // 5. Blame & m1778
      temp = temp.replace(/\b(blame|m1778)\b/g, '<span class="tok-blame">$1</span>');

      // 6. Keywords
      temp = temp.replace(/\b(fn|fun|let|var|val|pub|priv|static|const|readonly|if|else|for|foreach|while|do|defer|return|struct|class|interface|implements|enum|type|namespace|import|export|new|delete|cast|sizeof|as|from|switch|case|default|in)\b/g, '<span class="tok-kw">$1</span>');

      // 7. Types & Allocators
      temp = temp.replace(/\b(int|uint|short|ushort|long|ulong|float|double|bool|char|string|void|noret|any|object|auto|rptr|wptr|Self|i8|u8|i16|u16|i32|u32|i64|u64|f32|f64|usize|isize|byte|Number|Integer|Float|Signed|Unsigned|Arena|Buffer|Stream|File)\b/g, '<span class="tok-type">$1</span>');

      // 8. Numbers (decimal, float, hex)
      temp = temp.replace(/\b(\d+(?:\.\d+)?(?:e[+-]?\d+)?|0x[0-9a-fA-F]+)\b/g, '<span class="tok-num">$1</span>');

      // Restore tokens
      tokens.forEach(t => {
        temp = temp.replace(t.id, t.html);
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
    if (filterButtons.length === 0 || cards.length === 0) return;

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

