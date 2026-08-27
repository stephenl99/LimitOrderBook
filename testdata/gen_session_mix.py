#!/usr/bin/env python3
"""Build testdata/session_mix.bin — multi-message BinaryFILE for dump + gtests."""

from pathlib import Path


def be(n: int, width: int) -> bytes:
    return n.to_bytes(width, "big")


def frame(payload: bytes) -> bytes:
    return be(len(payload), 2) + payload


def header(msg_type: str, locate: int, track: int, ts_ns: int) -> bytearray:
    p = bytearray()
    p.append(ord(msg_type))
    p += be(locate, 2)
    p += be(track, 2)
    p += be(ts_ns, 6)
    return p


def add_order(
    *,
    ref: int,
    side: str,
    shares: int,
    stock: str,
    price: int,
    locate: int = 1,
    ts_ns: int = 1_000_000_000,
) -> bytes:
    p = header("A", locate, 0, ts_ns)
    p += be(ref, 8)
    p.append(ord(side))
    p += be(shares, 4)
    p += stock.encode("ascii").ljust(8)[:8]
    p += be(price, 4)
    assert len(p) == 36
    return frame(p)


def add_order_mpid(
    *,
    ref: int,
    side: str,
    shares: int,
    stock: str,
    price: int,
    mpid: str,
    locate: int = 1,
    ts_ns: int = 1_100_000_000,
) -> bytes:
    p = header("F", locate, 0, ts_ns)
    p += be(ref, 8)
    p.append(ord(side))
    p += be(shares, 4)
    p += stock.encode("ascii").ljust(8)[:8]
    p += be(price, 4)
    p += mpid.encode("ascii").ljust(4)[:4]
    assert len(p) == 40
    return frame(p)


def order_executed(*, ref: int, executed: int, match: int, ts_ns: int = 1_200_000_000) -> bytes:
    p = header("E", 1, 0, ts_ns)
    p += be(ref, 8)
    p += be(executed, 4)
    p += be(match, 8)
    assert len(p) == 31
    return frame(p)


def order_cancel(*, ref: int, cancelled: int, ts_ns: int = 1_300_000_000) -> bytes:
    p = header("X", 1, 0, ts_ns)
    p += be(ref, 8)
    p += be(cancelled, 4)
    assert len(p) == 23
    return frame(p)


def order_delete(*, ref: int, ts_ns: int = 1_400_000_000) -> bytes:
    p = header("D", 1, 0, ts_ns)
    p += be(ref, 8)
    assert len(p) == 19
    return frame(p)


def order_replace(
    *,
    old_ref: int,
    new_ref: int,
    shares: int,
    price: int,
    ts_ns: int = 1_500_000_000,
) -> bytes:
    p = header("U", 1, 0, ts_ns)
    p += be(old_ref, 8)
    p += be(new_ref, 8)
    p += be(shares, 4)
    p += be(price, 4)
    assert len(p) == 35
    return frame(p)


def trade_print(
    *,
    ref: int,
    side: str,
    shares: int,
    stock: str,
    price: int,
    match: int,
    ts_ns: int = 1_600_000_000,
) -> bytes:
    p = header("P", 1, 0, ts_ns)
    p += be(ref, 8)
    p.append(ord(side))
    p += be(shares, 4)
    p += stock.encode("ascii").ljust(8)[:8]
    p += be(price, 4)
    p += be(match, 8)
    assert len(p) == 44
    return frame(p)


def main() -> None:
    out = Path(__file__).resolve().parent / "session_mix.bin"
    blob = b"".join(
        [
            # 1–3: resting liquidity
            add_order(ref=10001, side="B", shares=100, stock="AAPL", price=1_500_000),
            add_order(ref=10002, side="S", shares=200, stock="AAPL", price=1_500_500),
            add_order(ref=10003, side="B", shares=50, stock="AAPL", price=1_499_900),
            # 4: attributed add
            add_order_mpid(
                ref=10004, side="S", shares=75, stock="AAPL", price=1_501_000, mpid="GSCO"
            ),
            # 5–7: mutations (book handlers may ignore until wired)
            order_executed(ref=10001, executed=30, match=9001),
            order_cancel(ref=10002, cancelled=50),
            order_delete(ref=10003),
            # 8: replace
            order_replace(old_ref=10004, new_ref=10005, shares=60, price=1_500_800),
            # 9: tape print (not a displayed-book update)
            trade_print(
                ref=0, side="B", shares=10, stock="AAPL", price=1_500_000, match=9002
            ),
            # 10: another add after activity
            add_order(
                ref=10006,
                side="B",
                shares=25,
                stock="AAPL",
                price=1_499_500,
                ts_ns=1_700_000_000,
            ),
        ]
    )
    out.write_bytes(blob)
    print(f"wrote {out} ({len(blob)} bytes, ~{blob.count(b'') and 'ok'})")
    # count frames
    i = 0
    n = 0
    while i + 2 <= len(blob):
        ln = int.from_bytes(blob[i : i + 2], "big")
        i += 2 + ln
        n += 1
    print(f"frames={n}")


if __name__ == "__main__":
    main()
