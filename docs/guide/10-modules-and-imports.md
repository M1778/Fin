# 10. Modules and imports

A module is a `.fin` file. `finc` takes one root source file; imported declarations
are resolved from that root. This repository does not define a package-manager
manifest format. `finn` is a separate project.

## Import spellings

These are fragments showing the four forms:

```fin fragment
import { Collection } from collection::std;
import networking;
import stdio::std as io;
import * from types::std;
```

A selected import brings names into the current scope. A whole-module import
binds the module name; an alias changes that name. A module function call uses a
dot, such as `io.printf(...)`.

`collection::std` means module `collection`, namespace `std` inside it. The selector
is not a directory separator. Its validation has known gaps; use the namespace
that the module actually declares.

This qualified ambient function call builds:

```fin
import stdio::std as io;
fun main() <noret> { io.printf("module call\n"); }
```

```output
module call
```

## What crosses the backend boundary

Imported generic structs can instantiate in the root program. Whether an ordinary
imported Fin function body reaches the backend depends on the call: many library
calls (`println_str`, `len`, `split_str`, `replace`, `print_bool`, `copy_file`)
build and run, while other paths are still refused. Resolving the function's
signature does not by itself make a call executable, so build the program to
confirm. This example builds and runs:

```fin
import { println_str } from stdio::std;
fun main() <noret> { println_str("hello"); }
```

```output
hello
```

For a small executable, keep ordinary application functions in the root source
until the needed module path is supported. Do not change a helper into a generic
solely to work around code generation. An imported template can still reach an
unsupported method, free function, or erased operation when instantiated.

A complete two-file example of the supported generic-struct path lives in
[examples/modules/main.fin](../examples/modules/main.fin) and
[examples/modules/box.fin](../examples/modules/box.fin):

```sh
finc docs/examples/modules/main.fin -o module-demo
./module-demo
```

The documentation checker builds and runs those files too.

## Same-named declarations in two modules

A call inside an imported module resolves to that module's own same-named
declaration, regardless of the root program's import order. `strings::std` and
`path::std` both declare `join` with different signatures, and `strings::std`
`replace` calls `join` on its split parts: that inner call reaches strings'
`join` even when `path::std` is imported first. (No ADR records this rule yet;
it is covered by `Soundness_Modules.AModuleBodyCallsItsOwnModulesFunctionWhateverTheImportOrder`
in `tests/test_stdlib.cpp`.)

## Quoted and pathless imports

`import { Box } from "box.fin";` searches relative to the importing file first.
Use quoted paths for sibling files when you want source-relative resolution.
A pathless import such as `import collection;` searches configured paths instead.

The configured order is:

1. Directories from `-I` / `--include`, in written order.
2. Paths from `--fin-libs` if supplied; otherwise nonempty `FIN_LIBS`.
3. The bundled `<binary-directory>/../lib/std`, if no library override was supplied.
4. The working directory, under the same no-override condition.

`-I` does **not** suppress the bundle. `--fin-libs` replaces the environment's
library list; it does not append. Explicit `--fin-libs=` suppresses the bundle
and working-directory fallback. An empty `FIN_LIBS` counts as unset.

Each base tries the name itself, `name.fin`, then `name/index.fin`. Quoted imports
also try absolute paths and the configured paths after source-relative lookup.
A missing module diagnostic lists the searched directories.

```sh
finc main.fin -I ./src -I ./vendor
finc main.fin --fin-libs /path/to/Fin/lib/std:/path/to/vendor
```

Use `;` instead of `:` for a Windows library path list, and quote the whole list
in a shell. Keep an explicit standard-library path when pinning dependencies.

## Namespaces, visibility, and aliases

Namespace declarations group names. Mark intended exports with `pub` and, where
used by the library, `#[export]`. The export loader does not fully enforce every
visibility boundary yet; acceptance is not permission to rely on private names.

```fin fragment
namespace geometry {
    pub struct Point { pub x <int>, pub y <int> }
}
```

Import a selected namespace with `import { Point } from geometry_module::geometry;`.
Same-file qualified namespace expressions and renamed symbols have lowering gaps.
`extern geometry::Point as Point;` introduces a symbol alias without importing a
file; `extern * from geometry;` lifts names. Use the simplest spelling that the
compiler can build for the actual use site.

`#[global]` permits an ambient declaration only inside `namespace std`. The live
publication path covers foreign declarations such as `printf`; it is not a way
for an application to install arbitrary global functions. `format!` is separately
implemented as a compiler builtin and needs no import.

Next: [macros and preprocessing](11-macros-and-preprocessor.md).