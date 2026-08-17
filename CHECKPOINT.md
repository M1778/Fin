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
- [x] **Implements Block** (Rust-like trait implementation): `StructName implements <Interface> { ... }`

## Active Plan
### 1. Semantic Analysis (COMPLETED)
- [x] Implement `self` injection for methods (Verified)
- [x] Implement `implements` logic for operators and types
- [x] Implement `prototype` compatibility checks for literals and types
- [x] Implement `blame` semantic checking (assert vs throw)
- [x] Implement `ImplementsBlock` analysis

### 2. Testing and Validation (COMPLETED)
- [x] Update `DiagnosticEngine` to track error counts
- [x] Update `test_parser.cpp` to detect hidden parsing errors
- [x] Create test cases for `blame` assert syntax (`blame_assert.fin`)
- [x] Create test cases for Rust-like macros (`macro_definitions.fin`)
- [x] Create test cases for `prototype` literals and types (`prototype_test.fin`)
- [x] Create test cases for Type Annotations (`type_annotations.fin`)
- [x] Create test case for implements block (`implements_block.fin`)
- [x] Fix import paths: `import { HashMap } from hashmap;` not `from stdlib`

## New Syntax Reference

### Implements Block (Trait Implementation)
```fin
interface GetVal<T> {
    pub fun get_val() <T>;
}

struct MyStruct {
    val <int>
}

// Implement interface for struct AFTER its definition
MyStruct implements <GetVal<int>> {
    pub fun get_val() <int> {
        return self.val;
    }
}
```

### Prototype (JSON-like) Literals
```fin
// Type: <{string, int}>
let map <{string, int}> = { "a": 1, "b": 2 };

// With HashMap (must have from_prototype static method)
let hm <HashMap<string, int>> = HashMap::from_prototype({ "x": 10 });
```

### Import Syntax
```fin
// Correct: import from module name (found in $PATH)
import { HashMap } from hashmap;
import { Collection } from collection;

// NOT: import { X } from stdlib;
```

### @implements (The overwriter or implementor?)
```fin
@implements Collection<T> { // Overwriter. overwrites or adds methods/operators 
  pub fun test() <noret> {} // adds or overwrites test to Collection
}

@implements Collection<T>::push_back = (self: Self, other: T) <noret> => {} // Safe Single overwrite

interface NoLengthCollection {
  Self();
}

Collection<T> implements <NoLengthCollection> {
  Collection() {
    return Collection{length: 5, filled: 0, _arr: new [T, 5]{}};
  }
}
```

### easy nullifier (default is null)
```fin
struct A {
    b? <int>, // using '?' makes its default value null and it might not exists (equavelant to `b <int> = null,`)
}
struct maybe<T: Castable> {
    value? <T>,
    pub static fun unpack(v: any) <T> {
        return cast<T>(v); // raises a panic if cast fails
    }
}
fun make_A(n: int) <A?> { // using '?' in types tells us that it might return that type OR null (equavelant to <Maybe<A>>)
    if (n > 0){
        return A{};
    } else {
        return null;
    }
}

fun main() <noret> {
    let myvar <A?> = make_A(1); // Handling unknowns
    maybe::unpack::<A>(myvar); // raises an error if myvar is null

    // Or an easier way to handle it (let compiler handle it):
    let myvar2 <A> = make_A(-1)?; // Using '?' at the end of an expression makes any null value to raise an panic error OR just returns the normal value "unpacked"

    // another example:
    let mibombo <any?> = null;
    // what happens if we try to unnullify an any type:
    let _ <any> = mibombo?; // This is a crucial thing we have to handle (this should be an error since type any cannot be null)
}
```

## Task List
1. [x] **Implement 'blame' as assert**
2. [x] **Implement Rust-like macros**
3. [x] **Implement 'prototype' data type**
4. [x] **Implement type annotations**
5. [x] **Semantic Analyzer: self injection & implements logic**
6. [x] **Parser Test Validation Improvements**
7. [x] **Test Case Generation**
8. [x] **Implements Block** (Rust-like trait implementation)
