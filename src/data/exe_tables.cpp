#include "data/exe_tables.h"

#include <algorithm>
#include <cstring>
#include <format>

#include "data/dgroup_offsets.h"
#include "util/byte_reader.h"
#include "util/error.h"
#include "util/log.h"

namespace tmport {

ExeTables::ExeTables(std::shared_ptr<const ne::NeImage> exe) : exe_(std::move(exe)) {
    segNum_ = exe_->autoDataSegment();
    if (segNum_ == 0 || segNum_ > exe_->segmentCount())
        throw FormatError(std::format("{}: no automatic data segment", exe_->name()));
    data_ = exe_->segmentData(segNum_);
    baseSize_ = data_.size();

    // Resolve the far pointers through the relocation records (dgroup_map.md §1.1).
    ByteReader r(data_, exe_->name() + " DGROUP");
    const auto relocs = exe_->relocations(segNum_);
    relocCount_ = relocs.size();
    for (const ne::Relocation& rel : relocs) {
        if (rel.addressType != static_cast<uint8_t>(ne::RelocAddr::FarPointer)) {
            log::debug("{}: DGROUP relocation at {:04x} has address type {} (ignored)", exe_->name(), rel.site,
                       rel.addressType);
            continue;
        }
        FarPtr p;
        switch (rel.target.kind) {
            case ne::RelocTarget::Kind::Internal:
                p.segment = rel.target.segment;
                p.offset = rel.target.offset;
                if (rel.additive) p.offset = static_cast<uint16_t>(p.offset + r.u16(rel.site));
                p.kind = p.segment == segNum_ ? FarPtr::Kind::DGroup : FarPtr::Kind::Segment;
                break;
            case ne::RelocTarget::Kind::ImportOrdinal:
            case ne::RelocTarget::Kind::ImportName:
                p.kind = FarPtr::Kind::Import;
                p.module = rel.target.module;
                p.ordinal = rel.target.ordinal;
                p.name = rel.target.name;
                break;
            case ne::RelocTarget::Kind::OsFixup:
                p.kind = FarPtr::Kind::Unrelocated;
                break;
        }
        farPtrs_[rel.site] = std::move(p);
    }

    // Relocation-applied copy of the image.
    patched_ = data_;
    for (const auto& [site, p] : farPtrs_) {
        if (p.kind != FarPtr::Kind::DGroup && p.kind != FarPtr::Kind::Segment) continue;
        if (static_cast<size_t>(site) + 4 > patched_.size()) continue;
        const uint16_t sel = p.selector();
        patched_[site] = static_cast<uint8_t>(p.offset & 0xFF);
        patched_[site + 1u] = static_cast<uint8_t>(p.offset >> 8);
        patched_[site + 2u] = static_cast<uint8_t>(sel & 0xFF);
        patched_[site + 3u] = static_cast<uint8_t>(sel >> 8);
    }
}

std::unique_ptr<ExeTables> ExeTables::load(const vfs::DataSource& source) {
    auto file = source.open("TMTWINCD.EXE");
    if (!file) throw MissingFileError("TMTWINCD.EXE", "the game file TMTWINCD.EXE was not found in " + source.describe());
    auto img = std::make_shared<const ne::NeImage>(ne::NeImage::open(std::move(file), "TMTWINCD.EXE"));
    return std::make_unique<ExeTables>(std::move(img));
}

uint8_t ExeTables::u8(uint32_t off) const { return ByteReader(data_, "DGROUP").u8(off); }
int8_t ExeTables::i8(uint32_t off) const { return ByteReader(data_, "DGROUP").i8(off); }
uint16_t ExeTables::u16(uint32_t off) const { return ByteReader(data_, "DGROUP").u16(off); }
int16_t ExeTables::i16(uint32_t off) const { return ByteReader(data_, "DGROUP").i16(off); }
uint32_t ExeTables::u32(uint32_t off) const { return ByteReader(data_, "DGROUP").u32(off); }
int32_t ExeTables::i32(uint32_t off) const { return ByteReader(data_, "DGROUP").i32(off); }
std::span<const uint8_t> ExeTables::bytes(uint32_t off, size_t count) const {
    return ByteReader(data_, "DGROUP").bytes(off, count);
}

std::span<const uint8_t> ExeTables::cstr(uint32_t off) const {
    if (off >= data_.size()) throw FormatError(std::format("DGROUP: string offset {:04x} outside the segment", off));
    size_t e = off;
    while (e < data_.size() && data_[e] != 0) ++e;
    if (e == data_.size()) throw FormatError(std::format("DGROUP: unterminated string at {:04x}", off));
    return std::span<const uint8_t>(data_).subspan(off, e - off);
}

FarPtr ExeTables::farPtr(uint32_t off) const {
    if (off > 0xFFFF) throw FormatError("DGROUP: offset outside the segment");
    if (auto it = farPtrs_.find(static_cast<uint16_t>(off)); it != farPtrs_.end()) return it->second;
    FarPtr p;
    const uint16_t o = u16(off), s = u16(off + 2);
    if (o == 0 && s == 0) return p;  // NULL
    p.kind = FarPtr::Kind::Unrelocated;
    p.segment = s;
    p.offset = o;
    return p;
}

std::optional<uint16_t> ExeTables::dgPtr(uint32_t off) const {
    const FarPtr p = farPtr(off);
    if (p.kind != FarPtr::Kind::DGroup) return std::nullopt;
    return p.offset;
}

std::span<const uint8_t> ExeTables::farCstr(uint32_t off) const {
    auto t = dgPtr(off);
    if (!t) return {};
    return cstr(*t);
}

ExeTables::Validation ExeTables::validate() const {
    Validation v;
    auto check = [&](bool ok, std::string what) {
        v.messages.push_back((ok ? "ok:   " : "FAIL: ") + what);
        if (!ok) v.ok = false;
    };
    auto hasBytes = [&](uint32_t off, const char* s, size_t n) {
        return off + n <= data_.size() && std::memcmp(data_.data() + off, s, n) == 0;
    };
    check(segNum_ == dg::kSegmentNumber, std::format("auto-data segment is {} (expected {})", segNum_, dg::kSegmentNumber));
    check(baseSize_ == dg::kLength, std::format("DGROUP length {:#x} (expected {:#x})", baseSize_, dg::kLength));
    check(hasBytes(dg::kUsersMagic, "USR\x1A", 4), std::format("\"USR\\x1A\" at DS:{:04x}", dg::kUsersMagic));
    check(hasBytes(dg::kRundTag, "RUND", 4), std::format("\"RUND\" at DS:{:04x}", dg::kRundTag));
    check(farPtrs_.size() == dg::kRelocationCount,
          std::format("{} far-pointer relocations (expected {})", farPtrs_.size(), dg::kRelocationCount));
    size_t intoDg = 0;
    for (const auto& [site, p] : farPtrs_)
        if (p.kind == FarPtr::Kind::DGroup) ++intoDg;
    v.messages.push_back(std::format("info: {} far pointers into DGROUP, {} into code", intoDg, farPtrs_.size() - intoDg));
    return v;
}

std::array<RoomDef, 50> ExeTables::rooms() const {
    std::array<RoomDef, 50> rooms{};
    for (int i = 0; i < dg::kRoomCount; ++i) {
        const uint32_t o = dg::kRooms + static_cast<uint32_t>(i) * dg::kRoomSize;
        RoomDef& r = rooms[static_cast<size_t>(i)];
        r.startX = i16(o + 0x00);
        r.startY = i16(o + 0x02);
        r.startFrame = u8(o + 0x04);
        r.startFacingLeft = u8(o + 0x05);
        r.exitLeft = u8(o + 0x06);
        r.exitRight = u8(o + 0x07);
        r.exitDoor = u8(o + 0x08);
        r.unused09 = u8(o + 0x09);
        r.typeLeft = u8(o + 0x0A);
        r.typeRight = u8(o + 0x0B);
        r.typeDoor = u8(o + 0x0C);
        r.unused0D = u8(o + 0x0D);
        r.doorX = i16(o + 0x0E);
        r.unused10 = i16(o + 0x10);
        r.bgPmap = u16(o + 0x12);
        for (uint32_t k = 0; k < 3; ++k) {
            r.slotObject[k] = u8(o + 0x14 + k);
            r.slotContent[k] = u8(o + 0x17 + k);
        }
        r.level = u8(o + 0x1A);
        r.music = u8(o + 0x1B);
    }
    return rooms;
}

std::array<Rgb8, 16> ExeTables::egaColours() const {
    std::array<Rgb8, 16> c{};
    for (uint32_t i = 0; i < 16; ++i) {
        const uint32_t o = dg::kEgaRgb + i * 6;
        c[i] = Rgb8{static_cast<uint8_t>(u16(o)), static_cast<uint8_t>(u16(o + 2)), static_cast<uint8_t>(u16(o + 4))};
    }
    return c;
}

std::array<uint8_t, 16> ExeTables::vga16Index() const {
    std::array<uint8_t, 16> v{};
    for (uint32_t i = 0; i < 16; ++i) v[i] = static_cast<uint8_t>(u16(dg::kVga16Index + i * 2) & 0x0F);
    return v;
}

void ExeTables::patch(uint32_t off, std::span<const uint8_t> bytes) {
    if (off > baseSize_ || bytes.size() > baseSize_ - off)
        throw FormatError(std::format("DGROUP patch of {} byte(s) at {:04x} is outside the image", bytes.size(), off));
    for (const auto& [site, p] : farPtrs_)
        if (site + 4u > off && site < off + bytes.size())
            throw FormatError(std::format("DGROUP patch at {:04x} would overwrite the far pointer at {:04x}", off, site));
    std::copy(bytes.begin(), bytes.end(), data_.begin() + off);
    std::copy(bytes.begin(), bytes.end(), patched_.begin() + off);
}

uint16_t ExeTables::repointString(uint32_t site, std::span<const uint8_t> text) {
    auto it = site <= 0xFFFF ? farPtrs_.find(static_cast<uint16_t>(site)) : farPtrs_.end();
    if (it == farPtrs_.end() || it->second.kind != FarPtr::Kind::DGroup)
        throw FormatError(std::format("DGROUP {:04x} is not a far pointer into DGROUP", site));
    const size_t off = data_.size();
    if (off + text.size() + 1 > 0xFFFF) throw FormatError("DGROUP: no room for more strings (64 KB segment)");
    data_.insert(data_.end(), text.begin(), text.end());
    data_.push_back(0);
    patched_.insert(patched_.end(), text.begin(), text.end());
    patched_.push_back(0);
    it->second.offset = static_cast<uint16_t>(off);
    patched_[site] = static_cast<uint8_t>(off & 0xFF);
    patched_[site + 1u] = static_cast<uint8_t>(off >> 8);
    return static_cast<uint16_t>(off);
}

}  // namespace tmport
