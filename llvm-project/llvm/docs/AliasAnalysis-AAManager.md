# AliasAnalysis `AAManager` overview

This document explains the `AAManager` and related classes in LLVM’s alias analysis interface, focusing on the section around `AAManager` in [llvm/include/llvm/Analysis/AliasAnalysis.h](../include/llvm/Analysis/AliasAnalysis.h).

## What problem `AAManager` solves

LLVM has multiple alias analyses (AAs). Each AA can answer queries like “do these two memory locations alias?” or “what memory does this call read/write?”. `AAManager` gathers the available AA results and exposes a single aggregated interface (`AAResults`). The aggregation order matters and is the order in which analyses are registered.

## Key types and relationships

### `AAResults`
`AAResults` is the aggregated alias analysis interface used by passes. It owns a list of **type-erased AA implementations**, each wrapped in a `Model<T>` inside a `Concept` interface. The high-level API on `AAResults` (e.g., `alias`, `getModRefInfo`, `getMemoryEffects`) forwards the query to these underlying AA results in sequence.

`AAResults` also tracks **dependency IDs** for invalidation in the new pass manager (`addAADependencyID`). If any of the component analyses become invalid, the aggregate results are invalidated too.

### `AAQueryInfo`
`AAQueryInfo` stores **per-query state**, mostly caches used by BasicAA. It also tracks recursion depth and “assumption” usage. This data can be reused across multiple queries to avoid recomputation.

### `BatchAAResults`
`BatchAAResults` is a wrapper around `AAResults` that reuses a single `AAQueryInfo` across many queries, which is efficient when IR does not change between queries. If you need to switch back to normal (non-batch) behavior, create a new `BatchAAResults` or use `AAResults` directly.

### `AAManager`
`AAManager` is the **analysis manager adaptor** that registers alias analyses and produces the aggregated `AAResults` for a function. It handles two kinds of analyses:

- **Function analyses** (registered via `registerFunctionAnalysis<AnalysisT>()`) are retrieved from the `FunctionAnalysisManager`.
- **Module analyses** (registered via `registerModuleAnalysis<AnalysisT>()`) are retrieved via the `ModuleAnalysisManagerFunctionProxy`.

`AAManager::run` builds the `AAResults` by calling the registered result-getter callbacks in order.

## How registration works

`AAManager` stores a vector of function pointers (`ResultGetters`). Each registration call appends a getter that, when invoked, will:

- obtain the analysis result for the current function/module
- add that result to the `AAResults` aggregation
- register any invalidation dependency (function analyses)

The registration order determines the order AA results are queried. This matters for precision because earlier AAs can return more precise answers and avoid querying later ones.

## Invalidation behavior

- `AAResults::invalidate` uses the stored dependency IDs to see if any component analysis is invalidated.
- If any component is invalidated, the entire aggregate `AAResults` is invalidated.
- This is why you **do not need to explicitly preserve `AAManager`** if you preserve all underlying analyses.

## The `Concept` / `Model` type-erasure

`AAResults` uses a type-erased interface:

- `AAResults::Concept` is an abstract interface containing the AA query methods.
- `AAResults::Model<T>` wraps an AA result type `T` and forwards calls to it.

This pattern allows `AAResults` to store a heterogenous list of AA result types while providing a uniform query interface.

## Typical usage in a pass

1. `AAManager` is registered in the pass pipeline and configured with analyses.
2. In `AAManager::run`, the aggregated `AAResults` is created.
3. Passes request `AAResults` and query it for alias and mod/ref info.

## Common pitfalls

- **Register analyses before running**: Do not register more analyses after `AAManager::run`.
- **Order matters**: Register more precise AAs first.
- **Batch mode assumptions**: `BatchAAResults` assumes the IR is stable across queries.

## Related docs

- Alias Analysis overview: [docs/AliasAnalysis.html](../docs/AliasAnalysis.html)
- Header definition: [llvm/include/llvm/Analysis/AliasAnalysis.h](../include/llvm/Analysis/AliasAnalysis.h)
