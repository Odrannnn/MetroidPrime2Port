#pragma once

// Suit colour customisation: each suit's colours, picked by the player, baked
// into recoloured copies of the suit's own diffuse textures.
//
// Echoes' suits are drawn first person as the arm cannon (and the left arm of
// the Dark and Light suits, CGrappleArm) and as the morph ball, with the same
// CMPR TXTRs wherever they are drawn, so serving recoloured textures under the
// retail ids changes every view with no render change. The glow, lens and grey
// metal textures are never touched. Which TXTR ids belong to which suit, and how
// they were found, is in port_suit_colours_disc.cpp.
//
// Each suit has one to three colour slots: the hue clusters of its textures,
// measured offline (the medians in port_suit_colours.cpp). A texel is coloured
// when it is saturated and not dark, belongs to the slot whose hue is nearest
// (cleaned by a majority filter and by whole islands between dark seams), and
// moves to the picked colour: hue = picked + 0.35 x its offset from the slot's
// median, saturation and value scaled by picked / median. The suits are warm:
// they never touch hues 70-205.
//
// The Light Suit's silver and white body has no hue to cluster, so its second
// slot ("main", Suit::bodySlot) works on value instead: the low-saturation,
// not-dark texels of its body textures take the picked hue and saturation and
// keep their shading. Its first slot stays the gold trim, so a saved
// "FFD000" still means the accent.
//
// PortMods serves the textures under every mod, from a cache in the user folder
// (generated/suit-colours/<suit>/). The mods load at startup, so changing a
// colour takes a restart, or the Apply suit colours button's mod reload.

#include "port_mods.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace PortSuitColours {

// A decoded texture: RGBA8, row by row.
struct Image {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> rgba;  // width * height * 4
};

// Bump when the output changes: the cache is remade when its stamp differs.
inline constexpr int kVersion = 4;

// CPlayerState::EPlayerSuit order.
inline constexpr int kSuitCount = 3;
inline constexpr int kMaxSlots = 3;

// A slot's colour on the disc: the median of its texels, hue in degrees,
// saturation and value in 0..1.
struct Hsv {
  double h = 0.0;
  double s = 0.0;
  double v = 0.0;
};

struct Suit {
  const char* key;   // in settings keys and the cache folder
  const char* name;  // in the UI and the log
  int slots;
  std::array<Hsv, kMaxSlots> medians;
  std::array<const char*, kMaxSlots> slotKeys;
  std::array<const char*, kMaxSlots> slotNames;
  bool warm;      // never touch hues 70-205
  bool glowOnly;  // only bright, saturated texels count as coloured (unused by Echoes' suits)
  int bodySlot;   // the slot that tints the low-saturation body (not a hue cluster), or -1
};
const Suit& SuitInfo(int suit);

// The picked colours, 0xRRGGBB; nullopt = the original.
using Picks = std::array<std::optional<uint32_t>, kMaxSlots>;

// The slot's original colour as 0xRRGGBB (its median).
uint32_t DefaultColour(int suit, int slot);
std::optional<uint32_t> Colour(int suit, int slot);
void SetColour(int suit, int slot, std::optional<uint32_t> rgb);
Picks SuitPicks(int suit);

// Random picks for every slot of the suit: hues spread round the circle from
// a random start, saturated and bright enough to read.
void Randomise(int suit, uint32_t seed);

// Settings text of one slot: "RRGGBB", or empty for the original. Parse
// accepts an optional '#'; anything else reads as the original.
std::string FormatColour(std::optional<uint32_t> rgb);
std::optional<uint32_t> ParseColour(const std::string& text);

// A suit's picks as one string, a slot each, comma-separated, "-" for the
// original ("FFD000,-"; "" when none is set): the settings value, the cache
// stamp's, and what tells whether a reload is due. SetPicksKey reads one
// back; a missing or bad slot is the original.
std::string PicksKey(int suit);
std::string PicksKey(int suit, const Picks& picks);
void SetPicksKey(int suit, const std::string& key);

// What the last PortMods::Initialize served: each suit's PicksKey ("" for a
// suit it left as retail). Pending: the picks differ from it.
void SetServed(const std::array<std::string, kSuitCount>& served);
bool Pending(bool wanted);

// Recolours a decoded texture of `suit` towards `picks`. Returns how many
// texels changed. `body`: the texture is one of the suit's body textures, where
// the body slot (if it has one) applies; the rest keep their silver and grey.
size_t Recolour(Image& image, int suit, const Picks& picks, bool body = true);

// CMPR TXTRs (GX format 10): decode the first level to RGBA, or encode an image
// (with a box-filtered mip chain down to 8x8 or the original's count) as a TXTR.
bool DecodeCmprTxtr(const std::vector<uint8_t>& txtr, Image& out);
std::vector<uint8_t> EncodeCmprTxtr(const Image& image, uint32_t mipCount);

// The suit's textures recoloured to `picks` (one snapshot of SuitPicks, so the
// stamp and the images agree), made or taken from the cache under `userPath`, to
// serve as loose resources. `loose` is what PortMods has so far: a texture a mod
// brings is left out. Empty when nothing is picked or the disc lacks the sources
// (logged). Needs the disc open; for PortMods::Initialize.
std::vector<PortMods::LooseResource> Collect(
    int suit, const Picks& picks, const std::map<std::pair<uint32_t, uint32_t>, PortMods::LooseResource>& loose,
    const std::string& userPath);

}  // namespace PortSuitColours
