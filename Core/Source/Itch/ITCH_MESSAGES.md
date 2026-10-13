# ITCH 5.0 messages

Reference for the message types in `Core/Source/Itch/`. Offsets are byte positions in the payload (after the 2-byte
length prefix). Spec: NASDAQ TotalView-ITCH 5.0.

## Framing

The sample files are BinaryFILE: each message is `uint16` length (big-endian) followed by that many payload bytes.
Byte 0 of the payload is the message type. No pcap, no MoldUDP64 headers.

- all integers big-endian
- prices are `uint32` with 4 implied decimals (`1500000` = 150.0000)
- timestamps are 6-byte nanoseconds since midnight
- stock symbols are 8 bytes ASCII, right-padded with spaces

## Common header (every message)

| offset | size | field |
|---|---|---|
| 0 | 1 | message type |
| 1 | 2 | stock locate |
| 3 | 2 | tracking number |
| 5 | 6 | timestamp (ns) |

Locate is the per-day id for a symbol, assigned by `R`. Not stable across days.

## What each one does to the book

| type | name | size | book effect | class |
|---|---|---|---|---|
| `R` | Stock Directory | 39 | none, maps locate -> symbol | `StockDirectoryMessage` |
| `A` | Add Order | 36 | insert at back of price level | `AddOrderMessage` |
| `F` | Add Order w/ MPID | 40 | same as `A` | `AddOrderMpidMessage` |
| `E` | Order Executed | 31 | reduce shares, remove at 0 | `OrderExecutedMessage` |
| `C` | Order Executed w/ Price | 36 | same as `E` | `OrderExecutedWithPriceMessage` |
| `X` | Order Cancel | 23 | reduce shares, remove at 0 | `OrderCancelMessage` |
| `D` | Order Delete | 19 | remove order | `OrderDeleteMessage` |
| `U` | Order Replace | 35 | remove old ref, insert new ref at back of queue | `OrderReplaceMessage` |
| `P` | Trade (non-cross) | 44 | none — hidden/non-displayed liquidity | `TradeMessage` |
| `Q` | Cross Trade | 40 | none — opening/closing/IPO cross print | `CrossTradeMessage` |

`E`/`C`/`X`/`D`/`U` only carry the order reference, not price or side. That's why the book needs
`order_mapping` (ref -> iterator): without it you'd have to search every level.

## Layouts

### R — Stock Directory (39)
| offset | size | field |
|---|---|---|
| 11 | 8 | stock |
| 19 | 1 | market category |
| 20 | 19 | financial status, round lot size, etc. (not decoded) |

### A — Add Order (36)
| offset | size | field |
|---|---|---|
| 11 | 8 | order reference |
| 19 | 1 | side (`B` / `S`) |
| 20 | 4 | shares |
| 24 | 8 | stock |
| 32 | 4 | price |

### F — Add Order with MPID (40)
Same as `A`, plus:
| offset | size | field |
|---|---|---|
| 36 | 4 | attribution (MPID) |

### E — Order Executed (31)
| offset | size | field |
|---|---|---|
| 11 | 8 | order reference |
| 19 | 4 | executed shares |
| 23 | 8 | match number |

### C — Order Executed with Price (36)
Same as `E`, plus:
| offset | size | field |
|---|---|---|
| 31 | 1 | printable (`Y` / `N`) |
| 32 | 4 | execution price |

Printable `N` means don't count it toward volume (it's already been reported elsewhere). Doesn't change the book effect.

### X — Order Cancel (23)
| offset | size | field |
|---|---|---|
| 11 | 8 | order reference |
| 19 | 4 | cancelled shares |

Partial cancel. A full cancel comes as `D`.

### D — Order Delete (19)
| offset | size | field |
|---|---|---|
| 11 | 8 | order reference |

### U — Order Replace (35)
| offset | size | field |
|---|---|---|
| 11 | 8 | original order reference |
| 19 | 8 | new order reference |
| 27 | 4 | shares |
| 31 | 4 | price |

Side and stock carry over from the original order. The new order loses time priority.

### P — Trade, non-cross (44)
| offset | size | field |
|---|---|---|
| 11 | 8 | order reference (always 0 in practice) |
| 19 | 1 | side |
| 20 | 4 | shares |
| 24 | 8 | stock |
| 32 | 4 | price |
| 36 | 8 | match number |

### Q — Cross Trade (40)
| offset | size | field |
|---|---|---|
| 11 | **8** | shares |
| 19 | 8 | stock |
| 27 | 4 | cross price |
| 31 | 8 | match number |
| 39 | 1 | cross type (`O` open, `C` close, `H` halt/IPO, `I` intraday) |

**Known bug:** `CrossTradeMessage` currently reads shares as 4 bytes at 11 and every later field 4 bytes early
(stock 15, price 23, match 27, type 35). Its offsets only add up to 36 of the 40 bytes. Doesn't affect the book
since `Q` is ignored there, but `itch_dump` output for crosses is wrong.

## Offsets that don't line up

`Offsets` in `Message.h` holds the shared positions, but several types break the pattern:

- quantity is at **20** for `A`/`F`/`P`, **19** for `E`/`C`/`X`, **27** for `U`, **11** for `Q`
- price is at **32** for `A`/`F`/`P` and the execution price in `C`, **31** for `U`, **27** for `Q`
- stock is at **24** for `A`/`F`/`P`, **11** for `R`, **19** for `Q`

## Not handled yet

Decoded as `nullptr` and skipped: `S` system event, `H` trading action (halts), `Y` Reg SHO, `L` market participant
position, `V`/`W` MWCB, `K` IPO quoting period, `J` LULD auction collar, `h` operational halt, `I` NOII, `N` RPII,
`O` direct listing. None of these change the displayed book, though `H` and `S` matter if you want to know when the
book is actually live.
