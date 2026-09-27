# ScopeX

A first-class scope for C++: type-safe, move-only handles over per-type
pools, where misuse throws instead of invoking undefined behavior.

## What it is

C++ already gives every block of code a scope: whatever is declared inside
dies when control leaves it. ScopeX is the movable counterpart — a scope
you can create, hold, pass around, move objects out of, and end on your own
terms. Objects pushed into it are constructed in place, destroyed together
when it ends, and referred to through `Handle<T>` rather than pointers.

The claim: **hold handles, not pointers, and use-after-exit, use-after-move,
and use of an empty handle stop being undefined behavior — they throw.**
Careful pointer code can reach the same safety, but it takes discipline,
mastery, and debugging time. A handle turns that discipline into a property
of the type.

We recommend it over `unique_ptr` and `shared_ptr` for anything whose
lifetime is naturally a scope's, including a scope holding a single handle.
The safety guarantee doesn't depend on how many objects a scope holds: a
null `unique_ptr` dereferenced is undefined behavior, while an empty or
exited `Handle` throws. With many objects it is also faster than one heap
allocation per object (see [`BENCHMARKS.md`](./BENCHMARKS.md)). It doesn't
replace plain values: if something can just be a local or a member, let it
be one.

This is a claim about code that uses ScopeX. It does not make C++ as a
language memory-safe, and nothing stops unrelated code in the same program
from using raw pointers.

## The rules

The guarantee holds under two rules:

1. **Hold handles, not pointers.** `get()`, `->` and `*` yield a raw
   pointer for the duration of an expression. Don't store it. A stored
   pointer outlives the checks the handle performs.
2. **Keep the `Scope` object alive as long as its handles.** `exit()` is
   safe: every handle into an exited scope throws. But a `Handle` that
   outlives the `Scope` *object* itself (destroyed, or replaced by
   assignment) is currently undefined behavior.

## Scope vs. arena

They are not the same thing. An arena is a memory source: bump-allocate,
free everything at once, usually raw pointers out and often no destructors
run. ScopeX is arena-shaped underneath (chunked storage, bulk teardown),
but the abstraction is a scope: typed objects properly constructed and
destroyed, handles instead of pointers, and defined failure after `exit()`.

## The idea

C++ has always offered exactly two ways to get memory safety: pay for it at
runtime with reference counting or garbage collection, or pay for it at
compile time by adopting a language that fights you for every pointer —
Rust's borrow checker being the most visible example, and the recent "Safe
C++" proposals essentially importing that same fight into C++ itself. Both
routes work. Both also mean giving something up: runtime cost in the first
case, a genuinely different way of writing code — lifetimes, borrow
annotations, an unfamiliar compiler adversary — in the second.

ScopeX is a bet that C++ already has the tools to get most of that safety
cheaply, using idioms the language has had since before Rust existed:
RAII and move semantics, aimed at a slightly different target than usual.

- **RAII, generalized.** A C++ scope already destroys what it owns when
  control leaves it — that's ordinary. `Scope` takes that same guarantee
  and makes it a first-class, user-controlled object: you can hold
  hundreds of heterogeneous objects in one `Scope`, and either let it fall
  out of scope normally or call `exit()` on your own schedule to collapse
  all of their lifetimes at once, deterministically, with no collector ever
  running in the background.
- **Move semantics as the borrow checker you already own.** Rust prevents
  use-after-move by rejecting the program at compile time, globally, via a
  dedicated checker. `Handle<T>` gets the same *outcome* — a moved-from
  handle can't be silently reused — by being an ordinary move-only C++
  type: the compiler already refuses to copy it, and after a move its own
  fields are what's left holding (or not holding) a valid location. No new
  syntax, no annotations, no second compiler pass.
- **Type safety without a runtime check.** `Handle<T>` carries `T` in its
  own type, not as a separate argument supplied again at the call site —
  so calling `get()` with the "wrong" type isn't something the library has
  to detect at runtime, because it isn't something you can write in the
  first place.

That's the whole thesis: safety as a byproduct of how the types are shaped,
not as a checker bolted on top or a cost paid on every access.

## What it does

- One typed pool per distinct type `T` used with a `Scope`.
- Objects are allocated into growable chunks. Chunk memory is heap-allocated
  once and never moved, so a `Handle<T>` stays valid for the pool's
  lifetime even as new chunks are appended.
- `Handle<T>` is type-safe and move-only: `T` is part of the handle's own
  type, so there is no call site at which a caller can supply the wrong
  `T`, and no way for two live handles to ever refer to the same object at
  once.
- `Handle<T>::get()` (or `operator->`/`operator*`) resolves through a
  cached direct pointer — a single virtual call, no hashmap lookup.
- `Handle<T>::moveTo(Scope&)` moves the object into another scope and
  rewrites the handle's own fields to point at the new location — the same
  variable keeps working afterward, now resolving into the new scope.
- `Scope::exit()` destroys every object the scope's pools still hold, then
  swaps each pool's implementation for a throwing stand-in. Any further
  `push()` on that scope, or `get()`/`moveTo()` on any handle still pointing
  into it, throws `ScopeExitedError` via ordinary virtual dispatch.
- `Scope` has exactly two responsibilities: `push()` and `exit()`. Moving
  an object between scopes is initiated from the handle, not from `Scope`.

## Usage

```cpp
#include "scope.hpp"

Scope scope;
Handle<MyType> h = scope.push<MyType>(args...);
MyType* obj = h.get();       // or h->member, or *h  (don't store obj)

Scope other;
h.moveTo(other);   // h now points into other; same variable, still usable

scope.exit();      // destroys everything scope still owns; scope.push() and
                   // any handle into it now throw. The Scope object remains
                   // valid memory: assign a fresh Scope to reuse the slot.
```

## Building

```
mkdir build && cd build
cmake ..
make
./scopex_test
```

## Benchmarking

`benchmark.cpp` compares ScopeX against `std::vector<std::unique_ptr<T>>`
across four scenarios: small high-frequency allocation, large payload
throughput, bulk teardown, and mutable in-place updates. It's a separate,
optional target — not part of the default build or `run.sh`'s quality
gate, since it's slow and hardware-dependent rather than something that
should run on every push.

```
./benchmark.sh
```

Or directly via CMake, built in Release mode — an unoptimized build makes
the `unique_ptr` comparison look artificially slow and skews every result:

```
cmake -DCMAKE_BUILD_TYPE=Release ..
make scopex_benchmark
./scopex_benchmark
```

Run it locally when changing `TypedPool`, `Chunk`, or allocation logic —
changes to `Handle` semantics alone don't need a rerun. Results vary with
whatever else is running on the machine; for a number worth quoting,
run it a few times and take the median, not the first result.

Full results, methodology, and an honest disclosure of where ScopeX
currently trades memory density for O(1) access: see
[`BENCHMARKS.md`](./BENCHMARKS.md).

## Design notes and limits

- No way to free a single object early short of moving it elsewhere; the
  only bulk-free primitive is `exit()` — deliberate, not a limitation.
- One concrete type per handle: `Handle<T>` holds exactly a `T`. There is
  no `Handle<Base>` referring to a `Derived`; use `unique_ptr` for
  polymorphic ownership.
- A `Scope` has fixed setup cost (a chunk of `chunkCapacity` slots per
  type, 1024 by default). For a scope that will hold only a few objects,
  pass a small capacity.
- Destruction order within one type's pool is allocation order; across
  different types it is unspecified.
- No thread-safety: concurrent calls into the same `Scope` are a data race.

## Related writing

[Separation of Compilation from Attestation in C++](https://gist.github.com/mksunny1/fe88dc882278cb76181e7b2b3eb1d5ce)
is a separate essay on binary-level safety attestation: a toolchain
proposal for making safety rules machine-checkable. It is **not
implemented** in this repository — there is no attestation tooling,
cryptographic tagging, or `.cppsafe` mechanism here. It's linked as
related thinking on the broader problem, not as a description of what this
code does.

## License
MIT