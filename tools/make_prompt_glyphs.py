#!/usr/bin/env python3
"""Generate per-device button-prompt replacements for Metroid Prime 2: Echoes.

Ported from the Prime 1 port. The port binds most prompt textures by asset id
(kAssets in platform/port_prompts.cpp) to the bindings/ icons; the per-device
sets below only cover the few left in kKeys, matched by texel hash.

The game's prompts are individual textures: 32x32 buttons (A, B, X, Y, Start,
L, R) and 64x32 stick, C-stick, D-pad and Z art, each listed with its hash in
kKeys in platform/port_prompts.cpp (found in Prime 1 with that port's
tools/extract_textures.py, whose index.tsv carries the developers' names such
as LStickN or DPadR). Each device
gets its own icon for every action, so the in-game prompt matches the pad (or
keyboard) in use. The keyboard icons label the keys the port binds by default
(A action is X, B action is Z, the sticks WASD and IJKL, the D-pad the arrows).
The bindings/ set holds one icon per key and pad input, which the port swaps
in when an action is rebound.

Icons come from Kenney's "Input Prompts" pack, which is CC0, and the Steam Deck
set from Xelu's "Free Controllers & Keyboard Prompts", also CC0; the ones used are
vendored in tools/prompt_icons/ (see the README there). Run with an output
directory, usually the `textures` folder beside the executable:

    tools/make_prompt_glyphs.py textures

Output is an uncompressed 32-bit RGBA DDS. DDS rather than PNG because the
game's texture is RGB5A3 and Aurora's DDS path keeps the alpha where the PNG
path does not; the vertical flip matches how Aurora reads and writes DDS (the
dumps come out flipped). The icon is inset to match the game's own art: the
32x32 buttons are opaque only from about (6,5) to (27,26), so the glyph is
22px (a full-bleed icon reads as a solid square), and the 64x32 art is a
centred 28px square.

Each bindings icon is written at SCALE times the size of the texture it
replaces (the names keep the game slot's size); the port takes any whole
multiple of it, the same on both axes. At the game's own size the glyph is 22px
of the 64px source art, which reads blurry once the HUD is scaled up (#229,
from Prime 1). The per-device sets are replacements for the game's own textures
by name, which the game draws at their declared size, so they stay at 1x.
"""
import os
import re
import struct
import sys
from PIL import Image

SIZE = 32
ICON_SIZE = 22
INSET = (SIZE - ICON_SIZE) // 2
# Bindings icons are written at this multiple of the game's size (#229); the
# port accepts any whole multiple.
SCALE = 2
ICONS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "prompt_icons")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT_PROMPTS = os.path.join(ROOT, "platform", "port_prompts.cpp")
OUT = sys.argv[1] if len(sys.argv) > 1 else "textures"

# Icon per device and action. The keyboard set is the fallback used when a
# binding has no icon of its own, so it labels the port's default keys.
#
# Z is the right shoulder in every default pad mapping Aurora ships, so the pad
# sets give it the bumper. There is no "gamecube" set on purpose: the game's own
# art already is the GameCube set.
#
# Y is the pause screen's Zoom. The Switch labels are positional, as in
# PAD_ICONS: SDL3 names buttons by position, so A is the bottom face button,
# which a Switch pad labels B.
DIRECTIONS = ("up", "down", "left", "right")


def pad_sticks_and_dpad(prefix):
    """The control stick (left), the C-stick (right) and the D-pad, whole and
    per direction, for a pad whose Kenney icons start with `prefix`."""
    icons = {"stick": f"{prefix}_stick_l.png", "cstick": f"{prefix}_stick_r.png",
             "dpad": f"{prefix}_dpad.png"}
    for d in DIRECTIONS:
        icons[f"stick_{d}"] = f"{prefix}_stick_l_{d}.png"
        icons[f"cstick_{d}"] = f"{prefix}_stick_r_{d}.png"
        icons[f"dpad_{d}"] = f"{prefix}_dpad_{d}.png"
    return icons


DEVICE_ICONS = {
    "xbox": {"a": "xbox_button_color_a.png", "b": "xbox_button_color_b.png",
             "x": "xbox_button_color_x.png", "y": "xbox_button_color_y.png",
             "start": "xbox_button_start.png",
             "l": "xbox_lt.png", "r": "xbox_rt.png", "z": "xbox_rb.png",
             **pad_sticks_and_dpad("xbox")},
    "playstation": {"a": "playstation_button_color_cross.png",
                    "b": "playstation_button_color_circle.png",
                    "x": "playstation_button_color_square.png",
                    "y": "playstation_button_color_triangle.png",
                    "start": "playstation3_button_start.png",
                    "l": "playstation_trigger_l2.png", "r": "playstation_trigger_r2.png",
                    "z": "playstation_trigger_r1.png",
                    **pad_sticks_and_dpad("playstation")},
    # Nintendo pads map by label (pad.cpp's Pro Controller default), so A is A.
    "switch": {"a": "switch_button_a.png", "b": "switch_button_b.png",
               "x": "switch_button_x.png", "y": "switch_button_y.png",
               "start": "switch_button_plus.png",
               "l": "switch_button_zl.png", "r": "switch_button_zr.png",
               "z": "switch_button_r.png",
               **pad_sticks_and_dpad("switch")},
    # Valve's controllers (the Steam Deck, the Steam Controller): Xbox-positioned face buttons, so
    # A is A. The pack has no per-direction stick art; those are built (deck_stick_direction).
    "steamdeck": {"a": "steamdeck_a.png", "b": "steamdeck_b.png",
                  "x": "steamdeck_x.png", "y": "steamdeck_y.png",
                  "start": "steamdeck_menu.png",
                  "l": "steamdeck_l2.png", "r": "steamdeck_r2.png", "z": "steamdeck_r1.png",
                  "stick": "steamdeck_left_stick.png", "cstick": "steamdeck_right_stick.png",
                  "dpad": "steamdeck_dpad.png",
                  **{f"stick_{d}": f"steamdeck_left_stick_{d}.png" for d in DIRECTIONS},
                  **{f"cstick_{d}": f"steamdeck_right_stick_{d}.png" for d in DIRECTIONS},
                  **{f"dpad_{d}": f"steamdeck_dpad_{d}.png" for d in DIRECTIONS}},
    # The sticks and the D-pad on their default keys: WASD, IJKL and the arrows.
    "keyboard": {"a": "keyboard_x.png", "b": "keyboard_z.png", "x": "keyboard_c.png",
                 "y": "keyboard_v.png", "start": "keyboard_enter.png",
                 "l": "keyboard_q.png", "r": "keyboard_e.png", "z": "keyboard_f.png",
                 "stick": "keyboard_wasd", "cstick": "keyboard_ijkl", "dpad": "keyboard_arrows",
                 **{f"stick_{d}": f"keyboard_{k}.png" for d, k in zip(DIRECTIONS, "wsad")},
                 **{f"cstick_{d}": f"keyboard_{k}.png" for d, k in zip(DIRECTIONS, "ikjl")},
                 **{f"dpad_{d}": f"keyboard_arrow_{d}.png" for d in DIRECTIONS}},
}

# Key clusters drawn as an inverted T of the keys' own icons (up on top; left,
# down, right below). The pack's keyboard_arrows is a small unlabelled glyph
# that reads much weaker than the letter clusters, so the arrows are built too.
COMPOSITES = {
    "keyboard_arrows": ("keyboard_arrow_up", "keyboard_arrow_left",
                        "keyboard_arrow_down", "keyboard_arrow_right"),
    "keyboard_wasd": ("keyboard_w", "keyboard_a", "keyboard_s", "keyboard_d"),
    "keyboard_ijkl": ("keyboard_i", "keyboard_j", "keyboard_k", "keyboard_l"),
}

def pad_icons_for_axes(prefix):
    """The stems the port builds for sticks and the D-pad: <device>_stick_l
    or _stick_r, optionally with the way it is pushed, and <device>_dpad; the
    D-pad's directions are buttons and named like them (dpad_up...)."""
    icons = {"stick_l": f"{prefix}_stick_l.png", "stick_r": f"{prefix}_stick_r.png",
             "dpad": f"{prefix}_dpad.png"}
    for d in DIRECTIONS:
        icons[f"stick_l_{d}"] = f"{prefix}_stick_l_{d}.png"
        icons[f"stick_r_{d}"] = f"{prefix}_stick_r_{d}.png"
        icons[f"dpad_{d}"] = f"{prefix}_dpad_{d}.png"
    return icons


# Per-device icons for the pad's own buttons, keyed by the SDL gamepad button
# the port reports in the controller's mapping. The port builds the icon name as
# "<device>_<key>", so a remapped pad button follows the new button.
PAD_ICONS = {
    "xbox": {
        "south": "xbox_button_color_a.png", "east": "xbox_button_color_b.png",
        "west": "xbox_button_color_x.png", "north": "xbox_button_color_y.png",
        "start": "xbox_button_start.png", "back": "xbox_button_view.png",
        "leftshoulder": "xbox_lb.png", "rightshoulder": "xbox_rb.png",
        "leftstick": "xbox_stick_l_press.png", "rightstick": "xbox_stick_r_press.png",
        "lt": "xbox_lt.png", "rt": "xbox_rt.png",
        **pad_icons_for_axes("xbox"),
    },
    "playstation": {
        "south": "playstation_button_color_cross.png",
        "east": "playstation_button_color_circle.png",
        "west": "playstation_button_color_square.png",
        "north": "playstation_button_color_triangle.png",
        "start": "playstation3_button_start.png", "back": "playstation3_button_select.png",
        "leftshoulder": "playstation_trigger_l1.png", "rightshoulder": "playstation_trigger_r1.png",
        "leftstick": "playstation_stick_l_press.png", "rightstick": "playstation_stick_r_press.png",
        "lt": "playstation_trigger_l2.png", "rt": "playstation_trigger_r2.png",
        **pad_icons_for_axes("playstation"),
    },
    "switch": {
        "south": "switch_button_b.png", "east": "switch_button_a.png",
        "west": "switch_button_y.png", "north": "switch_button_x.png",
        "start": "switch_button_plus.png", "back": "switch_button_minus.png",
        "leftshoulder": "switch_button_l.png", "rightshoulder": "switch_button_r.png",
        "leftstick": "switch_stick_l_press.png", "rightstick": "switch_stick_r_press.png",
        "lt": "switch_button_zl.png", "rt": "switch_button_zr.png",
        **pad_icons_for_axes("switch"),
    },
    # The Steam Deck / Steam Controller set (tools/prompt_icons/steamdeck_*, from Xelu's pack). Menu
    # and View are Start and Back; the back grips are SDL's paddles (L4/R4 the first pair).
    "steamdeck": {
        "south": "steamdeck_a.png", "east": "steamdeck_b.png",
        "west": "steamdeck_x.png", "north": "steamdeck_y.png",
        "start": "steamdeck_menu.png", "back": "steamdeck_inventory.png",
        "leftshoulder": "steamdeck_l1.png", "rightshoulder": "steamdeck_r1.png",
        "leftstick": "steamdeck_left_stick_click.png",
        "rightstick": "steamdeck_right_stick_click.png",
        "lt": "steamdeck_l2.png", "rt": "steamdeck_r2.png",
        "leftpaddle1": "steamdeck_l4.png", "rightpaddle1": "steamdeck_r4.png",
        "leftpaddle2": "steamdeck_l5.png", "rightpaddle2": "steamdeck_r5.png",
        "stick_l": "steamdeck_left_stick.png", "stick_r": "steamdeck_right_stick.png",
        "dpad": "steamdeck_dpad.png",
        **{f"stick_l_{d}": f"steamdeck_left_stick_{d}.png" for d in DIRECTIONS},
        **{f"stick_r_{d}": f"steamdeck_right_stick_{d}.png" for d in DIRECTIONS},
        **{f"dpad_{d}": f"steamdeck_dpad_{d}.png" for d in DIRECTIONS},
    },
    # Keyed by the GameCube button rather than the SDL one: the port names a GC
    # pad's button after the action its default mapping gives it (see
    # GameCubeStemForButton in platform/port_prompts.cpp), and only when remapped.
    "gamecube": {
        "a": "gamecube_button_color_a.png", "b": "gamecube_button_color_b.png",
        "x": "gamecube_button_x.png", "y": "gamecube_button_y.png",
        "z": "gamecube_button_z.png", "start": "gamecube_button_start.png",
        "l": "gamecube_trigger_l.png", "r": "gamecube_trigger_r.png",
    },
}
PAD_ICONS["standard"] = PAD_ICONS["xbox"]

# PAD_BUTTON_* / PAD_TRIGGER_* / PROMPT_* to the action name used above.
ACTION_FOR_BUTTON = {
    "PAD_BUTTON_A": "a",
    "PAD_BUTTON_B": "b",
    "PAD_BUTTON_X": "x",
    "PAD_BUTTON_Y": "y",
    "PAD_BUTTON_START": "start",
    "PAD_TRIGGER_L": "l",
    "PAD_TRIGGER_R": "r",
    "PAD_TRIGGER_Z": "z",
    "PROMPT_STICK": "stick",
    "PROMPT_CSTICK": "cstick",
    "PROMPT_DPAD": "dpad",
    **{f"PROMPT_STICK_{d.upper()}": f"stick_{d}" for d in DIRECTIONS},
    **{f"PROMPT_CSTICK_{d.upper()}": f"cstick_{d}" for d in DIRECTIONS},
    **{f"PAD_BUTTON_{d.upper()}": f"dpad_{d}" for d in DIRECTIONS},
}


def read_prompt_keys():
    """Parse the prompt texture table out of platform/port_prompts.cpp.

    Keeping the table in one place means a texture only has to be identified
    once, in the C++ where it is used.
    """
    pattern = re.compile(
        r"\{\s*((?:PAD|PROMPT)_\w+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*0x([0-9a-fA-F]+)ull\s*,\s*\"(\w+)\"\s*\}")
    with open(PORT_PROMPTS) as f:
        keys = pattern.findall(f.read())
    if not keys:
        raise SystemExit(f"no prompt keys found in {PORT_PROMPTS}")
    missing = sorted({k[0] for k in keys} - set(ACTION_FOR_BUTTON))
    if missing:
        raise SystemExit(f"no icon mapping for: {', '.join(missing)}")
    return [(ACTION_FOR_BUTTON[button], int(w), int(h), hsh, fmt) for button, w, h, hsh, fmt in keys]


def texture_name(w, h, hsh, fmt):
    return f"tex1_{w}x{h}_{hsh}_{fmt}.dds"

# One icon per input that can be bound, written to <out>/bindings/<stem>.dds.
# The port registers the icon for whichever input is bound to a prompt's
# action, so a rebound key or mouse button is reflected in game. Stems match
# the tables in platform/port_prompts.cpp.
BINDING_ICONS = [
    *[f"keyboard_{c}" for c in "abcdefghijklmnopqrstuvwxyz"],
    *[f"keyboard_{d}" for d in "0123456789"],
    "keyboard_arrow_up", "keyboard_arrow_down", "keyboard_arrow_left", "keyboard_arrow_right",
    *[f"keyboard_f{i}" for i in range(1, 13)],
    "keyboard_space", "keyboard_enter", "keyboard_escape", "keyboard_tab",
    "keyboard_backspace", "keyboard_delete", "keyboard_insert", "keyboard_home",
    "keyboard_end", "keyboard_page_up", "keyboard_page_down",
    "keyboard_shift", "keyboard_ctrl", "keyboard_alt",
    "keyboard_minus", "keyboard_equals", "keyboard_bracket_open", "keyboard_bracket_close",
    "keyboard_semicolon", "keyboard_apostrophe", "keyboard_comma", "keyboard_period",
    "keyboard_slash_forward", "keyboard_slash_back", "keyboard_tilde", "keyboard_capslock",
    "keyboard_win", "keyboard_printscreen", "keyboard_scroll_lock", "keyboard_pause",
    # Keypad keys without art of their own share the main keys' (digits, minus,
    # period, slash); these are the ones that have it.
    "keyboard_numpad_enter", "keyboard_numpad_plus", "keyboard_asterisk", "keyboard_numlock",
    "mouse_left", "mouse_right", "mouse_scroll", "mouse_side_back", "mouse_side_forward",
    "keyboard_arrows", "keyboard_wasd", "keyboard_ijkl",
]


def write_dds(img, path):
    img = img.convert("RGBA").transpose(Image.FLIP_TOP_BOTTOM)
    w, h = img.size
    header = b"DDS " + struct.pack(
        "<7I44x",
        124,                        # dwSize
        0x1 | 0x2 | 0x4 | 0x1000,   # CAPS|HEIGHT|WIDTH|PIXELFORMAT
        h, w,
        0,                          # pitch (unused)
        0,                          # depth
        1,                          # mip count
    )
    pf = struct.pack("<2I4s5I", 32, 0x41, b"\0\0\0\0", 32,
                     0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000)
    caps = struct.pack("<5I", 0x1000, 0, 0, 0, 0)  # CAPS, CAPS2-4, reserved2
    with open(path, "wb") as f:
        f.write(header + pf + caps + img.tobytes())


def load_icon(name):
    stem = name[:-4] if name.endswith(".png") else name
    m = re.fullmatch(r"(steamdeck_(?:left|right)_stick)_(up|down|left|right)", stem)
    if m:
        return deck_stick_direction(m.group(1), m.group(2))
    return Image.open(os.path.join(ICONS_DIR, name)).convert("RGBA")


def kenney_arrow():
    """Kenney's direction arrow, lifted out of xbox_stick_l_left (the pack draws it into each
    stick-direction icon rather than on its own): the connected shape holding the icon's leftmost
    texel, which is the arrow, standing clear of the stick. It points left, cropped. The left one
    is used because up and down touch the stick's outline in the Xbox art."""
    pushed = load_icon("xbox_stick_l_left.png")
    pp = pushed.load()
    w, h = pushed.size
    start = min(((x, y) for y in range(h) for x in range(w) if pp[x, y][3] > 0))
    shape, stack = {start}, [start]
    while stack:
        x, y = stack.pop()
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                q = (x + dx, y + dy)
                if 0 <= q[0] < w and 0 <= q[1] < h and q not in shape and pp[q][3] > 0:
                    shape.add(q)
                    stack.append(q)
    arrow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ap = arrow.load()
    for q in shape:
        ap[q] = pp[q]
    return arrow.crop(arrow.getbbox()), w


# The Steam Deck art's colours: the grey of a stick or button face and its dark rim.
DECK_FACE = (79, 79, 79)
DECK_RIM = (21, 21, 21)


def deck_arrow(arrow):
    """Kenney's white arrow in the Deck art's colours: the face grey, with a dark rim around it
    as the Deck icons have."""
    from PIL import ImageFilter
    rim = max(1, round(min(arrow.size) / 6))
    pad = rim + 1
    canvas = Image.new("RGBA", (arrow.width + 2 * pad, arrow.height + 2 * pad), (0, 0, 0, 0))
    canvas.alpha_composite(arrow, (pad, pad))
    alpha = canvas.getchannel("A")
    outline = alpha.filter(ImageFilter.MaxFilter(2 * rim + 1))
    out = Image.new("RGBA", canvas.size, DECK_RIM + (0,))
    out.putalpha(outline)
    face = Image.new("RGBA", canvas.size, DECK_FACE + (0,))
    face.putalpha(alpha)
    out.alpha_composite(face)
    return out


def deck_stick_direction(stick, direction):
    """A Steam Deck stick pushed one way: the stick at the size of its plain icon, as the other
    sets keep theirs, with Kenney's arrow (kenney_arrow, in the Deck's colours) at the canvas
    edge on that side. The Deck art fills its canvas, so the arrow sits over the stick's rim
    rather than in a margin; its own dark rim keeps it apart from the stick."""
    ARROW_SCALE = 1.1
    out = load_icon(stick + ".png")
    size = out.width
    arrow, kenney_size = kenney_arrow()
    scale = size / kenney_size * ARROW_SCALE
    arrow = arrow.resize((max(1, round(arrow.width * scale)), max(1, round(arrow.height * scale))),
                         Image.LANCZOS)
    arrow = deck_arrow(arrow)
    arrow = arrow.rotate({"left": 0, "down": 90, "right": 180, "up": 270}[direction], expand=True)
    pos = {"left": (0, (size - arrow.height) // 2), "right": (size - arrow.width, (size - arrow.height) // 2),
           "up": ((size - arrow.width) // 2, 0), "down": ((size - arrow.width) // 2, size - arrow.height)}
    out.alpha_composite(arrow, pos[direction])
    return out


def make_cluster(stems):
    """An inverted T of key icons (up, left, down, right), cropped to the keys."""
    keys = [load_icon(s + ".png") for s in stems]
    keys = [k.crop(k.getbbox()) for k in keys]
    kw, kh = keys[0].size
    gap = max(1, kw // 16)
    img = Image.new("RGBA", (3 * kw + 2 * gap, 2 * kh + gap), (0, 0, 0, 0))
    up, left, down, right = keys
    img.alpha_composite(up, (kw + gap, 0))
    for i, key in enumerate((left, down, right)):
        img.alpha_composite(key, (i * (kw + gap), kh + gap))
    return img


def make_icon(name, width=SIZE, height=SIZE, scale=SCALE):
    """Scale the icon to the game's inset for a texture of this size: 22px in
    the 32x32 buttons, 28px in the 64x32 stick and D-pad art (which the game
    draws as a centred 28px square). The image is `scale` times that size; the
    name keeps the game's."""
    if width == height:
        size = max(1, int(round(ICON_SIZE * width / SIZE)))
    else:
        size = max(1, min(width, height) - 4)
    width, height, size = width * scale, height * scale, size * scale
    tile = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    stem = name[:-4] if name.endswith(".png") else name
    if stem in COMPOSITES:
        # Four keys at the height of one: as tall as the inset, and wider.
        icon = make_cluster(COMPOSITES[stem])
        fit = min(width / icon.width, size / icon.height)
        icon = icon.resize((max(1, round(icon.width * fit)), max(1, round(icon.height * fit))),
                           Image.LANCZOS)
        tile.alpha_composite(icon, ((width - icon.width) // 2, (height - icon.height) // 2))
        return tile
    icon = load_icon(name).resize((size, size), Image.LANCZOS)
    tile.alpha_composite(icon, ((width - size) // 2, (height - size) // 2))
    return tile


def main():
    keys = read_prompt_keys()
    for device, icons in DEVICE_ICONS.items():
        d = os.path.join(OUT, device)
        os.makedirs(d, exist_ok=True)
        for action, w, h, hsh, fmt in keys:
            if action not in icons:
                continue  # this device has no icon for that action
            # 1x: these replace the game's own texture by name, drawn at its declared size.
            write_dds(make_icon(icons[action], w, h, scale=1),
                      os.path.join(d, texture_name(w, h, hsh, fmt)))
        print(f"{device}: {len(keys)} textures -> {d}")

    bindings = os.path.join(OUT, "bindings")
    os.makedirs(bindings, exist_ok=True)
    padCount = 0
    # One binding stem can serve textures of more than one size - the C-stick
    # prompts here are one 32x32 and one 64x32 - so each binding is also written
    # with its size in the name. platform/port_prompts.cpp looks for the exact
    # size first and falls back to the unsuffixed 32x32 file. Without this a
    # 64x32 slot gets 32x32 bytes: the game does not scale the image, it reads
    # 64x32 of pixels out of half the data and draws the rest as noise.
    # The prompt textures bound by asset id (kAssets) come in these sizes; kKeys
    # now holds only the few matched by hash, so it no longer lists them all.
    sizes = sorted({(SIZE, SIZE), (64, 32), (64, 64)} | {(w, h) for _, w, h, _, _ in keys})
    for device, icons in PAD_ICONS.items():
        for key, icon in icons.items():
            for w, h in sizes:
                suffix = "" if (w, h) == (SIZE, SIZE) else f"_{w}x{h}"
                write_dds(make_icon(icon, w, h),
                          os.path.join(bindings, f"{device}_{key}{suffix}.dds"))
                padCount += 1
    missing = []
    for stem in BINDING_ICONS:
        src = os.path.join(ICONS_DIR, stem + ".png")
        if stem not in COMPOSITES and not os.path.exists(src):
            missing.append(stem)
            continue
        # At every size in the table, not just 32x32. The key stems serve the
        # same actions as the pad stems, so the two 64x32 rows need 64x32 key
        # art; writing only 32x32 made Apply fall back to it, refuse it on size,
        # and leave the key showing the static set for the default bindings -
        # so a rebound Z still read "F" on the map.
        for w, h in sizes:
            suffix = "" if (w, h) == (SIZE, SIZE) else f"_{w}x{h}"
            write_dds(make_icon(stem + ".png", w, h),
                      os.path.join(bindings, f"{stem}{suffix}.dds"))
    print(f"bindings: {len(BINDING_ICONS) - len(missing)} key icons, {padCount} pad icons -> {bindings}")
    if missing:
        print("  missing from tools/prompt_icons: " + ", ".join(missing))


if __name__ == "__main__":
    main()
