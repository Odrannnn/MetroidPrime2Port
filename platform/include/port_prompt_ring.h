#pragma once

class CColor;
class CGuiModel;

// Options > On-screen prompts > Render button prompt rings (PortOverlay::PromptRing): the scan,
// message and logbook boxes' button, drawn instead of read from the game's 32x32 ring texture.
//
// The game draws that button additively from two small textures, a ring that carries the game's
// own letter and a lit disc, inside a notch cut into the box's frame: right at 480p, ragged and
// gapped at today's resolutions, and with any glyph set but the GameCube's still holding the game's
// letter. With the setting on, the prompt code (port_prompts.cpp) serves the ring texture as a
// clean white ring and the disc as the game's teal with the bound button's symbol, and this draws
// the rest: the ring as a round mesh at the screen's resolution, through the game's own ring
// material (so in the game's ring colour), in place of the quad the ring texture sits on, which is
// emptied, and sized to its disc as in the scan box; the frame line's ends moved to meet its
// outside edge; and the fill's hexagonal notch made a round hole. On a box the widescreen spread
// draws in slices, the ring stays round, centred on the button's disc, and the line's ends follow.
//
// CGuiModel calls these around each model of a frame's batch. With the setting off, or no glyph set
// replacing the button, they return at once, and the models' geometry is put back as the game's.
namespace PortPromptRing {
// Before the model draws: closes the notch in the box's frame line and fill (once per model, put
// back when no ring is drawn), and notes where the button's disc is.
void BeforeDraw(const CGuiModel& widget);
// After the model draws: draws the fill round the button's round hole after the fill model, and
// queues the ring for a model carrying the boxes' ring texture. True when it drew (the caller's
// model draw state is stale then).
bool AfterDraw(const CGuiModel& widget, const CColor& color);
// At the end of the frame's batch: draws the queued rings over the frame's models. True when it
// drew (the caller's model draw state is stale then).
bool EndBatch();
// A CCubeModel being destroyed: whatever was recorded for it is dropped (CCubeModel's destructor).
void ModelGone(const void* model);
} // namespace PortPromptRing
