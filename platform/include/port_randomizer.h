#ifndef METROID_PRIME_PORT_PORT_RANDOMIZER_H
#define METROID_PRIME_PORT_PORT_RANDOMIZER_H
#include <cstdint>
#include <string>

namespace PortRandomizer {

// Loads the configured seed once. Call on the game thread; never throws.
void EnsureLoaded();
// True when a seed contains at least one placement.
bool Enabled();
// True when MP_RANDO_DUMP is set to a non-empty value other than "0".
bool DumpEnabled();
// Name from the loaded seed, or an empty string when none is available.
const char* SeedName();
// Number of pickup checks recorded during this process.
int CheckCount();
// Short status line suitable for the F1 overlay.
const char* StatusText();
// Logs a location in dump mode, or applies its placement when enabled.
bool ApplyPickup(uint32_t worldAssetId, uint32_t areaAssetId, uint32_t entityId,
                 int& itemType, int& capacity, int& amount);
// The seed's item at a location, without dump logging (the map's pickup kinds): true, with
// itemType and capacity set, when the seed places it; else both are left alone.
bool Placed(uint32_t worldAssetId, uint32_t areaAssetId, uint32_t entityId, int& itemType,
            int& capacity);
// For a location the seed places: true, and `out` is the text to show on pickup (the seed's first
// hud_text, else "<display name> acquired!", with spaced names, not the ItemName). False for an unplaced location or when disabled.
bool AcquisitionText(uint32_t worldAssetId, uint32_t areaAssetId, uint32_t entityId, int itemType,
                     std::string& out);
// Marks a MEMO (by unique id) reachable from a randomized pickup's Arrived chain: its next
// SetToZero is dropped, whenever it arrives (relays and timers delay it), so the vanilla item's
// text is not shown. TakeMemoSuppressed returns true once per mark.
void SuppressMemoOnce(uint16_t uniqueId);
bool TakeMemoSuppressed(uint16_t uniqueId);
// Records a collected pickup check when randomizer or dump mode is active.
void RecordCheck(uint32_t worldAssetId, uint32_t areaAssetId, uint32_t entityId, int itemType);
// The seed file the loader reads (MP_RANDO_SEED, else <user>/randomizer_seed.json) and the log
// dump mode writes (<user>/randomizer_locations.log; the generator no longer reads it).
std::string SeedFilePath();
std::string DumpLogFilePath();
// Writes a seed from the logic-based generator (port_rando_gen.h), for the F1 overlay's Randomizer
// page. The running game keeps the seed it loaded at start-up; the new one is read by the next
// launch. False, with the reason in `error`, when no seed was written.
bool GenerateSeed(const char* seedName, std::string& error);

// Returns the retail item name, or "Unknown" for an out-of-range item type.
const char* ItemName(int itemType);
// Returns an item type for a case-insensitive exact name, or -1 when unknown.
int ItemFromName(const char* name);
// Formats a location as three uppercase, zero-padded eight-digit hexadecimal IDs (defined in
// port_rando_gen.cpp).
void FormatLocationKey(uint32_t worldAssetId, uint32_t areaAssetId, uint32_t entityId,
                       char* out, int outSize);

} // namespace PortRandomizer

#endif
