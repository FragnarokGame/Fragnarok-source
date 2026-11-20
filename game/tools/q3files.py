"""Readers for compiled maps, with only the standard library: the BSP's entities and leaves, and the
bot navigation file (AAS). check_nav.py and blender/check_map.py use them.

The layouts follow ioquake3's qfiles.h (BSP version 46) and botlib's aasfile.h (AAS version 5).
"""
import re
import struct

# botlib's travel types (be_aas_def.h)
TRAVEL_NAMES = {
    2: "walk", 3: "crouch", 4: "barrier jump", 5: "jump", 6: "ladder", 7: "walk off ledge",
    8: "swim", 9: "water jump", 10: "teleport", 11: "elevator", 12: "rocket jump", 13: "bfg jump",
    14: "grapple", 15: "double jump", 16: "ramp jump", 17: "strafe jump", 18: "jump pad", 19: "func_bobbing",
}
# the ones a bot doesn't use without a special weapon or skill (TFL_DEFAULT in be_aas_def.h)
SPECIAL_TRAVEL = {12, 13, 14, 15, 16, 17}
TRAVEL_ELEVATOR = 11
TRAVELTYPE_MASK = 0xFFFFFF


class Bsp:
    def __init__(self, path):
        data = open(path, "rb").read()
        ident, version = struct.unpack_from("<4si", data, 0)
        if ident != b"IBSP" or version != 46:
            raise ValueError(f"{path} isn't a Quake 3 BSP")
        lumps = [struct.unpack_from("<ii", data, 8 + 8 * i) for i in range(17)]

        def lump(i, fmt):
            ofs, length = lumps[i]
            size = struct.calcsize(fmt)
            return [struct.unpack_from(fmt, data, ofs + k * size) for k in range(length // size)]

        ofs, length = lumps[0]
        self.entity_text = data[ofs:ofs + length].rstrip(b"\0").decode("latin-1")
        self.planes = lump(2, "<4f")
        self.nodes = lump(3, "<3i6i")
        self.leaves = lump(4, "<12i")

    def entities(self):
        out = []
        for block in re.findall(r"\{([^{}]*)\}", self.entity_text):
            out.append(dict(re.findall(r'"([^"]*)"\s+"([^"]*)"', block)))
        return out

    def leaf(self, p):
        n = 0
        while n >= 0:
            plane, front, back = self.nodes[n][:3]
            nx, ny, nz, dist = self.planes[plane]
            n = front if nx * p[0] + ny * p[1] + nz * p[2] - dist >= 0 else back
        return self.leaves[-n - 1]

    def in_world(self, p):
        """False for points in solid or out in the void around the map (leaves with no cluster)."""
        return self.leaf(p)[0] >= 0


def origin(ent):
    return tuple(float(v) for v in ent.get("origin", "0 0 0").split())


class Aas:
    def __init__(self, path):
        data = bytearray(open(path, "rb").read())
        ident, version = struct.unpack_from("<4si", data, 0)
        if ident != b"EAAS" or version not in (4, 5):
            raise ValueError(f"{path} isn't a bot navigation file")
        header = 12 + 14 * 8
        if version == 5:
            # version 5 headers are scrambled (AAS_DData in be_aas_file.c)
            for i in range(header - 8):
                data[8 + i] ^= (i * 119) & 0xFF
        lumps = [struct.unpack_from("<ii", data, 12 + 8 * i) for i in range(14)]

        def lump(i, fmt):
            ofs, length = lumps[i]
            size = struct.calcsize(fmt)
            return [struct.unpack_from(fmt, data, ofs + k * size) for k in range(length // size)]

        self.planes = lump(2, "<4fi")
        self.areas = lump(7, "<3i9f")
        self.settings = lump(8, "<7i")
        # aas_reachability_t: three ints, two vectors, the travel type and a short travel time, padded to 44
        self.reach = lump(9, "<3i6fiH2x")
        self.nodes = lump(10, "<3i")

    def area_at(self, p):
        """The area a point is in, or 0 in solid."""
        n = 1
        while n > 0:
            plane, front, back = self.nodes[n]
            nx, ny, nz, dist, _ = self.planes[plane]
            n = front if nx * p[0] + ny * p[1] + nz * p[2] - dist > 0 else back
        return -n

    def area_under(self, p, depth=96):
        """The area for a point entity, as a bot finds it. AAS areas hold the places a standing
        player's origin can be, 24 above the floor, and items and spawn points sit right on that
        boundary, often with only a thin grounded area under open air. A bot links its whole box
        into every area it touches, so look a little around the point, above it and then down, and
        take the first area a bot can stand and move on (AREA_GROUNDED, with moves out), else the
        first area found."""
        found = []
        # a grounded area can be under a unit thick, so step finely near the point
        for dz in tuple(k / 4 for k in range(49)) + (16, 24, 32) + tuple(range(-1, -33, -1)) + tuple(range(-36, -depth - 1, -4)):
            for dx, dy in ((0, 0), (8, 0), (-8, 0), (0, 8), (0, -8)):
                a = self.area_at((p[0] + dx, p[1] + dy, p[2] + dz))
                if a and self.settings[a][1] & 1 and self.settings[a][5]:
                    return a
                if a:
                    found.append(a)
        return found[0] if found else 0

    def links(self):
        """(from area, to area, travel type) for every reachability."""
        for a, s in enumerate(self.settings):
            first, num = s[6], s[5]
            for r in self.reach[first:first + num]:
                yield a, r[0], r[9] & TRAVELTYPE_MASK
