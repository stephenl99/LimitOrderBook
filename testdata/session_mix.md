# session_mix.bin

Multi-message BinaryFILE for dump tooling and GoogleTest.

Regenerate:

```bash
python3 testdata/gen_session_mix.py
```

## Sequence (10 frames)

| # | Type | Summary |
|---|------|---------|
| 1 | A | Add bid ref=10001, 100 @ 150.0000 AAPL |
| 2 | A | Add ask ref=10002, 200 @ 150.0500 AAPL |
| 3 | A | Add bid ref=10003, 50 @ 149.9900 AAPL |
| 4 | F | Add ask ref=10004, 75 @ 150.1000, MPID=GSCO |
| 5 | E | Execute 30 shares of 10001, match=9001 |
| 6 | X | Cancel 50 shares of 10002 |
| 7 | D | Delete 10003 |
| 8 | U | Replace 10004 → 10005, 60 @ 150.0800 |
| 9 | P | Trade print (ignore for displayed book) |
| 10 | A | Add bid ref=10006, 25 @ 149.9500 |

With **current** book wiring (A/F insert, E execute, D delete), after replay you should see **2** bid levels (150.00 with 70 shares on 10001, 149.95 on 10006) and **2** ask levels (150.05 on 10002, 150.10 on 10004). X/U/P are decoded by `itch_dump` but not yet applied to the book.
