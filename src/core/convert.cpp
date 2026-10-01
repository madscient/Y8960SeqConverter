#include "convert.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>

#include "opcodes.h"

namespace y8 {
namespace {

constexpr int kDevSSGS = 0;
constexpr int kDevOPLLEX1 = 1;
constexpr int kDevSCC = 7;
constexpr int kRhythmChannel = 10;
constexpr int kTrackCount = 16;

// One frame of the source is one tick at this tempo: 60 / (75 * 48) s = 1/60 s.
constexpr std::uint8_t kFrameTempo = 75;

// The longest length one event carries. The readers take 15 bits, but they add
// the length up Q times into 16 bits to find the gate: 8191 * 8 still fits.
constexpr int kLengthMax = 8191;

// A source note n (01h being O1C) as a Y8960 note number, the same pitch in
// Hz. Y8960's O4 is C4 on every chip. MuSICA's PSG and SCC are too, but its FM
// plays O4 on block 3, an octave below (BGM.BIN's note code and F-Number
// table). 推測 for OPLLDRV: taken to be MuSICA's, as FM-BIOS was not read.
constexpr int kNoteOffsetFm = -1;
constexpr int kNoteOffsetDivider = 11;

// The Y8960 running state each track starts every round from (bytecode.md,
// "走行状態").
constexpr int kInitOctave = 4;
constexpr int kInitLoudness = 99;
constexpr int kInitQuant = 8;

// What the drivers start a channel with. MuSICA's are the manual's: volume
// 60h, instrument 7Ah, Q 8. OPLLDRV's MSTART leaves the instrument and the
// volume as the chip has them; these are what FM-BIOS's INIOPL puts there
// (instrument 11, attenuation 3), and its rhythm_ini gives every rhythm
// instrument attenuation 3.
constexpr int kMusicaFmVolume = 0;   // attenuation
constexpr int kOpllFmVolume = 3;
constexpr int kMusicaInstrument = 10;
constexpr int kOpllInstrument = 11;
// 推測 for MuSICA: the rhythm's starting volume is not in its manual.
constexpr int kMusicaRhythmAtt = 0;
constexpr int kOpllRhythmAtt = 3;

// The registers OPLLEX takes a write to (basic-reference.md, `Y`).
bool opllexRegister(int r) {
    return r <= 0x07 || (r >= 0x0E && r <= 0x18) || (r >= 0x20 && r <= 0x28) ||
           (r >= 0x30 && r <= 0x38);
}

std::string hex4(unsigned v) {
    static const char* digits = "0123456789ABCDEF";
    std::string s = "0000h";
    for (int i = 3; i >= 0; --i) {
        s[static_cast<std::size_t>(i)] = digits[v & 0xF];
        v >>= 4;
    }
    return s;
}

std::string hex2(unsigned v) {
    static const char* digits = "0123456789ABCDEF";
    return std::string{digits[(v >> 4) & 0xF], digits[v & 0xF], 'h'};
}

// A source level 0-15 as Y8960's volume, where V n is 4n + 67: every reader
// of devices 0-7 takes it back to the same level.
int loudness(int level) { return level * 4 + 67; }

struct Shared {
    const Song& song;
    const ConvertOptions& options;
    const std::string& name;
    Diagnostics& diag;
    std::vector<VoiceSlot> voices;
    std::vector<Envelope> envelopes;
    std::set<std::string> said;  // each message once, however often a trial run repeats it
    bool failed = false;

    void error(const std::string& msg) {
        if (said.insert("E" + msg).second) diag.error(name, 0, 0, msg);
        failed = true;
    }
    void warning(const std::string& msg) {
        if (said.insert("W" + msg).second) diag.warning(name, 0, 0, msg);
    }

    // The envelope's number, from 1.
    int internEnvelope(const Envelope& e) {
        for (std::size_t i = 0; i < envelopes.size(); ++i) {
            if (envelopes[i] == e) return static_cast<int>(i) + 1;
        }
        envelopes.push_back(e);
        return static_cast<int>(envelopes.size());
    }

    int intern(bool isWave, const std::vector<std::uint8_t>& record) {
        for (std::size_t i = 0; i < voices.size(); ++i) {
            if (voices[i].isWave == isWave && voices[i].record == record) return static_cast<int>(i);
        }
        voices.push_back({isWave, record});
        return static_cast<int>(voices.size()) - 1;
    }
};

// Everything a channel's conversion depends on, the source driver's state and
// what the Y8960 reader already holds alike. Two equal states turn the same
// commands into the same bytes, which is what lets a repeated block become a
// loop (see TrackBuilder::step).
struct State {
    // The source driver's.
    int volume = 0;       // FM: attenuation; PSG, SCC: level
    int instrument = kMusicaInstrument;
    bool haveUser = false;
    PackedVoice user{};   // what 83h or 82h last put in the user voice
    bool voiceDirty = true;
    bool legato = false;
    int quant = 8;
    // The previous note is still keyed on at its end, so the next note
    // continues it. The drivers decide that when the note ends, before they
    // read what follows it: what counts is legato as it was when that note was
    // read, not as it is at the next one (BGM.BIN, the gate test at D67Ah).
    // MuSICA's "(d i50) d i" is legato on, d, vibrato, legato off, d - one d,
    // the vibrato starting half way.
    bool held = false;
    std::array<int, 5> rhythmAtt{};  // B S T C H, attenuation

    // The Y8960 reader's, as far as it is known. -1: not known.
    int yOctave = kInitOctave;
    int yLoud = kInitLoudness;
    int yVoice = -1;      // 82h n as n, 85h n as 256 + n
    int yQuant = kInitQuant;
    int ySustain = 0;
    int yAccents = -1;
    // Not running state: the reader resets them only when the sequence starts
    // from the top, so a repeat of the whole sequence finds the levels its end
    // left. Unknown at the start, then, not the reader's 8.
    std::array<int, 5> yRhyLevels{-1, -1, -1, -1, -1};  // B S T C H
    int yEnvelope = 0;

    bool operator==(const State& o) const {
        return volume == o.volume && instrument == o.instrument && haveUser == o.haveUser &&
               user == o.user && voiceDirty == o.voiceDirty && legato == o.legato &&
               quant == o.quant && held == o.held &&
               rhythmAtt == o.rhythmAtt && yOctave == o.yOctave &&
               yLoud == o.yLoud && yVoice == o.yVoice && yQuant == o.yQuant &&
               ySustain == o.ySustain && yAccents == o.yAccents && yRhyLevels == o.yRhyLevels &&
               yEnvelope == o.yEnvelope;
    }

    // What a loop's head can count on whichever round it is in. The octave and
    // the accents are forgotten there on purpose: the body then sets them
    // itself, and so reads the same every round (bytecode.md asks for A8h
    // after a loop's start for the same reason).
    State atLoopHead() const {
        State s = *this;
        s.yOctave = -1;
        s.yAccents = -1;
        return s;
    }
};

// A channel's sequence with its repeats folded: a node is a block, or a group
// of nodes, played `count` times.
struct Node {
    int block = -1;  // -1: a group
    int count = 1;
    std::vector<Node> body;

    bool operator==(const Node& o) const {
        return block == o.block && count == o.count && body == o.body;
    }
};

// MuSICA's sequences repeat a block with a count, but a run of blocks that
// comes back (A B A B A B) is written out in full. The longest such run from
// each place becomes a group, so that it can become a loop instead.
std::vector<Node> foldNodes(std::vector<Node> in) {
    // The same block twice in a row is one block played the sum.
    std::vector<Node> merged;
    for (Node& n : in) {
        if (!merged.empty() && merged.back().block >= 0 && merged.back().block == n.block &&
            merged.back().count + n.count <= 256) {
            merged.back().count += n.count;
        } else {
            merged.push_back(std::move(n));
        }
    }

    std::vector<Node> out;
    const std::size_t size = merged.size();
    std::size_t i = 0;
    while (i < size) {
        std::size_t bestPeriod = 0, bestTimes = 1;
        for (std::size_t period = 2; i + period * 2 <= size; ++period) {
            std::size_t times = 1;
            while (i + period * (times + 1) <= size &&
                   std::equal(merged.begin() + static_cast<std::ptrdiff_t>(i),
                              merged.begin() + static_cast<std::ptrdiff_t>(i + period),
                              merged.begin() + static_cast<std::ptrdiff_t>(i + period * times))) {
                ++times;
            }
            if (times > 1 && period * (times - 1) > bestPeriod * (bestTimes - 1)) {
                bestPeriod = period;
                bestTimes = times;
            }
        }
        if (bestPeriod == 0) {
            out.push_back(std::move(merged[i]));
            ++i;
            continue;
        }
        Node group;
        group.count = static_cast<int>(bestTimes);
        group.body.assign(merged.begin() + static_cast<std::ptrdiff_t>(i),
                          merged.begin() + static_cast<std::ptrdiff_t>(i + bestPeriod));
        group.body = foldNodes(std::move(group.body));
        out.push_back(std::move(group));
        i += bestPeriod * bestTimes;
    }
    return out;
}

std::vector<Node> fold(const std::vector<SeqStep>& steps) {
    std::vector<Node> nodes;
    for (const SeqStep& st : steps) nodes.push_back({st.block, st.count, {}});
    return foldNodes(std::move(nodes));
}

class TrackBuilder {
public:
    TrackBuilder(Shared& shared, const Channel& ch, bool first)
        : sh_(shared), ch_(ch), first_(first), name_(channelName(shared.song, ch)) {
        const bool musica = shared.song.format == Format::Musica;
        if (ch.part == Part::Fm) {
            s_.volume = musica ? kMusicaFmVolume : kOpllFmVolume;
            s_.instrument = musica ? kMusicaInstrument : kOpllInstrument;
        }
        // The manual's 60h is the default for every part; on the PSG and the
        // SCC it is the quietest.
        if (ch.part == Part::Psg || ch.part == Part::Scc) s_.volume = 0;
        s_.rhythmAtt.fill(musica ? kMusicaRhythmAtt : kOpllRhythmAtt);
        // The SSGS's @0, tone only, is what the reader starts from.
        if (ch.part == Part::Psg) s_.yVoice = 0;
    }

    std::vector<std::uint8_t> build() {
        out_ = &bytes_;
        if (first_) put(OpTempo, kFrameTempo);
        // MuSICA writes its level to the SCC's volume register as it is; the
        // reader's own table would make the quiet levels quieter still.
        if (ch_.part == Part::Scc) put(OpSccVolTable, 0);
        for (const Node& nd : fold(ch_.steps)) node(nd, 0);
        put(OpEnd);
        return bytes_;
    }

private:
    // --- the bytes -------------------------------------------------------

    void put(int b) { out_->push_back(static_cast<std::uint8_t>(b)); }
    void put(int a, int b) { put(a); put(b); }
    void putWord(int v) { put(v & 0xFF); put((v >> 8) & 0xFF); }
    void putLength(int len) {
        if (len < 0x80) {
            put(len);
        } else {
            put(0x80 | (len >> 8));
            put(len & 0xFF);
        }
    }

    // A length-carrying event, split into pieces the readers can take. `tie`
    // joins a note's pieces; rests and waits simply repeat.
    void timed(int op, int len, bool tie) {
        while (len > kLengthMax) {
            put(op);
            putLength(kLengthMax);
            if (tie) put(OpTie);
            len -= kLengthMax;
        }
        put(op);
        putLength(len);
    }

    void octave(int oct) {
        if (s_.yOctave == oct) return;
        if (s_.yOctave >= 0 && oct == s_.yOctave + 1) {
            put(OpOctUp);
        } else if (s_.yOctave >= 0 && oct == s_.yOctave - 1) {
            put(OpOctDown);
        } else {
            put(OpOctave, oct);
        }
        s_.yOctave = oct;
    }

    void noteOp(int srcNote, int len) {
        const int number =
            srcNote + (ch_.part == Part::Fm ? kNoteOffsetFm : kNoteOffsetDivider);
        octave(number / 12);
        timed(OpNote + number % 12, len, true);
    }

    void rest(int len) {
        timed(OpRest, len, false);
    }

    void wait(int len) { timed(OpWait, len, false); }

    void loud(int level) {
        const int v = loudness(level);
        if (s_.yLoud == v) return;
        put(OpVolume, v);
        s_.yLoud = v;
    }

    void quant(int q) {
        if (s_.yQuant == q) return;
        put(OpQuantize, q);
        s_.yQuant = q;
    }

    void voice(int key) {
        if (s_.yVoice == key) return;
        if (key >= 256) {
            put(OpSeqVoice, key - 256);
        } else {
            put(OpVoice, key);
        }
        s_.yVoice = key;
    }

    void regWrite(int reg, int data, int mask) {
        put(OpRegWrite);
        put(reg);
        put(data);
        put(mask);
    }

    // --- structure -------------------------------------------------------

    // A node played `count` times. It becomes a loop when its body reads the
    // same in every round; otherwise the rounds that differ are written out,
    // and so is everything past the readers' four levels of nesting.
    void node(const Node& nd, int depth) {
        int remaining = nd.count;
        while (remaining > 0) {
            if (remaining > 1 && depth < kLoopDepth) {
                const State saved = s_;
                const State head = s_.atLoopHead();
                s_ = head;
                std::vector<std::uint8_t> body;
                std::vector<std::uint8_t>* outer = out_;
                out_ = &body;
                inside(nd, depth + 1);
                out_ = outer;
                if (s_.atLoopHead() == head) {
                    const int n = std::min(remaining, 255);
                    put(OpLoopStart);
                    out_->insert(out_->end(), body.begin(), body.end());
                    put(OpLoopEnd, n);
                    // Back to the byte after 42h, counted from the end of the distance.
                    putWord(0x10000 - static_cast<int>(body.size()) - 4);
                    s_.yAccents = -1;
                    remaining -= n;
                    continue;
                }
                s_ = saved;
            }
            inside(nd, depth);
            --remaining;
        }
    }

    void inside(const Node& nd, int depth) {
        if (nd.block >= 0) {
            events(sh_.song.blocks[static_cast<std::size_t>(nd.block)]);
            return;
        }
        for (const Node& c : nd.body) node(c, depth);
    }

    void events(const Block& block) {
        for (const SrcEvent& e : block.events) {
            switch (ch_.part) {
            case Part::Fm: fm(e); break;
            case Part::Rhythm: rhythm(e); break;
            case Part::Psg:
            case Part::Scc: psg(e); break;
            }
        }
    }

    // --- FM melody -------------------------------------------------------

    void fm(const SrcEvent& e) {
        switch (e.kind) {
        case SrcEvent::Note: fmNote(e.value, e.length); break;
        case SrcEvent::Rest:
            // OPLDRV's rest touches no key: a note held by legato sounds on
            // through it, and the next note continues it (int_note).
            if (sh_.song.format == Format::Opll && s_.held) {
                wait(e.length);
                break;
            }
            rest(e.length);
            s_.held = false;
            break;
        case SrcEvent::Wait: wait(e.length); break;
        case SrcEvent::Volume:
            s_.volume = e.value;
            loud(15 - e.value);
            break;
        case SrcEvent::Instrument:
            s_.instrument = e.value;
            s_.voiceDirty = true;
            break;
        case SrcEvent::Sustain: {
            const int bit = e.value ? 0x20 : 0x00;
            if (s_.ySustain != bit) {
                regWrite(0x20 + ch_.partIndex, bit, 0xDF);
                s_.ySustain = bit;
            }
            break;
        }
        case SrcEvent::RomVoice: romVoice(e.value); break;
        case SrcEvent::UserVoice: userVoice(e.addr); break;
        case SrcEvent::Legato: s_.legato = e.value != 0; break;
        case SrcEvent::Quantize:
            // OPLDRV takes Q AND 7, 0 being the whole length.
            s_.quant = sh_.song.format == Format::Opll ? ((e.value & 7) ? (e.value & 7) : 8) : e.value;
            break;
        case SrcEvent::RegWrite:
            if (opllexRegister(e.value)) {
                regWrite(e.value, e.value2, 0);
            } else {
                sh_.warning(name_ + ": a write to register " + hex2(static_cast<unsigned>(e.value)) +
                            " was left out; OPLLEX does not take it");
            }
            break;
        default: break;  // counted and reported before the build (unsupported())
        }
    }

    void romVoice(int n) {
        if ((n & 0x7F) >= 64) {
            sh_.warning(name_ + ": 82h names voice " + std::to_string(n & 0x7F) +
                        ", past the ROM's 64; the original reads whatever follows the table, and "
                        "this takes voice " + std::to_string(n & 63));
        }
        if (!sh_.options.romVoices.empty()) {
            s_.user = recordFromOpll(sh_.options.romVoices.data() + (n & 63) * 8);
        } else {
            // FM-BIOS's 82h writes the ROM's eight bytes and nothing else, and
            // its notes have no transpose to add (int_e_voice, int_note). The
            // preset's transpose is MSX-AUDIO BASIC's, for a voice that mostly
            // has the same multipliers as the ROM's.
            s_.user = packVoice(presetVoice(n & 63));
            s_.user[1] = 0;
        }
        s_.haveUser = true;
        // Loaded, not selected: 70h selects it (FM-BIOS's int_e_voice).
        if (s_.instrument == 0) s_.voiceDirty = true;
    }

    void userVoice(std::uint16_t addr) {
        const Song& song = sh_.song;
        if (song.format == Format::Opll && !song.baseKnown) {
            sh_.error(name_ + ": a user voice (83h) names an absolute address; without a BSAVE "
                              "header, --base has to say where the data was loaded");
            return;
        }
        const int size = (ch_.part == Part::Psg) ? 6 : (ch_.part == Part::Scc) ? 36 : 8;
        if (!song.readable(addr, static_cast<std::uint32_t>(size))) {
            sh_.error(name_ + ": the voice at " + hex4(addr) + " is outside the data");
            return;
        }
        std::uint8_t raw[36];
        for (int i = 0; i < size; ++i) raw[i] = song.at(addr + static_cast<std::uint32_t>(i));

        if (ch_.part == Part::Fm) {
            s_.user = recordFromOpll(raw);
            s_.haveUser = true;
            // Both drivers only load the voice, and 70h selects it (FM-BIOS's
            // int_u_voice, MuSICA's manual).
            if (s_.instrument == 0) s_.voiceDirty = true;
            return;
        }

        // The software envelope, the first four bytes. Y8960's works as
        // MuSICA's does, and chunk 04 takes the rate bytes as they are.
        bool exactA = true, exactD = true, exactR = true;
        Envelope env;
        env.ar = envelopeRate(raw[0], exactA);
        env.dr = envelopeRate(raw[1], exactD);
        env.sl = raw[2] & 0x0F;
        env.rr = envelopeRate(raw[3], exactR);
        if (!(exactA && exactD && exactR)) {
            sh_.warning(name_ + ": the envelope of the voice at " + hex4(addr) +
                        " has a rate Y8960 does not have; it takes the nearest in speed");
        }
        const int number = sh_.internEnvelope(env);
        if (s_.yEnvelope != number) {
            put(OpSoftEnv, number);
            s_.yEnvelope = number;
        }
        if (ch_.part == Part::Psg) {
            // The driver writes the noise period only for a voice that sounds
            // the noise (BGM.BIN's 83h).
            const bool noise = (raw[5] & 0x08) == 0;
            if (noise) regWrite(0x06, raw[4] & 0x1F, 0);
            // SSGS's @n: bit5 set silences the tone, bit6 set sounds the noise.
            const int mixer = ((raw[5] & 0x01) ? 0x20 : 0) | (noise ? 0x40 : 0);
            voice(mixer);
        } else {
            voice(256 + slot(true, std::vector<std::uint8_t>(raw + 4, raw + 36)));
        }
    }

    int slot(bool isWave, const std::vector<std::uint8_t>& rec) { return sh_.intern(isWave, rec); }
    int slot(const PackedVoice& v) { return sh_.intern(false, {v.begin(), v.end()}); }

    void fmVoice() {
        if (!s_.voiceDirty) return;
        s_.voiceDirty = false;
        if (s_.instrument != 0) {
            voice(s_.instrument);  // bank 0, the YM2413's own
            return;
        }
        if (!s_.haveUser) {
            sh_.warning(name_ + ": the user voice plays before one is loaded; it sounds as a "
                                "voice of all zero registers");
            s_.user = recordFromOpll(std::array<std::uint8_t, 8>{}.data());
            s_.haveUser = true;
        }
        voice(256 + slot(s_.user));
    }

    // Legato holds the note on past its length; the next note continues it
    // (45h) and Q does not apply. Q1-7 keys off after length * Q / 8, at least
    // 1, which is the reader's own rule; Q0 is MuSICA's "cut the last count",
    // which no Q of the bytecode says, so it is written out. MuSICA's gate is
    // read off BGM.BIN; OPLLDRV's is taken to be the same.
    void melodyNote(int srcNote, int len) {
        const bool join = s_.held;
        const bool cutLast = !s_.legato && s_.quant == 0;
        quant((s_.legato || s_.quant == 0 || s_.quant > 8) ? 8 : s_.quant);
        if (join) put(OpTie);
        if (cutLast && len >= 1) {
            if (len > 1) noteOp(srcNote, len - 1);
            rest(1);
            s_.held = false;
            return;
        }
        noteOp(srcNote, len);
        s_.held = s_.legato;
    }

    void fmNote(int srcNote, int len) {
        fmVoice();
        loud(15 - s_.volume);
        melodyNote(srcNote, len);
    }

    // --- PSG and SCC -----------------------------------------------------

    void psg(const SrcEvent& e) {
        switch (e.kind) {
        case SrcEvent::Note: psgNote(e.value, e.length); break;
        case SrcEvent::Rest:
            rest(e.length);
            s_.held = false;
            break;
        case SrcEvent::Wait: wait(e.length); break;
        case SrcEvent::Volume:
            s_.volume = e.value;
            loud(e.value);
            break;
        case SrcEvent::UserVoice: userVoice(e.addr); break;
        case SrcEvent::Legato: s_.legato = e.value != 0; break;
        case SrcEvent::Quantize: s_.quant = e.value; break;
        case SrcEvent::RegWrite:
            // The driver hands a PSG write to the BIOS as it is, where a number
            // past 0Dh reaches no sound register, and an SCC write to 9880h
            // plus the number, which counted from the window is 80h up.
            if (ch_.part == Part::Psg && e.value > 0x0D) {
                sh_.warning(name_ + ": a write to register " + hex2(static_cast<unsigned>(e.value)) +
                            " was left out; it is no sound register of the PSG");
            } else if (ch_.part == Part::Scc) {
                regWrite(0x80 | e.value, e.value2, 0);
            } else {
                regWrite(e.value, e.value2, 0);
            }
            break;
        default: break;
        }
    }

    void psgNote(int srcNote, int len) {
        if (ch_.part == Part::Scc && s_.yVoice < 0) {
            sh_.warning(name_ + ": notes play before a waveform is set; they take the Y8960 "
                                "preset waveform 0");
            const VoiceRecord w = presetWave(0);
            voice(256 + slot(true, {w.begin(), w.end()}));
        }
        loud(s_.volume);
        melodyNote(srcNote, len);
    }

    // --- rhythm ----------------------------------------------------------

    void rhythm(const SrcEvent& e) {
        switch (e.kind) {
        case SrcEvent::RhythmHit: hit(e.value, e.length); break;
        case SrcEvent::RhythmVolume:
            // Both drivers set the instruments whose bits are set, and with
            // none they set nothing (FM-BIOS's next_event2, BGM.BIN's D63Ah).
            for (int i = 0; i < 5; ++i) {
                if (e.value & (0x10 >> i)) s_.rhythmAtt[static_cast<std::size_t>(i)] = e.value2;
            }
            break;
        case SrcEvent::Wait: wait(e.length); break;
        case SrcEvent::RegWrite:
            if (opllexRegister(e.value)) {
                regWrite(e.value, e.value2, 0);
            } else {
                sh_.warning(name_ + ": a write to register " + hex2(static_cast<unsigned>(e.value)) +
                            " was left out; OPLLEX does not take it");
            }
            break;
        default: break;
        }
    }

    // Every instrument's level is its plain volume; the accent is never used.
    // A level is set when an instrument is struck, and the same D8 takes along
    // every other instrument that wants that level too, which costs nothing.
    void hit(int bits, int len) {
        if (bits == 0) {
            wait(len);
            return;
        }
        std::array<int, 5> want{};
        for (int i = 0; i < 5; ++i) want[static_cast<std::size_t>(i)] = 15 - s_.rhythmAtt[static_cast<std::size_t>(i)];
        for (int i = 0; i < 5; ++i) {
            const int level = want[static_cast<std::size_t>(i)];
            if (!(bits & (0x10 >> i)) || s_.yRhyLevels[static_cast<std::size_t>(i)] == level) continue;
            int mask = 0;
            for (int j = 0; j < 5; ++j) {
                if (want[static_cast<std::size_t>(j)] == level && s_.yRhyLevels[static_cast<std::size_t>(j)] != level) {
                    mask |= 0x10 >> j;
                    s_.yRhyLevels[static_cast<std::size_t>(j)] = level;
                }
            }
            if (mask == 0x1F) {
                put(OpRhythmVolume, level);
            } else {
                put(OpRhythmInstVolume, mask);
                put(level);
            }
        }
        if (s_.yAccents != 0) {
            put(OpRhythmAccent, 0);
            s_.yAccents = 0;
        }
        put(OpRhythmHit, bits);
        int first = std::min(len, kLengthMax);
        putLength(first);
        if (len > first) wait(len - first);
    }

    Shared& sh_;
    const Channel& ch_;
    bool first_;
    std::string name_;
    State s_;
    std::vector<std::uint8_t> bytes_;
    std::vector<std::uint8_t>* out_ = nullptr;
};

// What the conversion leaves out, counted over the whole song once so that a
// block played a hundred times is not reported a hundred times.
void reportUnsupported(Shared& sh, const Channel& ch) {
    std::map<SrcEvent::Kind, long> counts;
    for (const SeqStep& st : ch.steps) {
        for (const SrcEvent& e : sh.song.blocks[static_cast<std::size_t>(st.block)].events) {
            switch (e.kind) {
            case SrcEvent::Detune:
            case SrcEvent::Portamento:
            case SrcEvent::Vibrato:
            case SrcEvent::LfoSpeed:
                if (e.value != 0) counts[e.kind] += st.count;
                break;
            default: break;
            }
        }
    }
    static const std::map<SrcEvent::Kind, const char*> names = {
        {SrcEvent::Detune, "detune (87h)"},
        {SrcEvent::Portamento, "portamento (88h)"},
        {SrcEvent::Vibrato, "vibrato (89h)"},
        {SrcEvent::LfoSpeed, "LFO speed (8Bh)"},
    };
    for (const auto& kv : counts) {
        sh.warning(channelName(sh.song, ch) + ": " + names.at(kv.first) + " is not converted (" +
                   std::to_string(kv.second) + " time(s) as played)");
    }
}

bool hasEvents(const Song& song, const Channel& ch) {
    for (const SeqStep& st : ch.steps) {
        if (!song.blocks[static_cast<std::size_t>(st.block)].events.empty()) return true;
    }
    return false;
}

int partOrder(Part p) {
    switch (p) {
    case Part::Fm: return 0;
    case Part::Rhythm: return 1;
    case Part::Psg: return 2;
    case Part::Scc: return 3;
    }
    return 4;
}

} // namespace

int envelopeRate(std::uint8_t musica, bool& exact) {
    const int count = musica >> 4;
    const int step = musica & 0x0F;
    exact = count != 0 && step != 0;
    if (exact) return musica;
    // MuSICA takes a counter of 0 as 256 interrupts. A step of 0 never moves,
    // which the slowest rate is nearest to.
    const double speed = static_cast<double>(step) / (count == 0 ? 256 : count);
    int best = 0;
    double bestDiff = 1e9;
    for (int c = 1; c <= 15; ++c) {
        for (int d = 1; d <= 15; ++d) {
            const double diff = std::fabs(static_cast<double>(d) / c - speed);
            if (diff < bestDiff) {
                bestDiff = diff;
                best = c << 4 | d;
            }
        }
    }
    return best;
}

PackedVoice recordFromOpll(const std::uint8_t* o) {
    // Chunk 01: C0h (bit3-1 FB, bit0 CNT), the transpose, then the modulator
    // and the carrier, each 40h 60h 80h 20h and the waveform.
    PackedVoice r{};
    r[0] = static_cast<std::uint8_t>((o[3] & 0x07) << 1);
    r[2] = o[2];
    r[3] = o[4];
    r[4] = o[6];
    r[5] = o[0];
    r[6] = static_cast<std::uint8_t>((o[3] >> 3) & 1);   // DM
    r[7] = static_cast<std::uint8_t>(o[3] & 0xC0);       // the carrier's KSL; OPLL has no carrier TL
    r[8] = o[5];
    r[9] = o[7];
    r[10] = o[1];
    r[11] = static_cast<std::uint8_t>((o[3] >> 4) & 1);  // DC
    return r;
}

bool convert(const Song& song, const ConvertOptions& options, const std::string& name,
             Sequence& seq, Diagnostics& diag) {
    Shared sh{song, options, name, diag, {}, {}, {}, false};

    if (!options.romVoices.empty() && options.romVoices.size() != kRomVoiceTableSize) {
        sh.error("the ROM voice table is " + std::to_string(options.romVoices.size()) +
                 " bytes; it has to be " + std::to_string(kRomVoiceTableSize));
        return false;
    }

    std::set<std::string> known;
    std::vector<const Channel*> chosen;
    for (const Channel& ch : song.channels) {
        const std::string cname = channelName(song, ch);
        known.insert(cname);
        if (std::find(options.drop.begin(), options.drop.end(), cname) != options.drop.end()) continue;
        if (!hasEvents(song, ch)) continue;
        chosen.push_back(&ch);
    }
    for (const std::string& d : options.drop) {
        if (!known.count(d)) sh.warning("--drop " + d + ": the data has no such channel");
    }
    std::stable_sort(chosen.begin(), chosen.end(), [](const Channel* a, const Channel* b) {
        if (partOrder(a->part) != partOrder(b->part)) return partOrder(a->part) < partOrder(b->part);
        return a->partIndex < b->partIndex;
    });

    if (chosen.size() > kTrackCount) {
        std::string list;
        for (const Channel* ch : chosen) list += " " + channelName(song, *ch);
        sh.error("the data uses " + std::to_string(chosen.size()) +
                 " channels and a Y8960 sequence holds " + std::to_string(kTrackCount) +
                 " tracks; --drop leaves some out:" + list);
        return false;
    }

    Sequence out;
    for (std::size_t i = 0; i < chosen.size(); ++i) {
        const Channel& ch = *chosen[i];
        reportUnsupported(sh, ch);
        TrackBuilder builder(sh, ch, i == 0);
        Track t;
        t.number = static_cast<int>(i);
        t.name = channelName(song, ch);
        switch (ch.part) {
        case Part::Fm:
            t.device = kDevOPLLEX1;
            t.channel = ch.partIndex;
            break;
        case Part::Rhythm:
            t.device = kDevOPLLEX1;
            t.channel = kRhythmChannel;
            break;
        case Part::Psg:
            t.device = kDevSSGS;
            t.channel = ch.partIndex;
            break;
        case Part::Scc:
            t.device = kDevSCC;
            t.channel = ch.partIndex;
            break;
        }
        t.bytes = builder.build();
        if (t.bytes.size() > static_cast<std::size_t>(kTrackBytesMax)) {
            sh.error(t.name + ": the track is " + std::to_string(t.bytes.size()) +
                     " bytes and a Y8960 track holds " + std::to_string(kTrackBytesMax));
        }
        out.tracks.push_back(std::move(t));
    }

    if (sh.voices.size() > static_cast<std::size_t>(kVoiceSlots)) {
        sh.error("the data uses " + std::to_string(sh.voices.size()) +
                 " voices and waveforms, and a Y8960 sequence holds " + std::to_string(kVoiceSlots));
    }
    if (sh.envelopes.size() > static_cast<std::size_t>(kEnvelopeMax)) {
        sh.error("the data uses " + std::to_string(sh.envelopes.size()) +
                 " software envelopes, and a Y8960 sequence holds " + std::to_string(kEnvelopeMax));
    }
    if (sh.failed) return false;
    out.voices = std::move(sh.voices);
    out.envelopes = std::move(sh.envelopes);
    seq = std::move(out);
    return true;
}

} // namespace y8
