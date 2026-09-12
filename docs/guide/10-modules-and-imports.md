# 10. Modules and imports

A module is a `.fin` file. There is no manifest and no module declaration — importing a file
is what makes it a module.

Before the details, one limitation that shapes this whole chapter: a call to an imported
function is not yet lowered to machine code. An importing program type-checks cleanly and
then reports `codegen: a call to 'add' is not lowered yet` under `-o`. So everything below
is a type-checking story today, and the examples are shown being checked rather than built.

## `import`

Four forms, in increasing breadth.

**Selected symbols** — brings exactly the named symbols in:

```fin
import { HashMap } from hashmap::std;
import { Collection } from collection::std;
```

**Whole module** — brings the module in under its own name:

```fin
import networking;
```

**Renamed** — `as` gives the module a local name:

```fin
import networking as net;
import stdio::std as stdio;
```

The renamed form is how a program can hold two things of the same name at once. In
`tests/samples/complex.fin`, `stdio.printf(...)` reaches the library's and a bare `printf`
reaches a locally `@define`d one.

**Everything** — lifts every exported symbol into the importing scope:

```fin
import * from somelib;
import * from somelib::std;
```

## The `::` selector

`from <module>::<namespace>` imports from a namespace inside a module:

```fin
import { HashMap } from hashmap::std;
```

That reads: from the module `hashmap`, from the namespace `std` inside it, take `HashMap`.
Without it you would write `std::HashMap` at every use site.

One caveat: the selector is not actually checked yet, so a wrong namespace name is
currently accepted. Write it correctly anyway.

## Quoted paths

A quoted import names a file rather than a module:

```fin
import { Vector3 } from "tests/samples/structs.fin";
import "tests/samples/macros.fin";
```

A quoted path is tried in three places, in order: relative to the importing file, as an
absolute path, then through the search paths. The last step ends in the working directory,
which means a relative quoted path can resolve from one directory and fail from another.
Prefer paths relative to the importing file, or pathless module names with `-I`.

## How a pathless module is found

`import somelib;` gives the compiler no path. It looks in, in order:

1. every path given with `-I` / `--include`
2. the library search paths — `--fin-libs` if it was given, otherwise `$FIN_LIBS`
3. the standard library bundled beside the compiler, *only* if neither of the above named
   any paths
4. the working directory, *only* if neither named any paths

For each candidate base it tries `<base>/<name>`, then `<base>/<name>.fin`, then
`<base>/<name>/index.fin`. That third candidate is why a library can be a directory: the
bundled `somelib` is `lib/std/somelib/index.fin`.

Two design points in that list are deliberate. `--fin-libs` **replaces** `$FIN_LIBS` rather
than extending it, and naming library paths suppresses both the bundled standard library and
the working directory — a build that pins its paths gets exactly those paths, and nothing can
appear underneath the pin. That is what makes a package manager able to guarantee what a
build compiled against.

A missing module names what it searched:

```
error: module not found: missingmod
   = help: searched: /home/M1778/Fin/lib/std, .
```

## `namespace`

`namespace` groups declarations. Members are reached with `::`:

```fin
namespace tools {

pub fun triple(x: int) <int> {
    return x * 3;
}

}
```

Namespaces nest — `lib/std/operators.fin` declares `namespace std { namespace ops { ... } }`.

To use a namespaced name from another file, import it with the selector:

```fin
import { triple } from libns::tools;

fun main() <noret> {
    let x <int> = triple(3);
}
```

`import * from libns::tools;` lifts everything out of that namespace at once.

Within a single file, reaching a namespace member through `namespace::name` at an expression
site does not resolve yet — `tools::triple(3)` in the same file reports `Undefined type
'tools'`. Use `extern ... as` (below) to give it a local name.

## `pub` and `#[export]`

`pub` marks a declaration visible outside its namespace. `#[export]` marks it part of the
module's public surface. The standard library writes both:

```fin
namespace std {

#[export]
pub struct CollectionError : <Error> {}

}
```

`#[export]` also has a block form, applying to several declarations at once:

```fin
#[export] %{
pub type i32 = int;
pub type i64 = int{64};
pub type u32 = uint;
}%
```

Importing a name a module does not export is a diagnostic:

```
error: Module 'libns' does not export 'nosuch'
```

The export check is on the name being in the module, not yet on its visibility — importing a
non-`pub` name currently succeeds. Write `pub` on everything you intend to be importable.

## `extern ... as`

`extern ... as` renames a symbol without importing anything. It gives an existing name a
second, equivalent spelling.

For a plain variable:

```fin
const myglobv <int> = 10;
extern myglobv as myglobv_diffname;   // same variable, second name
```

For a namespace member, which is where it earns its place:

```fin
@define printf(fmt: string, ...) <noret>;

namespace geometry {

pub fun area(w: int, h: int) <int> {
    return w * h;
}

}

extern geometry::area as area;

fun main() <noret> {
    printf("%d\n", area(3, 4));
}
```

The shortcut travels: a file importing this one can write `import { area } from thisfile;`
rather than reaching into the namespace.

`extern * from <namespace>` lifts every symbol out — the equivalent of C++'s `using
namespace`:

```fin
namespace a_namespace {
pub fun a() <void> {}
pub fun b() <void> {}
}

extern * from a_namespace;
```

It works on enums too, which is the use you will meet most often (chapter 8):

```fin
enum MyEnum { A, B, C }
extern * from MyEnum;
```

`extern int as Integer;` also works for a type, but `type Integer = int;` says the same thing
more clearly — `extern as` is for symbols and namespaces. In practice, prefer imports across
files: `import * from a_library::a_namespace` does the same job where the need usually arises.

## `#[global]`

One mechanism escapes the import system entirely. A declaration marked `#[global]` is visible
to every file in the compilation with no import written anywhere:

```fin
#[llvm_name="printf"]
#[global]
#[export]
@define printf(fmt: string, ...) <noret>;
```

That is `lib/std/stdio.fin`, and the marked set is exactly two names — `printf` and
`format!` (of which only `printf` is live; `format!` is a macro and macros are not published
yet). The attribute is opt-in per declaration and legal **only inside `namespace std`**.
Written anywhere else it is an error, not a warning:

```
error: #[global] is usable only inside `namespace std` (ADR 0021)
```

The reason for the confinement is that one ambient name is a convenience while an arbitrary
library's ability to mint them is a collision generator — and the collision would surface in
a third file that imported neither party. An attribute rather than a compiler-injected
prelude, because an attribute is written on the declaration, so the file that declares the
name is the file that says how far it reaches, at per-declaration granularity.

An ambient name is not just resolvable — it *builds*. A file that calls `printf` with no
declaration and no import compiles to a working executable, because the compiler splices the
prototype for each published extern into the program before the backend runs. This is the one
place where a name reaches the backend from another file; a call to an ordinary imported
function still refuses, as the top of this chapter says.

The mechanism is limited to `@define` — an extern is a symbol and a signature with nothing to
emit, so a copy of one is free. `#[global]` on a `fun`, a `struct`, an `interface`, an `enum`
or a `type` parses and validates and then publishes nothing, so a consuming file still
reports the name as undefined. Only the extern form works today.

Note also that `#[global]` answers only the "used bare and declared nowhere" case. It does
*not* make a `pub` declaration in `namespace std` visible to a sibling file that does not
import it — `Any`, `Enum`, `getkeyid` and `keyidof` all remain import-only. A file that uses
one owes an import. The full argument is in
`docs/adr/0021-a-global-declaration-needs-no-import-and-only-std-may-mark-one.md`.

Next: [macros and the preprocessor](11-macros-and-preprocessor.md).
