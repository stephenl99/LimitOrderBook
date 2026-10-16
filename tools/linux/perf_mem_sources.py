#!/usr/bin/env python3
import collections
import functools
import re
import sys
from pathlib import Path

CATEGORY_BY_OWNER = {
    "SlotIndex": "order index",
    "NodeOrderIndex": "order index",
    "BasicFlatOrderIndex": "order index",
    "MultiplicativeHash": "order index",
    "IdentityHash": "order index",
    "RestingOrder": "order index",
    "SortedSlots": "price levels",
    "LadderSlots": "price levels",
    "SortedLevels": "price levels",
    "MapLevels": "price levels",
    "PriceLadder": "price levels",
    "FlatLevelMap": "price levels",
    "Level": "price levels",
    "PooledBook": "order queue",
    "LazyQueue": "order queue",
    "HashedQueues": "order queue",
    "SlotQueues": "order queue",
    "HandleSlotQueues": "order queue",
    "Order": "order queue",
    "LazyQueueBook": "book logic",
    "AggregateSecurityBook": "book logic",
    "BasicSecurityBook": "book logic",
    "OuterBook": "instrument lookup",
}

CATEGORY_BY_FILE = [
    (re.compile(r"hashtable|unordered_map"), "order index"),
    (re.compile(r"stl_tree|stl_map|LevelSearch\.h|stl_algo"), "price levels"),
    (re.compile(r"stl_list|list\.tcc"), "order queue"),
    (re.compile(r"SecurityBook\.cpp"), "book logic"),
    (re.compile(r"OuterBook"), "instrument lookup"),
    (re.compile(r"BookBench\.cpp|OrderFlow|GeneratedFlow"), "benchmark harness"),
    (re.compile(r"stl_vector|vector\.tcc"), "std::vector access"),
]

OWNER_LINE = re.compile(r"^(?:template\s*<.*>\s*)?(?:class|struct)\s+(\w+)")


@functools.cache
def source_index(root):
    files = {}
    for path in Path(root, "Core").rglob("*"):
        if path.suffix in (".h", ".cpp"):
            files.setdefault(path.name, path)
    return files


@functools.cache
def owners(path):
    result = []
    current = None
    for line in Path(path).read_text().splitlines():
        match = OWNER_LINE.match(line)
        if match:
            current = match.group(1)
        result.append(current)
    return result


def attribute(root, srcline):
    if not srcline or "kallsyms" in srcline or srcline.startswith("["):
        return "[kernel]", "", "kernel"
    file_part, _, line_part = srcline.rpartition(":")
    name = Path(file_part).name
    source = f"{name}:{line_part}"
    path = source_index(root).get(name)
    if path is not None and line_part.isdigit():
        table = owners(path)
        line = int(line_part)
        owner = table[line - 1] if 0 < line <= len(table) else None
        if owner in CATEGORY_BY_OWNER:
            return source, owner, CATEGORY_BY_OWNER[owner]
        for pattern, category in CATEGORY_BY_FILE:
            if pattern.search(name):
                return source, owner or "", category
        return source, owner or "", "other"
    for pattern, category in CATEGORY_BY_FILE:
        if pattern.search(name):
            return source, "", category
    return source, "", "other"


def level_of(sample):
    match = re.search(r"\|LVL ([^|]+)\|", sample)
    if not match:
        return "unknown"
    text = match.group(1)
    for key, label in (("L1", "L1"), ("LFB", "fill buffer"), ("L2", "L2"), ("L3", "L3"), ("RAM", "DRAM")):
        if key in text:
            return label
    return text.strip()


def main():
    root, scenario, impl = sys.argv[1:4]
    lines = sys.stdin.read().split("\n")
    totals = collections.defaultdict(lambda: [0, 0])
    for i, sample in enumerate(lines):
        if "mem-loads" not in sample or i + 1 >= len(lines):
            continue
        tokens = sample.split()
        try:
            latency = int(tokens[-2])
        except (ValueError, IndexError):
            continue
        source, owner, category = attribute(root, lines[i + 1].strip())
        entry = totals[(source, owner, category, level_of(sample))]
        entry[0] += 1
        entry[1] += latency
    for (source, owner, category, level), (loads, latency) in sorted(totals.items(), key=lambda kv: -kv[1][1]):
        print(f"{scenario},{impl},{source},{owner},{category},{level},{loads},{latency}")


if __name__ == "__main__":
    main()
