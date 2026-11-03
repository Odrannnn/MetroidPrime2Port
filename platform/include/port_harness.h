#ifndef _PORT_HARNESS_H
#define _PORT_HARNESS_H

// The test harness's run length, and the only knob that lets a run end itself.
//
// A sweep is normally ended by `timeout`, and the timeout cannot tell a run that finished its work
// from one that hung: both are exit 124. `MP2_FRAMES=<n>` ends the run cleanly once n *game*
// updates have happened - `CStateManager::Update` calls, i.e. frames of a running game, not the
// boot, pak-loading or menu frames - and the exit is the game loop's own quit path (the one the
// window's close button takes): the loop ends, Aurora shuts down and the process exits 0. A run
// that ends this way says so on stderr, so the last lines of a log are the run's verdict.
//
// It is a *count*, not a wait: an unarmed run is unaffected, and a run whose frame limit is never
// reached (the game never started, the area never loaded) still ends on the script's `timeout`,
// which stays in place as the backstop.

namespace PortHarness {

// One game update. Returns true on the update that reaches the limit, so the caller can report
// where the run got to before the loop stops. Always false when MP2_FRAMES is unset or not
// positive.
bool GameUpdate();

// True once a run has reached its limit and asked to end; the platform polls this.
bool ExitRequested();

// How many game updates have happened, 0 until the game's first one. The `MP2_INPUT` script's frames
// are counted in these (platform/port_events.cpp, "Input" in platform/README.md): the boot between
// the loop's first iteration and the game's first update is thousands of iterations long, so a
// script that counted iterations put its early tokens somewhere the game never reached.
unsigned GameUpdates();

// A direct boot (CStateManager::PortDirectBoot) starts the game in the area a save-world boot only
// reached after its warp, kDirectBootUpdates game updates later. Starting the count there keeps every
// number keyed on it - MP2_FRAMES, `@f` lines, `frame`, `at`, MP2_INPUT tokens, "gameplay reached
// (frame 240)" - meaning what it meant. Called once, before the first game update.
constexpr unsigned kDirectBootUpdates = 120;
void StartCountAt(unsigned updates);

// True while a loop iteration needs no 60 Hz pacing: a fixed-tick fast run (MP2_TICKS_PER_FRAME with
// MP2_FRAME_LIMIT=0, whose ticks are not wall time anyway), or MP2_FAST_LOAD=1 with
// MP2_SKIP_FRONTEND before the game's first update (pak loading and the world load, where no game
// time passes and no front end is shown). Both pacers ask it: CGraphics::EndScene's and the main
// loop's 60 FPS cap.
bool Unpaced();

// Called every loop iteration by the platform's event poll. Prints one line, once, when the game
// has stopped updating for a while after having updated at least once - a paused game (START
// opens the pause menu, and a paused game calls no CStateManager::Update) or a wedged load look
// exactly like a hang otherwise: the frame limit is a count of game updates, so it is never
// reached and only the script's `timeout` ends the run. Re-arms after the game updates again, so
// a second stall is reported too.
void IdleCheck();

// Wall seconds since the first call into this file (the game loop's start). A log that says how long
// a phase took is worth more than one that says how many frames it used, because the loop's rate is
// not constant: pak loading runs at tens of iterations a second and a rendered game area at a
// handful.
double Seconds();

// MP2_SKIP_INTRO=1: drive a run into gameplay from the port side, with no input - the semantics of
// MP1's MP_SMOKE_WORLD (MetroidPrimePort/platform/smoke.cpp): wait until the game is running, serve
// the boot warp (MP2_WORLD / MP2_TELEPORT, platform/port_warp.cpp), wait until it has landed, then
// report. MP2_SKIP_FRONTEND removes the boot splash chain; this removes the other blocker, the HUD
// memo an area can show (`CStateManager::ShowPausedHUDMemo` -> `CMFGame::EnterMessageScreen`, which
// pauses the game and waits for a button): `EnterMessageScreen` asks IntroDismissMemo and, while it
// says yes, takes the game's own dismissal path (`UnpauseGame` + the HUD-message frame counter)
// instead of pausing. A real pause menu never goes through it. A cinematic camera before gameplay
// gets the skip request START makes (IntroSkipCinematic); MP2_SKIP_CINEMATICS=1 makes unseen ones skippable.
// The level-load text the loader plays over the load (STRG_IntroLevelLoad: "Data Confirmation",
// "Mission File ...") and the world-name card are not played at all (IntroSkipLevelText).
// "Playing" is a game update with the state manager running, no warp pending, streaming or handed
// to a transition, no cinematic camera and the flow in kFS_InGame; kSettleUpdates of those in a row
// log `[harness] gameplay reached at <s> (frame <game update>)` once, and the dismissal ends.
// MP2_SKIP_INTRO_FRAMES (default 3600 game updates) bounds it: past that it logs that it gave up.

// CMFGame::SetFlowState publishes every flow state here (state is CMFGame::EFlowState).
void GameFlowState(int state, bool inGame);
// The last flow state published there (CMFGame::EFlowState: 1 kFS_InGame, 2 kFS_Paused), -1
// before the first; safe from any thread (the touch overlay polls it from Android's UI thread).
int FlowState();
// CMFGame::EnterMapScreen and PauseGame (the teleport map) publish the map screen here after they
// pause for it; the next flow state clears it. True while the map screen is up; safe from any thread.
void GameMapScreen();
bool MapScreenOpen();

// CMFGame::EnterMessageScreen asks this before pausing for a HUD memo (message is its STRG id).
// True: dismiss it (logged).
bool IntroDismissMemo(unsigned message);

// CMFGame's timer tick asks this while a cinematic camera is current in kFS_InGame (camera is its
// script id). True: request the skip START would (logged once per camera).
bool IntroSkipCinematic(unsigned camera);

// CMFGameLoader asks this while it decides whether to play the level-load text - the
// "Data Confirmation" / "Mission File ..." cards of STRG_IntroLevelLoad, or the world-name card on
// any area load. True: play neither, so a run that asked for no intro lands in gameplay without
// them. The load transition itself still runs; with nothing enabled it finishes in two seconds.
bool IntroSkipLevelText();

// CStateManager::Update, once per game update: whether this update counts as playing (see above).
void NotePlaying(bool playing);

} // namespace PortHarness

#endif
