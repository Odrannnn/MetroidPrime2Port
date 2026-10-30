#pragma once

#include "port_strings.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

// Discord Rich Presence (desktop), the Prime 1 port's feature (its platform/port_discord.cpp,
// MP1 commits e1f7dd7c5 and on): shows the current area, energy, missiles and item percentage on
// the player's Discord profile. It talks to the local Discord client over its IPC socket (a Unix
// socket, or a named pipe on Windows) with the documented frame format: a little-endian opcode and
// length, then JSON. No Discord SDK is vendored: the whole protocol is the two frames below.
//
// The helpers here are pure so they can be tested without Discord; the glue in port_discord.cpp
// runs the connection on a background thread and is fed from CStateManager (the per-tick presence)
// and CMainFlow (the menus). JSON goes together with port::JsonQuote (port_strings.h) instead of
// MP1's port::JsonWriter, which this tree has no other use for.
namespace PortDiscord {

enum EOpcode : uint32_t {
  kOp_Handshake = 0,
  kOp_Frame = 1,
  kOp_Close = 2,
  kOp_Ping = 3,
  kOp_Pong = 4,
};

// Discord drops frames larger than this; anything bigger from the peer is a broken stream.
constexpr uint32_t kMaxFrame = 64 * 1024;

inline std::string EncodeFrame(uint32_t op, const std::string& payload) {
  std::string out(8, '\0');
  const uint32_t length = static_cast<uint32_t>(payload.size());
  for (int i = 0; i < 4; ++i) {
    out[i] = static_cast<char>((op >> (8 * i)) & 0xff);
    out[4 + i] = static_cast<char>((length >> (8 * i)) & 0xff);
  }
  return out + payload;
}

struct Frame {
  uint32_t op = 0;
  std::string payload;
};

// Moves the first complete frame out of `buffer`. Returns false while the frame is incomplete;
// sets `bad` (and returns false) for an oversized one.
inline bool TakeFrame(std::string& buffer, Frame& out, bool& bad) {
  bad = false;
  if (buffer.size() < 8) {
    return false;
  }
  uint32_t op = 0, length = 0;
  for (int i = 0; i < 4; ++i) {
    op |= static_cast<uint32_t>(static_cast<unsigned char>(buffer[i])) << (8 * i);
    length |= static_cast<uint32_t>(static_cast<unsigned char>(buffer[4 + i])) << (8 * i);
  }
  if (length > kMaxFrame) {
    bad = true;
    return false;
  }
  if (buffer.size() < 8 + static_cast<size_t>(length)) {
    return false;
  }
  out.op = op;
  out.payload = buffer.substr(8, length);
  buffer.erase(0, 8 + static_cast<size_t>(length));
  return true;
}

// Discord takes 2 to 128 characters in details/state; longer text is cut on a UTF-8 boundary and a
// single character is padded.
inline std::string FitField(std::string text) {
  if (text.size() > 128) {
    size_t cut = 128;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xc0) == 0x80) {
      --cut;
    }
    text.resize(cut);
  }
  if (text.size() == 1) {
    text += ' ';
  }
  return text;
}

// Game text (a STRG string) to UTF-8, without the formatting tags ("&just=center;" and the like).
// "&&" is a literal &. The host's wchar_t is 32-bit, so a code point is one unit; the surrogate
// handling stays for the 16-bit builds and for a lone surrogate inside a 32-bit unit.
template < typename Char >
std::string GameTextToUtf8(const Char* text) {
  std::string out;
  if (text == nullptr) {
    return out;
  }
  for (const Char* p = text; *p != 0; ++p) {
    uint32_t c = static_cast< uint32_t >(*p);
    if (c == '&') {
      if (p[1] == '&') {
        out += '&';
        ++p;
        continue;
      }
      const Char* end = p + 1;
      while (*end != 0 && *end != ';') {
        ++end;
      }
      if (*end == 0) {
        break;
      }
      p = end;
      continue;
    }
    if (c >= 0xd800 && c <= 0xdbff && p[1] >= 0xdc00 && p[1] <= 0xdfff) {
      c = 0x10000 + ((c - 0xd800) << 10) + (static_cast< uint32_t >(p[1]) - 0xdc00);
      ++p;
    } else if ((c >= 0xd800 && c <= 0xdfff) || c > 0x10ffff) {
      continue; // lone surrogate or invalid Unicode scalar
    }
    if (c < 0x80) {
      out += static_cast< char >(c);
    } else if (c < 0x800) {
      out += static_cast< char >(0xc0 | (c >> 6));
      out += static_cast< char >(0x80 | (c & 0x3f));
    } else if (c < 0x10000) {
      out += static_cast< char >(0xe0 | (c >> 12));
      out += static_cast< char >(0x80 | ((c >> 6) & 0x3f));
      out += static_cast< char >(0x80 | (c & 0x3f));
    } else {
      out += static_cast< char >(0xf0 | (c >> 18));
      out += static_cast< char >(0x80 | ((c >> 12) & 0x3f));
      out += static_cast< char >(0x80 | ((c >> 6) & 0x3f));
      out += static_cast< char >(0x80 | (c & 0x3f));
    }
  }
  // Names can carry line breaks for the map screen.
  for (char& c : out) {
    if (c == '\n' || c == '\r') {
      c = ' ';
    }
  }
  return out;
}

struct Presence {
  std::string details; // first line
  std::string state;   // second line (may be empty)
  std::string hover;   // the large image's tooltip, empty = the default one
  int64_t start = 0;   // Unix seconds the elapsed timer counts from, 0 = none

  bool operator==(const Presence& other) const {
    return details == other.details && state == other.state && hover == other.hover &&
           start == other.start;
  }
  bool operator!=(const Presence& other) const { return !(*this == other); }
};

// The art asset the Discord application has for the large image.
constexpr const char* kLargeImage = "logo";
constexpr const char* kDefaultText = "Metroid Prime 2: Echoes native port";
constexpr const char* kActivityName = "Metroid Prime 2: Echoes";

inline std::string HandshakePayload(const std::string& appId) {
  return "{\"v\":1,\"client_id\":" + port::JsonQuote(appId) + "}";
}

inline std::string ActivityPayload(long pid, const Presence& presence, const std::string& nonce) {
  std::string out = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(pid) +
                    ",\"activity\":{\"name\":" + port::JsonQuote(kActivityName) +
                    ",\"details\":" + port::JsonQuote(FitField(presence.details));
  if (!presence.state.empty()) {
    out += ",\"state\":" + port::JsonQuote(FitField(presence.state));
  }
  if (presence.start > 0) {
    out += ",\"timestamps\":{\"start\":" + std::to_string(presence.start) + "}";
  }
  out += ",\"assets\":{\"large_image\":" + port::JsonQuote(kLargeImage) +
         ",\"large_text\":" +
         port::JsonQuote(presence.hover.empty() ? std::string(kDefaultText) : FitField(presence.hover)) +
         "}}},\"nonce\":" + port::JsonQuote(nonce) + "}";
  return out;
}

// Clears the activity (presence turned off while connected).
inline std::string ClearPayload(long pid, const std::string& nonce) {
  return "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(pid) +
         "},\"nonce\":" + port::JsonQuote(nonce) + "}";
}

// What the game reports each tick.
struct GameInfo {
  std::string world;
  std::string area;
  uint32_t worldId = 0; // MLVL, for the log and the console
  int percent = 0;      // items collected
  bool hard = false;
  int energy = 0;    // total, tanks included
  int missiles = -1; // -1 = no launcher yet
};

// The in-game lines: area on top, then energy, missiles and item percentage. The world and hard
// mode go in the logo's tooltip (the world goes on top while the area name loads).
inline Presence GamePresence(const GameInfo& info, int64_t start) {
  const std::string dot = " \xc2\xb7 ";
  Presence presence;
  presence.details = !info.area.empty() ? info.area : (!info.world.empty() ? info.world : "In game");
  std::string state = std::to_string(info.energy) + " energy";
  if (info.missiles >= 0) {
    state += dot + std::to_string(info.missiles) + (info.missiles == 1 ? " missile" : " missiles");
  }
  state += dot + std::to_string(info.percent) + "% items";
  presence.state = state;
  presence.hover = info.area.empty() ? std::string() : info.world;
  if (info.hard) {
    presence.hover += presence.hover.empty() ? "Hard mode" : dot + "Hard mode";
  }
  presence.start = start;
  return presence;
}

// Where the Discord client listens, in the order the official SDK tries them: the runtime/temp
// directory (also inside the Flatpak and Snap Discord's sandboxes), sockets 0 to 9.
inline std::vector< std::string >
SocketCandidates(const std::function< const char*(const char*) >& getEnv) {
  std::vector< std::string > out;
#if defined(_WIN32)
  (void)getEnv;
  for (int i = 0; i < 10; ++i) {
    out.push_back("\\\\?\\pipe\\discord-ipc-" + std::to_string(i));
  }
#else
  std::string base = "/tmp";
  for (const char* name : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"}) {
    const char* value = getEnv(name);
    if (value != nullptr && value[0] != 0) {
      base = value;
      break;
    }
  }
  while (base.size() > 1 && base.back() == '/') {
    base.pop_back();
  }
  for (const char* sub : {"", "app/com.discordapp.Discord/", "snap.discord/"}) {
    for (int i = 0; i < 10; ++i) {
      out.push_back(base + "/" + sub + "discord-ipc-" + std::to_string(i));
    }
  }
#endif
  return out;
}

// Port glue (port_discord.cpp).

enum EStatus { kStatus_Off, kStatus_Connecting, kStatus_Connected, kStatus_Failed };
// False where there is no Discord client to talk to (Android).
bool Supported();
EStatus Status();
// The last error (no Discord running, application not found), or empty.
std::string LastError();
// The port's Discord application, the Prime 1 port's (it has the logo art); `MP2_DISCORD_APP`
// replaces it for a run. Set the port's own application there once one is registered.
constexpr const char* kDefaultAppId = "1557127540030701690";
std::string ApplicationId();
// Connects while enabled, reconnecting every few seconds; clears the presence and disconnects
// when not. The game's tick and the F1 row both call it.
void Configure(bool enabled);
// Cheap check for the game tick, which only looks up names while this is on.
bool Enabled();
// What the game shows: the front end, or a world and area. Sent when it changes, at most once
// every few seconds (Discord's rate limit).
void SetMenu();
void SetGame(const GameInfo& info);
// The presence as last set, for the overlay and console ("details / state").
std::string CurrentText();
// Called at exit: takes the presence down and stops the thread.
void Shutdown();

} // namespace PortDiscord
