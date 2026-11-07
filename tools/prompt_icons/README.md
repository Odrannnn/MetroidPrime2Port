# Prompt icons

Vendored icons used by `tools/make_prompt_glyphs.py` to build the per-device
button-prompt replacements in `textures/`.

They come from Kenney's **Input Prompts** pack:
https://kenney.nl/assets/input-prompts

License: Creative Commons CC0 1.0 (public domain). The pack may be used in any
project, commercial or not, with no attribution required; credit is given here
anyway. The pack ships a `License.txt` alongside the download.

Only the icons the port needs are copied here.

The `steamdeck_*.png` set (100x100, the Steam Deck's buttons, also used for the Steam
Controller and Steam Input's virtual pad) comes from Xelu's **Free Controllers & Keyboard
Prompts** pack: https://thoseawesomeguys.com/prompts/ — also Creative Commons CC0 1.0. The
files are the pack's `Steam Deck` folder, renamed from `SteamDeck_<Name>.png` to
`steamdeck_<name>.png`. Menu is Start, Inventory (the View button) is Back, L4/R4/L5/R5 are
the back grips (SDL's paddles). The pack has no per-direction stick art, so
`tools/make_prompt_glyphs.py` builds it: the Deck stick at full size, with Kenney's arrow
(lifted out of `xbox_stick_l_left.png`, rotated per direction, recoloured to the Deck art's
grey face and dark rim) over its rim on that side.

The device icons label the A, B, X, Y, Start, L, R and Z prompts, and the
stick, C-stick and D-pad prompts (whole and per direction), for each
controller family. The Switch set is positional, like SDL3's button names: the
A action sits on the bottom face button, which the Switch labels B. L
and R are the pad's analog triggers, so they take the trigger art (LT/RT, L2/R2,
ZL/ZR), while the Z prompt, which is a digital shoulder, takes the bumper.
The keyboard set doubles as the fallback used when a binding has no icon of its
own, so it labels the port's default keys:

| File | Used for |
| --- | --- |
| `xbox_button_color_a.png`, `xbox_button_color_b.png` | Xbox A / B prompts |
| `xbox_lt.png`, `xbox_rt.png` | Xbox LT / RT trigger prompts, which is what the port maps the GameCube L and R triggers onto |
| `xbox_lb.png`, `xbox_rb.png` | Xbox LB / RB shoulder prompts, used for the Z prompt |
| `playstation_button_color_cross.png`, `playstation_button_color_circle.png` | PlayStation Cross / Circle prompts |
| `playstation_trigger_l2.png`, `playstation_trigger_r2.png` | PlayStation L2 / R2 trigger prompts |
| `switch_button_a.png`, `switch_button_b.png` | Switch A / B prompts |
| `switch_button_zl.png`, `switch_button_zr.png` | Switch ZL / ZR trigger prompts |
| `keyboard_x.png`, `keyboard_z.png` | keyboard A / B, matching the default keys |
| `keyboard_q.png`, `keyboard_e.png` | keyboard L / R, matching the default keys |
| `keyboard_f.png` | keyboard Z, matching the default key |
| `xbox_button_color_y.png`, `playstation_button_color_triangle.png`, `switch_button_x.png`, `keyboard_v.png` | Y prompts (the pause screen's Zoom) |
| `xbox_button_color_x.png`, `playstation_button_color_square.png`, `switch_button_y.png`, `keyboard_c.png` | X prompts |
| `xbox_button_start.png`, `playstation3_button_start.png`, `switch_button_plus.png`, `keyboard_enter.png` | Start prompts |
| `{xbox,playstation,switch}_stick_l.png`, `…_stick_r.png`, `…_dpad.png` | whole-stick, C-stick and D-pad prompts |
| `{xbox,playstation,switch}_{stick_l,stick_r,dpad}_{up,down,left,right}.png` | the same prompts for one direction (the map screen's stick frames, the visor hints' D-pad arrows) |
| `keyboard_w/a/s/d.png`, `keyboard_i/j/k/l.png`, `keyboard_arrow_*.png` | keyboard stick, C-stick and D-pad directions; the whole-stick prompts are drawn from them as an inverted T of keys (`keyboard_wasd`, `keyboard_ijkl`, `keyboard_arrows`), since the pack has no such icons |
| `playstation_trigger_r1.png`, `switch_button_r.png` | PlayStation / Switch Z prompts (the right shoulder) |
| `gamecube_button_color_*.png`, `gamecube_button_start.png`, `gamecube_trigger_l.png`, `gamecube_trigger_r.png` | a GameCube pad's buttons, used only when an action is remapped; there is no static GameCube set, since that is the game's own art |

Which game textures they are written as comes from the prompt table in
`platform/port_prompts.cpp`, which `tools/make_prompt_glyphs.py` parses, so a
newly identified prompt texture only has to be added there.

The rest are keyboard and mouse icons for the inputs that can be bound. They are
written to `<textures>/bindings/<stem>.dds` and served per binding, so the prompt
shows whatever key or mouse button is actually bound to the action. Stems match
the tables in `platform/port_prompts.cpp`: `keyboard_a`–`keyboard_z`,
`keyboard_0`–`keyboard_9`, the arrows, `keyboard_f1`–`keyboard_f12`, the named
keys (space, enter, escape, tab, backspace, delete, insert, home, end, page up,
page down, shift, ctrl, alt, win, caps lock, print screen, scroll lock, pause),
punctuation (minus, equals, brackets, semicolon, apostrophe, comma, period,
slashes, tilde for the grave key), the keypad keys that have art of their own
(enter, plus, asterisk, num lock; its digits, minus, period and slash reuse the
main keys'), and `mouse_left` / `mouse_right` / `mouse_scroll` (middle button) /
`mouse_side_back` / `mouse_side_forward`.
