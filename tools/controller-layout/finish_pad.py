r"""UVC Controller Layout: from Blender's render to what the mod would use, and the pictures to judge it by.

    py -3 -I finish_pad.py <render dir> <out dir> [preview.png]

The pad is named in the render's anchors.json.

Reads pad.png and anchors.json from the render, and writes into <out dir>:
    pad_cropped.png   the render cropped to the pad, straight alpha
    anchors.json      each control in the cropped picture's pixels and in the graphic's 1280x720 design
    anchors_table.txt the same as a C++ table for ButtonAt
    pad_<pad>.swf     the picture as a DefineBitsLossless2 bitmap, premultiplied, on a shape of the design size: no script
    layout_mock.png   every box and leader with real Gameplay rows
and the owner's preview, the pad on the dark sheet with three leaders, to [preview.png].
"""
import itertools
import json
import os
import struct
import sys
import zlib

from PIL import Image, ImageDraw, ImageFont

RENDER, OUT = sys.argv[1], sys.argv[2]
PREVIEW = sys.argv[3] if len(sys.argv) > 3 else None
os.makedirs(OUT, exist_ok=True)

SHEET = (0, 0, 0)  # the graphic's sheet: 90% black over the journal, which is near enough black to judge against
# The pad's place in the 1280x720 design, between the two columns of boxes.
PAD_W = 620.0
PAD_X = 640.0 - PAD_W / 2
PAD_CENTRE_Y = 322.0

BOX_W = 300.0
LEFT_X = 12.0
RIGHT_X = 1280.0 - 12.0 - BOX_W
COL_TOP = 66.0
COL_BOTTOM = 676.0
LINE_H = 17.0
BOX_PAD = 9.0
GAP = 5.0

im = Image.open(os.path.join(RENDER, "pad.png")).convert("RGBA")
src = json.load(open(os.path.join(RENDER, "anchors.json"), encoding="utf-8"))
PAD = src["pad"]
PSPAD = PAD == "playstation"
XBOXPAD = PAD == "xbox"
STEAMPAD = PAD == "steam"
l, t, r, b = im.getchannel("A").point(lambda v: 255 if v > 3 else 0).getbbox()
m = 6
box = (max(0, l - m), max(0, t - m), min(im.width, r + m), min(im.height, b + m))
crop = im.crop(box)
crop.save(os.path.join(OUT, "pad_cropped.png"), optimize=True)
if src["pad"] == "steam":
    PAD_CENTRE_Y = 358.0  # the deepest pad: lower, so the shoulders keep their room and X's leader clears A
scale = PAD_W / crop.width
PAD_H = crop.height * scale
PAD_Y = PAD_CENTRE_Y - PAD_H / 2

anchors = {}
for name, a in src["anchors"].items():
    px, py = a["x"] - box[0], a["y"] - box[1]
    anchors[name] = {"px": round(px, 1), "py": round(py, 1), "dx": round(PAD_X + px * scale, 1), "dy": round(PAD_Y + py * scale, 1), "dr": round(a["rim"] * scale, 1), "ds": round(a["size"] * scale, 1), "occludedBy": a["occludedBy"]}
json.dump({"pad": PAD, "picture": [crop.width, crop.height], "design": [PAD_X, PAD_Y, PAD_W, round(PAD_H, 1)], "anchors": anchors}, open(os.path.join(OUT, "anchors.json"), "w"), indent=1)

ROW_NAMES = {"LT": "kLT", "LB": "kLB", "RT": "kRT", "RB": "kRB", "Create": "kBack", "Options": "kStart", "DUp": "kDUp", "DDown": "kDDown", "DLeft": "kDLeft", "DRight": "kDRight",
             "LStick": "kLStick", "RStick": "kRStick", "Y": "kY", "A": "kA", "X": "kX", "B": "kB"}
with open(os.path.join(OUT, "anchors_table.txt"), "w") as f:
    f.write(f"// {PAD}: the picture {crop.width}x{crop.height}, drawn at {PAD_W:g}x{PAD_H:.1f} at ({PAD_X:g}, {PAD_Y:.1f}) in the 1280x720 design\n")
    for name, row in ROW_NAMES.items():
        f.write(f"case rows::{row}: return {{ {anchors[name]['dx']}, {anchors[name]['dy']}, {anchors[name]['dr']} }};  // x, y, rim\n")


# ---- the SWF: version 8, no script, one bitmap on one shape
class Bits:
    def __init__(self):
        self.b = []

    def u(self, v, n):
        self.b += [(v >> i) & 1 for i in range(n - 1, -1, -1)]

    def s(self, v, n):
        self.u(v & ((1 << n) - 1), n)

    def done(self):
        bits = self.b + [0] * ((8 - len(self.b) % 8) % 8)
        return bytes(int("".join(map(str, bits[i:i + 8])), 2) for i in range(0, len(bits), 8))


def rect(x0, x1, y0, y1):
    n = max(max(v.bit_length() for v in map(abs, (x0, x1, y0, y1))) + 1, 1)
    w = Bits()
    w.u(n, 5)
    for v in (x0, x1, y0, y1):
        w.s(v, n)
    return w.done()


def tag(code, body):
    return struct.pack("<H", (code << 6) | 63) + struct.pack("<I", len(body)) + body


def premultiplied_argb(image):
    raw = image.tobytes()
    out = bytearray(len(raw))
    for i in range(0, len(raw), 4):
        r_, g, b_, a = raw[i:i + 4]
        out[i] = a
        out[i + 1] = (r_ * a + 127) // 255
        out[i + 2] = (g * a + 127) // 255
        out[i + 3] = (b_ * a + 127) // 255
    return bytes(out)


def fixed_matrix(sx, sy):
    w = Bits()
    fx, fy = int(round(sx * 65536)), int(round(sy * 65536))
    n = max(fx.bit_length(), fy.bit_length()) + 1
    w.u(1, 1)
    w.u(n, 5)
    w.s(fx, n)
    w.s(fy, n)
    w.u(0, 1)  # no rotate
    w.u(0, 5)  # translate 0
    return w.done()


def make_swf(image, w_px, h_px):
    tw, th = int(round(w_px * 20)), int(round(h_px * 20))
    bitmap = struct.pack("<HBHH", 1, 5, image.width, image.height) + zlib.compress(premultiplied_argb(image), 9)
    fill = bytes([1, 0x41]) + struct.pack("<H", 1) + fixed_matrix(w_px * 20 / image.width, h_px * 20 / image.height)
    rec = Bits()
    # style change: move to (0,0), fill1 = 1
    n = max(tw.bit_length(), th.bit_length()) + 1
    rec.u(1, 4)  # NumFillBits
    rec.u(0, 4)  # NumLineBits
    rec.u(0, 1)
    rec.u(0, 1)  # no new styles
    rec.u(0, 1)  # no line style
    rec.u(1, 1)  # fill1
    rec.u(0, 1)  # no fill0
    rec.u(1, 1)  # move
    rec.u(1, 5)
    rec.s(0, 1)
    rec.s(0, 1)
    rec.u(1, 1)  # fill style 1

    def line(dx, dy):
        rec.u(1, 1)
        rec.u(1, 1)  # straight
        rec.u(n - 2, 4)
        rec.u(0, 1)  # not general
        if dx:
            rec.u(0, 1)
            rec.s(dx, n)
        else:
            rec.u(1, 1)
            rec.s(dy, n)

    line(tw, 0)
    line(0, th)
    line(-tw, 0)
    line(0, -th)
    rec.u(0, 6)
    shape = struct.pack("<H", 2) + rect(0, tw, 0, th) + fill + b"\x00" + rec.done()
    place = bytes([0x02 | 0x04]) + struct.pack("<HH", 1, 2)
    p = Bits()
    p.u(0, 1)
    p.u(0, 1)
    p.u(0, 5)
    place = bytes([0x02]) + struct.pack("<H", 1) + struct.pack("<H", 2) + p.done()
    body = rect(0, tw, 0, th) + struct.pack("<HH", 24 << 8, 1)
    body += tag(69, struct.pack("<I", 0))  # FileAttributes: AS2, no network flag
    body += tag(9, bytes([0, 0, 0]))
    body += tag(36, bitmap) + tag(32, shape) + tag(26, place) + struct.pack("<H", 1 << 6) + struct.pack("<H", 0)
    compressed = zlib.compress(body, 9)
    return b"CWS" + bytes([8]) + struct.pack("<I", 8 + len(body)) + compressed


with open(os.path.join(OUT, f"pad_{PAD}.swf"), "wb") as f:
    f.write(make_swf(crop, PAD_W, PAD_H))

# ---- the pictures
FONT = "C:/Windows/Fonts/segoeui.ttf"
FONT_B = "C:/Windows/Fonts/segoeuib.ttf"
K = 2.0  # output pixels per design unit; drawn at 2x that and halved


def canvas(k):
    sheet = Image.new("RGBA", (int(1280 * k), int(720 * k)), SHEET + (255,))
    pic = crop.resize((round(PAD_W * k), round(PAD_H * k)), Image.LANCZOS)
    sheet.alpha_composite(pic, (round(PAD_X * k), round(PAD_Y * k)))
    return sheet


def leader_end(start, anchor):
    """Where the leader stops: at the control's rim on the side it comes from."""
    sx, sy = start
    stub = 16 if anchor["dx"] > sx else -16
    vx, vy = sx + stub - anchor["dx"], sy - anchor["dy"]
    vlen = (vx * vx + vy * vy) ** 0.5
    return anchor["dx"] + vx / vlen * anchor["dr"], anchor["dy"] + vy / vlen * anchor["dr"]


def leader(d, k, start, anchor, colour=(214, 216, 222, 255), width=1.5):
    """A short level stub out of the box, then straight to the control's rim, ending in a ringed dot: the button's symbol stays clear."""
    sx, sy = start
    stub = 16 if anchor["dx"] > sx else -16
    tx, ty = leader_end(start, anchor)
    d.line([(sx * k, sy * k), ((sx + stub) * k, sy * k), (tx * k, ty * k)], fill=colour, width=max(1, round(width * k)), joint="curve")
    rr = 3.4 * k
    d.ellipse((tx * k - rr, ty * k - rr, tx * k + rr, ty * k + rr), fill=(10, 10, 12, 255), outline=(245, 245, 248, 255), width=max(1, round(1.3 * k)))


def box_at(d, k, x, y, w, h):
    d.rectangle((x * k, y * k, (x + w) * k, (y + h) * k), fill=(10, 10, 12, 255), outline=(235, 235, 238, 255), width=max(1, round(1.0 * k)))


def intersects(a, b, c, e):
    def ccw(p, q, s):
        return (s[1] - p[1]) * (q[0] - p[0]) - (q[1] - p[1]) * (s[0] - p[0])

    return ccw(a, c, e) * ccw(b, c, e) < 0 and ccw(a, b, c) * ccw(a, b, e) < 0


# Gameplay as it would read on the owner's list once [Functions] rows are in it (hotkey-table.md, read 2026-11-04, the rows UVC has),
# with the mod's name in brackets left off the row's name.
GAMEPLAY = {
    "LT": ["Modifier", "Sense Sweep"],
    "LB": ["Left Attack/Block", "L2 + Shout"],
    "Create": ["Tween Menu"],
    "DUp": ["Toggle POV", "Call Horse"],
    "DDown": ["Favorites", "Wheel Menu"],
    "DLeft": ["Hotkey 1", "Stance", "Trail Key"],
    "DRight": ["Hotkey 2"],
    "LStick": ["Move", "L3: Sneak"],
    "RT": ["Power Attack"],
    "RB": ["Right Attack/Block"],
    "Options": ["Pause"],
    "Y": ["Jump"],
    "B": ["Sprint", "Dodge"],
    "A": ["Activate"],
    "X": ["Ready Weapon"],
    "RStick": ["Look", "R3: Target Lock"],
}
if PSPAD:
    GAMEPLAY["Touchpad"] = ["Touchpad", "Not bound by the game"]
if STEAMPAD:
    GAMEPLAY["LeftPad"] = ["Left trackpad", "Up: K", "Down: SCREENSHOT", "Left: F3", "Right: F2"]
    GAMEPLAY["RightPad"] = ["Right trackpad", "Up: G", "Down: BACKSLASH", "Left: H", "Right: PAGE DOWN"]
if XBOXPAD:
    for key, lines in GAMEPLAY.items():
        GAMEPLAY[key] = [ln.replace("L2 +", "LT +").replace("L3:", "LS:").replace("R3:", "RS:") for ln in lines]
LEFT = ["LT", "LB", "Create", "DUp", "DLeft", "DRight", "DDown", "LStick"]
RIGHT = ["RT", "RB", "Options", "Y", "X", "B", "A", "RStick"]


def box_heights(names):
    return [len(GAMEPLAY[n]) * LINE_H + 2 * BOX_PAD for n in names]


def starts(names):
    """The column's boxes stacked from the top with their gaps opened up evenly: each box as tall as its lines."""
    hs = box_heights(names)
    free = (COL_BOTTOM - COL_TOP) - sum(hs)
    gap = min(16.0, max(GAP, free / (len(names) - 1)))
    total = sum(hs) + gap * (len(names) - 1)
    y = COL_TOP + ((COL_BOTTOM - COL_TOP) - total) / 2
    out = []
    for h in hs:
        out.append((y, h))
        y += h + gap
    return out


def segments(names, x):
    sign = 1 if x < 640 else -1
    out = {}
    for n, (y, h) in zip(names, starts(names)):
        start = (x, y + h / 2)
        out[n] = ((x + sign * 16, y + h / 2), leader_end(start, anchors[n]))
    return out


def crossings(names, x):
    seg = list(segments(names, x).values())
    return sum(intersects(*a, *c) for a, c in itertools.combinations(seg, 2))


def point_to_segment(p, a, e):
    dx, dy = e[0] - a[0], e[1] - a[1]
    t = max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / (dx * dx + dy * dy)))
    return ((p[0] - a[0] - t * dx) ** 2 + (p[1] - a[1] - t * dy) ** 2) ** 0.5


def passes_over(names, x):
    """Which leaders run across another control on their way, by the sizes from the 3D scene."""
    hits = []
    for n, (a, e) in segments(names, x).items():
        for other, o in anchors.items():
            if other != n and o["ds"] > 0 and point_to_segment((o["dx"], o["dy"]), a, e) < o["ds"] * 0.92:
                hits.append((n, other))
    return hits


def best_order(fixed_head, movable, x):
    best, best_score = None, 1e9
    for perm in itertools.permutations(movable):
        names = fixed_head + list(perm)
        c = crossings(names, x)
        over = len(passes_over(names, x))
        # no crossings first, then off the other controls, then shortest leaders
        length = sum(abs(y + h / 2 - anchors[n]["dy"]) for n, (y, h) in zip(names, starts(names)))
        score = c * 100000 + over * 3000 + length
        if score < best_score:
            best, best_score = names, score
    return best, crossings(best, x)


if PSPAD:
    # settled with the owner's first review of this picture: the order is kept, not searched
    LEFT = ["LT", "LB", "Touchpad", "Create", "DUp", "DLeft", "DDown", "DRight", "LStick"]
    RIGHT = ["RT", "RB", "Options", "Y", "B", "X", "A", "RStick"]
    cl, cr = crossings(LEFT, LEFT_X + BOX_W), crossings(RIGHT, RIGHT_X)
else:
    LEFT, cl = best_order(LEFT[:2], LEFT[2:], LEFT_X + BOX_W)
    RIGHT, cr = best_order(RIGHT[:2], RIGHT[2:], RIGHT_X)
    if STEAMPAD:
        LEFT = ["LT", "LB", "DUp", "DLeft", "DDown", "DRight", "Create", "LStick"]
        print("steam without trackpad boxes: crossings", crossings(LEFT, LEFT_X + BOX_W), crossings(["RT", "RB", "Y", "B", "X", "A", "Options", "RStick"], RIGHT_X))
        # by eye: the search's best sends Menu's leader across B; this one runs it clear below the face buttons
        RIGHT = ["RT", "RB", "Y", "B", "X", "A", "Options", "RStick"]
        LEFT, RIGHT = LEFT + ["LeftPad"], RIGHT + ["RightPad"]
        cl, cr = crossings(LEFT, LEFT_X + BOX_W), crossings(RIGHT, RIGHT_X)
with open(os.path.join(OUT, "anchors_table.txt"), "a") as f:
    for name, a in anchors.items():
        if name not in ROW_NAMES:
            f.write(f"// also: {name} {a['dx']}, {a['dy']}\n")
    f.write("// box order, top to bottom: left " + ", ".join(ROW_NAMES[n] for n in LEFT if n in ROW_NAMES) + "\n")
    f.write("//                          right " + ", ".join(ROW_NAMES[n] for n in RIGHT if n in ROW_NAMES) + "\n")
_saved = json.load(open(os.path.join(OUT, "anchors.json")))
_saved["left"] = [ROW_NAMES.get(n, "part:" + n) for n in LEFT]
_saved["right"] = [ROW_NAMES.get(n, "part:" + n) for n in RIGHT]
_saved["rows"] = ROW_NAMES
json.dump(_saved, open(os.path.join(OUT, "anchors.json"), "w"), indent=1)
print("left", LEFT, "crossings", cl, "over", passes_over(LEFT, LEFT_X + BOX_W))
print("right", RIGHT, "crossings", cr, "over", passes_over(RIGHT, RIGHT_X))
print("rows on the mock", sum(len(v) for v in GAMEPLAY.values()), "tallest", max(len(v) for v in GAMEPLAY.values()))
print("picture", crop.size, "design", (PAD_X, round(PAD_Y, 1), PAD_W, round(PAD_H, 1)), "scale", round(1 / scale, 2))
print("swf bytes", os.path.getsize(os.path.join(OUT, f"pad_{PAD}.swf")))

if PSPAD:
    LABEL = {"LT": "L2", "LB": "L1", "RT": "R2", "RB": "R1", "Create": "Cr", "Options": "Op", "DUp": "Up", "DDown": "Dn", "DLeft": "Lt", "DRight": "Rt", "LStick": "L3", "RStick": "R3",
             "Y": "\u25b3", "B": "\u25cb", "A": "\u2715", "X": "\u25a1", "Touchpad": "TP"}
elif STEAMPAD:
    LABEL = {"LT": "L2", "LB": "L1", "RT": "R2", "RB": "R1", "Create": "Vw", "Options": "Mn", "DUp": "Up", "DDown": "Dn", "DLeft": "Lt", "DRight": "Rt", "LStick": "L3", "RStick": "R3",
             "Y": "Y", "B": "B", "A": "A", "X": "X", "LeftPad": "TP", "RightPad": "TP"}
else:
    LABEL = {"LT": "LT", "LB": "LB", "RT": "RT", "RB": "RB", "Create": "Vw", "Options": "Mn", "DUp": "Up", "DDown": "Dn", "DLeft": "Lt", "DRight": "Rt", "LStick": "LS", "RStick": "RS",
             "Y": "Y", "B": "B", "A": "A", "X": "X"}
BUMP_L, BUMP_R, CLOSE = ("L1", "R1", "\u25cb  Close") if PSPAD else (("L1", "R1", "B  Close") if STEAMPAD else ("LB", "RB", "B  Close"))


def draw_mock():
    k = K * 2
    sheet = canvas(k)
    d = ImageDraw.Draw(sheet)
    body = ImageFont.truetype(FONT, int(14 * k))
    title = ImageFont.truetype(FONT, int(26 * k))
    tiny = ImageFont.truetype(FONT, int(12 * k))
    head = ImageFont.truetype("C:/Windows/Fonts/seguisym.ttf", int(15 * k))
    d.text((640 * k, 32 * k), "Gameplay", font=title, fill=(255, 255, 255, 255), anchor="mm")
    d.text((640 * k, 56 * k), "1 / 12", font=tiny, fill=(170, 172, 178, 255), anchor="mm")
    for pill, x in ((BUMP_L, 400), (BUMP_R, 880)):
        d.rounded_rectangle(((x - 22) * k, 20 * k, (x + 22) * k, 44 * k), radius=4 * k, outline=(235, 235, 238, 255), width=int(k))
        d.text((x * k, 32 * k), pill, font=tiny, fill=(255, 255, 255, 255), anchor="mm")
    for names, x, right in ((LEFT, LEFT_X, False), (RIGHT, RIGHT_X, True)):
        for n, (y, h) in zip(names, starts(names)):
            leader(d, k, (x if right else x + BOX_W, y + h / 2), anchors[n])
    for names, x in ((LEFT, LEFT_X), (RIGHT, RIGHT_X)):
        for n, (y, h) in zip(names, starts(names)):
            box_at(d, k, x, y, BOX_W, h)
            d.text(((x + 22) * k, (y + h / 2) * k), LABEL[n], font=head, fill=(255, 255, 255, 255), anchor="mm")
            ly = y + BOX_PAD
            for line in GAMEPLAY[n]:
                d.text(((x + 44) * k, (ly + LINE_H / 2) * k), line, font=body, fill=(255, 255, 255, 255), anchor="lm")
                ly += LINE_H
    d.text((640 * k, 660 * k), CLOSE, font=head, fill=(255, 255, 255, 255), anchor="mm")
    sheet.resize((int(1280 * K), int(720 * K)), Image.LANCZOS).convert("RGB").save(os.path.join(OUT, "layout_mock.png"), optimize=True)


draw_mock()

if PREVIEW:
    k = 3.0
    sheet = canvas(k)
    d = ImageDraw.Draw(sheet)
    font = ImageFont.truetype(FONT, int(15 * k))
    for name, x, y, text in (("LT", LEFT_X, 96, f"{LABEL['LT']}   Modifier"), ("RB", RIGHT_X, 140, f"{LABEL['RB']}   Right Attack/Block"), ("Y", RIGHT_X, 300, ("Triangle" if PSPAD else "Y") + "   Jump")):
        h = 36
        right = x > 640
        leader(d, k, (x if right else x + BOX_W, y + h / 2), anchors[name])
        box_at(d, k, x, y, BOX_W, h)
        d.text(((x + 14) * k, (y + h / 2) * k), text, font=font, fill=(255, 255, 255, 255), anchor="lm")
    sheet.resize((1920, 1080), Image.LANCZOS).convert("RGB").save(PREVIEW, optimize=True)
    print("preview", PREVIEW)
