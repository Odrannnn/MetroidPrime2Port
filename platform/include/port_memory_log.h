#pragma once

#include <cstddef>
#include <cstdint>

// Memory lines for the log (MP1 issue #102, ported with its #162/#187 follow-ups): an Android
// low-memory kill leaves no crash report, only a log that stops), and the memory-pressure checks
// that read them. Linux, Android and Windows; elsewhere no-ops. Lines go to stderr, so the log
// file and (on Android) logcat both get them.
namespace PortMemoryLog {

// Once, after the log starts: how the previous run ended (Android 11+, from the system's exit
// records, via MetroidPrime2Activity.exitReport()) and the memory the device has now.
void LogStartup();
// Each main-loop iteration: samples once a second and writes a line (process RSS, system
// available, texture/target/lighting totals, Dawn's GPU memory) every 30 s, or sooner when the
// process or the system's free memory moved a lot. Also runs the pipeline pressure check below.
void Tick(uint64_t nowNs);
// Android only (false elsewhere): whether the device is short on RAM (under 4096 MiB in total, or
// ActivityManager.isLowRamDevice()); `why` says which, or gives the RAM size. MP1 issue #162.
bool LowRamDevice(char* why, size_t whySize);
// The checks below read the headroom: the least of the system's available RAM, its available
// commit (Windows) and what is left of the GPU's memory budget (Windows, DXGI).
// Lazy pipeline cache (MP1 issue #187): Tick checks the headroom every 5 s and, below moderateMib
// or severeMib, asks Aurora to release built pipelines no recent frame drew with (idle for 3600
// frames, or for 16). GLOBAL pipelines stay pinned. 0 turns a level off; both 0 (the default)
// turns the check off.
void SetPipelinePressure(int64_t moderateMib, int64_t severeMib);
// Texture replacements (MP1 issue #266): Tick passes the headroom to Aurora every second. Below
// moderateMib it frees replacements idle for 600 frames and loads new ones at most 2048 px; below
// severeMib those idle for 60, at most 1024 px, and reloads drawn ones that are larger. 0 turns a
// level off; both 0 (the default) turns the check off.
void SetTexturePressure(int64_t moderateMib, int64_t severeMib);

} // namespace PortMemoryLog
