#pragma once

#include <cstddef>
#include <string>

// The log file (`MP2_LOG_FILE`, F1 > Testing > Log file): everything the process writes to
// stdout and stderr - the port's messages, Aurora's, SDL's, and the fatal line Aurora prints
// right before it aborts - also lands in a file, so a run on a machine without a terminal (or
// on the phone) leaves something to read afterwards.
//
// The file is `<user>/metroid_prime2_port.log` on the desktop, and on Android
// `<external files dir>/metroid_prime2_port.log` (Android/data/<package>/files, which USB file
// transfer can read) when SDL reports one, falling back to the user directory there too. The
// previous run's log is renamed to `metroid_prime2_port.old.log` when a new one is started, so
// restarting after a crash does not lose it. A run stops writing at 64 MiB, with one last line
// saying so; the next run's rotation starts a fresh file.
//
// On by default; `MP2_LOG_FILE=0` turns it off. It is started once per run, before Aurora, so
// Aurora's own startup lines are in the file. On the desktop the process keeps printing to its
// terminal because a small forked copier writes each byte to both; on Android the logcat pump
// hands each line to Write().
namespace PortLogFile {

// Starts the file log, once per run, before Aurora comes up so its startup lines are in it.
void Start(const std::string& userDirectory);
bool Enabled();
std::string Path();
// Android: one line as the existing stdout/stderr pump sees it. Elsewhere a no-op (the copier
// process writes the bytes itself).
void Write(const std::string& line);
// Bytes as they are, to wherever stderr goes (and so the log); async-signal-safe, for crash reports.
void WriteRaw(const char* data, size_t size);

#if defined(_WIN32)
// The copying process Start launches on Windows (`--log-copy <pipe> <file> <terminal> <ready>`, the
// handles' values): copies the pipe into the file and, as it is, into the terminal until the game
// has gone; it signals the ready event first. main calls it first, before anything else starts.
int RunCopy(const char* pipe, const char* file, const char* terminal, const char* ready);
// The exe is a console-subsystem or GUI one that may not own a console; started from a terminal,
// print to that one. Left alone when stderr already goes somewhere.
void AttachParentConsole();
#endif

} // namespace PortLogFile
