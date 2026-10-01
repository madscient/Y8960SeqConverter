#pragma once

// The records a sequence has to carry for the voices it names. See the rights
// section of README.md before redistributing: the FM presets are not this
// project's own work.

#include <array>
#include <cstdint>
#include <cstring>

namespace y8 {

constexpr int kVoiceRecordSize = 32;
constexpr int kPackedVoiceSize = 12;   // chunk 01: an FM voice as a block carries it
constexpr int kPresetVoiceCount = 64;  // @0-@63 on OPLLEX and OPL2EX
constexpr int kPresetWaveCount = 16;   // @0-@15 on the SCC

// An SCC waveform, or an FM voice in the MSX-AUDIO layout the preset table is
// written in.
using VoiceRecord = std::array<std::uint8_t, kVoiceRecordSize>;
// An FM voice in the register image of bytecode.md's chunk 01.
using PackedVoice = std::array<std::uint8_t, kPackedVoiceSize>;

VoiceRecord presetVoice(int n);
VoiceRecord presetWave(int n);

// The ROM's TAB_VOIPACK: the name, the spare bytes, the velocity byte and
// MSX-AUDIO's flags are dropped, and the transpose rounds to whole semitones.
PackedVoice packVoice(const VoiceRecord& basic);

} // namespace y8
