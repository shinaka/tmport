#include "gg/save/players_dat.h"

#include <algorithm>
#include <cstring>
#include <format>

#include "util/error.h"

namespace tmport::gg {

namespace {

// Little-endian field cursor over a fixed-size byte image (read or write).
class In {
public:
    explicit In(std::span<const uint8_t> b) : b_(b) {}
    uint8_t u8() { return b_[pos_++]; }
    uint16_t u16() {
        const uint16_t v = static_cast<uint16_t>(b_[pos_] | (b_[pos_ + 1] << 8));
        pos_ += 2;
        return v;
    }
    int16_t i16() { return static_cast<int16_t>(u16()); }
    uint32_t u32() {
        const uint32_t lo = u16();
        return lo | (static_cast<uint32_t>(u16()) << 16);
    }
    template <size_t N>
    void bytes(std::array<uint8_t, N>& a) {
        std::copy_n(b_.begin() + static_cast<std::ptrdiff_t>(pos_), N, a.begin());
        pos_ += N;
    }
    size_t pos() const { return pos_; }

private:
    std::span<const uint8_t> b_;
    size_t pos_ = 0;
};

class Out {
public:
    explicit Out(std::span<uint8_t> b) : b_(b) {}
    void u8(uint8_t v) { b_[pos_++] = v; }
    void u16(uint16_t v) {
        b_[pos_] = static_cast<uint8_t>(v);
        b_[pos_ + 1] = static_cast<uint8_t>(v >> 8);
        pos_ += 2;
    }
    void i16(int16_t v) { u16(static_cast<uint16_t>(v)); }
    void u32(uint32_t v) {
        u16(static_cast<uint16_t>(v));
        u16(static_cast<uint16_t>(v >> 16));
    }
    template <size_t N>
    void bytes(const std::array<uint8_t, N>& a) {
        std::copy(a.begin(), a.end(), b_.begin() + static_cast<std::ptrdiff_t>(pos_));
        pos_ += N;
    }
    size_t pos() const { return pos_; }

private:
    std::span<uint8_t> b_;
    size_t pos_ = 0;
};

void readLive(In& in, LiveState& s) {
    s.blueprintSeen = in.i16();
    s.projectiles = in.i16();
    s.tubeRoom = in.i16();
    s.partsNeeded = in.i16();
    s.partsInstalled = in.i16();
    s.vehicleBonusGiven = in.i16();
    s.paintColor = in.i16();
    s.hintCancel1046 = in.i16();
    s.hintCancel1048 = in.i16();
    s.setupCoin = in.i16();
}

void writeLive(Out& out, const LiveState& s) {
    for (const int16_t v : {s.blueprintSeen, s.projectiles, s.tubeRoom, s.partsNeeded, s.partsInstalled,
                            s.vehicleBonusGiven, s.paintColor, s.hintCancel1046, s.hintCancel1048, s.setupCoin})
        out.i16(v);
}

// The 23 words of an actor's saved range, in record order (+0x14..+0x40).
template <class A>
auto actorWords(A& a) {
    return std::array{&a.anchorX,   &a.anchorY,  &a.seq,        &a.room,       &a.floor,      &a.prevSeq,
                      &a.exitSeq,   &a.step,     &a.doorCooldown, &a.loopCounter, &a.loopStart, &a.disabled,
                      &a.pendingSeq, &a.wallsOn, &a.contactsOn, &a.blocked,    &a.unused34,   &a.doorObject,
                      &a.pad,       &a.edgeObject, &a.prevRoom, &a.immune,     &a.item};
}
static_assert(23 * 2 == kActorStateSize);

void readBlock(In& in, BuildingBlock& b) {
    readLive(in, b.live);
    for (auto& p : b.parts) {
        p.holder = in.i16();
        p.flags = in.u16();
    }
    in.bytes(b.doorTypes);
    in.bytes(b.roomItems);
    for (auto& a : b.actors)
        for (int16_t* w : actorWords(a)) *w = in.i16();
}

void writeBlock(Out& out, const BuildingBlock& b) {
    writeLive(out, b.live);
    for (const auto& p : b.parts) {
        out.i16(p.holder);
        out.u16(p.flags);
    }
    out.bytes(b.doorTypes);
    out.bytes(b.roomItems);
    for (const auto& a : b.actors)
        for (const int16_t* w : actorWords(a)) out.i16(*w);
}

std::string cString(std::span<const uint8_t> b) {
    const auto nul = std::find(b.begin(), b.end(), uint8_t{0});
    return {reinterpret_cast<const char*>(b.data()), static_cast<size_t>(nul - b.begin())};
}

}  // namespace

// --- BuildingBlock --------------------------------------------------------------------------------

std::vector<std::vector<SavedItem>> BuildingBlock::readRoomItems(int roomCount) const {
    std::vector<std::vector<SavedItem>> rooms;
    size_t pos = 0;
    const auto word = [&]() {
        if (pos + 2 > roomItems.size()) throw FormatError("PLAYERS.DAT: room item lists run past their area");
        const auto v = static_cast<int16_t>(roomItems[pos] | (roomItems[pos + 1] << 8));
        pos += 2;
        return v;
    };
    for (int r = 0; r < roomCount; ++r) {
        const auto n = static_cast<uint16_t>(word());
        std::vector<SavedItem> items(n);
        for (auto& it : items) {
            it.x = word();
            it.floor = word();
            it.kind = word();
        }
        rooms.push_back(std::move(items));
    }
    return rooms;
}

void BuildingBlock::writeRoomItems(const std::vector<std::vector<SavedItem>>& rooms) {
    size_t need = 0;
    for (const auto& r : rooms) need += 2 + 6 * r.size();
    if (need > roomItems.size()) throw FormatError("room item lists do not fit in the building block");
    size_t pos = 0;
    const auto word = [&](int v) {
        roomItems[pos] = static_cast<uint8_t>(v);
        roomItems[pos + 1] = static_cast<uint8_t>(static_cast<unsigned>(v) >> 8);
        pos += 2;
    };
    for (const auto& r : rooms) {
        word(static_cast<int>(r.size()));
        for (const auto& it : r) {
            word(it.x);
            word(it.floor);
            word(it.kind);
        }
    }
}

// --- PlayerRecord -----------------------------------------------------------------------------------

PlayerRecord PlayerRecord::fromBytes(std::span<const uint8_t, kPlayerRecordSize> bytes) {
    PlayerRecord r;
    In in(bytes);
    in.bytes(r.name);
    r.points = in.u32();
    r.slot = in.i16();
    r.withChimps = in.i16();
    r.unused18 = in.u16();
    readLive(in, r.live);
    r.raceIndex = in.i16();
    r.building = in.i16();
    r.level = in.i16();
    for (auto& v : r.racesWon) v = in.i16();
    for (auto& v : r.buildingInProgress) v = in.i16();
    r.hintsOff = in.i16();
    for (auto& v : r.hintSeen) v = in.u16();
    r.unused68 = in.u16();
    r.soundOff = in.i16();
    r.unused6C = in.u16();
    for (auto& kind : r.puzzleSolved)
        for (auto& v : kind) v = in.u16();
    for (auto& b : r.blocks) readBlock(in, b);
    r.racesWonTotal = in.i16();
    in.bytes(r.vehicleColor);
    return r;
}

void PlayerRecord::toBytes(std::span<uint8_t, kPlayerRecordSize> bytes) const {
    Out out(bytes);
    out.bytes(name);
    out.u32(points);
    out.i16(slot);
    out.i16(withChimps);
    out.u16(unused18);
    writeLive(out, live);
    out.i16(raceIndex);
    out.i16(building);
    out.i16(level);
    for (const auto v : racesWon) out.i16(v);
    for (const auto v : buildingInProgress) out.i16(v);
    out.i16(hintsOff);
    for (const auto v : hintSeen) out.u16(v);
    out.u16(unused68);
    out.i16(soundOff);
    out.u16(unused6C);
    for (const auto& kind : puzzleSolved)
        for (const auto v : kind) out.u16(v);
    for (const auto& b : blocks) writeBlock(out, b);
    out.i16(racesWonTotal);
    out.bytes(vehicleColor);
}

std::vector<uint8_t> PlayerRecord::bytes() const {
    std::vector<uint8_t> b(kPlayerRecordSize);
    toBytes(std::span<uint8_t, kPlayerRecordSize>(b.data(), kPlayerRecordSize));
    return b;
}

PlayerRecord PlayerRecord::blank() {
    PlayerRecord r;
    r.building = -1;
    r.racesWon.fill(-1);
    return r;
}

std::string PlayerRecord::nameString() const { return cString(name); }

void PlayerRecord::setName(std::string_view s) {
    name.fill(0);
    std::copy_n(s.begin(), std::min(s.size(), kPlayerNameSize - 1), name.begin());
}

// --- RosterEntry / Roster -------------------------------------------------------------------------------

RosterEntry RosterEntry::fromBytes(std::span<const uint8_t, kRosterEntrySize> bytes) {
    RosterEntry e;
    In in(bytes);
    in.bytes(e.name);
    e.points = in.u32();
    e.slot = in.u16();
    return e;
}

void RosterEntry::toBytes(std::span<uint8_t, kRosterEntrySize> bytes) const {
    Out out(bytes);
    out.bytes(name);
    out.u32(points);
    out.u16(slot);
}

RosterEntry RosterEntry::ofRecord(const PlayerRecord& rec) {
    RosterEntry e;
    e.name = rec.name;
    e.points = rec.points;
    e.slot = static_cast<uint16_t>(rec.slot);
    return e;
}

std::string RosterEntry::nameString() const { return cString(name); }

Roster Roster::fromBytes(std::span<const uint8_t, kRosterSize> bytes) {
    Roster r;
    std::copy_n(bytes.begin(), 4, r.magic.begin());
    r.version = static_cast<uint16_t>(bytes[4] | (bytes[5] << 8));
    r.count = static_cast<uint16_t>(bytes[6] | (bytes[7] << 8));
    for (int i = 0; i < kRosterEntryCount; ++i)
        r.entries[static_cast<size_t>(i)] =
            RosterEntry::fromBytes(bytes.subspan(PlayersDat::entryOffset(i)).first<kRosterEntrySize>());
    return r;
}

void Roster::toBytes(std::span<uint8_t, kRosterSize> bytes) const {
    std::copy(magic.begin(), magic.end(), bytes.begin());
    bytes[4] = static_cast<uint8_t>(version);
    bytes[5] = static_cast<uint8_t>(version >> 8);
    bytes[6] = static_cast<uint8_t>(count);
    bytes[7] = static_cast<uint8_t>(count >> 8);
    for (int i = 0; i < kRosterEntryCount; ++i)
        entries[static_cast<size_t>(i)].toBytes(bytes.subspan(PlayersDat::entryOffset(i)).first<kRosterEntrySize>());
}

std::vector<uint8_t> Roster::bytes() const {
    std::vector<uint8_t> b(kRosterSize);
    toBytes(std::span<uint8_t, kRosterSize>(b.data(), kRosterSize));
    return b;
}

// --- PlayersDat ----------------------------------------------------------------------------------------

std::optional<PlayersDat> PlayersDat::parse(std::span<const uint8_t> file) {
    if (file.size() != kPlayersFileSize) return std::nullopt;
    PlayersDat d;
    d.roster = Roster::fromBytes(file.first<kRosterSize>());
    if (!d.roster.magicOk()) return std::nullopt;
    for (int s = 0; s < kPlayerRecordCount; ++s)
        d.records[static_cast<size_t>(s)] = PlayerRecord::fromBytes(file.subspan(recordOffset(s)).first<kPlayerRecordSize>());
    return d;
}

std::vector<uint8_t> PlayersDat::bytes() const {
    std::vector<uint8_t> b(kPlayersFileSize);
    const std::span<uint8_t> all(b);
    roster.toBytes(all.first<kRosterSize>());
    for (int s = 0; s < kPlayerRecordCount; ++s)
        records.at(static_cast<size_t>(s)).toBytes(all.subspan(recordOffset(s)).first<kPlayerRecordSize>());
    return b;
}

PlayersDat PlayersDat::create(int rosterIndex) {
    // Roster_LoadOrCreate 2caa:004f: the header and index, written first (Players_Write 0x8A0).
    PlayersDat d;
    for (int e = 0; e < kMaxPlayers; ++e) d.roster.entries[static_cast<size_t>(e)].slot = static_cast<uint16_t>(e);
    // Player_InitRecord, then Players_SaveCurrent for slot 0..98: the record goes to its slot and
    // its first 22 bytes to index entry g_rosterIndex.
    for (int s = 0; s < kPlayerRecordCount; ++s) {
        PlayerRecord rec = PlayerRecord::blank();
        rec.slot = static_cast<int16_t>(s);
        d.records[static_cast<size_t>(s)] = rec;
        d.roster.entries.at(static_cast<size_t>(rosterIndex)) = RosterEntry::ofRecord(rec);
    }
    return d;
}

}  // namespace tmport::gg
