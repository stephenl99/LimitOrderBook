# Dummy ITCH Add Order (A) — BinaryFILE

File: add_order_a.bin
Layout: uint16 length (big-endian) + payload. Payload[0] = message type.

## Expected decode

| Field | Offset in payload | Value |
|---|---|---|
| Message Type | 0 | A |
| Stock Locate | 1 | 1 |
| Tracking Number | 3 | 0 |
| Timestamp (ns since midnight) | 5 | 1000000000 |
| Order Reference Number | 11 | 12345 |
| Buy/Sell | 19 | B |
| Shares | 20 | 100 |
| Stock | 24 | "AAPL" (space-padded to 8) |
| Price | 32 | 1500000 (= 150.0000) |

Payload length: 36 bytes. Full file length: 38 bytes (2 + 36).

## Hex dump (whole file)

0000: 00 24 41 00 01 00 00 00 00 3b 9a ca 00 00 00 00
0010: 00 00 00 30 39 42 00 00 00 64 41 41 50 4c 20 20
0020: 20 20 00 16 e3 60

## Parse sketch

1. Read 2 bytes → length (big-endian) → should be 36
2. Read 36 bytes → payload
3. Check payload[0] == 'A'
4. Decode multi-byte integers as big-endian

Do not use floating point for price; keep 1500000 as uint32_t ticks.
