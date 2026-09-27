# ScopeX Benchmarks

## Why these numbers

ScopeX claims to be memory-safe and zero-overhead. "Memory-safe" is a
design argument (see the main README). "Zero-overhead" is a testable
claim, so it gets tested here, against the thing it's actually competing
with in ordinary C++ code: `std::vector<std::unique_ptr<T>>`.

## Environment

*(fill in before publishing — these numbers are meaningless without them)*

- CPU: `TODO`
- OS: `TODO`
- Compiler + version: `TODO`
- Build flags: `-O3 -DNDEBUG` (Release; see `benchmark.sh`)
- Date: `TODO`

## Methodology

`benchmark.cpp` runs four scenarios, each comparing vanilla
`unique_ptr`-per-object against a single `Scope`:

1. **Small object, high frequency** — 2,000,000 × 16-byte objects.
2. **Large payload throughput** — 20,000 × 4KB objects.
3. **Bulk teardown / churn** — 1,000,000 objects, destruction time only.
4. **Mutable state update** — 1,000,000 objects, allocate then mutate in place.

Each scenario reports wall-clock time and, for Scenarios 1–2, peak RSS
(Resident Set Size — actual physical memory the process holds, read from
`/proc/self/statm` on Linux) measured **while the data is still live**,
immediately before teardown.

### A bug we found and fixed along the way

The first version of this benchmark measured RSS *after* the closing
brace — i.e. after the container being measured had already been
destructed. That produced a residual-memory number, not a peak-usage
number, and it was actively misleading: it made ScopeX look like it used
*more* memory than vanilla, for reasons unrelated to either allocator's
actual footprint. Moving the RSS read to just before teardown (matching
where the timer already stopped) fixed it — full diff is in the repo
history. Flagging it here because a benchmark that hides its own false
starts isn't one worth trusting.

## Results

Two runs since the RSS fix (a third, pre-fix run is not included below —
its timings are consistent with these but its RSS numbers used the buggy
methodology). Two data points is not a rigorous sample; treat the figures
below as directional, not authoritative, until run several more times.

| Scenario | Vanilla | ScopeX | Speedup |
|---|---|---|---|
| 1. Small objects (2M × 16B) — time | ~186 ms | ~102 ms | ~1.8x |
| 1. Small objects — peak RSS | 46 MB | 62 MB | — |
| 2. Large payload (20K × 4KB) — time | ~49 ms | ~59 ms | ~0.8x (vanilla faster) |
| 2. Large payload — peak RSS | 78 MB | 78 MB | tie |
| 3. Bulk teardown (1M objects) — time | ~132 ms | ~18 ms | ~7.4x |
| 4. Mutable update (1M objects) — time | ~135 ms | ~33 ms | ~4.1x |

## What this shows

- **Bulk teardown is the clearest win, and the most defensible one.**
  Vanilla destructs and frees 1,000,000 scattered heap objects one at a
  time; `Scope::exit()` walks one contiguous array. ~7x is exactly what a
  region allocator is built to deliver, and it's the one number here
  worth leading with.
- **Small, high-frequency allocation is a solid, consistent win** (~1.8x)
  from skipping per-object `malloc` calls in favor of one pre-sized chunk.
- **Mutation is faster too** (~4x), likely a combination of no per-object
  allocation cost plus cache-friendlier contiguous iteration versus
  vanilla's pointer-chasing through scattered heap objects — not
  separately isolated in this benchmark, so treat that "likely" as an
  informed guess, not a measured breakdown.

## What this doesn't show — an honest limitation

**Scenario 1's peak RSS is higher for ScopeX (62 MB vs 46 MB), and this
is real, not a measurement artifact.** The cause: the benchmark keeps
every reference in a `std::vector`. A `unique_ptr<T>` is one 8-byte
pointer; a `Handle<T>` carries a pointer plus two `uint32_t` location
fields, so it's roughly twice as wide. At 2,000,000 objects, that
difference alone accounts for most of the ~16 MB gap — it's outweighing
vanilla's own per-allocation heap bookkeeping overhead, at least at this
object size.

This is a genuine trade-off in the current design (`Handle<T>` resolves
in O(1) with no lookup, and that costs some width), not a bug, and **it
only shows up when every handle is kept alive at once in a dense array**.
A workload that pushes into a scope and mostly lets `exit()` do the
cleanup — rather than retaining millions of handles simultaneously —
wouldn't pay this cost. Scenario 2 confirms the effect shrinks to nothing
once object size dominates: at 20,000 × 4KB, both sides land on identical
78 MB, because the payload itself swamps the handle-width difference.

## Reproducing this

```
./benchmark.sh
```

or directly:

```
cmake -DCMAKE_BUILD_TYPE=Release ..
make scopex_benchmark
./scopex_benchmark
```

Run it several times — timing varies with whatever else is running on the
machine (run 1's Scenario 1 ScopeX time was 84ms; run 3's was 71ms; run 2's
was 133ms, same code, same machine). Report a median across at least 5
runs before citing a number publicly.