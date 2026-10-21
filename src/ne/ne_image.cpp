#include "ne/ne_image.h"

#include <algorithm>
#include <format>

#include "mischief/util/byte_reader.h"
#include "mischief/util/error.h"
#include "mischief/util/file_util.h"

namespace tmport::ne {

namespace {

// Alignment shift counts come from the file. Real files use 4..9; anything above 15 would shift a
// 16-bit field past 32 bits (UB) or point gigabytes into the file (REVIEW_1 L-2).
constexpr uint16_t kMaxAlignShift = 15;

std::string pascalString(const mischief::ByteReader& r, size_t off) {
    const uint8_t n = r.u8(off);
    auto b = r.bytes(off + 1, n);
    return std::string(b.begin(), b.end());
}

}  // namespace

NeImage NeImage::open(std::shared_ptr<mischief::vfs::File> file, std::string name) {
    if (!file) throw mischief::Error(std::format("{}: no file", name));
    NeImage img;
    img.file_ = std::move(file);
    img.name_ = std::move(name);

    const auto mz = img.file_->readRange(0, 0x40);
    mischief::ByteReader mzr(mz, img.name_ + " (MZ header)");
    if (mz.size() < 0x40 || mz[0] != 'M' || mz[1] != 'Z') throw mischief::FormatError(img.name_ + ": not an MZ executable");
    img.neOffset_ = mzr.u32(0x3C);

    // All NE tables start within 64 KB of the NE header (their offsets are 16-bit); the entry
    // table may extend a little further. Read one block that covers them.
    const uint64_t fsize = img.file_->size();
    if (img.neOffset_ + 0x40ull > fsize) throw mischief::FormatError(img.name_ + ": NE header outside the file");
    const size_t blobLen = static_cast<size_t>(std::min<uint64_t>(fsize - img.neOffset_, 0x20000));
    const std::vector<uint8_t> hdr = img.file_->readRange(img.neOffset_, blobLen);
    mischief::ByteReader r(hdr, img.name_ + " (NE header)");
    if (r.u8(0) != 'N' || r.u8(1) != 'E') throw mischief::FormatError(img.name_ + ": not an NE executable");

    const uint16_t entOff = r.u16(0x04);
    const uint16_t entLen = r.u16(0x06);
    img.flags_ = r.u16(0x0C);
    img.autoData_ = r.u16(0x0E);
    img.heap_ = r.u16(0x10);
    img.stack_ = r.u16(0x12);
    const uint16_t segCount = r.u16(0x1C);
    const uint16_t modCount = r.u16(0x1E);
    const uint16_t segTab = r.u16(0x22);
    const uint16_t rsrcTab = r.u16(0x24);
    const uint16_t resNames = r.u16(0x26);
    const uint16_t modRef = r.u16(0x28);
    const uint16_t impNames = r.u16(0x2A);
    img.alignShift_ = r.u16(0x32);
    if (img.alignShift_ == 0) img.alignShift_ = 9;
    if (img.alignShift_ > kMaxAlignShift)
        throw mischief::FormatError(std::format("{}: invalid NE segment alignment shift {}", img.name_, img.alignShift_));
    img.targetOs_ = r.u8(0x36);

    // Segment table.
    for (uint16_t i = 0; i < segCount; ++i) {
        const size_t o = segTab + static_cast<size_t>(i) * 8;
        Segment s;
        const uint16_t sector = r.u16(o);
        const uint16_t len = r.u16(o + 2);
        s.flags = r.u16(o + 4);
        const uint16_t minAlloc = r.u16(o + 6);
        s.fileOffset = static_cast<uint32_t>(sector) << img.alignShift_;
        s.length = sector == 0 ? 0 : (len == 0 ? 0x10000u : len);
        s.minAlloc = minAlloc == 0 ? 0x10000u : minAlloc;
        img.segments_.push_back(s);
    }

    // Resident names: the first entry is the module name.
    if (resNames != 0 && resNames < hdr.size() && r.u8(resNames) != 0) img.moduleName_ = pascalString(r, resNames);

    // Imported names and module references.
    if (impNames != 0 && impNames <= hdr.size()) {
        const size_t end = (entOff > impNames && entOff <= hdr.size()) ? entOff : hdr.size();
        img.importNames_.assign(hdr.begin() + impNames, hdr.begin() + static_cast<std::ptrdiff_t>(end));
        mischief::ByteReader ir(img.importNames_, img.name_ + " (imported names)");
        for (uint16_t i = 0; i < modCount; ++i) {
            const uint16_t off = r.u16(modRef + static_cast<size_t>(i) * 2);
            img.modules_.push_back(off < img.importNames_.size() ? pascalString(ir, off) : std::string());
        }
    }

    img.parseEntries(hdr, entOff, entLen);

    if (rsrcTab != 0 && rsrcTab != resNames) {
        mischief::ByteReader rr(std::span<const uint8_t>(hdr).subspan(rsrcTab), img.name_ + " (resource table)");
        img.rsrcShift_ = rr.u16(0);
        if (img.rsrcShift_ > kMaxAlignShift)
            throw mischief::FormatError(std::format("{}: invalid NE resource alignment shift {}", img.name_, img.rsrcShift_));
        // A resource may end at most one alignment unit past the end of the file (the last one can
        // lack its padding); readResource() allocates the declared length, so nothing longer is
        // accepted (REVIEW_1 L-2: up to 0xFFFF << 15 bytes could otherwise be zero-filled).
        const uint64_t resourceLimit = fsize + (uint64_t{1} << img.rsrcShift_);
        size_t p = 2;
        for (;;) {
            const uint16_t typeId = rr.u16(p);
            if (typeId == 0) break;
            const uint16_t count = rr.u16(p + 2);
            p += 8;
            std::string typeName;
            if (!(typeId & 0x8000)) typeName = pascalString(rr, typeId);
            for (uint16_t k = 0; k < count; ++k, p += 12) {
                Resource res;
                res.typeId = (typeId & 0x8000) ? static_cast<uint16_t>(typeId & 0x7FFF) : 0;
                res.typeName = typeName;
                const uint16_t off = rr.u16(p);
                const uint16_t len = rr.u16(p + 2);
                res.flags = rr.u16(p + 4);
                const uint16_t id = rr.u16(p + 6);
                if (id & 0x8000)
                    res.id = static_cast<uint16_t>(id & 0x7FFF);
                else
                    res.name = pascalString(rr, id);
                res.fileOffset = static_cast<uint32_t>(off) << img.rsrcShift_;
                res.length = static_cast<uint32_t>(len) << img.rsrcShift_;
                if (static_cast<uint64_t>(res.fileOffset) + res.length > resourceLimit)
                    throw mischief::FormatError(std::format("{}: resource {} of type {} ({} bytes at {:#x}) extends past the end of "
                                                  "the file ({} bytes)",
                                                  img.name_, id & 0x7FFF, typeId & 0x7FFF, res.length, res.fileOffset,
                                                  fsize));
                if (res.integerType() && res.integerId())
                    img.resourceIndex_.emplace(std::make_pair(res.typeId, res.id), img.resources_.size());
                img.resources_.push_back(std::move(res));
            }
        }
        img.parseNameTable();
    }
    return img;
}

void NeImage::parseEntries(const std::vector<uint8_t>& hdr, uint16_t entOff, uint16_t entLen) {
    if (entOff == 0 || entLen == 0 || entOff >= hdr.size()) return;
    const size_t end = std::min<size_t>(hdr.size(), static_cast<size_t>(entOff) + entLen);
    mischief::ByteReader r(std::span<const uint8_t>(hdr).subspan(entOff, end - entOff), name_ + " (entry table)");
    size_t p = 0;
    uint16_t ordinal = 1;
    while (p < r.size()) {
        const uint8_t count = r.u8(p);
        if (count == 0) break;
        const uint8_t type = r.u8(p + 1);
        p += 2;
        for (uint8_t i = 0; i < count; ++i, ++ordinal) {
            if (type == 0) continue;  // unused ordinals, no data
            EntryPoint e;
            e.ordinal = ordinal;
            if (type == 0xFF) {  // movable: flags, INT 3Fh, segment, offset
                e.flags = r.u8(p);
                e.segment = r.u8(p + 3);
                e.offset = r.u16(p + 4);
                e.movable = true;
                p += 6;
            } else {  // fixed segment number (0xFE = constant)
                e.flags = r.u8(p);
                e.segment = type == 0xFE ? 0 : type;
                e.offset = r.u16(p + 1);
                p += 3;
            }
            entries_.push_back(e);
        }
    }
}

void NeImage::parseNameTable() {
    const Resource* nt = nullptr;
    for (const Resource& res : resources_)
        if (res.integerType() && res.typeId == 15) {
            nt = &res;
            break;
        }
    if (!nt) return;
    const std::vector<uint8_t> data = readResource(*nt);
    mischief::ByteReader r(data, name_ + " (RT_NAMETABLE)");
    size_t p = 0;
    while (r.has(p, 2)) {
        const uint16_t cb = r.u16(p);
        if (cb == 0) break;
        if (cb < 8 || !r.has(p, cb)) throw mischief::FormatError(std::format("{}: bad name-table entry at {:#x}", name_, p));
        NameTableEntry e;
        e.typeOrdinal = static_cast<uint16_t>(r.u16(p + 2) & 0x7FFF);
        e.idOrdinal = static_cast<uint16_t>(r.u16(p + 4) & 0x7FFF);
        size_t q = p + 6;
        const size_t end = p + cb;
        while (q < end && r.u8(q) != 0) e.typeName.push_back(static_cast<char>(r.u8(q++)));
        ++q;
        while (q < end && r.u8(q) != 0) e.idName.push_back(static_cast<char>(r.u8(q++)));
        if (auto it = resourceIndex_.find({e.typeOrdinal, e.idOrdinal}); it != resourceIndex_.end())
            nameIndex_.emplace(std::make_pair(mischief::toUpperAscii(e.typeName), e.idOrdinal), it->second);
        nameTable_.push_back(std::move(e));
        p += cb;
    }
}

uint64_t NeImage::fileSize() const { return file_->size(); }

const Segment& NeImage::segment(int number) const {
    if (number < 1 || static_cast<size_t>(number) > segments_.size())
        throw mischief::Error(std::format("{}: no segment {}", name_, number));
    return segments_[static_cast<size_t>(number - 1)];
}

std::vector<uint8_t> NeImage::segmentData(int number) const {
    const Segment& s = segment(number);
    if (s.fileOffset == 0) return {};
    auto data = file_->readRange(s.fileOffset, s.length);
    if (data.size() != s.length) throw mischief::FormatError(std::format("{}: segment {} is truncated", name_, number));
    return data;
}

size_t NeImage::relocationRecordCount(int number) const {
    const Segment& s = segment(number);
    if (!s.hasRelocations() || s.fileOffset == 0) return 0;
    const auto cnt = file_->readRange(static_cast<uint64_t>(s.fileOffset) + s.length, 2);
    if (cnt.size() != 2) throw mischief::FormatError(std::format("{}: relocation table of segment {} is missing", name_, number));
    return static_cast<size_t>(cnt[0] | (cnt[1] << 8));
}

std::vector<Relocation> NeImage::relocations(int number) const {
    std::vector<Relocation> out;
    const Segment& s = segment(number);
    const size_t count = relocationRecordCount(number);
    if (count == 0) return out;
    const auto recs = file_->readRange(static_cast<uint64_t>(s.fileOffset) + s.length + 2, count * 8);
    mischief::ByteReader rr(recs, std::format("{} (relocations of segment {})", name_, number));
    const std::vector<uint8_t> data = segmentData(number);
    mischief::ByteReader dr(data, std::format("{} (segment {})", name_, number));
    mischief::ByteReader ir(importNames_, name_ + " (imported names)");

    for (size_t k = 0; k < count; ++k) {
        const size_t o = k * 8;
        const uint8_t atype = rr.u8(o);
        const uint8_t rtype = rr.u8(o + 1);
        const uint16_t site = rr.u16(o + 2);
        const uint16_t a = rr.u16(o + 4);
        const uint16_t b = rr.u16(o + 6);

        RelocTarget t;
        switch (rtype & 3) {
            case 0:
                t.kind = RelocTarget::Kind::Internal;
                if ((a & 0xFF) == 0xFF) {
                    t.viaEntryTable = true;
                    t.entryOrdinal = b;
                    if (auto e = entry(b)) {
                        t.segment = e->segment;
                        t.offset = e->offset;
                    }
                } else {
                    t.segment = static_cast<uint16_t>(a & 0xFF);
                    t.offset = b;
                }
                break;
            case 1:
                t.kind = RelocTarget::Kind::ImportOrdinal;
                t.moduleIndex = a;
                t.ordinal = b;
                if (a >= 1 && a <= modules_.size()) t.module = modules_[a - 1];
                break;
            case 2:
                t.kind = RelocTarget::Kind::ImportName;
                t.moduleIndex = a;
                if (a >= 1 && a <= modules_.size()) t.module = modules_[a - 1];
                if (b < importNames_.size()) t.name = pascalString(ir, b);
                break;
            default:
                t.kind = RelocTarget::Kind::OsFixup;
                t.osType = a;
                break;
        }

        Relocation rel;
        rel.addressType = atype;
        rel.additive = (rtype & 4) != 0;
        rel.target = t;
        if (rel.additive) {
            rel.site = site;
            out.push_back(rel);
            continue;
        }
        // Fix-up chain: each site holds the offset of the next site; 0xFFFF ends the chain.
        uint16_t cur = site;
        for (int guard = 0; cur != 0xFFFF && guard < 0x10000; ++guard) {
            if (!dr.has(cur, 2)) throw mischief::FormatError(std::format("{}: relocation chain leaves segment {}", name_, number));
            rel.site = cur;
            out.push_back(rel);
            cur = dr.u16(cur);
        }
    }
    return out;
}

std::optional<EntryPoint> NeImage::entry(uint16_t ordinal) const {
    for (const auto& e : entries_)
        if (e.ordinal == ordinal) return e;
    return std::nullopt;
}

const Resource* NeImage::findResource(uint16_t typeId, uint16_t id) const {
    auto it = resourceIndex_.find({typeId, id});
    return it == resourceIndex_.end() ? nullptr : &resources_[it->second];
}

std::vector<uint8_t> NeImage::readResource(const Resource& r) const {
    std::vector<uint8_t> data = file_->readRange(r.fileOffset, r.length);
    data.resize(r.length, 0);  // the last resource may lack its padding
    return data;
}

const Resource* NeImage::findByTypeName(std::string_view typeName, uint16_t id) const {
    auto it = nameIndex_.find({mischief::toUpperAscii(typeName), id});
    return it == nameIndex_.end() ? nullptr : &resources_[it->second];
}

std::vector<uint16_t> NeImage::idsForTypeName(std::string_view typeName) const {
    std::vector<uint16_t> ids;
    for (const auto& e : nameTable_)
        if (mischief::iequalsAscii(e.typeName, typeName)) ids.push_back(e.idOrdinal);
    return ids;
}

}  // namespace tmport::ne
