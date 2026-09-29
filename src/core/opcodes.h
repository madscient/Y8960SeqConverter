#pragma once

#include <cstdint>

namespace y8 {

// The event opcodes of bytecode.md. The top bits say how many bytes follow, so
// a reader can step over an event it does not know; nothing here may be
// renumbered without that table moving too.
enum Op : std::uint8_t {
    // common
    OpNote = 0x00,      // 00-0B, c c+ d d+ e f f+ g g+ a a+ b, then a length
    OpRest = 0x0C,      // then a length
    OpWait = 0x0E,      // then a length
    OpNoteDown = 0x0F,  // c-, the b of the octave below; then a length
    OpNoteUp = 0x10,    // b+, the c of the octave above; then a length
    OpOctUp = 0x40,
    OpOctDown = 0x41,
    OpLoopStart = 0x42,
    OpDaCapo = 0x43,
    OpCoda = 0x44,
    OpTie = 0x45,
    OpOctave = 0x80,
    OpVolume = 0x81,
    OpVoice = 0x82,     // the number the chip resolves by itself
    OpQuantize = 0x83,
    OpTempo = 0x84,
    OpSeqVoice = 0x85,  // the slot in this sequence's own voice set
    OpSegno = 0x86,     // a mark with its number; nothing plays it
    OpNoteAbs = 0xC0,   // note number, then a length
    OpBend = 0xD0,
    OpBendRel = 0xD1,
    OpPorta = 0xD2,
    OpBlockEnd = 0xD3,
    OpFine = 0xD4,
    OpDalSegno = 0xD5,
    OpRegWrite = 0xE0,
    OpBlockStart = 0xE1,
    OpLoopEnd = 0xE2,
    OpToCoda = 0xF0,
    OpEnd = 0xFF,

    // FM family only
    OpRhythmAccent = 0xA8,
    OpRhythmVolume = 0xA9,
    OpRhythmAccentVol = 0xAA,
    OpRhythmHit = 0xC8,  // the instrument bitmap, then a length
    OpRhythmInstVolume = 0xD8,  // the instrument bitmap, then their plain volume

    // PSG family only
    OpSsgShape = 0xB0,
    OpSsgPan = 0xB1,
    OpSoftEnv = 0xB2,   // the software envelope's number, 0 for none; SSGS, SCC, DCSG
    OpSsgPeriod = 0xDC,  // 16 bits
};

// ~ with no number: start from whatever is sounding. Chosen outside the range
// cents can reach.
constexpr std::uint16_t kPortaFromHere = 0x8000;

constexpr int kTicksQuarter = 48;
constexpr int kTicksWhole = kTicksQuarter * 4;
constexpr int kLengthMax = 96;      // the denominator of Ln and a note's own
constexpr int kNoteNumberMax = 96;  // Nn
constexpr int kCentMax = 1200;
constexpr int kLoopDepth = 4;
constexpr int kSegnoMax = 4;    // (*)0 to (*)3
constexpr int kMarkMax = 8;     // (TC) and (FINE) share the track's counters
constexpr int kXChainMax = 8;   // how deep Xx; may nest
constexpr int kVoiceSlots = 32; // the sequence's own voice set
constexpr int kEnvelopeMax = 31; // software envelopes 1-31, chunk 04
constexpr int kTrackBytesMax = 2048;  // the event stream, the FF included

// The rhythm instrument bits, the layout both chips' rhythm registers use.
constexpr std::uint8_t kRhythmBass = 0x10;
constexpr std::uint8_t kRhythmSnare = 0x08;
constexpr std::uint8_t kRhythmTom = 0x04;
constexpr std::uint8_t kRhythmCymbal = 0x02;
constexpr std::uint8_t kRhythmHiHat = 0x01;

} // namespace y8
