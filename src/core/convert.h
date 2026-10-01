#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "diag.h"
#include "song.h"
#include "voicedata.h"

namespace y8 {

struct ConvertOptions {
    // Channel names (channelName) to leave out.
    std::vector<std::string> drop;
    // 64 voices of 8 bytes, OPLL registers 00h-07h, read out of the machine's
    // own ROM. Empty: OPLLDRV's 82h takes the Y8960 preset of the same number.
    std::vector<std::uint8_t> romVoices;
};

struct Track {
    int number = 0;   // 0-15
    int device = 0;   // bytecode.md's device number
    int channel = 0;
    std::string name; // the source channel, for messages
    std::vector<std::uint8_t> bytes;  // the events, FFh included
};

struct VoiceSlot {
    bool isWave = false;  // chunk 02 rather than 01
    std::vector<std::uint8_t> record;  // a PackedVoice, or a waveform's 32 bytes
};

// Chunk 04: each rate a byte, the frames per step in the high nibble and the
// step in the low, both 1-15; the level 0-15.
struct Envelope {
    int ar = 0, dr = 0, sl = 0, rr = 0;
    bool operator==(const Envelope& o) const {
        return ar == o.ar && dr == o.dr && sl == o.sl && rr == o.rr;
    }
};

struct Sequence {
    std::vector<Track> tracks;
    std::vector<VoiceSlot> voices;    // the sequence's voice set; the index is what 85h names
    std::vector<Envelope> envelopes;  // number 1 first; B2h names index + 1
};

constexpr int kRomVoiceTableSize = 64 * 8;

// Writes nothing into `seq` it would have to take back: on false, the
// diagnostics say why.
bool convert(const Song& song, const ConvertOptions& options, const std::string& name,
             Sequence& seq, Diagnostics& diag);

// A MuSICA envelope rate byte (the counter in the high nibble, the step in the
// low) as chunk 04 takes it. MuSICA's counter 0 (256 interrupts) and step 0
// (never moves) have no place there; for those `exact` is false and the byte
// nearest in speed is taken.
int envelopeRate(std::uint8_t musica, bool& exact);

// The chunk 01 record for the eight OPLL user voice registers: the inverse of
// the ROM's OPLL_SETUSER.
PackedVoice recordFromOpll(const std::uint8_t* opll);

} // namespace y8
