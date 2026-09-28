"""Export verified Counter-Strike: Condition Zero NAV v5 places for YaPB.

The NAV layout follows ReGameDLL_CS game_shared/bot/nav_file.cpp (MIT/GPL
project source). Only same-size BSPs are exported. No NAV file is changed.
"""

from __future__ import annotations

import argparse
import math
import struct
from pathlib import Path


class NavError(ValueError):
    pass


class Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.offset = 0

    def read(self, fmt: str):
        size = struct.calcsize(fmt)
        if self.offset + size > len(self.data):
            raise NavError("truncated NAV file")
        result = struct.unpack_from(fmt, self.data, self.offset)
        self.offset += size
        return result[0] if len(result) == 1 else result

    def skip(self, size: int):
        if size < 0 or self.offset + size > len(self.data):
            raise NavError("invalid NAV record length")
        self.offset += size


def read_places(nav_path: Path, bsp_path: Path):
    r = Reader(nav_path.read_bytes())
    if r.read("<I") != 0xFEEDFACE or r.read("<I") != 5:
        raise NavError("requires CZ NAV version 5")
    if r.read("<I") != bsp_path.stat().st_size:
        raise NavError("NAV source BSP size differs from target BSP")

    count = r.read("<H")
    if count > 256:
        raise NavError("too many place names")
    names = []
    for _ in range(count):
        length = r.read("<H")
        if not 2 <= length <= 256:
            raise NavError("invalid place name length")
        raw = r.data[r.offset : r.offset + length]
        r.skip(length)
        if raw[-1] != 0:
            raise NavError("unterminated place name")
        name = raw[:-1].decode("ascii")
        if not name.isalnum():
            raise NavError("invalid place name")
        names.append(name)

    area_count = r.read("<I")
    if not 0 < area_count < 100000:
        raise NavError("invalid area count")
    areas: dict[str, list[tuple[float, float, float, float]]] = {}
    for _ in range(area_count):
        r.read("<I")  # area ID
        r.read("<B")  # attributes
        lo_x, lo_y, lo_z, hi_x, hi_y, hi_z = r.read("<6f")
        r.skip(8)  # NE and SW corner heights
        for _ in range(4):
            r.skip(4 * r.read("<I"))
        r.skip(17 * r.read("<B"))  # hiding spots
        r.skip(14 * r.read("<B"))  # approach areas
        for _ in range(r.read("<I")):
            r.skip(11)  # from ID/dir, to ID/dir, spot count
            spot_count = r.data[r.offset - 1]
            r.skip(5 * spot_count)
        place_entry = r.read("<H")
        if place_entry > count:
            raise NavError("unknown place entry")
        if place_entry == 0:
            continue
        values = (lo_x, lo_y, lo_z, hi_x, hi_y, hi_z)
        if not all(math.isfinite(value) for value in values):
            raise NavError("non-finite area coordinates")
        weight = max(1.0, (hi_x - lo_x) * (hi_y - lo_y))
        center = ((lo_x + hi_x) / 2, (lo_y + hi_y) / 2, (lo_z + hi_z) / 2)
        areas.setdefault(names[place_entry - 1], []).append((*center, weight))
    if r.offset != len(r.data):
        raise NavError(f"unexpected trailing NAV data ({len(r.data) - r.offset} bytes)")

    routes = {}
    for name, points in areas.items():
        total = sum(point[3] for point in points)
        average = tuple(sum(point[axis] * point[3] for point in points) / total for axis in range(3))
        representative = min(points, key=lambda point: sum((point[axis] - average[axis]) ** 2 for axis in range(3)))
        routes[name] = representative[:3]
    return routes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cz-maps", type=Path, required=True)
    parser.add_argument("--target-maps", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    exported = 0
    for nav_path in sorted(args.cz_maps.glob("*.nav")):
        bsp_path = args.target_maps / f"{nav_path.stem}.bsp"
        if not bsp_path.is_file():
            continue
        try:
            routes = read_places(nav_path, bsp_path)
        except (NavError, UnicodeError) as error:
            print(f"skip {nav_path.stem}: {error}")
            continue
        if not routes:
            continue
        lines = [
            f"# CZ NAV v5 places, verified against {bsp_path.name} size\n",
            f"bsp_size {bsp_path.stat().st_size}\n",
        ]
        lines.extend(f"{name} {x:.3f} {y:.3f} {z:.3f}\n" for name, (x, y, z) in sorted(routes.items()))
        (args.output / f"{nav_path.stem}.cfg").write_text("".join(lines))
        exported += 1
    print(f"exported {exported} maps")


if __name__ == "__main__":
    main()
