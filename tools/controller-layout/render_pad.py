r"""UVC Controller Layout: the controller picture, modelled and rendered in Blender with Cycles.

Run (CPU only, four threads at most):
    blender.exe --background --factory-startup --threads 4 --python render_pad.py -- --pad playstation|xbox|steam --out <dir> [--res 1800] [--samples 128]
While a game is running on the machine, start it with Idle priority as well.

Writes into <dir>:
    pad.png      the render, straight-alpha RGBA on a transparent background
    anchors.json each control's 2D position and the radius of its rim, projected through the camera from a marker placed on the control in 3D,
                 in the picture's pixels and as fractions of the picture

Model units: 1 unit = 1 cm, a pad about 16 wide. +X is the player's right, +Y is away from the player (towards the shoulders),
+Z is out of the face. The body is one lofted, subdivided shell; the buttons sit on it where the real ones do.
"""
import argparse
import json
import math
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    p = argparse.ArgumentParser()
    p.add_argument("--pad", choices=("playstation", "xbox", "steam"), default="playstation")
    p.add_argument("--out", required=True)
    p.add_argument("--anchors-only", action="store_true", help="write anchors.json and leave the picture as it is")
    p.add_argument("--res", type=int, default=1200, help="width of the picture in pixels")
    p.add_argument("--samples", type=int, default=64)
    p.add_argument("--threads", type=int, default=4)
    return p.parse_args(argv)


ARGS = parse_args()
PS = ARGS.pad == "playstation"
XBOX = ARGS.pad == "xbox"
STEAM = ARGS.pad == "steam"  # the 2026 model, proportions taken from photographs of it

# ------------------------------------------------------------------ the scene
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene


def material(name, base, rough=0.5, metal=0.0, coat=0.0, emission=None, emission_strength=0.0, spec=0.5):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    n = m.node_tree.nodes["Principled BSDF"]
    n.inputs["Base Color"].default_value = (*base, 1.0)
    n.inputs["Roughness"].default_value = rough
    n.inputs["Metallic"].default_value = metal
    n.inputs["Specular IOR Level"].default_value = spec
    n.inputs["Coat Weight"].default_value = coat
    n.inputs["Coat Roughness"].default_value = 0.18
    if emission:
        n.inputs["Emission Color"].default_value = (*emission, 1.0)
        n.inputs["Emission Strength"].default_value = emission_strength
    return m


def grained(mat, scale=340.0, depth=0.012):
    """A fine moulded-plastic grain on the bump, in object space so it does not stretch."""
    nt = mat.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    tc = nt.nodes.new("ShaderNodeTexCoord")
    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = scale
    noise.inputs["Detail"].default_value = 3.0
    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.35
    bump.inputs["Distance"].default_value = depth
    nt.links.new(tc.outputs["Object"], noise.inputs["Vector"])
    nt.links.new(noise.outputs["Fac"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    return mat


M_SHELL = grained(material("shell", (0.034, 0.035, 0.040), rough=0.34))
M_PLATE = material("plate", (0.024, 0.025, 0.029), rough=0.24, coat=0.4)
M_TOUCH = grained(material("touchpad", (0.075, 0.078, 0.088), rough=0.30), scale=700.0, depth=0.004)
M_GRIP = grained(material("grip", (0.020, 0.021, 0.024), rough=0.48), scale=180.0, depth=0.03)
M_BUTTON = material("button", (0.040, 0.041, 0.046), rough=0.26, coat=0.5)
M_RUBBER = grained(material("rubber", (0.040, 0.041, 0.046), rough=0.50), scale=420.0, depth=0.02)
M_RING = material("ring", (0.11, 0.115, 0.125), rough=0.26, metal=0.85)
M_TRIGGER = material("trigger", (0.040, 0.041, 0.047), rough=0.24, coat=0.5)
M_SLOT = material("slot", (0.002, 0.002, 0.003), rough=0.8)
M_LIGHTBAR = material("lightbar", (0.02, 0.02, 0.02), rough=0.3, emission=(0.42, 0.56, 0.95), emission_strength=0.9)
M_GLYPH_WHITE = material("glyph", (0.92, 0.92, 0.94), rough=0.35, emission=(1, 1, 1), emission_strength=0.25)
M_GLYPH_TRI = material("g_tri", (0.27, 0.86, 0.72), rough=0.35, emission=(0.27, 0.86, 0.72), emission_strength=0.55)
M_GLYPH_CIR = material("g_cir", (0.92, 0.24, 0.27), rough=0.35, emission=(0.92, 0.24, 0.27), emission_strength=0.55)
M_GLYPH_CRO = material("g_cro", (0.34, 0.56, 0.96), rough=0.35, emission=(0.34, 0.56, 0.96), emission_strength=0.55)
M_GLYPH_SQU = material("g_squ", (0.86, 0.44, 0.78), rough=0.35, emission=(0.86, 0.44, 0.78), emission_strength=0.55)

M_LET_A = material("l_a", (0.25, 0.80, 0.30), rough=0.35, emission=(0.25, 0.80, 0.30), emission_strength=0.55)
M_LET_B = material("l_b", (0.92, 0.22, 0.20), rough=0.35, emission=(0.92, 0.22, 0.20), emission_strength=0.55)
M_LET_X = material("l_x", (0.22, 0.48, 0.96), rough=0.35, emission=(0.22, 0.48, 0.96), emission_strength=0.55)
M_LET_Y = material("l_y", (0.96, 0.78, 0.18), rough=0.35, emission=(0.96, 0.78, 0.18), emission_strength=0.55)

M_LET_WHITE = material("l_white", (0.92, 0.92, 0.94), rough=0.35, emission=(1, 1, 1), emission_strength=0.8)
M_DPAD_STEAM = material("dpad_steam", (0.150, 0.153, 0.168), rough=0.24, coat=0.6)

pad_root = bpy.data.objects.new("pad", None)
scene.collection.objects.link(pad_root)


def add_object(name, mesh, mat, smooth=True, parent=pad_root):
    obj = bpy.data.objects.new(name, mesh)
    scene.collection.objects.link(obj)
    obj.parent = parent
    if mat:
        mesh.materials.append(mat)
    if smooth:
        for poly in mesh.polygons:
            poly.use_smooth = True
    return obj


def add_subsurf(obj, levels=3):
    m = obj.modifiers.new("subsurf", "SUBSURF")
    m.levels = levels
    m.render_levels = levels
    return m


def add_bevel(obj, width=0.04, segments=3, angle=45.0):
    m = obj.modifiers.new("bevel", "BEVEL")
    m.width = width
    m.segments = segments
    m.limit_method = "ANGLE"
    m.angle_limit = math.radians(angle)
    m.harden_normals = False
    return m


# ------------------------------------------------------------------ the shell
def catmull(points, per=8, closed=True):
    """A smooth closed outline through the given points."""
    out = []
    n = len(points)
    for i in range(n if closed else n - 1):
        p0 = Vector(points[(i - 1) % n])
        p1 = Vector(points[i])
        p2 = Vector(points[(i + 1) % n])
        p3 = Vector(points[(i + 2) % n])
        for s in range(per):
            t = s / per
            out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t))
    return out


# The outline seen from the face, right half, top centre round to the bottom centre (x, y). It is mirrored for the left.
RIGHT_OUTLINE = [
    (0.0, 3.55), (1.8, 3.55), (3.6, 3.70), (5.0, 3.72), (6.2, 3.35), (7.05, 2.45), (7.55, 1.05), (7.92, -0.75), (8.20, -2.65),
    (8.28, -4.30), (7.95, -5.55), (7.05, -6.15), (6.0, -5.95), (5.25, -5.10), (4.75, -3.95), (4.15, -3.20), (3.2, -2.95), (1.6, -2.90), (0.0, -2.90),
]
if XBOX:
    # a fuller body: a higher crown between the shoulders, shorter and rounder grips set closer together
    RIGHT_OUTLINE = [
        (0.0, 3.40), (1.8, 3.48), (3.5, 3.82), (5.0, 3.95), (6.2, 3.55), (7.0, 2.60), (7.45, 1.20), (7.75, -0.60), (7.95, -2.40),
        (7.90, -4.00), (7.40, -5.10), (6.50, -5.40), (5.60, -5.00), (4.85, -4.10), (4.10, -3.25), (3.10, -3.00), (1.60, -3.00), (0.0, -3.00),
    ]


if STEAM:
    # a broad, deep body: a nearly flat top, wide square-shouldered, a straight chin between long grips (159 by 111 mm)
    RIGHT_OUTLINE = [
        (0.0, 3.60), (2.0, 3.62), (4.0, 3.55), (5.6, 3.42), (6.9, 2.90), (7.40, 1.90), (7.70, 0.30), (7.95, -2.00), (8.00, -4.20),
        (7.80, -6.00), (7.20, -7.20), (6.40, -7.60), (5.60, -7.30), (5.00, -6.40), (4.65, -5.40), (4.10, -4.95), (3.00, -4.90), (1.50, -4.90), (0.0, -4.90),
    ]


def outline_ring():
    right = RIGHT_OUTLINE
    left = [(-x, y) for x, y in reversed(right[1:-1])]
    return catmull(right + left, per=10)


def top_edge_y(x):
    """The outline's top edge at this |x|, for what sits along the shoulder."""
    pts = RIGHT_OUTLINE[:7]
    return float(np.interp(abs(x), [p[0] for p in pts], [p[1] for p in pts]))


def smoothstep(a, b, v):
    t = np.clip((v - a) / (b - a), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def distance_to_outline(xy, ring):
    p0 = ring
    p1 = np.roll(ring, -1, axis=0)
    seg = p1 - p0
    best = np.full(len(xy), 1e9)
    for a, s in zip(p0, seg):
        rel = xy - a
        t = np.clip((rel @ s) / (s @ s), 0.0, 1.0)
        d = np.hypot(rel[:, 0] - t * s[0], rel[:, 1] - t * s[1])
        best = np.minimum(best, d)
    return best


def inside_outline(xy, ring):
    x, y = xy[:, 0], xy[:, 1]
    inside = np.zeros(len(xy), dtype=bool)
    p1 = np.roll(ring, -1, axis=0)
    for a, b in zip(ring, p1):
        cond = (a[1] > y) != (b[1] > y)
        with np.errstate(divide="ignore", invalid="ignore"):
            xint = (b[0] - a[0]) * (y - a[1]) / (b[1] - a[1]) + a[0]
        inside ^= cond & (x < xint)
    return inside


def build_shell():
    """Two halves, face and back, each a height over the outline: rounded by the distance to the edge, so the shape has no lumps
    however the outline bends. The grips fall away behind the face, as a real pad's do."""
    ring = np.array([(p.x, p.y) for p in outline_ring()])
    n = len(ring)
    prev, nxt = np.roll(ring, 1, axis=0), np.roll(ring, -1, axis=0)
    tangent = nxt - prev
    tangent /= np.linalg.norm(tangent, axis=1, keepdims=True)
    normal = np.stack([-tangent[:, 1], tangent[:, 0]], axis=1)
    # make the normals point inwards
    if not inside_outline((ring + normal * 0.02)[:1], ring)[0]:
        normal = -normal
    pts = [ring]
    offsets = [0.015, 0.04, 0.08, 0.13, 0.2, 0.28, 0.38, 0.5, 0.64, 0.8, 0.98, 1.18, 1.4]
    for off in offsets:
        cand = ring + normal * off
        keep = inside_outline(cand, ring)
        cand = cand[keep]
        dist = distance_to_outline(cand, ring)
        pts.append(cand[np.abs(dist - off) < off * 0.12 + 0.004])
    gx, gy = np.meshgrid(np.arange(-8.4, 8.4, 0.16), (np.arange(-7.9, 3.9, 0.16) if STEAM else np.arange(-6.3, 3.9, 0.16)))
    grid = np.stack([gx.ravel(), gy.ravel()], axis=1)
    grid = grid[inside_outline(grid, ring)]
    grid = grid[distance_to_outline(grid, ring) > 1.5]
    pts.append(grid)
    all_pts = np.concatenate(pts)
    from mathutils.geometry import delaunay_2d_cdt

    cdt = delaunay_2d_cdt([Vector(p) for p in all_pts], [], [list(range(n))], 1, 1e-5)
    verts2d = np.array([(v.x, v.y) for v in cdt[0]])
    tris = [tuple(f) for f in cdt[2]]
    d = distance_to_outline(verts2d, ring)
    x, y = verts2d[:, 0], verts2d[:, 1]
    # how far into a grip, and the midline that bends back with it
    weight = smoothstep(2.5, 4.6, np.abs(x))
    g = np.maximum(0.0, -y - (4.3 if STEAM else 2.0)) * weight
    mid = -0.2 * g ** 1.25
    # the face, flat where the buttons are
    t = np.clip(d / 0.72, 0.0, 1.0)
    round_front = np.sqrt(1.0 - (1.0 - t) ** 2)
    front = mid + (1.0 - 0.24 * g - mid) * round_front
    # the back, deep and round in the grips
    t = np.clip(d / 1.4, 0.0, 1.0)
    round_back = np.sqrt(1.0 - (1.0 - t) ** 2)
    hb = 1.05 + 0.65 * smoothstep(2.0 if STEAM else 0.5, 5.0 if STEAM else 3.5, -y) * weight + 0.22 * smoothstep(2.0, 3.5, y)
    back = mid - hb * round_back
    for name, z, mat, flip in (("shell_face", front, M_SHELL, False), ("shell_back", back, M_GRIP, True)):
        mesh = bpy.data.meshes.new(name)
        faces_ = [tuple(reversed(f)) for f in tris] if flip else tris
        mesh.from_pydata([(float(a), float(b), float(c)) for (a, b), c in zip(verts2d, z)], [], faces_)
        mesh.update()
        add_object(name, mesh, mat)


build_shell()


def outline_mesh(name, points, z, height, mat, bevel=0.03, segments=3, taper=0.0, solid_top=True):
    """An upright prism from a closed outline, bevelled, standing from z to z + height."""
    bm = bmesh.new()
    low = [bm.verts.new((p[0], p[1], z)) for p in points]
    top = [bm.verts.new((p[0] * (1 - taper), p[1] * (1 - taper), z + height)) for p in points]
    n = len(points)
    for i in range(n):
        bm.faces.new((low[i], low[(i + 1) % n], top[(i + 1) % n], top[i]))
    bm.faces.new(top)
    bm.faces.new(list(reversed(low)))
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    obj = add_object(name, mesh, mat)
    if bevel:
        add_bevel(obj, bevel, segments, 30.0)
    return obj


def disc_points(r, n=64, cx=0.0, cy=0.0):
    return [(cx + math.cos(2 * math.pi * i / n) * r, cy + math.sin(2 * math.pi * i / n) * r) for i in range(n)]


def round_rect_points(cx, cy, w, h, r, per=8):
    pts = []
    for (ox, oy, start) in ((w / 2 - r, h / 2 - r, 0), (-w / 2 + r, h / 2 - r, 90), (-w / 2 + r, -h / 2 + r, 180), (w / 2 - r, -h / 2 + r, 270)):
        for i in range(per + 1):
            a = math.radians(start + 90 * i / per)
            pts.append((cx + ox + math.cos(a) * r, cy + oy + math.sin(a) * r))
    return pts


# ------------------------------------------------------------------ the face
FACE_Z = 1.0

if PS:
    # the touchpad, a broad raised slab across the top of the face, wider at its top edge
    touch_outline = catmull(
        [(-3.05, 3.08), (0.0, 3.14), (3.05, 3.08), (3.22, 2.7), (2.92, 0.92), (2.65, 0.55), (0.0, 0.50), (-2.65, 0.55), (-2.92, 0.92), (-3.22, 2.7)], per=6
    )
    outline_mesh("touch_bed", [(p.x * 1.035, 1.8 + (p.y - 1.8) * 1.05) for p in touch_outline], FACE_Z - 0.06, 0.08, M_SLOT, bevel=0)
    outline_mesh("touchpad", [(p.x, p.y) for p in touch_outline], FACE_Z - 0.02, 0.14, M_TOUCH, bevel=0.05, segments=4)

    # the light bar, two strips along the touchpad's lower corners
    for side in (-1, 1):
        bar = catmull(
            [(side * 2.96, 0.98), (side * 3.14, 2.2), (side * 3.20, 2.2), (side * 3.02, 0.92)], per=3
        )
        outline_mesh(f"lightbar_{side}", [(p.x, p.y) for p in bar], FACE_Z + 0.0, 0.1, M_LIGHTBAR, bevel=0)

    # the plate between the sticks, where the centre button and the mic sit
    strip = catmull([(-0.95, 0.30), (0.0, 0.34), (0.95, 0.30), (0.95, -1.35), (0.0, -1.55), (-0.95, -1.35)], per=6)
    outline_mesh("centre_plate", [(p.x, p.y) for p in strip], FACE_Z - 0.2, 0.22, M_PLATE, bevel=0.03, segments=2)

CONTROL_MARKERS = {}
CONTROL_SIZES = {}  # how big the control is, in model units: another leader should stay off it
CONTROL_RIMS = {}  # how far from the marker a leader stops, in model units: 0 lands on the part itself


def marker(name, pos, rim=0.0, size=None):
    CONTROL_MARKERS[name] = Vector(pos)
    CONTROL_RIMS[name] = rim
    CONTROL_SIZES[name] = rim if size is None else size


def well(name, cx, cy, r, z=FACE_Z):
    """A dark recess ring round a control."""
    outline_mesh(name, disc_points(r, 72, cx, cy), z - 0.1, 0.14, M_SLOT, bevel=0.02, segments=2)


def glyph_triangle(cx, cy, z, s, mat, stroke=0.075):
    outer = [(cx, cy + s * 1.1), (cx + s, cy - s * 0.72), (cx - s, cy - s * 0.72)]
    glyph_frame("g_tri", outer, stroke, z, mat)


def glyph_square(cx, cy, z, s, mat, stroke=0.075):
    outer = [(cx - s, cy + s), (cx + s, cy + s), (cx + s, cy - s), (cx - s, cy - s)]
    glyph_frame("g_squ", outer, stroke, z, mat)


def glyph_circle(cx, cy, z, s, mat, stroke=0.075):
    outer = [(cx + math.cos(a) * s, cy + math.sin(a) * s) for a in (2 * math.pi * i / 48 for i in range(48))]
    glyph_frame("g_cir", outer, stroke, z, mat)


def glyph_frame(name, outer, stroke, z, mat):
    """A closed ring of the given width on a button's top."""
    bm = bmesh.new()
    n = len(outer)
    cx = sum(p[0] for p in outer) / n
    cy = sum(p[1] for p in outer) / n
    o = []
    i_ = []
    for p in outer:
        d = Vector((p[0] - cx, p[1] - cy))
        length = d.length
        # the inner ring is offset by the stroke along the corner bisector: near enough for regular shapes
        k = (length - stroke * (1.6 if n <= 4 else 1.0)) / length
        o.append(bm.verts.new((p[0], p[1], z)))
        i_.append(bm.verts.new((cx + d.x * k, cy + d.y * k, z)))
    for j in range(n):
        bm.faces.new((o[j], o[(j + 1) % n], i_[(j + 1) % n], i_[j]))
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    obj = add_object(name, mesh, mat, smooth=False)
    s = obj.modifiers.new("solid", "SOLIDIFY")
    s.thickness = 0.012
    s.offset = 1


def glyph_cross(cx, cy, z, s, mat, stroke=0.07):
    for k, sign in enumerate((1, -1)):
        pts = []
        d = Vector((s, sign * s))
        d.normalize()
        perp = Vector((-d.y, d.x)) * stroke
        a = Vector((cx, cy)) + d * s * 1.32
        b = Vector((cx, cy)) - d * s * 1.32
        pts = [a + perp, b + perp, b - perp, a - perp]
        bm = bmesh.new()
        bm.faces.new([bm.verts.new((p.x, p.y, z + 0.001 * k)) for p in pts])
        mesh = bpy.data.meshes.new(f"g_cro{k}")
        bm.to_mesh(mesh)
        bm.free()
        obj = add_object(f"g_cro{k}", mesh, mat, smooth=False)
        so = obj.modifiers.new("solid", "SOLIDIFY")
        so.thickness = 0.012
        so.offset = 1


def domed_button(name, cx, cy, r, z, height=0.24, mat=None):
    """A round button with a shallow domed top."""
    profile = [(1.0, 0.0), (1.0, 0.72), (0.97, 0.86), (0.86, 0.95), (0.55, 1.0), (0.0, 1.03)]
    bm = bmesh.new()
    n = 48
    rings = []
    for rr, zz in profile[:-1]:
        rings.append([bm.verts.new((cx + math.cos(2 * math.pi * i / n) * r * rr, cy + math.sin(2 * math.pi * i / n) * r * rr, z + zz * height)) for i in range(n)])
    tip = bm.verts.new((cx, cy, z + profile[-1][1] * height))
    for a, b in zip(rings, rings[1:]):
        for i in range(n):
            bm.faces.new((a[i], a[(i + 1) % n], b[(i + 1) % n], b[i]))
    for i in range(n):
        bm.faces.new((rings[-1][i], rings[-1][(i + 1) % n], tip))
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    return add_object(name, mesh, mat or M_BUTTON)


def letter(name, text, x, y, z, size, mat, tilt=0.0):
    """Raised lettering, in the model's axes, tipped by tilt degrees towards the top edge."""
    curve = bpy.data.curves.new(name, "FONT")
    curve.body = text
    curve.font = LETTER_FONT
    curve.size = size
    curve.align_x = "CENTER"
    curve.align_y = "CENTER"
    curve.extrude = 0.006
    curve.materials.append(mat)
    obj = bpy.data.objects.new(name, curve)
    scene.collection.objects.link(obj)
    obj.parent = pad_root
    obj.location = (x, y, z)
    obj.rotation_euler = (math.radians(-tilt), 0.0, 0.0)
    return obj


LETTER_FONT = bpy.data.fonts.load("C:/Windows/Fonts/segoeuib.ttf")


def face_diamond(centre, spread, radius, marks, bed=True):
    """The four face buttons in their diamond. marks: button -> a function that marks its top."""
    if bed:
        outline_mesh("face_bed", disc_points(spread + radius + 0.12, 96, *centre), FACE_Z - 0.09, 0.115, M_PLATE, bevel=0.02, segments=2)
    for key, (ox, oy) in (("Y", (0, 1)), ("B", (1, 0)), ("A", (0, -1)), ("X", (-1, 0))):
        x, y = centre[0] + ox * spread, centre[1] + oy * spread
        well(f"well_{key}", x, y, radius + 0.05)
        domed_button(f"btn_{key}", x, y, radius, FACE_Z, 0.20)
        top = FACE_Z + 0.20 * 1.03 + 0.006
        marks[key](x, y, top)
        marker(key, (x, y, top), radius)


def dpad_cross(centre, bed_radius, w, root, tip, rim, size, arrow_scale=1.0):
    """The D-pad: one cross moulded as four petals, with a dimple in the middle."""
    outline_mesh("dpad_bed", disc_points(bed_radius, 96, *centre), FACE_Z - 0.09, 0.115, M_PLATE, bevel=0.02, segments=2)
    for name, glyph_dir in (("DUp", 90), ("DRight", 0), ("DDown", 270), ("DLeft", 180)):
        # a petal: from a narrow root by the middle to a broad square end
        a = math.radians(glyph_dir)
        ux = Vector((math.cos(a), math.sin(a)))
        uy = Vector((-math.sin(a), math.cos(a)))
        c = Vector(centre)
        pts = [c + ux * root, c + ux * (root + 0.28) + uy * w, c + ux * tip + uy * w, c + ux * tip - uy * w, c + ux * (root + 0.28) - uy * w]
        outline_mesh(f"dpad_{name}", [(p.x, p.y) for p in pts], FACE_Z - 0.01, 0.20, M_BUTTON, bevel=0.05, segments=3)
        # an arrow pressed into the end of the petal
        arrow_at = c + ux * (tip - 0.42 * max(arrow_scale, 0.8))
        arrow = [arrow_at + ux * 0.14 * arrow_scale, arrow_at - ux * 0.08 * arrow_scale + uy * 0.15 * arrow_scale, arrow_at - ux * 0.08 * arrow_scale - uy * 0.15 * arrow_scale]
        bm = bmesh.new()
        bm.faces.new([bm.verts.new((p.x, p.y, FACE_Z + 0.196)) for p in arrow])
        mesh = bpy.data.meshes.new(f"arrow_{name}")
        bm.to_mesh(mesh)
        bm.free()
        obj = add_object(f"arrow_{name}", mesh, M_GLYPH_WHITE, smooth=False)
        obj.modifiers.new("solid", "SOLIDIFY").thickness = 0.008
        marker(name, (arrow_at.x, arrow_at.y, FACE_Z + 0.2), rim, size)


def stick(name, cx, cy):
    """A thumbstick: a well in the shell, a metal ring, a neck, a dished rubber cap with a raised rim."""
    outline_mesh(f"{name}_well", disc_points(1.16, 96, cx, cy), FACE_Z - 0.4, 0.40, M_SLOT, bevel=0.02, segments=2)
    outline_mesh(f"{name}_ring", disc_points(1.16, 96, cx, cy), FACE_Z - 0.4, 0.44, M_RING, bevel=0.02, segments=2)
    outline_mesh(f"{name}_inner", disc_points(1.02, 96, cx, cy), FACE_Z - 0.0, 0.06, M_SLOT, bevel=0.0)
    profile = [
        (0.56, 0.00), (0.54, 0.42), (0.66, 0.54), (0.86, 0.58), (0.90, 0.66), (0.88, 0.76), (0.80, 0.80), (0.68, 0.76), (0.42, 0.70), (0.18, 0.67), (0.0, 0.66),
    ]
    bm = bmesh.new()
    n = 64
    rings = []
    for rr, zz in profile[:-1]:
        rings.append([bm.verts.new((cx + math.cos(2 * math.pi * i / n) * rr, cy + math.sin(2 * math.pi * i / n) * rr, FACE_Z + zz)) for i in range(n)])
    tip = bm.verts.new((cx, cy, FACE_Z + profile[-1][1]))
    for a, b in zip(rings, rings[1:]):
        for i in range(n):
            bm.faces.new((a[i], a[(i + 1) % n], b[(i + 1) % n], b[i]))
    for i in range(n):
        bm.faces.new((rings[-1][i], rings[-1][(i + 1) % n], tip))
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    add_object(name, mesh, M_RUBBER)
    marker(name, (cx, cy, FACE_Z + 0.68), 0.5, 0.95)


if PS:
    face_diamond((5.25, 1.25), 1.02, 0.50, {
        "Y": lambda x, y, top: glyph_triangle(x, y, top, 0.205, M_GLYPH_TRI),
        "B": lambda x, y, top: glyph_circle(x, y, top, 0.235, M_GLYPH_CIR),
        "A": lambda x, y, top: glyph_cross(x, y, top, 0.20, M_GLYPH_CRO),
        "X": lambda x, y, top: glyph_square(x, y, top, 0.19, M_GLYPH_SQU),
    })
    dpad_cross((-5.25, 1.25), 1.62, 0.40, 0.26, 1.36, 0.34, 0.42)
    # D-pad right's leader comes from the lower left: it lands on the petal's lower outer corner, so it clears D-pad down
    marker("DRight", (-5.25 + 1.20, 1.25 - 0.30, FACE_Z + 0.2), 0.0, 0.42)
    # the touchpad's own leader comes in over the top of the D-pad and Create: it lands near the pad's upper left
    marker("Touchpad", (-2.25, 2.70, FACE_Z + 0.12), 0.0, 0.0)

    stick("LStick", -2.55, -0.95)
    stick("RStick", 2.55, -0.95)

    # Create and Options: small pills in the corners of the touchpad
    for name, x in (("Create", -3.72), ("Options", 3.72)):
        pts = round_rect_points(x, 2.72, 0.34, 0.74, 0.15, 6)
        outline_mesh(f"{name}_well", round_rect_points(x, 2.72, 0.46, 0.86, 0.2, 6), FACE_Z - 0.1, 0.13, M_SLOT, bevel=0)
        outline_mesh(name, pts, FACE_Z - 0.01, 0.13, M_BUTTON, bevel=0.04, segments=3)
        marker(name, (x, 2.72, FACE_Z + 0.13), 0.3)

    # the centre button: a round button with a plain ring, no logo
    outline_mesh("Centre_well", disc_points(0.40, 48, 0.0, -0.72), FACE_Z - 0.1, 0.14, M_SLOT, bevel=0)
    domed_button("Centre", 0.0, -0.72, 0.33, FACE_Z - 0.01, 0.13)
    marker("Centre", (0.0, -0.72, FACE_Z + 0.12), 0.33)
    # mic holes above it, a detail that stops the strip reading as a blank
    for i in range(-2, 3):
        outline_mesh(f"mic_{i}", disc_points(0.028, 10, i * 0.13, -0.08), FACE_Z + 0.02, 0.006, M_SLOT, bevel=0)

if XBOX:
    # the glossy panel round the centre button and the two small ones
    panel = catmull([(-2.15, 3.02), (0.0, 3.08), (2.15, 3.02), (2.05, 1.05), (1.35, 0.55), (0.0, 0.42), (-1.35, 0.55), (-2.05, 1.05)], per=6)
    outline_mesh("centre_panel", [(p.x, p.y) for p in panel], FACE_Z - 0.2, 0.225, M_PLATE, bevel=0.03, segments=2)
    face_diamond((4.85, 1.45), 0.96, 0.47, {
        "Y": lambda x, y, top: letter("let_Y", "Y", x, y, top, 0.56, M_LET_Y),
        "B": lambda x, y, top: letter("let_B", "B", x, y, top, 0.56, M_LET_B),
        "A": lambda x, y, top: letter("let_A", "A", x, y, top, 0.56, M_LET_A),
        "X": lambda x, y, top: letter("let_X", "X", x, y, top, 0.56, M_LET_X),
    }, bed=False)
    stick("LStick", -4.85, 1.45)
    stick("RStick", 2.40, -1.40)
    dpad_cross((-2.40, -1.40), 1.20, 0.33, 0.18, 1.06, 0.28, 0.36, arrow_scale=0.8)
    # as on the PlayStation pad: D-pad right's leader lands on the petal's lower outer corner, so it clears D-pad down
    marker("DRight", (-2.40 + 0.90, -1.40 - 0.24, FACE_Z + 0.2), 0.0, 0.36)
    # the centre button: round and plain, with a ring, no logo
    outline_mesh("Centre_well", disc_points(0.56, 64, 0.0, 2.25), FACE_Z - 0.1, 0.15, M_SLOT, bevel=0)
    domed_button("Centre", 0.0, 2.25, 0.48, FACE_Z - 0.01, 0.13)
    marker("Centre", (0.0, 2.25, FACE_Z + 0.12), 0.48)
    # View and Menu: small round buttons, each with its plain symbol
    for name, x in (("Create", -1.35), ("Options", 1.35)):
        outline_mesh(f"{name}_well", disc_points(0.33, 40, x, 1.20), FACE_Z - 0.1, 0.15, M_SLOT, bevel=0)
        domed_button(name, x, 1.20, 0.27, FACE_Z - 0.01, 0.12)
        marker(name, (x, 1.20, FACE_Z + 0.12), 0.27)
    top = FACE_Z + 0.12 * 1.03 + 0.006
    glyph_frame("view_a", [(-1.35 - 0.13, 1.25 + 0.09), (-1.35 + 0.05, 1.25 + 0.09), (-1.35 + 0.05, 1.25 - 0.05), (-1.35 - 0.13, 1.25 - 0.05)], 0.018, top, M_GLYPH_WHITE)
    glyph_frame("view_b", [(-1.35 - 0.05, 1.15 + 0.09), (-1.35 + 0.13, 1.15 + 0.09), (-1.35 + 0.13, 1.15 - 0.05), (-1.35 - 0.05, 1.15 - 0.05)], 0.018, top, M_GLYPH_WHITE)
    for k, oy in enumerate((0.09, 0.0, -0.09)):
        bm = bmesh.new()
        bm.faces.new([bm.verts.new((1.35 + a, 1.20 + oy + b, top)) for a, b in ((-0.13, 0.022), (0.13, 0.022), (0.13, -0.022), (-0.13, -0.022))])
        mesh = bpy.data.meshes.new(f"menu_{k}")
        bm.to_mesh(mesh)
        bm.free()
        add_object(f"menu_{k}", mesh, M_GLYPH_WHITE, smooth=False).modifiers.new("solid", "SOLIDIFY").thickness = 0.008

if STEAM:
    # the apron: a shallow raised step round the trackpads, its top edge dipping under the sticks and rising to the sides
    apron = catmull([(-6.55, -0.25), (-5.2, -0.15), (-3.9, -0.55), (-2.55, -0.72), (-1.1, -0.72), (0.0, -0.72), (1.1, -0.72), (2.55, -0.72), (3.9, -0.55), (5.2, -0.15), (6.55, -0.25),
                     (6.95, -1.6), (6.75, -3.5), (5.6, -4.45), (3.6, -4.55), (0.0, -4.55), (-3.6, -4.55), (-5.6, -4.45), (-6.75, -3.5), (-6.95, -1.6)], per=5)
    outline_mesh("apron", [(p.x, p.y) for p in apron], FACE_Z - 0.2, 0.235, M_SHELL, bevel=0.05, segments=4)

    # the two square trackpads, 34.5 mm, angled inward
    def rotated_square(cx, cy, half, radius, degrees):
        a = math.radians(degrees)
        out = []
        for x, y in round_rect_points(0.0, 0.0, half * 2, half * 2, radius, 8):
            out.append((cx + x * math.cos(a) - y * math.sin(a), cy + x * math.sin(a) + y * math.cos(a)))
        return out

    for side, name in ((-1, "LeftPad"), (1, "RightPad")):
        cx, cy, deg = side * 3.15, -2.45, side * 9.0
        outline_mesh(f"{name}_bed", rotated_square(cx, cy, 1.80, 0.42, deg), FACE_Z - 0.06, 0.12, M_SLOT, bevel=0)
        outline_mesh(name, rotated_square(cx, cy, 1.72, 0.36, deg), FACE_Z - 0.02, 0.13, M_TOUCH, bevel=0.04, segments=4)
        a = math.radians(deg)
        ux, uy = Vector((math.cos(a), math.sin(a))), Vector((-math.sin(a), math.cos(a)))
        c = Vector((cx, cy))
        marker(name, (cx, cy, FACE_Z + 0.11), 0.0, 0.0)
        # where a leader lands when the player's config makes this pad a D-pad
        for part, v in (("Up", uy), ("Down", -uy), ("Left", -ux), ("Right", ux)):
            p = c + v * 1.10
            marker(f"{name}{part}", (p.x, p.y, FACE_Z + 0.11), 0.0, 0.0)

    face_diamond((5.20, 1.55), 0.96, 0.44, {
        "Y": lambda x, y, top: letter("let_Y", "Y", x, y, top, 0.54, M_LET_WHITE),
        "B": lambda x, y, top: letter("let_B", "B", x, y, top, 0.54, M_LET_WHITE),
        "A": lambda x, y, top: letter("let_A", "A", x, y, top, 0.54, M_LET_WHITE),
        "X": lambda x, y, top: letter("let_X", "X", x, y, top, 0.54, M_LET_WHITE),
    }, bed=False)

    # the D-pad: one raised glossy plus, no bed
    dc = Vector((-5.15, 1.60))
    arm, half = 1.30, 0.44
    plus = [(-half, arm), (half, arm), (half, half), (arm, half), (arm, -half), (half, -half), (half, -arm), (-half, -arm), (-half, -half), (-arm, -half), (-arm, half), (-half, half)]
    outline_mesh("dpad_well", [(dc.x + x * 1.05, dc.y + y * 1.05) for x, y in plus], FACE_Z - 0.08, 0.11, M_SLOT, bevel=0)
    plus_obj = outline_mesh("dpad_plus", [(dc.x + x, dc.y + y) for x, y in plus], FACE_Z - 0.01, 0.24, M_DPAD_STEAM, bevel=0.09, segments=4)
    for name, (ox, oy) in (("DUp", (0, 1)), ("DDown", (0, -1)), ("DLeft", (-1, 0)), ("DRight", (1, 0))):
        marker(name, (dc.x + ox * 0.92, dc.y + oy * 0.92, FACE_Z + 0.23), 0.0, 0.34)
    # D-pad right's leader lands on the arm's lower edge, as on the other pads, so it clears D-pad down
    marker("DRight", (dc.x + 0.95, dc.y - 0.30, FACE_Z + 0.23), 0.0, 0.34)

    stick("LStick", -2.55, 0.80)
    stick("RStick", 2.55, 0.80)

    # the centre button: round and plain, with a ring, no logo
    outline_mesh("Centre_well", disc_points(0.70, 64, 0.0, 1.55), FACE_Z - 0.1, 0.15, M_SLOT, bevel=0)
    domed_button("Centre", 0.0, 1.55, 0.62, FACE_Z - 0.01, 0.13)
    marker("Centre", (0.0, 1.55, FACE_Z + 0.12), 0.62)
    outline_mesh("led", disc_points(0.05, 16, 0.0, 2.72), FACE_Z - 0.02, 0.03, M_LIGHTBAR, bevel=0)

    def pill(name, x, y, w=1.12, h=0.50):
        outline_mesh(f"{name}_well", round_rect_points(x, y, w + 0.12, h + 0.12, (h + 0.12) / 2 - 0.01, 8), FACE_Z - 0.1, 0.15, M_SLOT, bevel=0)
        outline_mesh(name, round_rect_points(x, y, w, h, h / 2 - 0.01, 8), FACE_Z - 0.01, 0.13, M_BUTTON, bevel=0.04, segments=3)
        marker(name, (x, y, FACE_Z + 0.12), 0.46, 0.46)

    pill("Create", -2.85, 2.75)   # View
    pill("Options", 2.85, 2.75)   # Menu
    marker("Options", (2.85 - 0.42, 2.75 - 0.20, FACE_Z + 0.12), 0.0, 0.46)  # its leader comes from the lower right and passes clear below X
    top = FACE_Z + 0.125
    glyph_frame("view_a", [(-2.85 - 0.15, 2.80 + 0.09), (-2.85 + 0.05, 2.80 + 0.09), (-2.85 + 0.05, 2.80 - 0.05), (-2.85 - 0.15, 2.80 - 0.05)], 0.018, top, M_GLYPH_WHITE)
    glyph_frame("view_b", [(-2.85 - 0.05, 2.70 + 0.09), (-2.85 + 0.15, 2.70 + 0.09), (-2.85 + 0.15, 2.70 - 0.05), (-2.85 - 0.05, 2.70 - 0.05)], 0.018, top, M_GLYPH_WHITE)
    for k, oy in enumerate((0.09, 0.0, -0.09)):
        bm = bmesh.new()
        bm.faces.new([bm.verts.new((2.85 + a, 2.75 + oy + b, top)) for a, b in ((-0.15, 0.022), (0.15, 0.022), (0.15, -0.022), (-0.15, -0.022))])
        mesh = bpy.data.meshes.new(f"menu_{k}")
        bm.to_mesh(mesh)
        bm.free()
        add_object(f"menu_{k}", mesh, M_GLYPH_WHITE, smooth=False).modifiers.new("solid", "SOLIDIFY").thickness = 0.008
    pill("QuickAccess", 0.0, -2.45)
    for k in (-1, 0, 1):
        outline_mesh(f"qam_{k}", disc_points(0.05, 14, k * 0.2, -2.45), FACE_Z + 0.115, 0.012, M_GLYPH_WHITE, bevel=0)
    # the four back buttons are hidden behind the pad: their leaders land on the body's edge beside them
    for name, x, y in (("L4", -7.30, -1.6), ("L5", -7.45, -3.6), ("R4", 7.30, -1.6), ("R5", 7.45, -3.6)):
        marker(name, (x, y, 0.75), 0.0, 0.0)

# ------------------------------------------------------------------ the shoulders
TOP_Y = 3.55


def along_shoulder(name, side, x0, x1, section, y_off, z_at, marker_at, mat):
    """A bar lofted along the top edge from x0 to x1, its section (y, z) carried on the outline's curve and narrowing at both ends."""
    n = 24
    bm = bmesh.new()
    rings = []
    for i in range(n + 1):
        t = i / n
        x = x0 + (x1 - x0) * t
        y = top_edge_y(x) + y_off
        k = 0.42 + 0.58 * (1.0 - abs(2 * t - 1) ** 5) ** 0.5
        rings.append([bm.verts.new((side * x, y + yo * k, z_at(t) + zo * k)) for yo, zo in section])
    m = len(section)
    for a, b in zip(rings, rings[1:]):
        for j in range(m):
            f = (a[j], a[(j + 1) % m], b[(j + 1) % m], b[j])
            bm.faces.new(f if side > 0 else tuple(reversed(f)))
    bm.faces.new(rings[0] if side > 0 else list(reversed(rings[0])))
    bm.faces.new(list(reversed(rings[-1])) if side > 0 else rings[-1])
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    obj = add_object(name, mesh, mat)
    add_subsurf(obj, 3)
    marker(name, marker_at)
    return obj


def bumper(name, side):
    """L1 or R1, LB or RB: a broad shallow bar on the top edge, next to the face."""
    section = [(-0.30, 0.40), (0.05, 0.44), (0.34, 0.34), (0.46, 0.08), (0.42, -0.20), (0.10, -0.32), (-0.30, -0.30)]
    if XBOX:
        # long, reaching in towards the centre, with its name on it
        x = 5.25
        along_shoulder(name, side, 2.05, 6.20, section, 0.04, lambda t: 0.40, (side * x, top_edge_y(x) + 0.04 + 0.40, 0.40 + 0.20), M_TRIGGER)
        tx = 3.75
        letter(f"let_{name}", name, side * tx, top_edge_y(tx) + 0.04 + 0.18, 0.40 + 0.455, 0.42, M_GLYPH_WHITE, tilt=38.0)
        return
    if STEAM:
        x = 5.85
        along_shoulder(name, side, 3.85, 6.65, section, 0.03, lambda t: 0.40, (side * x, top_edge_y(x) + 0.03 + 0.40, 0.40 + 0.20), M_TRIGGER)
        tx = 4.75
        letter(f"let_{name}", "L1" if side < 0 else "R1", side * tx, top_edge_y(tx) + 0.03 + 0.18, 0.40 + 0.455, 0.42, M_GLYPH_WHITE, tilt=38.0)
        return
    x = 5.0
    along_shoulder(name, side, 3.25, 6.35, section, 0.05, lambda t: 0.40, (side * x, top_edge_y(x) + 0.05 + 0.40, 0.40 + 0.20), M_TRIGGER)


def trigger(name, side):
    """L2 or R2, LT or RT: a big curled paddle behind the bumper, on the back half of the shoulder."""
    section = [(-0.25, 0.08), (0.20, 0.18), (0.62, 0.12), (0.92, -0.14), (1.02, -0.52), (0.82, -0.86), (0.40, -0.98), (-0.25, -0.92)]
    if XBOX:
        x = 5.30
        along_shoulder(name, side, 3.45, 6.00, section, 0.24, lambda t: -0.20, (side * x, top_edge_y(x) + 0.24 + 0.92, -0.20 - 0.20), M_TRIGGER)
        tx = 4.25
        letter(f"let_{name}", name, side * tx, top_edge_y(tx) + 0.24 + 0.68, -0.20 + 0.10, 0.42, M_GLYPH_WHITE, tilt=62.0)
        return
    if STEAM:
        x = 5.85
        along_shoulder(name, side, 4.05, 6.50, section, 0.20, lambda t: -0.20, (side * x, top_edge_y(x) + 0.20 + 0.92, -0.20 - 0.20), M_TRIGGER)
        tx = 4.85
        letter(f"let_{name}", "L2" if side < 0 else "R2", side * tx, top_edge_y(tx) + 0.20 + 0.68, -0.20 + 0.10, 0.42, M_GLYPH_WHITE, tilt=62.0)
        return
    x = 5.0
    along_shoulder(name, side, 3.55, 6.05, section, 0.28, lambda t: -0.20, (side * x, top_edge_y(x) + 0.28 + 0.92, -0.20 - 0.20), M_TRIGGER)


bumper("LB", -1)
bumper("RB", 1)
trigger("LT", -1)
trigger("RT", 1)


# ------------------------------------------------------------------ the pose: top edge towards the camera, turned to the left
# Rotations on the root: the top of the pad tips towards the viewer so the shoulders show, then a turn so the left grip recedes.
TILT = 29.0   # degrees past facing the camera: the top edge comes towards the viewer and the face looks slightly down
TURN = -15.0  # degrees about the vertical: the face turns towards the left
ROLL = 0.0
pad_root.rotation_mode = "XYZ"
pad_root.rotation_euler = (math.radians(90.0 + TILT), math.radians(ROLL), math.radians(TURN))

# the camera, long lens so the verticals barely converge
cam_data = bpy.data.cameras.new("cam")
cam_data.lens = 110.0
cam_data.sensor_width = 36.0
cam = bpy.data.objects.new("cam", cam_data)
scene.collection.objects.link(cam)
cam.location = (0.0, -78.0, 0.0)
cam.rotation_euler = (math.radians(90.0), 0.0, 0.0)
scene.camera = cam

# ------------------------------------------------------------------ the light: a soft key from high front left, a cool rim all round the top
# edge and the sides so that a black pad reads on black, and a weak fill


def area(name, loc, size, energy, colour=(1, 1, 1), target=(0, 0, 0), size_y=None):
    light = bpy.data.lights.new(name, "AREA")
    light.energy = energy
    light.color = colour
    if size_y:
        light.shape = "RECTANGLE"
        light.size = size
        light.size_y = size_y
    else:
        light.size = size
    obj = bpy.data.objects.new(name, light)
    scene.collection.objects.link(obj)
    obj.location = loc
    direction = Vector(target) - Vector(loc)
    obj.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
    return obj


area("key", (-16, -26, 30), 14, 4200, (1.0, 0.97, 0.93))
area("top_soft", (10, -12, 36), 20, 2600, (0.96, 0.97, 1.0))
area("fill", (22, -34, -6), 34, 900, (0.82, 0.88, 1.0))
area("rim_top", (0, 20, 26), 34, 5200, (0.88, 0.92, 1.0), size_y=7)
area("rim_left", (-30, 2, 2), 10, 6600, (0.86, 0.91, 1.0), size_y=30)
area("rim_right", (30, 6, 2), 8, 4200, (0.92, 0.95, 1.0), size_y=30)
area("rim_under", (0, 4, -26), 36, 1100, (0.80, 0.85, 1.0), size_y=8)

world = bpy.data.worlds.new("world")
world.use_nodes = True
world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.025, 0.026, 0.03, 1.0)
world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.6
scene.world = world

# ------------------------------------------------------------------ the render
scene.render.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.render.threads_mode = "FIXED"
scene.render.threads = ARGS.threads
scene.cycles.samples = ARGS.samples
scene.cycles.use_adaptive_sampling = True
scene.cycles.adaptive_threshold = 0.02
scene.cycles.use_denoising = True
scene.cycles.denoiser = "OPENIMAGEDENOISE"
scene.cycles.denoising_use_gpu = False
scene.render.film_transparent = True
scene.render.filter_size = 1.2
scene.view_settings.view_transform = "Standard"
scene.view_settings.look = "None"
scene.view_settings.exposure = 0.0

ASPECT = 0.82 if STEAM else 0.66  # height over width of the picture
scene.render.resolution_x = ARGS.res
scene.render.resolution_y = int(ARGS.res * ASPECT)
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_mode = "RGBA"
scene.render.image_settings.color_depth = "8"
scene.render.image_settings.compression = 15

# fit the frame to the model's corners
bpy.context.view_layer.update()
world_pts = []
for obj in scene.objects:
    if obj.type == "MESH":
        world_pts += [obj.matrix_world @ Vector(c) for c in obj.bound_box]
cam_inv = cam.matrix_world.inverted()
xs = [(cam_inv @ p).x for p in world_pts]
ys = [(cam_inv @ p).y for p in world_pts]
cam.location.x += (min(xs) + max(xs)) / 2
cam.location.z += (min(ys) + max(ys)) / 2
half_w = (max(xs) - min(xs)) / 2
half_h = (max(ys) - min(ys)) / 2
needed = max(half_w, half_h / ASPECT) * 1.06
# lens: half sensor over lens equals half width over distance
cam_data.lens = 18.0 * 78.0 / needed

bpy.context.view_layer.update()

# ------------------------------------------------------------------ the anchors, through the camera
from bpy_extras.object_utils import world_to_camera_view  # noqa: E402

deps = bpy.context.evaluated_depsgraph_get()
W = scene.render.resolution_x
H = scene.render.resolution_y
anchors = {}
root_m = pad_root.matrix_world
for name, local in CONTROL_MARKERS.items():
    world_pos = root_m @ local
    uvw = world_to_camera_view(scene, cam, world_pos)
    rim = CONTROL_RIMS.get(name, 0.0)
    rim_ends = [world_to_camera_view(scene, cam, root_m @ (local + Vector(v) * rim)) for v in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0))]
    rim_px = sum(math.hypot((e.x - uvw.x) * W, (e.y - uvw.y) * H) for e in rim_ends) / 4
    size = CONTROL_SIZES.get(name, 0.0)
    size_ends = [world_to_camera_view(scene, cam, root_m @ (local + Vector(v) * size)) for v in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0))]
    size_px = sum(math.hypot((e.x - uvw.x) * W, (e.y - uvw.y) * H) for e in size_ends) / 4
    # is anything between the camera and the marker? a marker on a control's own surface must be seen
    origin = cam.matrix_world.translation
    direction = (world_pos - origin).normalized()
    hit, location, _n, _i, hit_obj, _m = scene.ray_cast(deps, origin, direction, distance=(world_pos - origin).length - 0.15)
    anchors[name] = {
        "x": round(uvw.x * W, 2),
        "y": round((1.0 - uvw.y) * H, 2),
        "fx": round(uvw.x, 5),
        "fy": round(1.0 - uvw.y, 5),
        "rim": round(rim_px, 2),
        "size": round(size_px, 2),
        "occludedBy": hit_obj.name if hit else None,
    }

import os  # noqa: E402

os.makedirs(ARGS.out, exist_ok=True)
with open(os.path.join(ARGS.out, "anchors.json"), "w", encoding="utf-8") as f:
    json.dump({"pad": ARGS.pad, "width": W, "height": H, "anchors": anchors}, f, indent=1)

if not ARGS.anchors_only:
    scene.render.filepath = os.path.join(ARGS.out, "pad.png")
    bpy.ops.render.render(write_still=True)
print("DONE", W, H, "anchors", len(anchors))
