#pragma once
// The sound device of Gizmos & Gadgets as the engine names it (re/gg/docs/PORTING_NOTES.md §7,
// code_sound.md §3.1): the original's -S switches and the engine's sound option map onto it.
//   off      -SN / -S- / -S: the FM banks played silently (Snd_Silence)
//   speaker  -SI: the speaker banks on the PC speaker
//   fm       -SA / -SB / -SM, or no switch with an AdLib (the emulated OPL2)

#include <cstdint>
#include <optional>
#include <string_view>

namespace tmport::gg {

enum class SoundMode : uint8_t { Off, Speaker, Fm };

std::string_view soundModeName(SoundMode m);  // "off", "speaker", "fm"
// "off" / "speaker" / "fm", or an original switch: -SN, -S, -S- (off), -SI (speaker), -SA, -SB, -SM
// (fm), also with '/' and in any case; only the first three characters count (-SBLASTER = -SB).
std::optional<SoundMode> parseSoundMode(std::string_view s);

}  // namespace tmport::gg
