# ITCH BinaryFILE notes + fixtures

On disk: `[uint16 BE length][payload]…`. Payload byte 0 = message type.
Integers are **big-endian**. Prices are `uint32` with **4 implied decimals** (no floats).
Timestamps are 6-byte ns since midnight.

Displayed book updates: **A, F, E, C, X, D, U**.  
**P / Q** are prints — do **not** change the displayed book.

---

## Fixture: `add_order_a.bin` (Add Order only)

| Field | Offset | Value |
|---|---|---|
| Message Type | 0 | `A` |
| Stock Locate | 1 | 1 |
| Tracking Number | 3 | 0 |
| Timestamp (ns) | 5 | 1000000000 |
| Order Reference Number | 11 | 12345 |
| Buy/Sell | 19 | `B` |
| Shares | 20 | 100 |
| Stock | 24 | `"AAPL"` (space-padded to 8) |
| Price | 32 | 1500000 (= 150.0000) |

Payload length **36**. File = 2 + 36 = **38** bytes.

```
0000: 00 24 41 00 01 00 00 00 00 3b 9a ca 00 00 00 00
0010: 00 00 00 30 39 42 00 00 00 64 41 41 50 4c 20 20
0020: 20 20 00 16 e3 60
```

---

## Fixture: `add_then_delete.bin` (A then D)

Same Add as above, then Order Delete for ref **12345**. After replay: **0** orders on the book.

Delete payload (19 bytes):

| Field | Offset | Value |
|---|---|---|
| Message Type | 0 | `D` |
| Stock Locate | 1 | 1 |
| Tracking Number | 3 | 0 |
| Timestamp (ns) | 5 | 1000000000 |
| Order Reference Number | 11 | 12345 |

---

## Message layouts (NASDAQ TotalView-ITCH 5.0)

Offsets are within the **payload** (after the BinaryFILE length).  
Sizes are payload sizes.

### Add Order — `A` (36)

| Field | Off | Len |
|---|---|---|
| Message Type = `A` | 0 | 1 |
| Stock Locate | 1 | 2 |
| Tracking Number | 3 | 2 |
| Timestamp | 5 | 6 |
| Order Reference Number | 11 | 8 |
| Buy/Sell (`B`/`S`) | 19 | 1 |
| Shares | 20 | 4 |
| Stock | 24 | 8 |
| Price | 32 | 4 |

### Add Order with MPID — `F` (40)

Same as `A` through Price, then **Attribution** (MPID) at offset 36 (4 bytes). Treat like an add for the displayed book.

### Order Executed — `E` (31)

| Field | Off | Len |
|---|---|---|
| Message Type = `E` | 0 | 1 |
| Stock Locate | 1 | 2 |
| Tracking Number | 3 | 2 |
| Timestamp | 5 | 6 |
| Order Reference Number | 11 | 8 |
| Executed Shares | 20 | 4 |
| Match Number | 24 | 8 |

No price. Look up order by ref; reduce quantity; remove if qty → 0.

### Order Executed With Price — `C` (36)

| Field | Off | Len |
|---|---|---|
| Message Type = `C` | 0 | 1 |
| Stock Locate | 1 | 2 |
| Tracking Number | 3 | 2 |
| Timestamp | 5 | 6 |
| Order Reference Number | 11 | 8 |
| Executed Shares | 20 | 4 |
| Match Number | 24 | 8 |
| Printable | 32 | 1 |
| Execution Price | 33 | 4 |

Still a book update via order ref (reduce qty). Price here is execution detail, not “find the level.”

### Order Cancel — `X` (23)

| Field | Off | Len |
|---|---|---|
| Message Type = `X` | 0 | 1 |
| Stock Locate | 1 | 2 |
| Tracking Number | 3 | 2 |
| Timestamp | 5 | 6 |
| Order Reference Number | 11 | 8 |
| Cancelled Shares | 20 | 4 |

Partial cancel: reduce qty by cancelled shares; remove if qty → 0.

### Order Delete — `D` (19)

| Field | Off | Len |
|---|---|---|
| Message Type = `D` | 0 | 1 |
| Stock Locate | 1 | 2 |
| Tracking Number | 3 | 2 |
| Timestamp | 5 | 6 |
| Order Reference Number | 11 | 8 |

Remove the entire order. **No price/side on the wire** → need `order_ref → order` map.

### Order Replace — `U` (35)

| Field | Off | Len |
|---|---|---|
| Message Type = `U` | 0 | 1 |
| Stock Locate | 1 | 2 |
| Tracking Number | 3 | 2 |
| Timestamp | 5 | 6 |
| Original Order Reference | 11 | 8 |
| New Order Reference | 19 | 8 |
| Shares | 27 | 4 |
| Price | 31 | 4 |

Delete original (lose priority), add new order with new ref at new price/qty. Side comes from the original order.

### Trade (non-cross) — `P` (44) — **not a displayed-book update**

| Field | Off | Len |
|---|---|---|
| Message Type = `P` | 0 | 1 |
| Stock Locate | 1 | 2 |
| Tracking Number | 3 | 2 |
| Timestamp | 5 | 6 |
| Order Reference Number | 11 | 8 |
| Buy/Sell | 19 | 1 |
| Shares | 20 | 4 |
| Stock | 24 | 8 |
| Price | 32 | 4 |
| Match Number | 36 | 8 |

Ignore for maintaining bid/ask levels (print / tape).

### Cross Trade — `Q` — **not a displayed-book update**

Ignore for the displayed book (same idea as `P`).

---

## Why the book needs a map (not only a priority queue)

`E` / `C` / `X` / `D` / `U` identify an order by **reference number** only.  
A `priority_queue` cannot find/remove an arbitrary order efficiently (or at all without draining).

Target shape for deletes:

1. **`unordered_map<order_ref, Order>`** (or `unique_ptr<Order>`) — O(1) lookup  
2. **Per-side price levels** — `map<price, deque<order_ref>>` (bids high→low, asks low→high, FIFO at a price)

Add: insert into map + push ref onto that price’s deque.  
Delete: lookup map → know price/side → erase ref from deque → erase map entry.
