# invar-clob

A correctness-first C++20 matching engine for a central limit order book.

This project is being built around a simple standard: market behavior should be deterministic, explainable, and defensible before it is fast.

The goal is an authoritative server-side engine, not a client-side market-data replica.
The engine owns accepted orders, canonical L3 state, matching decisions, trades, and derived L2 market data.
The matching core stays independent of networking and transport so its behavior can be tested and reasoned about directly.

## Why this project exists

A matching engine is a compact system with unusually sharp correctness requirements.
Priority, ownership, partial fills, cancellation, replacement, stop activation, sequence gaps, and arithmetic overflow all change observable market behavior.

This implementation makes those rules explicit instead of hiding them behind incidental data structures or floating-point arithmetic.
The design favors clear invariants and replayable results over premature low-latency optimization.

## Version-one contract

The planned version-one core is a synchronous, single-threaded, transport-free C++20 engine with one instrument per engine instance.

It is designed to support:

- Limit, market, stop-market, and stop-limit orders.
- Good-till-cancelled, immediate-or-cancel, and fill-or-kill policies where their combinations are valid.
- Price-time and pro-rata matching policies selected when the engine is constructed.
- Submit, cancel, and replace commands.
- Private execution events and public L2 snapshots and deltas.
- Canonical client and order ownership.
- Deterministic command and market-data sequencing.
- Stop cascades with explicit activation ordering.
- Atomic validation and rejection before matching-state mutation.

## Design principles

### Integer market data

Prices are signed integer ticks.
Quantities are unsigned integer units.
Floating point does not participate in matching, allocation, or price equality.

Aggregate quantities and pro-rata calculations use checked or widened integer arithmetic.
An operation that cannot be represented is rejected without mutating authoritative state.

### Explicit lifecycle

Orders move through well-defined states: `DormantStop`, `Active`, `Filled`, and `Cancelled`.
Terminal records remain available for the lifetime of the engine so history and ownership checks remain unambiguous.

### No hidden ownership

The engine registers clients and assigns canonical client and order identifiers.
Cancel and replace operations verify ownership and instrument identity.
Unknown orders and orders owned by another client intentionally produce the same `OrderNotFound` result.

### Recoverable market data

The engine separates command sequencing from market-data sequencing.
Snapshots carry the exact state sequence at which they were captured.
Clients apply only contiguous L2 updates and can detect a missing batch instead of silently drifting from the authoritative book.

### Reproducible behavior

A crossing trade executes at the resting order's price.
Priority rules, self-trade prevention, stop activation, time-in-force remainders, and event ordering are part of the contract rather than implementation accidents.

## Verification approach

Correctness will be demonstrated with three complementary forms of evidence:

- Readable deterministic scenario tests for the public contract.
- Randomized property tests for invariants across varied order flows.
- Replay and snapshot checks showing that published L2 state reconstructs the same book as the engine.

Planned coverage includes validation matrices, overflow and atomicity, price-time priority, pro-rata allocation, partial and multi-level fills, cancel and replace ownership, IOC and FOK behavior, self-trade prevention, stop cascades, sequence gaps, and terminal order lifecycle rules.

Performance work is intentionally a later stage.
When it begins, throughput, latency distributions, allocations, memory growth, and scaling will be measured on reproducible workloads rather than reduced to a single headline number.
A future optimized implementation must pass a deep equivalence check against the correctness-first version.

