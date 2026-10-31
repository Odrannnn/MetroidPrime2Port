#include "Kyoto/Text/CTextParser.hpp"

#include "Kyoto/IObjectStore.hpp"
#include "Kyoto/Text/CRasterFont.hpp"
#include "Kyoto/Text/CTextExecuteBuffer.hpp"
#include "rstl/StringExtras.hpp"
#include "rstl/algorithm.hpp"

#include <stdio.h>
#include <stdlib.h>
#ifdef TARGET_PC
#include "port_entry.h"
#endif

#ifdef TARGET_PC
namespace {
// Trilogy's strings draw prompts with Wii-remote art that has no GameCube id, so the loader
// cannot fall through to GC art for them. These name the GC texture for the same action, with
// the crop the GC disc's own copy of the string draws it at; port_prompts then re-icons it for
// the active input. Built from the GC/Trilogy tag diff of every Echoes STRG (as MP1 6545eb9c9).
struct PortWiiPrompt {
  uint wiiId;
  uint gcId;
  float cropX;
  float cropY;
};
const PortWiiPrompt kPortWiiPrompts[] = {
    {0x574CDE37, 0xD523DE3B, 0.70f, 0.68f}, // A -> A
    {0x71AA55E3, 0x53B7AC95, 0.70f, 0.68f}, // 2 (jump) -> B
    {0x60ACB985, 0x967A3516, 1.0f, 1.0f},   // Z (lock-on, scan) -> L
    {0xB5F3DA09, 0xADCBEDAE, 0.6f, 0.85f},  // + (beams) -> C-stick
    {0xE2F4B915, 0xB4EDE4B6, 0.6f, 0.85f},  // - (visors) -> D-pad
    // Stand-ins CStringTable::PortMarkTrilogyPrompts writes into the strings that mean the GC
    // disc's own D-pad (Visor) and R (Spider Ball), so the rules below leave them alone.
    {0x13AD51EA, 0x13AD51E9, 0.6f, 0.85f},  // D-pad down (Visor switch) -> D-pad down
    {0x60ACB986, 0xD89B9F7C, 1.0f, 1.0f},   // Z (Spider Ball) -> R
};

// The Wii's Z both scans/grapples and holds the Spider Ball; the GC disc says L for the first
// and R for the second. CStringTable::PortMarkTrilogyPrompts renames the Z of the Spider Ball
// strings (by STRG id and index, so it holds in every language) to 60ACB986, mapped above.

// Trilogy draws these GameCube actions with art that means another button here (#39). Each is
// told apart by the shape of its tag, never by its words, so every language and disc agrees.
const uint kPortWiiC = 0x4475EAD6;     // Nunchuk C: Morph Ball (X) elsewhere, the map legend Key (Y)
const uint kPortWiiStart = 0x83A73184; // Start: pause Exit, but the map world toggle (X)
const uint kPortGcXArt = 0xF1FA8D68;   // GC X art; Trilogy's "Press X for Logbook" is really Start
const uint kPortGcYArt = 0x3AA65ECD;   // GC Y art; on Trilogy the same id is the map's "Z" prompt
const uint kPortGcMapArt = 0xBC322C63; // GC art for the map prompt
const uint kPortWiiDpadDown = 0x13AD51E9;
const PortWiiPrompt kPortKeyPrompt = {kPortWiiC, kPortGcYArt, 1.0f, 1.0f};                // -> Y
const PortWiiPrompt kPortWorldPrompt = {kPortWiiStart, kPortGcXArt, 0.70f, 1.0f};         // -> X
const PortWiiPrompt kPortZoomPrompt = {kPortWiiDpadDown, kPortGcXArt, 0.70f, 1.0f};       // -> X
const PortWiiPrompt kPortMissilePrompt = {kPortWiiDpadDown, kPortGcYArt, 1.0f, 0.68f};   // -> Y
const PortWiiPrompt kPortMapPrompt = {kPortGcYArt, kPortGcMapArt, 1.0f, 0.68f};           // -> Z
const PortWiiPrompt kPortLogbookPrompt = {kPortGcXArt, kPortWiiStart, 1.0f, 1.0f};        // -> Start
bool sPortWorldText = false;
bool sPortZoomText = false;

// True when the string has no text outside its &...; tags and whitespace (a bare button prompt).
// The only such string with Start art is the map's world toggle, in every language.
bool PortIsBareTags(const wchar_t* str, int len) {
  for (int i = 0; len == -1 ? str[i] != 0 : i < len; ++i) {
    if (str[i] == L'&') {
      while ((len == -1 || i < len) && str[i] && str[i] != L';')
        ++i;
    } else if (str[i] > L' ') {
      return false;
    }
  }
  return true;
}

// True for the map legend's "Zoom" line: a Wii D-pad-down animation first in the string, then
// at most a short one-line word (ENGL "Zoom", JAPN "ズーム"). The same animation is Missile or Power
// Bomb in the longer sentences that mention it, which never start with it and a few words.
bool PortIsZoomText(const wchar_t* str, int len) {
  int i = 0;
  const auto at = [&](int pos) { return (len == -1 || pos < len) ? str[pos] : L'\0'; };
  while (at(i) == L'&' && !(at(i + 1) == L'i' && at(i + 2) == L'm')) {
    while (at(i) && at(i) != L';')
      ++i;
    if (at(i) == L';')
      ++i;
  }
  if (!(at(i) == L'&' && at(i + 1) == L'i' && at(i + 2) == L'm' && at(i + 6) == L'='))
    return false;
  bool dpadDown = false;
  int tagBegin = i;
  while (at(i) && at(i) != L';')
    ++i;
  for (int j = tagBegin; j + 8 <= i; ++j) {
    static const wchar_t kId[] = L"13AD51E9";
    int k = 0;
    while (k < 8 && str[j + k] == kId[k])
      ++k;
    dpadDown = dpadDown || k == 8;
  }
  if (!dpadDown || at(i) != L';')
    return false;
  int count = 0;
  for (++i; at(i); ++i) {
    if (at(i) == L'\n' && count > 0)
      return false;
    if (at(i) > L' ')
      ++count;
  }
  return count > 0 && count < 12;
}

const PortWiiPrompt* PortFindWiiPrompt(CAssetId id, const CVector2f* crop, bool bare) {
  if (!port::entry::IsTrilogy(port::entry::CurrentDiscVersion()))
    return nullptr;
  const uint raw = static_cast< uint >(id);
  if (raw == kPortGcXArt && crop != nullptr && crop->GetX() == 0.6875f)
    return &kPortLogbookPrompt;
  if (raw == kPortGcYArt && crop != nullptr && crop->GetX() == 0.6875f)
    return &kPortMapPrompt;
  // A bare Nunchuk C (no SI/SA tag) is only the map legend's Key; Morph Ball's C is an SI tag.
  if (raw == kPortWiiC && bare)
    return &kPortKeyPrompt;
  if (sPortWorldText && raw == kPortWiiStart)
    return &kPortWorldPrompt;
  for (const PortWiiPrompt& prompt : kPortWiiPrompts) {
    if (prompt.wiiId == raw)
      return &prompt;
  }
  return nullptr;
}

// The id to load for `id`, setting `crop` when it stands in for Wii art.
CAssetId PortGcPrompt(CAssetId id, CVector2f* crop, bool bare = false) {
  const PortWiiPrompt* prompt = PortFindWiiPrompt(id, crop, bare);
  if (prompt == nullptr)
    return id;
  if (crop != nullptr)
    *crop = CVector2f(prompt->cropX, prompt->cropY);
  return prompt->gcId;
}
} // namespace
#endif

CTextParser::CTextParser(IObjectStore& store) : mObjectStore(store) {}

void CTextParser::ParseText(CTextExecuteBuffer& buffer, const wchar_t* str, int len,
                            const rstl::vector< rstl::pair< CAssetId, CAssetId > >* textureMap) {
#ifdef TARGET_PC
  const bool portWasWorldText = sPortWorldText;
  const bool portWasZoomText = sPortZoomText;
  const bool portTrilogy = port::entry::IsTrilogy(port::entry::CurrentDiscVersion());
  sPortWorldText = portTrilogy && PortIsBareTags(str, len);
  sPortZoomText = portTrilogy && PortIsZoomText(str, len);
#endif
  int begin = 0;
  int end = 0;
  while (str[end] && (len == -1 || end < len)) {
    if (str[end] == L'&') {
      if ((len == -1 || end + 1 < len) && str[end + 1] != L'&') {
        if (end > begin) {
          buffer.AddString(str + begin, end - begin);
        }
        ++end;
        begin = end;
        while ((len == -1 || end < len) && str[end] && str[end] != L';') {
          ++end;
        }
        ParseTag(buffer, str + begin, end - begin, textureMap);
        begin = end + 1;
      } else {
        buffer.AddString(str + begin, end + 1 - begin);
        end += 2;
        begin = end;
      }
    } else {
      ++end;
    }
  }
  if (end > begin) {
    buffer.AddString(str + begin, end - begin);
  }
#ifdef TARGET_PC
  sPortWorldText = portWasWorldText;
  sPortZoomText = portWasZoomText;
#endif
}

CAssetId CTextParser::GetAssetIdFromString(
    const rstl::string& text, const rstl::vector< rstl::pair< CAssetId, CAssetId > >* textureMap) {
  const rstl::wstring str = CStringExtras::ConvertToUNICODE(text);
  const CAssetId id = (static_cast< uint >(GetColorValue(str.data())) << 24) |
                      (GetColorValue(str.data() + 2) << 16) | (GetColorValue(str.data() + 4) << 8) |
                      GetColorValue(str.data() + 6);
  if (textureMap) {
    typedef rstl::pair< CAssetId, CAssetId > AssetPair;
    rstl::vector< AssetPair >::const_iterator it = rstl::binary_find(
        textureMap->begin(), textureMap->end(), CAssetId(id),
        rstl::pair_sorter_finder< AssetPair, rstl::less< CAssetId > >(rstl::less< CAssetId >()));
    if (it != textureMap->end()) {
      return it->second;
    }
  }
  return id;
}

TToken< CRasterFont > CTextParser::GetFont(const wchar_t* str, int len) {
  const CAssetId id = (static_cast< uint >(GetColorValue(str)) << 24) |
                      (GetColorValue(str + 2) << 16) | (GetColorValue(str + 4) << 8) |
                      GetColorValue(str + 6);
  return mObjectStore.GetObj(SObjectTag('FONT', id));
}

CFontImageDef CTextParser::GetImage(const wchar_t* str, int len,
                                    const rstl::vector< rstl::pair< CAssetId, CAssetId > >* vec) {
  const rstl::string text = CStringExtras::ConvertToANSI(rstl::wstring(str, len));
  int commaCount = 0;
  int pos = 0;
  while (true) {
    pos = text.find(',', pos);
    if (pos == -1) {
      break;
    }
    ++commaCount;
    ++pos;
  }
  if (commaCount > 0) {
    const rstl::vector< rstl::string > tokens =
        CStringExtras::TokenizeString(text, ",", commaCount + 1);
    if (rstl::operator==(rstl::istring(tokens[0].c_str()), rstl::istring_l("A"))) {
      const float fps = atof(tokens[1].c_str());
      rstl::vector< TToken< CTexture > > textures;
      textures.reserve(tokens.size() - 2);
      for (int i = 2; i < tokens.size(); ++i) {
#ifdef TARGET_PC
        textures.push_back_unsafe(mObjectStore.GetObj(
            SObjectTag('TXTR', PortGcPrompt(GetAssetIdFromString(tokens[i], vec), nullptr))));
#else
        textures.push_back_unsafe(
            mObjectStore.GetObj(SObjectTag('TXTR', GetAssetIdFromString(tokens[i], vec))));
#endif
      }
      return CFontImageDef(textures, fps, CVector2f(1.f, 1.f));
    }
    if (rstl::operator==(rstl::istring(tokens[0].c_str()), rstl::istring_l("SA")) &&
        tokens.size() >= 5) {
      const float fps = atof(tokens[1].c_str());
      const float cropX = atof(tokens[2].c_str());
      const float cropY = atof(tokens[3].c_str());
#ifdef TARGET_PC
      CVector2f crop(cropX, cropY);
      // Wii D-pad-down animation: the map's Zoom (X) or a Missile/Power Bomb prompt (Y); the one
      // Visor-switch prompt that GC keeps as D-pad was renamed on load. Every frame becomes the one
      // GC button.
      const PortWiiPrompt* portWhole = nullptr;
      if (port::entry::IsTrilogy(port::entry::CurrentDiscVersion())) {
        for (int i = 4; i < tokens.size(); ++i) {
          if (static_cast< uint >(GetAssetIdFromString(tokens[i], vec)) == kPortWiiDpadDown)
            portWhole = sPortZoomText ? &kPortZoomPrompt : &kPortMissilePrompt;
        }
      }
      if (portWhole != nullptr)
        crop = CVector2f(portWhole->cropX, portWhole->cropY);
#endif
      rstl::vector< TToken< CTexture > > textures;
      textures.reserve(tokens.size() - 4);
      for (int i = 4; i < tokens.size(); ++i) {
#ifdef TARGET_PC
        textures.push_back_unsafe(mObjectStore.GetObj(SObjectTag(
            'TXTR', portWhole != nullptr
                        ? portWhole->gcId
                        : PortGcPrompt(GetAssetIdFromString(tokens[i], vec), &crop))));
#else
        textures.push_back_unsafe(
            mObjectStore.GetObj(SObjectTag('TXTR', GetAssetIdFromString(tokens[i], vec))));
#endif
      }
#ifdef TARGET_PC
      return CFontImageDef(textures, fps, crop);
#else
      return CFontImageDef(textures, fps, CVector2f(cropX, cropY));
#endif
    }
    if (rstl::operator==(rstl::istring(tokens[0].c_str()), rstl::istring_l("SI")) &&
        tokens.size() == 4) {
      const float cropX = atof(tokens[1].c_str());
      const float cropY = atof(tokens[2].c_str());
#ifdef TARGET_PC
      CVector2f crop(cropX, cropY);
      return CFontImageDef(mObjectStore.GetObj(SObjectTag(
                               'TXTR', PortGcPrompt(GetAssetIdFromString(tokens[3], vec), &crop))),
                           crop);
#else
      return CFontImageDef(
          mObjectStore.GetObj(SObjectTag('TXTR', GetAssetIdFromString(tokens[3], vec))),
          CVector2f(cropX, cropY));
#endif
    }
  }
#ifdef TARGET_PC
  CVector2f crop(1.f, 1.f);
  return CFontImageDef(
      mObjectStore.GetObj(SObjectTag('TXTR', PortGcPrompt(GetAssetIdFromString(text, vec), &crop, true))),
      crop);
#else
  return CFontImageDef(mObjectStore.GetObj(SObjectTag('TXTR', GetAssetIdFromString(text, vec))),
                       CVector2f(1.f, 1.f));
#endif
}

uint CTextParser::HandleUserTag(CTextExecuteBuffer& buffer, const wchar_t* str, int len) {
  return 0;
}

void CTextParser::ParseTag(CTextExecuteBuffer& buffer, const wchar_t* string, int len,
                           const rstl::vector< rstl::pair< CAssetId, CAssetId > >* vec) {
  if (BeginsWith(string, len, L"font=")) {
    TToken< CRasterFont > font = GetFont(string + 5, len - 5);
    buffer.AddFont(font);
  } else if (BeginsWith(string, len, L"image=")) {
    CFontImageDef texture = GetImage(string + 6, len - 6, vec);
    buffer.AddImage(texture);
  } else if (BeginsWith(string, len, L"fg-color=")) {
    buffer.AddColor(kCT_Foreground, ParseColor(string + 9, len - 9));
  } else if (BeginsWith(string, len, L"main-color=")) {
    buffer.AddColor(kCT_Main, ParseColor(string + 11, len - 11));
  } else if (BeginsWith(string, len, L"geometry-color=")) {
    buffer.AddColor(kCT_Geometry, ParseColor(string + 11, len - 11));
  } else if (BeginsWith(string, len, L"outline-color=")) {
    buffer.AddColor(kCT_Outline, ParseColor(string + 14, len - 14));
  } else if (BeginsWith(string, len, L"color")) {
    int idx = string[6] - L'0';
    if (idx < 0 || idx > 9) {
      return;
    }
    const wchar_t* str_remain = string + 7;
    len -= 7;
    if (*str_remain >= L'0' && *str_remain <= L'9') {
      wchar_t tmp = *str_remain;
      ++str_remain;
      len--;
      idx = (idx * 10) + (tmp - L'0');
    }
    if (Equals(str_remain + 10, len - 10, L"no")) {
      buffer.AddRemoveColorOverride(idx);
    } else {
      buffer.AddColorOverride(idx, ParseColor(str_remain + 10, len - 10));
    }
  } else if (BeginsWith(string, len, L"line-spacing=")) {
    const float v = (float)ParseInt(string + 13, len - 13, true);
    buffer.AddLineSpacing(v / 100.f);
  } else if (BeginsWith(string, len, L"line-extra-space=")) {
    buffer.AddLineExtraSpace(ParseInt(string + 17, len - 17, true));
  } else if (BeginsWith(string, len, L"character-extra-space=")) {
    buffer.AddCharacterExtraSpace(ParseInt(string + 22, len - 22, true));
  } else if (BeginsWith(string, len, L"just=")) {
    if (Equals(string + 5, len - 5, L"left")) {
      buffer.AddJustification(kJustification_Left);
    } else if (Equals(string + 5, len - 5, L"center")) {
      buffer.AddJustification(kJustification_Center);
    } else if (Equals(string + 5, len - 5, L"right")) {
      buffer.AddJustification(kJustification_Right);
    } else if (Equals(string + 5, len - 5, L"full")) {
      buffer.AddJustification(kJustification_Full);
    } else if (Equals(string + 5, len - 5, L"nleft")) {
      buffer.AddJustification(kJustification_NLeft);
    } else if (Equals(string + 5, len - 5, L"ncenter")) {
      buffer.AddJustification(kJustification_NCenter);
    } else if (Equals(string + 5, len - 5, L"nright")) {
      buffer.AddJustification(kJustification_NRight);
    }
  } else if (BeginsWith(string, len, L"vjust=")) {
    if (Equals(string + 6, len - 6, L"top")) {
      buffer.AddVerticalJustification(kVerticalJustification_Top);
    } else if (Equals(string + 6, len - 6, L"center")) {
      buffer.AddVerticalJustification(kVerticalJustification_Center);
    } else if (Equals(string + 6, len - 6, L"bottom")) {
      buffer.AddVerticalJustification(kVerticalJustification_Bottom);
    } else if (Equals(string + 6, len - 6, L"full")) {
      buffer.AddVerticalJustification(kVerticalJustification_Full);
    } else if (Equals(string + 6, len - 6, L"ntop")) {
      buffer.AddVerticalJustification(kVerticalJustification_NTop);
    } else if (Equals(string + 6, len - 6, L"ncenter")) {
      buffer.AddVerticalJustification(kVerticalJustification_NCenter);
    } else if (Equals(string + 6, len - 6, L"nbottom")) {
      buffer.AddVerticalJustification(kVerticalJustification_NBottom);
    }
  } else if (Equals(string, len, L"push")) {
    buffer.AddPushState();
  } else if (Equals(string, len, L"pop")) {
    buffer.AddPopState();
  } else {
    HandleUserTag(buffer, string, len);
  }
}

bool CTextParser::BeginsWith(const wchar_t* str, int len, const wchar_t* prefix) {
  int i = 0;
  for (; prefix[i] && i < len; ++i) {
    if (str[i] != prefix[i]) {
      return false;
    }
  }
  return prefix[i] == L'\0';
}

bool CTextParser::Equals(const wchar_t* str, int len, const wchar_t* other) {
  int i = 0;
  for (; other[i] && i < len; ++i) {
    if (str[i] != other[i]) {
      return false;
    }
  }
  return other[i] == L'\0';
}

int CTextParser::ParseInt(const wchar_t* str, int len, bool allowSign) {
  bool negative = false;
  int pos = 0;
  if (allowSign && len > 0 && str[0] == L'-') {
    negative = true;
    pos = 1;
  }

  int value = 0;
  for (; pos < len; ++pos) {
    value = value * 10 + (str[pos] - L'0');
  }
  return negative ? -value : value;
}

int CTextParser::FromHex(wchar_t c) {
  if (c >= L'0' && c <= L'9') {
    return c - L'0';
  }
  if (c >= L'A' && c <= L'F') {
    return c - L'A' + 10;
  }
  if (c >= L'a' && c <= L'f') {
    return c - L'a' + 10;
  }
  return 0;
}

int CTextParser::GetColorValue(const wchar_t* str) {
  return (FromHex(str[0]) * 16) + FromHex(str[1]);
}

CTextColor CTextParser::ParseColor(const wchar_t* str, int len) {
  const int r = GetColorValue(str + 1);
  const int g = GetColorValue(str + 3);
  const int b = GetColorValue(str + 5);
  const int a = len == 9 ? GetColorValue(str + 7) : 255;
  return CTextColor(r, g, b, a);
}

// Guessed: an unreferenced helper (dead-stripped from the DOL) whose format string still leads
// this TU's string pool; it is the inverse of GetAssetIdFromString.
static void FormatAssetId(char* out, CAssetId id) {
  sprintf(out, "%02x%02x%02x%02x", id >> 24, (id >> 16) & 0xff, (id >> 8) & 0xff, id & 0xff);
}
