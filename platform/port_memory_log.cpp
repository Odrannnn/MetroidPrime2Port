#include "port_memory_log.h"

#include <aurora/gfx.h>

#include <cstdio>

#if defined(__linux__) || defined(_WIN32)
#include <algorithm>
#include <cstdlib>
#include <cstring>
#endif

#if defined(__linux__)
#include <unistd.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>
#include <psapi.h>
#endif

#if defined(__ANDROID__)
#include <SDL3/SDL_system.h>
#include <jni.h>

#include <string>
#endif

namespace PortMemoryLog {
#if defined(__linux__) || defined(_WIN32)
namespace {

constexpr uint64_t kSampleNs = 1000000000ull;
constexpr uint64_t kPeriodNs = 30 * kSampleNs;
constexpr int64_t kProcessJumpKb = 64 * 1024;
constexpr int64_t kSystemJumpKb = 128 * 1024;

struct ProcessMemory {
  int64_t residentKb = -1;
  int64_t commitKb = -1; // Windows: the private bytes this process charges against the commit limit
};

struct SystemMemory {
  int64_t totalKb = -1;
  int64_t availableKb = -1;
  // Windows: the commit limit (RAM plus page file) and what is left of it. An allocation past it
  // fails however much RAM is free, which is how an 8 GB PC with a small page file runs out.
  int64_t commitLimitKb = -1;
  int64_t commitAvailableKb = -1;
  // Windows: the GPU's local memory budget and this process's use of it (shared RAM on an iGPU).
  int64_t gpuBudgetKb = -1;
  int64_t gpuUsageKb = -1;
};

#if defined(__linux__)
// Resident size of this process, in KiB; -1 when /proc can't say.
int64_t ProcessKb() {
  FILE* file = std::fopen("/proc/self/statm", "r");
  if (file == nullptr) {
    return -1;
  }
  long long size = 0;
  long long resident = 0;
  const int read = std::fscanf(file, "%lld %lld", &size, &resident);
  std::fclose(file);
  return read == 2 ? resident * (sysconf(_SC_PAGESIZE) / 1024) : -1;
}

ProcessMemory ReadProcess() { return ProcessMemory{.residentKb = ProcessKb()}; }

SystemMemory ReadSystem() {
  SystemMemory memory;
  FILE* file = std::fopen("/proc/meminfo", "r");
  if (file == nullptr) {
    return memory;
  }
  char line[128];
  while (std::fgets(line, sizeof(line), file) != nullptr) {
    if (std::strncmp(line, "MemTotal:", 9) == 0) {
      memory.totalKb = std::strtoll(line + 9, nullptr, 10);
    } else if (std::strncmp(line, "MemAvailable:", 13) == 0) {
      memory.availableKb = std::strtoll(line + 13, nullptr, 10);
    }
  }
  std::fclose(file);
  return memory;
}
#else
ProcessMemory ReadProcess() {
  ProcessMemory memory;
  PROCESS_MEMORY_COUNTERS_EX counters{};
  if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast< PROCESS_MEMORY_COUNTERS* >(&counters),
                           sizeof(counters))) {
    memory.residentKb = static_cast< int64_t >(counters.WorkingSetSize / 1024);
    memory.commitKb = static_cast< int64_t >(counters.PrivateUsage / 1024);
  }
  return memory;
}

int64_t ProcessKb() { return ReadProcess().residentKb; }

SystemMemory ReadSystem() {
  SystemMemory memory;
  MEMORYSTATUSEX status{};
  status.dwLength = sizeof(status);
  if (GlobalMemoryStatusEx(&status)) {
    memory.totalKb = static_cast< int64_t >(status.ullTotalPhys / 1024);
    memory.availableKb = static_cast< int64_t >(status.ullAvailPhys / 1024);
    memory.commitLimitKb = static_cast< int64_t >(status.ullTotalPageFile / 1024);
    memory.commitAvailableKb = static_cast< int64_t >(status.ullAvailPageFile / 1024);
  }
  return memory;
}
#endif

// The GPU budget needs the device, so the startup line goes without it.
void ReadGpu(SystemMemory& memory) {
  uint64_t budget = 0;
  uint64_t usage = 0;
  if (aurora_get_gpu_memory_budget(&budget, &usage)) {
    memory.gpuBudgetKb = static_cast< int64_t >(budget / 1024);
    memory.gpuUsageKb = static_cast< int64_t >(usage / 1024);
  }
}

// What is left before something runs out: the least of free RAM, free commit and the GPU budget
// (which can be overrun: DXGI then moves memory out, and an iGPU's comes out of RAM); -1 when
// nothing could be read.
int64_t HeadroomKb(const SystemMemory& memory) {
  int64_t headroom = -1;
  const auto consider = [&](int64_t kb) {
    if (kb >= 0) {
      headroom = headroom < 0 ? kb : std::min(headroom, kb);
    }
  };
  consider(memory.availableKb);
  consider(memory.commitAvailableKb);
  if (memory.gpuBudgetKb >= 0 && memory.gpuUsageKb >= 0) {
    consider(std::max< int64_t >(memory.gpuBudgetKb - memory.gpuUsageKb, 0));
  }
  return headroom;
}

long long Mib(int64_t kb) { return kb < 0 ? -1 : static_cast< long long >(kb / 1024); }

// Pipeline retention under memory pressure (SetPipelinePressure). The check reuses Tick's
// once-a-second headroom sample, so it reads nothing of its own; a trim only raises Aurora's
// request flag, and the eviction runs at the next frame start, as onTrimMemory's does.
constexpr uint64_t kPressurePollNs = 5 * kSampleNs;
// A level that holds trims again this often, so a moderate trim catches what went idle since.
constexpr uint64_t kPressureRepeatNs = 30 * kSampleNs;
constexpr uint32_t kModerateIdleFrames = 3600; // a minute at 60 fps: areas left a while ago
constexpr uint32_t kSevereIdleFrames = 16;     // everything this frame and the last few don't draw
int64_t sPressureModerateKb = 0;
int64_t sPressureSevereKb = 0;

// Texture replacements under memory pressure (SetTexturePressure): Aurora gets the headroom each
// sample and frees idle replacements and loads smaller mips below these (MP1 issue #266).
int64_t sTextureModerateKb = 0;
int64_t sTextureSevereKb = 0;

void CheckTexturePressure(int64_t headroomKb) {
  static int sLastLevel = 0;
  if ((sTextureModerateKb <= 0 && sTextureSevereKb <= 0) || headroomKb < 0) {
    return;
  }
  aurora_set_memory_pressure(headroomKb * 1024, sTextureModerateKb * 1024, sTextureSevereKb * 1024);
  const int level = sTextureSevereKb > 0 && headroomKb < sTextureSevereKb       ? 2
                    : sTextureModerateKb > 0 && headroomKb < sTextureModerateKb ? 1
                                                                                : 0;
  if (level != sLastLevel) {
    std::fprintf(stderr, "port: memory pressure: textures %s, %lld MiB headroom (moderate under %lld, severe under %lld)\n",
                   level == 2 ? "severe" : level == 1 ? "moderate" : "off", Mib(headroomKb),
                   Mib(sTextureModerateKb), Mib(sTextureSevereKb));
    sLastLevel = level;
  }
}

void CheckPipelinePressure(uint64_t nowNs, int64_t availableKb) {
  static uint64_t sLastPollNs = 0;
  static uint64_t sLastTrimNs = 0;
  static int sLastLevel = 0;
  if ((sPressureModerateKb <= 0 && sPressureSevereKb <= 0) || availableKb < 0 ||
      (sLastPollNs != 0 && nowNs - sLastPollNs < kPressurePollNs)) {
    return;
  }
  sLastPollNs = nowNs;
  const int level = sPressureSevereKb > 0 && availableKb < sPressureSevereKb       ? 2
                    : sPressureModerateKb > 0 && availableKb < sPressureModerateKb ? 1
                                                                                   : 0;
  if (level == 0) {
    sLastLevel = 0;
    return;
  }
  if (level <= sLastLevel && nowNs - sLastTrimNs < kPressureRepeatNs) {
    return;
  }
  sLastLevel = level;
  sLastTrimNs = nowNs;
  const uint32_t idle = level == 2 ? kSevereIdleFrames : kModerateIdleFrames;
  // Aurora's "pipeline cache: evicted N" line at the next frame start gives the count.
  std::fprintf(stderr, "port: memory pressure: %s, %lld MiB headroom (under %lld): trimming pipelines unused "
                 "for %u frames\n",
                 level == 2 ? "severe" : "moderate", Mib(availableKb),
                 Mib(level == 2 ? sPressureSevereKb : sPressureModerateKb), idle);
  aurora_trim_pipelines(idle);
}

void WriteLine(const ProcessMemory& process, const SystemMemory& system, bool textures) {
  char gpu[320] = "";
  if (textures) {
    AuroraTextureStats stats{};
    aurora_get_texture_stats(&stats);
    int n = std::snprintf(gpu, sizeof(gpu), ", textures %u (%llu MiB), targets %u (%llu MiB), lighting %u (%llu MiB)",
                          stats.count[0], static_cast< unsigned long long >(stats.bytes[0] >> 20), stats.count[1],
                          static_cast< unsigned long long >(stats.bytes[1] >> 20), stats.count[2],
                          static_cast< unsigned long long >(stats.bytes[2] >> 20));
    AuroraDawnMemory dawn{};
    if (n > 0 && static_cast< size_t >(n) < sizeof(gpu) && aurora_get_dawn_memory(&dawn)) {
      std::snprintf(gpu + n, sizeof(gpu) - n, ", dawn %llu MiB (textures %llu, buffers %llu, allocated %llu)",
                    static_cast< unsigned long long >(dawn.total >> 20),
                    static_cast< unsigned long long >(dawn.textures >> 20),
                    static_cast< unsigned long long >(dawn.buffers >> 20),
                    static_cast< unsigned long long >(dawn.allocatorAllocated >> 20));
    }
  }
  char extra[160] = "";
  int n = 0;
  if (system.commitLimitKb >= 0) {
    n = std::snprintf(extra, sizeof(extra), ", commit %lld of %lld MiB available (process %lld)",
                      Mib(system.commitAvailableKb), Mib(system.commitLimitKb), Mib(process.commitKb));
  }
  if (system.gpuBudgetKb >= 0 && n >= 0 && static_cast< size_t >(n) < sizeof(extra)) {
    std::snprintf(extra + n, sizeof(extra) - n, ", gpu %lld of %lld MiB budget used", Mib(system.gpuUsageKb),
                  Mib(system.gpuBudgetKb));
  }
  std::fprintf(stderr, "port: memory: process %lld MiB, system %lld of %lld MiB available%s%s\n", Mib(process.residentKb),
                 Mib(system.availableKb), Mib(system.totalKb), extra, gpu);
}

#if defined(__ANDROID__)
// MetroidPrime2Activity.exitReport(): built in onCreate, before the log runs.
void LogPreviousExit() {
  JNIEnv* env = static_cast< JNIEnv* >(SDL_GetAndroidJNIEnv());
  jobject activity = static_cast< jobject >(SDL_GetAndroidActivity());
  if (env == nullptr || activity == nullptr) {
    return;
  }
  std::string report;
  jclass cls = env->GetObjectClass(activity);
  jmethodID method = cls != nullptr ? env->GetStaticMethodID(cls, "exitReport", "()Ljava/lang/String;") : nullptr;
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    method = nullptr;
  }
  if (method != nullptr) {
    auto text = static_cast< jstring >(env->CallStaticObjectMethod(cls, method));
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
      text = nullptr;
    }
    if (text != nullptr) {
      if (const char* chars = env->GetStringUTFChars(text, nullptr); chars != nullptr) {
        report = chars;
        env->ReleaseStringUTFChars(text, chars);
      }
      env->DeleteLocalRef(text);
    }
  }
  if (cls != nullptr) {
    env->DeleteLocalRef(cls);
  }
  env->DeleteLocalRef(activity);
  if (!report.empty()) {
    std::fprintf(stderr, "%s\n", report.c_str());
  }
}

// MetroidPrime2Activity.lowRamDevice(), set in onCreate.
bool ActivityLowRam() {
  JNIEnv* env = static_cast< JNIEnv* >(SDL_GetAndroidJNIEnv());
  jobject activity = static_cast< jobject >(SDL_GetAndroidActivity());
  if (env == nullptr || activity == nullptr) {
    return false;
  }
  bool low = false;
  jclass cls = env->GetObjectClass(activity);
  jmethodID method = cls != nullptr ? env->GetStaticMethodID(cls, "lowRamDevice", "()Z") : nullptr;
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    method = nullptr;
  }
  if (method != nullptr) {
    low = env->CallStaticBooleanMethod(cls, method) == JNI_TRUE;
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
      low = false;
    }
  }
  if (cls != nullptr) {
    env->DeleteLocalRef(cls);
  }
  env->DeleteLocalRef(activity);
  return low;
}
#endif

} // namespace

#if defined(__ANDROID__)
bool LowRamDevice(char* why, size_t whySize) {
  constexpr int64_t kLowRamKb = 4096ll * 1024;
  const int64_t totalKb = ReadSystem().totalKb;
  if (ActivityLowRam()) {
    std::snprintf(why, whySize, "ActivityManager.isLowRamDevice()");
    return true;
  }
  if (totalKb > 0 && totalKb < kLowRamKb) {
    std::snprintf(why, whySize, "%lld MiB of RAM, under 4096", Mib(totalKb));
    return true;
  }
  std::snprintf(why, whySize, "%lld MiB of RAM", Mib(totalKb));
  return false;
}
#else
bool LowRamDevice(char* why, size_t whySize) {
  if (whySize != 0) {
    why[0] = '\0';
  }
  return false;
}
#endif

void LogStartup() {
#if defined(__ANDROID__)
  LogPreviousExit();
#endif
  WriteLine(ReadProcess(), ReadSystem(), false);
}

void Tick(uint64_t nowNs) {
  static uint64_t sLastSampleNs = 0;
  static uint64_t sLastLineNs = 0;
  static int64_t sLineProcessKb = -1;
  static int64_t sLineAvailableKb = -1;
  if (sLastSampleNs != 0 && nowNs - sLastSampleNs < kSampleNs) {
    return;
  }
  sLastSampleNs = nowNs;
  const ProcessMemory process = ReadProcess();
  SystemMemory system = ReadSystem();
  ReadGpu(system);
  const int64_t headroomKb = HeadroomKb(system);
  CheckTexturePressure(headroomKb);
  CheckPipelinePressure(nowNs, headroomKb);
  const bool due = sLastLineNs == 0 || nowNs - sLastLineNs >= kPeriodNs ||
                   std::llabs(process.residentKb - sLineProcessKb) >= kProcessJumpKb ||
                   std::llabs(system.availableKb - sLineAvailableKb) >= kSystemJumpKb;
  if (!due) {
    return;
  }
  sLastLineNs = nowNs;
  sLineProcessKb = process.residentKb;
  sLineAvailableKb = system.availableKb;
  WriteLine(process, system, true);
}
#else
bool LowRamDevice(char* why, size_t whySize) {
  if (whySize != 0) {
    why[0] = '\0';
  }
  return false;
}
void LogStartup() {}
void Tick(uint64_t) {}
#endif

void SetPipelinePressure(int64_t moderateMib, int64_t severeMib) {
#if defined(__linux__) || defined(_WIN32)
  sPressureModerateKb = moderateMib > 0 ? moderateMib * 1024 : 0;
  sPressureSevereKb = severeMib > 0 ? severeMib * 1024 : 0;
#else
  (void)moderateMib;
  (void)severeMib;
#endif
}

void SetTexturePressure(int64_t moderateMib, int64_t severeMib) {
#if defined(__linux__) || defined(_WIN32)
  sTextureModerateKb = moderateMib > 0 ? moderateMib * 1024 : 0;
  sTextureSevereKb = severeMib > 0 ? severeMib * 1024 : 0;
#else
  (void)moderateMib;
  (void)severeMib;
#endif
}
} // namespace PortMemoryLog

#if defined(__ANDROID__)
// Android's memory warnings (onTrimMemory/onLowMemory), from the UI thread. Android 14
// no longer sends the "running low" levels to a visible app; older versions do.
extern "C" JNIEXPORT void JNICALL Java_org_metroidprime2_port_MetroidPrime2Activity_nativeMemoryWarning(
    JNIEnv*, jclass, jint level, jlong availableKb, jlong thresholdKb, jboolean low) {
  const char* name = "unknown";
  switch (level) {
  case -1: name = "low memory"; break;
  case 5: name = "running moderate"; break;
  case 10: name = "running low"; break;
  case 15: name = "running critical"; break;
  case 20: name = "ui hidden"; break;
  case 40: name = "background"; break;
  case 60: name = "moderate"; break;
  case 80: name = "complete"; break;
  default: break;
  }
  std::fprintf(stderr, "port: memory warning: %s (level %d), process %lld MiB, system %lld MiB available, "
                 "low-memory threshold %lld MiB%s\n",
                 name, static_cast< int >(level), PortMemoryLog::Mib(PortMemoryLog::ProcessKb()),
                 static_cast< long long >(availableKb / 1024), static_cast< long long >(thresholdKb / 1024),
                 low ? " (system is low on memory)" : "");
  // Running low or critical, or the app is in the background: release built pipelines no recent frame
  // drew with (MP1 issue #162). The frame loop does it at the next frame start; they rebuild on demand.
  if (level == -1 || level >= 10) {
    aurora_trim_pipelines(16);
    aurora_reduce_gpu_memory();
    // No headroom until the next sample, a second away: one severe texture sweep.
    if (PortMemoryLog::sTextureModerateKb > 0 || PortMemoryLog::sTextureSevereKb > 0) {
      aurora_set_memory_pressure(0, PortMemoryLog::sTextureModerateKb * 1024, PortMemoryLog::sTextureSevereKb * 1024);
    }
  }
}
#endif
