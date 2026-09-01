#!/usr/bin/env python3
"""Build testdata/big_mix.bin — large BinaryFILE for throughput benchmarking.

Default: one Stock Directory row + 100k Add Order (A) messages on locate 1.
Does not run the decoder; pair with your own timing loop around decode_file().

Regenerate:
  python3 testdata/gen_big_mix.py
  python3 testdata/gen_big_mix.py --adds 500000 --out testdata/big_mix.bin
"""

from __future__ import annotations

import argparse
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


def stock_directory(*, stock: str, locate: int, ts_ns: int = 0) -> bytes:
    p = header("R", locate, 0, ts_ns)
    p += stock.encode("ascii").ljust(8)[:8]
    p.append(ord("Q"))  # market category
    p += b"\x00" * (39 - len(p))
    assert len(p) == 39
    return frame(bytes(p))


def add_order(
    *,
    ref: int,
    side: str,
    shares: int,
    stock: str,
    price: int,
    locate: int,
    ts_ns: int,
) -> bytes:
    p = header("A", locate, 0, ts_ns)
    p += be(ref, 8)
    p.append(ord(side))
    p += be(shares, 4)
    p += stock.encode("ascii").ljust(8)[:8]
    p += be(price, 4)
    assert len(p) == 36
    return frame(bytes(p))


def order_executed(*, ref: int, executed: int, match: int, locate: int, ts_ns: int) -> bytes:
    p = header("E", locate, 0, ts_ns)
    p += be(ref, 8)
    p += be(executed, 4)
    p += be(match, 8)
    assert len(p) == 31
    return frame(bytes(p))


def order_cancel(*, ref: int, cancelled: int, locate: int, ts_ns: int) -> bytes:
    p = header("X", locate, 0, ts_ns)
    p += be(ref, 8)
    p += be(cancelled, 4)
    assert len(p) == 23
    return frame(bytes(p))


def count_frames(blob: bytes) -> int:
    i = 0
    n = 0
    while i + 2 <= len(blob):
        ln = int.from_bytes(blob[i : i + 2], "big")
        i += 2 + ln
        n += 1
    return n


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate a large ITCH BinaryFILE fixture.")
    parser.add_argument(
        "--out",
        type=Path,
        default=Path(__file__).resolve().parent / "big_mix.bin",
        help="output path (default: testdata/big_mix.bin)",
    )
    parser.add_argument(
        "--adds",
        type=int,
        default=100_000,
        help="number of Add Order (A) messages (default: 100000)",
    )
    parser.add_argument(
        "--locates",
        type=int,
        default=1,
        help="number of symbols / stock locates (default: 1)",
    )
    parser.add_argument(
        "--levels",
        type=int,
        default=50,
        help="price levels to cycle across per side (default: 50)",
    )
    parser.add_argument(
        "--tick",
        type=int,
        default=500,
        help="price step in 1/10000 units between levels (default: 500 = 0.05)",
    )
    parser.add_argument(
        "--base-price",
        type=int,
        default=1_500_000,
        help="center price for level 0 (default: 150.0000)",
    )
    parser.add_argument(
        "--shares",
        type=int,
        default=100,
        help="shares per add (default: 100)",
    )
    parser.add_argument(
        "--mutate-every",
        type=int,
        default=0,
        help="every N adds, emit one E then X on the previous ref (0 = adds only)",
    )
    parser.add_argument(
        "--no-directory",
        action="store_true",
        help="skip Stock Directory (R) preamble",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if args.adds < 0:
        raise SystemExit("--adds must be >= 0")
    if args.locates < 1:
        raise SystemExit("--locates must be >= 1")
    if args.levels < 1:
        raise SystemExit("--levels must be >= 1")

    symbols = ["AAPL", "MSFT", "GOOG", "AMZN", "TSLA", "NVDA", "META", "NFLX"]
    if args.locates > len(symbols):
        raise SystemExit(f"--locates max is {len(symbols)} with built-in symbol list")

    out: Path = args.out
    out.parent.mkdir(parents=True, exist_ok=True)

    frames = 0
    with out.open("wb") as f:
        if not args.no_directory:
            for locate in range(1, args.locates + 1):
                symbol = symbols[locate - 1]
                f.write(stock_directory(stock=symbol, locate=locate, ts_ns=0))
                frames += 1

        ref = 10_000
        match = 90_000
        for i in range(args.adds):
            locate = (i % args.locates) + 1
            symbol = symbols[locate - 1]
            side = "B" if i % 2 == 0 else "S"
            level = i % args.levels
            if side == "B":
                price = args.base_price - level * args.tick
            else:
                price = args.base_price + level * args.tick
            ts_ns = 1_000_000_000 + i

            f.write(
                add_order(
                    ref=ref,
                    side=side,
                    shares=args.shares,
                    stock=symbol,
                    price=price,
                    locate=locate,
                    ts_ns=ts_ns,
                )
            )
            frames += 1

            if args.mutate_every > 0 and i > 0 and i % args.mutate_every == 0:
                prev_ref = ref - 1
                f.write(
                    order_executed(
                        ref=prev_ref,
                        executed=min(10, args.shares),
                        match=match,
                        locate=locate,
                        ts_ns=ts_ns + 1,
                    )
                )
                frames += 1
                match += 1
                f.write(
                    order_cancel(
                        ref=prev_ref,
                        cancelled=min(5, args.shares),
                        locate=locate,
                        ts_ns=ts_ns + 2,
                    )
                )
                frames += 1

            ref += 1

    size = out.stat().st_size
    print(f"wrote {out} ({size:,} bytes)")
    print(f"frames={frames:,}  adds={args.adds:,}  locates={args.locates}")
    if args.mutate_every > 0:
        mutations = args.adds // args.mutate_every
        print(f"mutations={mutations:,}  (2 frames each: E + X)")


if __name__ == "__main__":
    main()
