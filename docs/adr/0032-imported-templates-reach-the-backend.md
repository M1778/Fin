# Imported templates reach the backend through a loader-backed registry

`finn` compiles one root file per invocation and hands `finc` source
directories (`-I`, `--fin-libs`); it builds no per-dependency objects, passes
none to link, and pins sources by content hash. Whole-program source
visibility is therefore the model the package manager already assumes, which
rules out true separate compilation with externs: there is nothing to declare
against and no link step to satisfy.

So the backend reads imported declarations out of the module loader's cache
instead. `ModuleLoader` keeps every loaded `Program` in `astStorage`; the
driver hands those Programs to codegen alongside the root, and only the
*registration* passes walk them: templates, interfaces, enums, `implements`
blocks, and generic functions. Emission stays root-only, and generic
instantiation stays lazy per use with the `LinkOnceODR` linkage the backend
already gives shared symbols. A concrete struct or function from a module is
never declared or emitted from the import side.

## Considered Options

- Clone-splice imported declarations into the root: reuses all passes
  unchanged but lowers eagerly, dragging in unused unlowerable declarations
  (`error.fin`'s `#[uncastable]`) and duplicating every concrete definition.
- True separate compilation with externs: contradicts `finn` everywhere (no
  objects built, no link flag, content hashes rather than ABI hashes) and
  would need a new cross-repo contract plus a generic-instantiation protocol.

## Consequences

The backend borrows module ASTs and must not outlive the loader; the driver
owns that ordering. `parentIsInterface` and `extrasFor` answer from the union
of root and modules, so an imported interface is never misread as a base
struct. The next refusals after unblocking move to what instantiation needs:
transitive templates, `any`/`fn` field types, and callee bodies.
