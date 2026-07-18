# Orderbook Project Plan

## Status

This is a design-only plan.

No implementation, project scaffolding, or tests have been created for the new orderbook.

The user will implement the project separately.

The date of this plan is 2026-07-18.

## Project intent

Build an authoritative server-side matching engine for a central limit order book.

The engine will own accepted orders, resting state, matching decisions, trades, and resulting market-data state.

The first version will prioritize correctness, clarity, and deterministic behavior over low latency.

The matching core will be independent of network transport.

A thin adapter may expose the core through TCP or another transport later.

## Later goal

Low latency

Expanding order types

Client-side orderbook + trading engine (like pulse-order)

## Reference project

Pulse-Order is reference material only.

It is not the implementation target for this project.

Pulse-Order is a C++20 and DPDK market-data-to-order packet processor.

Its current flow is:

```text
Market-data packet
    -> decode
    -> client-side L2 book update
    -> imbalance strategy
    -> risk check
    -> outbound order encoding
    -> DPDK TX
```

Pulse-Order uses a fixed-depth L2 book with five visible bid levels and five visible ask levels.

Its book is a market-data replica rather than an authoritative matching engine.

Its README explicitly excludes exchange simulation and lists fills, cancels, and replace handling as future work.

The new project will not modify Pulse-Order.

Pulse-Order may later serve as a strategy or client harness for the new engine.

## Architecture

The planned system is:

```text
Client order command
    -> pure matching engine
    -> authoritative internal L3 state
    -> trades and execution events
    -> public L2 snapshot and deltas
    -> client-side L2 replica
```

The matching core owns the source of truth.

The public market-data view is derived from authoritative engine state.

Client-side books are replicas and are not authoritative.

## Market model

The market model is a central limit order book.

Orders rest at price levels when they are not immediately executable.

Executable orders match against the opposite side of the book.

The book must never return with a crossed resting state.

The highest resting bid must be strictly below the lowest resting ask.

The first version will support one symbol.

The API should retain an instrument boundary so additional symbols can be added later.

## Book depth contract

### Internal state

The engine will maintain L3 state internally.

L3 means that individual resting orders and their quantities are retained.

Individual order state is required for cancel, replace, queue priority, ownership, self-trade prevention, and pro-rata allocation.

The planned internal structure is an ordered bid and ask price structure with a stable per-price order queue.

An order-ID index will support direct lookup for cancel and replace.

A likely correctness-first C++ representation is an ordered map, a stable linked queue, and an order-ID index.

The exact standard-library containers are not locked yet.

### Public state

The public market-data view will be L2.

L2 means that quantities are aggregated by side and price.

Individual order identity will not be published in the public view.

L2 snapshots will list bids from highest price to lowest price.

L2 snapshots will list asks from lowest price to highest price.

L1 may be derived from the L2 stream later.

### Market-data recovery

Clients will receive an initial L2 snapshot followed by sequence-numbered deltas.

The snapshot will carry the exact engine-wide sequence at which it was captured.

Clients will apply subsequent deltas in sequence order.

A sequence gap must be detectable.

A sequence gap must lead to an explicit recovery path.

Each L2 delta will identify a side, price, and resulting aggregate quantity.

A resulting quantity of zero will remove the price level.

Clients will replace the level state rather than infer it from signed quantity arithmetic.

## Numeric model

Prices will use integer ticks.

Quantities will use integer units.

Floating-point values will not participate in matching or price equality.

Prices and quantities must be positive bounded integers.

The engine or instrument configuration will define explicit maximum values.

## Order types

Version one will support limit, market, stop-market, and stop-limit orders.

Order commands will use explicit typed fields.

Limit price and stop trigger price will be separate optional fields.

Malformed field combinations will be rejected rather than silently normalized.

### Validation matrix

A limit order requires side, positive bounded quantity, positive bounded limit price, and client ID.

A limit order forbids a stop trigger field.

A market order requires side, positive bounded quantity, and client ID.

A market order forbids limit price and stop trigger fields.

A stop-market order requires side, positive bounded quantity, positive bounded stop trigger, and client ID.

A stop-market order forbids a limit price.

A stop-limit order requires side, positive bounded quantity, positive bounded stop trigger, positive bounded limit price, and client ID.

A stop-limit order has no additional forbidden price field.

## Matching priority

The engine will support price-time priority and pro-rata priority.

The policy will be selected when creating an engine instance.

The policy will remain fixed while resting orders exist.

The policy will not be changed dynamically in version one.

### Price-time priority

A better price has priority over a worse price.

At the same price, the earlier accepted order has priority.

A crossing trade executes at the resting order's price.

### Pro-rata priority

Orders at one price level will share available quantity proportionally.

Allocation will use integer quantities.

Leftover quantity will be assigned to the largest fractional remainders.

Equal fractional remainders will be resolved using time priority.

## Market-order behavior

A market order will consume available opposite-side liquidity.

An unfilled market-order remainder will be cancelled.

If the opposite book is empty, the entire market-order quantity will be cancelled.

A market order will never rest as a limit order.

## Stop-order behavior

Stops will use the last-trade price as their trigger reference.

A buy stop will trigger when the last-trade price reaches or exceeds its trigger price.

A sell stop will trigger when the last-trade price reaches or falls below its trigger price.

A stop-market order will become market behavior after activation.

A stop-limit order will become limit behavior after activation.

A triggered stop will be processed immediately after the triggering command's current matching completes.

Stop activation and resulting trades will remain inside the same atomic command event batch.

Stop-generated trades may trigger additional stops.

Stop cascades will continue within the same command until no new stop is triggered.

When multiple stops trigger from one trade, buy stops will activate in ascending trigger-price order.

When multiple stops trigger from one trade, sell stops will activate in descending trigger-price order.

Equal trigger prices will use ascending engine-assigned order ID.

Stop trigger prices must be positive and bounded.

When a last-trade price exists, buy stops must be above it.

When a last-trade price exists, sell stops must be below it.

Before the first trade, a stop may be accepted without directional validation.

A stop accepted before the first trade will wait for a future trade.

## Order identity and ownership

The engine will assign canonical order IDs.

The accepted event will return the assigned order ID to the client.

Each order will carry a client ID.

Client ID ownership will be used for self-trade prevention.

## Order lifecycle

Version one will support submit, cancel, and replace.

Orders will be good-till-cancelled by default.

Immediate-or-cancel is deferred.

Fill-or-kill is deferred.

A completed order cannot be cancelled or replaced.

A price change during replace loses queue priority.

A quantity decrease during replace preserves queue priority.

A quantity increase during replace loses queue priority.

Replace quantity represents the new total remaining quantity.

A replacement is rejected if its requested quantity is below quantity already executed.

## Self-trade prevention

If an incoming order would match an order from the same client, the incoming remainder will be cancelled.

Earlier fills against other clients will remain valid.

The resting same-client order will remain unchanged.

## Command failure behavior

Invalid commands will be rejected atomically.

Invalid commands will not partially mutate order, book, or trade state.

Every command receives an engine-wide sequence, including rejected commands.

Cancel requests for unknown, filled, or already cancelled order IDs will be rejected.

Replace requests for unknown or terminal order IDs will be rejected.

Rejected commands will not mutate book state.

## Events and sequencing

Each command will produce one atomic event batch.

A batch may contain multiple trades and multiple L2 changes.

Events will be ordered as command result, trades and execution effects, then L2 state deltas.

The engine will use one monotonically increasing sequence across all commands.

All events from one command batch will share the command sequence.

Events inside a batch will carry an ordered event index.

Candidate command results include accepted and rejected statuses.

Candidate execution events include trades, fills, cancellations, replacements, and stop triggers.

The final event schema remains an implementation-level design question.

## Invariants

The first runtime invariant scope will focus on book-level behavior.

The highest resting bid must be strictly below the lowest resting ask.

Best bid and best ask ordering must be correct.

Aggregate quantities must equal the quantities represented by active price levels.

Zero-quantity resting levels must not remain visible.

The public L2 state must agree with the authoritative book state.

Executable crossings must be resolved before a command completes.

Property tests should initially exercise these book-level invariants across generated command sequences.

Order-index and stop-registry consistency are known internal risks.

If those risks produce defects, invariant checks should expand to cover them.

## Verification approach

Correctness will use readable scenario tests and randomized property tests.

Scenario tests will explain intended matching behavior.

Property tests will exercise command sequences and book-level invariants.

The first verification target is deterministic matching correctness.

Latency benchmarks are not part of the current design phase.

No production-readiness or exchange-latency claims should be made without measured evidence.

## Implementation roadmap for the user

This roadmap is not being executed by the assistant.

### Stage 1: Pure core

Define command types, order types, price and quantity types, events, and engine configuration.

Define the authoritative L3 state model.

Define the deterministic engine API.

### Stage 2: Limit and price-time matching

Implement limit submission.

Implement price-time matching.

Implement partial and full fills.

Implement cancel and replace.

Implement trade events and L2 state changes.

### Stage 3: Public L2 feed

Implement initial snapshots.

Implement engine-wide sequence numbers.

Implement resulting-quantity level deltas.

Implement snapshot ordering and gap-detection data.

### Stage 4: Market orders

Implement multi-level consumption.

Implement cancellation of unfilled remainder.

Implement empty-book behavior.

### Stage 5: Stop orders

Implement stop-market and stop-limit storage.

Implement last-trade triggers.

Implement activation ordering.

Implement stop cascades.

### Stage 6: Pro-rata matching

Implement fixed engine-level policy selection.

Implement proportional integer allocation.

Implement largest-remainder handling.

Implement time-based tie-breaking.

### Stage 7: Thin adapter

Add a transport adapter only after the pure matching core is understood and verified.

Keep transport serialization separate from matching semantics.

## Deferred decisions

The transport serialization format is not decided.

The exact C++ module boundaries are not decided.

The complete event schema is not decided.

The complete property-test generator design is not decided.

Persistence and restart recovery are deferred.

Multi-symbol support is deferred beyond the initial instrument boundary.

REST, databases, dashboards, queues, and distributed deployment are deferred.

## Comparison with Pulse-Order

Pulse-Order prioritizes DPDK packet processing, fixed binary frames, strategy execution, inline risk checks, and application-side latency measurement.

This project prioritizes authoritative order state, matching semantics, fills, cancellations, replacement, and deterministic public market-data events.

Pulse-Order can later act as a client-side strategy harness for this engine.

A combined future flow could be:

```text
Market data
    -> Pulse-Order client-side L2 replica
    -> strategy and risk logic
    -> order command
    -> planned authoritative matching engine
    -> execution events and L2 updates
```

## Design risks

Supporting two matching policies and four order families makes the version-one contract broad.

The staged roadmap is intended to keep each behavior understandable while preserving the full target contract.

Checking only book-level invariants may miss order-index and stop-registry defects.

The initial implementation should keep this risk visible rather than treating book-level checks as complete proof of internal correctness.

The distinction between client-side L2 replication and server-side L3 matching must remain explicit throughout the design.

## Journal

Detailed decisions, rationale, rejected alternatives, unresolved questions, and progress themes are maintained in `DESIGN_JOURNAL.md`.
