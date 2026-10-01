#include "source.h"

#include <map>
#include <utility>

namespace y8 {
namespace {

constexpr std::uint8_t kBsaveMark = 0xFE;
constexpr std::size_t kBsaveHeader = 7;

constexpr std::uint16_t kOpllRhythmHeader = 0x000E;  // rhythm, then FM 1-6
constexpr std::uint16_t kOpllMelodyHeader = 0x0012;  // FM 1-9

constexpr int kMusicaChannels = 17;
constexpr std::size_t kMusicaHeader = 1 + kMusicaChannels * 2;

constexpr std::uint8_t kEnd = 0xFF;

// Guards against data that never reaches its FFh: a block this long is no
// block, and nothing a Y8960 track can hold comes near it.
constexpr std::size_t kMaxEvents = 65536;

std::string hex4(std::uint32_t v) {
    static const char* digits = "0123456789ABCDEF";
    std::string s = "0000h";
    for (int i = 3; i >= 0; --i) {
        s[static_cast<std::size_t>(i)] = digits[v & 0xF];
        v >>= 4;
    }
    return s;
}

class Reader {
public:
    Reader(Song& song, const std::string& name, Diagnostics& diag)
        : song_(song), name_(name), diag_(diag) {}

    bool byte(std::uint32_t& p, int& out) {
        if (!song_.readable(p, 1)) {
            diag_.error(name_, 0, 0, "the data runs out at " + hex4(p));
            return false;
        }
        out = song_.at(p++);
        return true;
    }

    bool word(std::uint32_t& p, int& out) {
        int lo = 0, hi = 0;
        if (!byte(p, lo) || !byte(p, hi)) return false;
        out = lo | (hi << 8);
        return true;
    }

    // Both drivers add another byte to a length for as long as the byte is FFh.
    bool length(std::uint32_t& p, int& out) {
        out = 0;
        for (;;) {
            int b = 0;
            if (!byte(p, b)) return false;
            out += b;
            if (b != 0xFF) return true;
        }
    }

    bool block(std::uint32_t addr, bool rhythm, Block& out) {
        out.addr = static_cast<std::uint16_t>(addr);
        std::uint32_t p = addr;
        const bool musica = song_.format == Format::Musica;
        while (out.events.size() < kMaxEvents) {
            SrcEvent e;
            e.at = static_cast<std::uint16_t>(p);
            int c = 0;
            if (!byte(p, c)) return false;
            if (c == kEnd) return true;
            if (rhythm ? !rhythmEvent(p, c, musica, e) : !melodyEvent(p, c, musica, e)) return false;
            out.events.push_back(e);
        }
        diag_.error(name_, 0, 0, "the block at " + hex4(addr) + " never reaches its end");
        return false;
    }

private:
    bool unknown(std::uint16_t at, int c) {
        static const char* digits = "0123456789ABCDEF";
        std::string code = {digits[(c >> 4) & 0xF], digits[c & 0xF], 'h'};
        diag_.error(name_, 0, 0, "unknown command " + code + " at " + hex4(at));
        return false;
    }

    bool melodyEvent(std::uint32_t& p, int c, bool musica, SrcEvent& e) {
        if (c < 0x60) {
            e.kind = (c == 0) ? SrcEvent::Rest : SrcEvent::Note;
            e.value = c;
            return length(p, e.length);
        }
        if (c < 0x70) {
            e.kind = SrcEvent::Volume;
            e.value = c & 0x0F;
            return true;
        }
        if (c < 0x80) {
            e.kind = SrcEvent::Instrument;
            e.value = c & 0x0F;
            return true;
        }
        switch (c) {
        case 0x80:
        case 0x81:
            e.kind = SrcEvent::Sustain;
            // FM-BIOS's manual has 80h release the sustain and 81h set it, but
            // its OPLDRV does the opposite (int_sus_on at 80h), and that is
            // what played.
            e.value = musica ? (c & 1) : (c == 0x80);
            return true;
        case 0x82:
            // MuSICA's driver reads the byte and does nothing with it.
            e.kind = musica ? SrcEvent::Ignored : SrcEvent::RomVoice;
            return byte(p, e.value);
        case 0x83: {
            e.kind = SrcEvent::UserVoice;
            int a = 0;
            if (!word(p, a)) return false;
            e.addr = static_cast<std::uint16_t>(a);
            return true;
        }
        case 0x84:
        case 0x85:
            e.kind = SrcEvent::Legato;
            e.value = c & 1;
            return true;
        case 0x86:
            e.kind = SrcEvent::Quantize;
            return byte(p, e.value);
        default:
            break;
        }
        if (!musica) return unknown(e.at, c);
        switch (c) {
        case 0x87: e.kind = SrcEvent::Detune; return byte(p, e.value);
        case 0x88: e.kind = SrcEvent::Portamento; return byte(p, e.value);
        case 0x89: e.kind = SrcEvent::Vibrato; return byte(p, e.value);
        case 0x8A: e.kind = SrcEvent::Ignored; return true;  // no operand, no effect
        case 0x8B: e.kind = SrcEvent::LfoSpeed; return byte(p, e.value);
        case 0x8C:
            e.kind = SrcEvent::RegWrite;
            return byte(p, e.value) && byte(p, e.value2);
        case 0x8D:
            e.kind = SrcEvent::Wait;
            return length(p, e.length);
        default:
            return unknown(e.at, c);
        }
    }

    // V01BSTCH: V clear strikes and a length follows, V set sets a volume and
    // one byte follows. MuSICA adds C0h, a register write.
    bool rhythmEvent(std::uint32_t& p, int c, bool musica, SrcEvent& e) {
        if ((c & 0xE0) == 0x20) {
            e.kind = SrcEvent::RhythmHit;
            e.value = c & 0x1F;
            return length(p, e.length);
        }
        if ((c & 0xE0) == 0xA0) {
            e.kind = SrcEvent::RhythmVolume;
            e.value = c & 0x1F;
            if (!byte(p, e.value2)) return false;
            e.value2 &= 0x0F;
            return true;
        }
        if (musica && c == 0xC0) {
            e.kind = SrcEvent::RegWrite;
            return byte(p, e.value) && byte(p, e.value2);
        }
        // BGM.BIN reads a length after C1h and strikes nothing (D4A8h).
        if (musica && c == 0xC1) {
            e.kind = SrcEvent::Wait;
            return length(p, e.length);
        }
        return unknown(e.at, c);
    }

    Song& song_;
    const std::string& name_;
    Diagnostics& diag_;
};

bool looksLikeOpll(const std::vector<std::uint8_t>& body) {
    if (body.size() < 2) return false;
    const unsigned w = body[0] | (body[1] << 8);
    return w == kOpllRhythmHeader || w == kOpllMelodyHeader;
}

bool looksLikeMusica(const std::vector<std::uint8_t>& body, bool baseKnown, std::uint32_t start) {
    if (body.size() < kMusicaHeader || body[0] > 1) return false;
    if (!baseKnown) return true;
    for (int ch = 0; ch < kMusicaChannels; ++ch) {
        const std::uint32_t a = body[1 + ch * 2] | (body[2 + ch * 2] << 8);
        if (a != 0 && (a < start || a >= start + body.size())) return false;
    }
    return true;
}

bool loadOpll(Song& song, const std::string& name, Diagnostics& diag) {
    Reader reader(song, name, diag);
    const std::uint32_t top = song.loadStart;
    int first = 0;
    std::uint32_t p = top;
    if (!reader.word(p, first)) return false;
    song.rhythmMode = first == kOpllRhythmHeader;

    // The header holds offsets from its own first byte, not addresses: only a
    // user voice (83h) names an address.
    const int entries = song.rhythmMode ? 7 : 9;
    std::map<std::uint32_t, int> seen;
    for (int i = 0; i < entries; ++i) {
        std::uint32_t q = top + static_cast<std::uint32_t>(i * 2);
        int offset = 0;
        if (!reader.word(q, offset)) return false;
        // 0 is a channel the song does not use: R-TYPE's songs leave them so,
        // and uniskie's OPLDRV_tool reads and writes them the same way.
        if (offset == 0) continue;
        Channel ch;
        const bool rhythm = song.rhythmMode && i == 0;
        ch.part = rhythm ? Part::Rhythm : Part::Fm;
        ch.number = rhythm ? 0 : (song.rhythmMode ? i : i + 1);
        ch.partIndex = rhythm ? 0 : ch.number - 1;
        const std::uint32_t addr = top + static_cast<std::uint32_t>(offset);
        Block block;
        if (!reader.block(addr, rhythm, block)) return false;
        song.blocks.push_back(std::move(block));
        ch.steps.push_back({static_cast<int>(song.blocks.size()) - 1, 1});
        song.channels.push_back(std::move(ch));
    }
    return true;
}

bool loadMusica(Song& song, const std::string& name, Diagnostics& diag) {
    Reader reader(song, name, diag);
    std::uint32_t p = song.loadStart;
    int mode = 0;
    if (!reader.byte(p, mode)) return false;
    song.rhythmMode = mode == 0;

    std::map<std::pair<std::uint32_t, bool>, int> blocks;
    bool zeroCount = false;
    for (int i = 0; i < kMusicaChannels; ++i) {
        int seq = 0;
        if (!reader.word(p, seq)) return false;
        if (seq == 0) continue;

        Channel ch;
        ch.number = i + 1;
        if (i < 9) {
            const bool rhythm = song.rhythmMode && i == 6;
            if (song.rhythmMode && i > 6) {
                diag.error(name, 0, 0,
                           "channel " + std::to_string(i + 1) +
                               " has data, but melody 6 and rhythm leaves channels 8 and 9 unused");
                return false;
            }
            ch.part = rhythm ? Part::Rhythm : Part::Fm;
            ch.partIndex = rhythm ? 0 : i;
        } else if (i < 12) {
            ch.part = Part::Psg;
            ch.partIndex = i - 9;
        } else {
            ch.part = Part::Scc;
            ch.partIndex = i - 12;
        }

        std::uint32_t q = static_cast<std::uint32_t>(seq);
        for (;;) {
            int addr = 0;
            if (!reader.word(q, addr)) return false;
            if (addr == 0) break;
            int count = 0;
            if (!reader.byte(q, count)) return false;
            if (count == 0) {
                // The manual gives 1 as the least. A counter that is decremented
                // before it is tested plays 0 as 256.
                zeroCount = true;
                count = 256;
            }
            const bool rhythm = ch.part == Part::Rhythm;
            auto key = std::make_pair(static_cast<std::uint32_t>(addr), rhythm);
            auto it = blocks.find(key);
            int index = 0;
            if (it == blocks.end()) {
                Block block;
                if (!reader.block(static_cast<std::uint32_t>(addr), rhythm, block)) return false;
                song.blocks.push_back(std::move(block));
                index = static_cast<int>(song.blocks.size()) - 1;
                blocks.emplace(key, index);
            } else {
                index = it->second;
            }
            ch.steps.push_back({index, count});
            if (ch.steps.size() > kMaxEvents) {
                diag.error(name, 0, 0, "the sequence of channel " + std::to_string(i + 1) +
                                           " never reaches its end");
                return false;
            }
        }
        song.channels.push_back(std::move(ch));
    }
    if (zeroCount) {
        diag.warning(name, 0, 0, "a block is to be played 0 times; it was played 256 times");
    }
    return true;
}

} // namespace

std::string channelName(const Song& song, const Channel& ch) {
    (void)song;
    switch (ch.part) {
    case Part::Fm: return "FM" + std::to_string(ch.partIndex + 1);
    case Part::Rhythm: return "RHY";
    case Part::Psg: return "PSG" + std::to_string(ch.partIndex + 1);
    case Part::Scc: return "SCC" + std::to_string(ch.partIndex + 1);
    }
    return "?";
}

bool loadSong(const std::vector<std::uint8_t>& file, const std::string& name,
              const LoadOptions& options, Song& song, Diagnostics& diag) {
    std::vector<std::uint8_t> body;
    bool baseKnown = false;
    std::uint32_t start = 0;

    if (!file.empty() && file[0] == kBsaveMark) {
        if (file.size() < kBsaveHeader) {
            diag.error(name, 0, 0, "the BSAVE header is cut short");
            return false;
        }
        start = file[1] | (file[2] << 8);
        const std::uint32_t end = file[3] | (file[4] << 8);
        if (end < start) {
            diag.error(name, 0, 0, "the BSAVE header ends before it starts");
            return false;
        }
        body.assign(file.begin() + kBsaveHeader, file.end());
        const std::size_t declared = end - start + 1;
        if (body.size() > declared) body.resize(declared);
        baseKnown = true;
        if (options.base && *options.base != start) {
            diag.warning(name, 0, 0, "--base is ignored: the BSAVE header places the data at " +
                                         hex4(start));
        }
    } else {
        body = file;
        if (options.base) {
            start = *options.base;
            baseKnown = true;
        }
    }
    if (start + body.size() > 0x10000) {
        diag.error(name, 0, 0, "the data does not fit below 10000h from " + hex4(start));
        return false;
    }

    Format format;
    if (options.format) {
        format = *options.format;
    } else if (looksLikeOpll(body)) {
        format = Format::Opll;
    } else if (looksLikeMusica(body, baseKnown, start)) {
        format = Format::Musica;
    } else {
        diag.error(name, 0, 0, "neither OPLLDRV nor MuSICA data; --format names it");
        return false;
    }
    if (format == Format::Musica && !baseKnown) {
        diag.error(name, 0, 0,
                   "MuSICA data names absolute addresses; without a BSAVE header, --base "
                   "has to say where it was loaded");
        return false;
    }

    song = Song();
    song.format = format;
    song.memory.assign(0x10000, 0);
    std::copy(body.begin(), body.end(), song.memory.begin() + start);
    song.loadStart = start;
    song.loadEnd = start + static_cast<std::uint32_t>(body.size());
    song.baseKnown = baseKnown;

    return format == Format::Opll ? loadOpll(song, name, diag) : loadMusica(song, name, diag);
}

} // namespace y8
