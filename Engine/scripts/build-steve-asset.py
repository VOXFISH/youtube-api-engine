"""Build a posed classic Steve mesh; preserve the supplied skin image unchanged.

The source is a 600x300 enlargement of the classic 64x32 Minecraft skin layout.
UV coordinates use that logical layout and are stored per triangle corner, as
required by Rice ShapeMesh. The character faces +Z and has its feet at y=0.
"""
from pathlib import Path
import hashlib
import json
import math
import shutil

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "Projects/Steve/Assets"
ASSETS.mkdir(parents=True, exist_ok=True)


def add(a, b):
    return tuple(x+y for x, y in zip(a, b))


def scale(a, s):
    return tuple(x*s for x in a)


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def normalize(v):
    return scale(v, 1/math.sqrt(sum(x*x for x in v)))


def atlas(offset, width, height, depth):
    x, y = offset
    return {
        "+z": (x+depth, y+depth, width, height),
        "-z": (x+depth+width+depth, y+depth, width, height),
        "+x": (x+depth+width, y+depth, depth, height),
        "-x": (x, y+depth, depth, height),
        "+y": (x+depth, y, width, depth),
        "-y": (x+depth+width, y, width, depth),
    }


POSITIONS, INDICES, UV = [], [], []
IDENTITY = ((1, 0, 0), (0, 1, 0), (0, 0, 1))


def cuboid(center, size, skin, basis=IDENTITY, mirror=False):
    first = len(POSITIONS)
    local = []
    for i in range(8):
        p = tuple((.5 if i & (1 << a) else -.5)*size[a] for a in range(3))
        local.append(p)
        world = center
        for a in range(3):
            world = add(world, scale(basis[a], p[a]))
        POSITIONS.append(world)
    # Face corner order is bottom-left, bottom-right, top-right, top-left
    # viewed from outside, with counterclockwise triangle winding.
    faces = {
        "+z": (4, 5, 7, 6), "-z": (1, 0, 2, 3),
        "+x": (5, 1, 3, 7), "-x": (0, 4, 6, 2),
        "+y": (6, 7, 3, 2), "-y": (0, 1, 5, 4),
    }
    for face, corners in faces.items():
        x, y, width, height = skin[face]
        # Half one source image pixel prevents linear minification sampling
        # adjacent atlas cells. It does not remove a logical Minecraft pixel.
        inset_x, inset_y = .5/600, .5/300
        u0, u1 = x/64+inset_x, (x+width)/64-inset_x
        v0, v1 = 1-(y+height)/32+inset_y, 1-y/32-inset_y
        if mirror:
            u0, u1 = u1, u0
        tex = ((u0, v0), (u1, v0), (u1, v1), (u0, v1))
        for k in (0, 1, 2, 2, 3, 0):
            INDICES.append(first+corners[k])
            UV.append(tex[k])


head = atlas((0, 0), 8, 8, 8)
torso = atlas((16, 16), 8, 12, 4)
limb = atlas((40, 16), 4, 12, 4)
leg = atlas((0, 16), 4, 12, 4)
cuboid((0, 2.8, 0), (.8, .8, .8), head)
cuboid((0, 1.8, 0), (.8, 1.2, .4), torso)
cuboid((-.2, .6, 0), (.4, 1.2, .4), leg)
cuboid((.2, .6, 0), (.4, 1.2, .4), leg, mirror=True)
cuboid((-.6, 1.8, 0), (.4, 1.2, .4), limb)

shoulder = (.6, 2.4, 0)
down = normalize((.64, -.4, .94))
y_axis = scale(down, -1)
x_axis = normalize((.94, 0, -.64))
z_axis = cross(x_axis, y_axis)
cuboid(add(shoulder, scale(down, .6)), (.4, 1.2, .4), limb,
        (x_axis, y_axis, z_axis), mirror=True)

low = tuple(min(p[a] for p in POSITIONS) for a in range(3))
high = tuple(max(p[a] for p in POSITIONS) for a in range(3))
pivot = tuple(-low[a]/(high[a]-low[a]) for a in range(3))


def number(value):
    return format(value, ".9g")


payload = " ".join(
    [str(len(POSITIONS)), str(len(INDICES))]
    + [number(x) for p in POSITIONS for x in p]
    + [str(i) for i in INDICES]
    + [number(x) for p in UV for x in p]
)
properties = " ".join(number(x) for x in pivot)+" 0 1 1 0 0 0 0 0 0 1 1 1 0 1 1 0 0 0 0 0 0 1 1 1\n"
shape = ASSETS / "SteveHolding.riceshape"
shape.write_text("RICE_SHAPE 3\n"+payload+"\n"+properties, encoding="utf-8")
texture = ASSETS / "steve.png"
shutil.copyfile(ROOT / "Engine/Sandbox/Assets/steve.png", texture)
asset_ids = {}
for asset in (shape, texture):
    asset_id = hashlib.sha256(("ShadowPresentation/"+asset.name).encode()).hexdigest()[:32]
    Path(str(asset)+".meta").write_text(asset_id, encoding="ascii")
    asset_ids[asset.name] = asset_id

# Basic geometry verification independent of the engine's deserializer.
assert len(POSITIONS) == 48 and len(INDICES) == 216 and len(UV) == 216
for i in range(0, len(INDICES), 3):
    a, b, c = (POSITIONS[index] for index in INDICES[i:i+3])
    ab, ac = add(b, scale(a, -1)), add(c, scale(a, -1))
    assert sum(x*x for x in cross(ab, ac)) > 1e-8
assert hashlib.sha256(texture.read_bytes()).digest() == hashlib.sha256((ROOT / "Engine/Sandbox/Assets/steve.png").read_bytes()).digest()
print(json.dumps({"shape": str(shape), "asset_ids": asset_ids, "bounds_low": low,
                  "bounds_high": high, "pivot": pivot,
                  "hand_center": add(shoulder, scale(down, 1.1)),
                  "hand_end": add(shoulder, scale(down, 1.2)),
                  "suggested_cube_center": (1.35, 2.12, 1.12)}, indent=2))
