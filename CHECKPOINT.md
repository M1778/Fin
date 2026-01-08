# Fin Compiler Development Checkpoint

## Current Status
- [x] Keywords removed: `bez`, `beton`, `@return`
- [x] `noret` refactored to `void`
- [x] Global variables with `pub`/`priv`/attributes supported
- [x] Pointer ambiguity `&&T` resolved
- [x] Visibility blocks `pub:` / `priv:` supported
- [x] Operator declarations enhanced (optional parens, `implements`)
- [x] `blame` upgraded to support assert-like syntax: `blame condition, "message";`
- [x] Rust-like macro definitions implemented: `macro name { (pattern) => { expansion } }`
- [x] `prototype` literal support added: `{"key": value}`
- [x] Type annotations implemented: `<int{64}>`
- [x] `prototype` type syntax added: `<{type, type}>`

## Active Plan
### 1. Semantic Analysis (IN PROGRESS)
- [ ] Implement `self` injection for methods
- [ ] Implement `implements` logic for operators and types
- [ ] Implement `prototype` compatibility checks for literals and types
- [ ] Implement `blame` semantic checking (assert vs throw)

## Task List
1. [x] **Implement 'blame' as assert**
2. [x] **Implement Rust-like macros**
3. [x] **Implement 'prototype' data type**
4. [x] **Implement type annotations**
5. [ ] **Semantic Analyzer: self injection & implements logic**
