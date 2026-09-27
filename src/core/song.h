#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace y8 {

enum class Format { Opll, Musica };

// What a channel drives. The two drivers read the same commands for the three
// melody parts; only the rhythm part has a grammar of its own.
enum class Part { Fm, Rhythm, Psg, Scc };

// One command of the source, decoded. Lengths are in the drivers' own unit,
// one timer interrupt (1/60 s).
struct SrcEvent {
    enum Kind : std::uint8_t {
        Note,          // value: 1-95, 01h being O1C; length
        Rest,          // length
        Wait,          // MuSICA 8Dh: time passes and nothing else happens; length
        Volume,        // value: the low nibble of 60h-6Fh
        Instrument,    // value: the low nibble of 70h-7Fh
        Sustain,       // value: 0 or 1
        RomVoice,      // OPLLDRV 82h; value: 0-63
        UserVoice,     // 83h; addr: where the record is
        Legato,        // value: 0 or 1
        Quantize,      // value: 0-8
        Detune,        // MuSICA 87h; value
        Portamento,    // MuSICA 88h; value
        Vibrato,       // MuSICA 89h; value
        LfoSpeed,      // MuSICA 8Bh; value
        RegWrite,      // value: register, value2: data
        RhythmHit,     // value: the BSTCH bits; length
        RhythmVolume,  // value: the BSTCH bits, value2: attenuation 0-15
        Ignored,       // MuSICA 82h and 8Ah: read and passed over by the driver
    };

    Kind kind = Rest;
    int value = 0;
    int value2 = 0;
    int length = 0;
    std::uint16_t addr = 0;
    std::uint16_t at = 0;  // where in the source the command is, for messages
};

// A run of commands ending in FFh. OPLLDRV has one per channel; MuSICA shares
// them between the channels' sequences.
struct Block {
    std::uint16_t addr = 0;
    std::vector<SrcEvent> events;
};

struct SeqStep {
    int block = 0;  // index into Song::blocks
    int count = 1;  // 1-256
};

struct Channel {
    int number = 0;  // the source's own: 1-9 for OPLLDRV (0 for rhythm), 1-17 for MuSICA
    Part part = Part::Fm;
    int partIndex = 0;  // which FM, PSG or SCC channel, from 0
    std::vector<SeqStep> steps;
};

struct Song {
    Format format = Format::Opll;
    bool rhythmMode = false;  // FM melody 6 and rhythm, rather than melody 9
    std::vector<Block> blocks;
    std::vector<Channel> channels;

    // The data as the driver sees it: a 64KB address space with the file in
    // its place. Absolute addresses in the data are read through this.
    std::vector<std::uint8_t> memory;
    std::uint32_t loadStart = 0;
    std::uint32_t loadEnd = 0;  // one past the last byte
    bool baseKnown = false;

    bool readable(std::uint32_t addr, std::uint32_t n) const {
        return addr >= loadStart && addr + n <= loadEnd;
    }
    std::uint8_t at(std::uint32_t addr) const { return memory[addr & 0xFFFF]; }
};

std::string channelName(const Song& song, const Channel& ch);

} // namespace y8
