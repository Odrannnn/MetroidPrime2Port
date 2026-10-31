#ifndef METROID_PRIME_PORT_PORTSCANTARGET_H
#define METROID_PRIME_PORT_PORTSCANTARGET_H

// Port-only: the CPU side of the scan visor's target buffers.
//
// The game decides what you are scanning by rendering each scannable's palette index into the
// EFB's alpha channel and reading a 32x64 region of it (plus the matching z) back into three
// buffers -- one byte of id per pixel, then the top and bottom halves of the 24-bit z. On the
// console that read-back is a `GXCopyTex` with a main-memory destination; Aurora resolves a copy
// into a texture instead, so the game's buffers were never filled and the scan visor could not
// lock onto anything.
//
// This is the port's replacement: `Request` asks Aurora (through GXRequestScanPeek) to copy the
// same region out of the frame's framebuffer, and once that copy has landed -- one to three
// frames later, which the game's own per-frame palette histogram tolerates -- `FillBuffers`
// copies it into the player's buffers. Everything here runs on the game thread; the staging
// arrays are filled by Aurora's render thread and handed over with a seqlock.
//
// Both entry points are called from upstream files under `#ifdef TARGET_PC`, and neither changes
// the console path.

namespace PortScanTarget {

// Asks for a read-back of the `width` x `height` region at (left, top), in the game's logical
// framebuffer coordinates, the same ones `CCubeRenderer::CopyTextureRegion` is given. Called once
// per scan capture, before the copies, so the request lands in the pass that holds the frame's
// scene draws.
// Returns false when the read-back was not issued this frame (disabled, or throttled: it is
// asked for every MP2_SCAN_PEEK_EVERY-th call, default 2). SetPick must then be skipped too, so the
// pick keeps matching the data in the buffers.
bool Request(int left, int top, int width, int height);

// Where the last request sat, for the code that reads the buffers back (CPlayer::FindScanTargetId):
// the region's top-left corner is `regionLeft/regionTop` logical pixels from the viewport's centre
// (+y down) and `regionWidth` x `regionHeight` big, and the 32x64 grid FillBuffers produces spans
// it. `radius` > 0 marks a circular pick (Trilogy's scan lens): pixels further than `radius` from
// (`centreX`, `centreY`), in region pixels, are left out and the grid is pooled, not sampled.
struct Pick {
  int viewportWidth = 0;
  int viewportHeight = 0;
  int regionWidth = 0;
  int regionHeight = 0;
  float regionLeft = 0.f;
  float regionTop = 0.f;
  float centreX = 0.f;
  float centreY = 0.f;
  float radius = 0.f;
};
// Called right after Request, with the geometry of the request just made.
void SetPick(const Pick& pick);
const Pick& GetPick();
// The Trilogy scan lens's outline radius in logical pixels for a viewport (CPlayerVisor's
// TriDrawScanLens draws its outline at 122 Wii pixels, scaled by the window's fit).
float LensRadius(int viewportWidth, int viewportHeight);

// Copies the most recent completed read-back into the player's scan target buffers, if one is
// there and newer than the last one consumed. The buffers are what `CPlayer::FindScanTargetId`
// reads: `idData` gets `width * height` bytes, `depthHighData` twice that (Z16, byte-swapped the
// way the console's copy left it) and `depthLowData` `width * height`.
void FillBuffers(void* idData, void* depthHighData, void* depthLowData, int width, int height);

// Diagnostic (`MP2_DEBUG_SCAN=1`): prints the visor `CPlayer::UpdateVisorState` settled on
// whenever that changes, which is how a scripted run finds the visor-cycle button.
void NoteVisorSelection(int currentVisor, int selectedVisor, bool requested);

} // namespace PortScanTarget

#endif
