#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Binding-aware button prompt icons.
//
// The game draws each prompt from a fixed texture, so a shipped replacement can
// only ever show one device's icon. This module instead registers, at runtime,
// the icon for the input actually bound to each prompt's action, so rebinding a
// key or mouse button is reflected in game. The icons are pre-rendered per
// input by tools/make_prompt_glyphs.py into <textures>/bindings/.
//
// A keyboard follows the key bindings and a pad follows its button mapping. The
// icons are registered above the static per-device set, which stays in place
// for any prompt the module has no icon for.
namespace PortPrompts {
// The device the player last used: "keyboard", the connected pad's type (see
// PortTextures::PadDeviceName), or the touch overlay's layout ("xbox" or
// "gamecube"). MP2_TEXTURE_DEVICE overrides it. Both the prompts and the static
// texture set follow this.
const char* ActiveDevice();

// True while the keyboard or mouse is the input the player last used, ignoring
// the MP2_TEXTURE_DEVICE override. The Prime 1 port holds rumble off then, so a
// pad left plugged in does not shake while the game is played on keyboard/mouse;
// nothing in Echoes asks yet.
bool KeyboardActive();

// True while a connected pad is the input the player last used (not the touch
// overlay's virtual one). Starts true: no key or click yet means no keyboard.
bool PadActive();

// What the game's A button is called on the device in use ("A", "Cross", "Enter"...), for text
// prompts such as the cinematic skip confirm.
const char* ConfirmLabel();

// The generated icon for the game's A button on the device in use, or empty when there is none
// (no bindings folder, or a key without an icon): cheap enough to ask every frame.
std::string ConfirmIconStem();

// The icon `stem` names as top-down RGBA8, for an ImGui texture. False when it is missing.
bool LoadIcon(const std::string& stem, std::vector< uint8_t >& rgba, uint32_t& width, uint32_t& height);

// Call once after Aurora is up, with the same texture root the replacements
// came from (null or empty disables the module).
void Initialize(const char* textureRoot);

// Re-registers an action's icon when its binding changed. Call once per frame.
void Poll();

// Tells the prompts that the touch overlay was just used, and in which layout
// (true for the twin-stick Xbox arrangement, false for the GameCube one), so the
// in-game prompts follow the input the player actually reached for.
void NoteTouchInput(bool xboxLayout);

// Prompt art is bound by asset id. FTextureFactory asks CTexture to bind the texture it just
// built: when `id` is a prompt's, its icon follows the binding from then on (registered under the
// CTexture's own address, so it holds whatever the texels are) and BindTexture returns true.
// UnbindTexture drops it (the CTexture's destructor). Both main-thread only.
bool IsPromptAsset(uint32_t id);
// The scan, message and logbook boxes' button, for the drawn ring (port_prompt_ring.h): the texture
// is the boxes' ring (whatever it is served as); it is served blank because the ring is drawn; any
// is; it is the boxes' teal disc, served.
bool IsBoxRing(const void* texture);
bool DrawsRing(const void* texture);
bool AnyDrawnRing();
bool IsTealDisc(const void* texture);
// The texture is the boxes' lit disc (whatever it is served as).
bool IsBoxDisc(const void* texture);
bool BindTexture(const void* owner, uint32_t id, uint32_t width, uint32_t height);
void UnbindTexture(const void* owner);
} // namespace PortPrompts
