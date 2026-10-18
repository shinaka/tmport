#pragma once
// Win16 NE ("New Executable") reader: MZ/NE headers, segment table, per-segment relocation
// records, entry table, imported module names, resource table (alignment shift, TYPEINFO /
// NAMEINFO, integer and named ids) and the type-15 RT_NAMETABLE that maps Mac-style type names
// ("PMAP", "SSND", ...) to integer resource types (re/docs/sys_resources_saves.md §5.1).
// Header tables are read once; segment data and resource payloads are read on demand.

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "vfs/data_source.h"

namespace tmport::ne {

struct Segment {
    uint32_t fileOffset = 0;  // sector << alignShift (0 = no data in the file)
    uint32_t length = 0;      // bytes in the file (0 in the table means 0x10000 when fileOffset != 0)
    uint16_t flags = 0;
    uint32_t minAlloc = 0;    // 0 in the table means 0x10000
    bool isData() const { return (flags & 0x0001) != 0; }
    bool hasRelocations() const { return (flags & 0x0100) != 0; }
};

// Relocation address types (what is patched at the site).
enum class RelocAddr : uint8_t {
    LowByte = 0,
    Selector = 2,
    FarPointer = 3,  // 16:16
    Offset = 5,
    FarPointer48 = 11,
    Offset32 = 13,
};

struct RelocTarget {
    enum class Kind { Internal, ImportOrdinal, ImportName, OsFixup };
    Kind kind = Kind::Internal;
    // Internal: segment number (1-based) and offset. A reference to a movable entry (segment
    // byte 0xFF in the record) is resolved through the entry table when possible.
    uint16_t segment = 0;
    uint16_t offset = 0;
    bool viaEntryTable = false;
    uint16_t entryOrdinal = 0;
    // Imports.
    uint16_t moduleIndex = 0;  // 1-based index into the module reference table
    std::string module;
    uint16_t ordinal = 0;
    std::string name;
    // OS fix-up.
    uint16_t osType = 0;
};

struct Relocation {
    uint8_t addressType = 0;  // RelocAddr
    uint16_t site = 0;        // offset inside the segment that is patched
    bool additive = false;    // add the target to the value stored at the site
    RelocTarget target;
};

struct Resource {
    uint16_t typeId = 0;   // integer type (bit 15 cleared); 0 for a named type
    std::string typeName;  // named type (empty for an integer type)
    uint16_t id = 0;       // integer id (bit 15 cleared); 0 for a named id
    std::string name;      // named id (empty for an integer id)
    uint32_t fileOffset = 0;  // offset << alignShift
    uint32_t length = 0;      // length << alignShift ("rounded size", what SizeofResource returns)
    uint16_t flags = 0;
    bool integerType() const { return typeName.empty(); }
    bool integerId() const { return name.empty(); }
};

// One record of the type-15 RT_NAMETABLE: {u16 cb; u16 typeOrd|0x8000; u16 id; "TYPE\0"; "\0"}.
struct NameTableEntry {
    uint16_t typeOrdinal = 0;  // without bit 15 (e.g. 32513)
    uint16_t idOrdinal = 0;
    std::string typeName;      // e.g. "PMAP"
    std::string idName;        // empty in all shipped files
};

struct EntryPoint {
    uint16_t ordinal = 0;
    uint16_t segment = 0;
    uint16_t offset = 0;
    uint8_t flags = 0;
    bool movable = false;
};

class NeImage {
public:
    // Parses the headers. name is used in messages (usually the file name). Throws FormatError.
    static NeImage open(std::shared_ptr<vfs::File> file, std::string name);

    const std::string& name() const { return name_; }
    uint64_t fileSize() const;

    // NE header fields.
    uint32_t neOffset() const { return neOffset_; }
    uint16_t flags() const { return flags_; }
    uint16_t autoDataSegment() const { return autoData_; }
    uint16_t heapSize() const { return heap_; }
    uint16_t stackSize() const { return stack_; }
    uint16_t alignShift() const { return alignShift_; }
    uint8_t targetOs() const { return targetOs_; }
    const std::string& moduleName() const { return moduleName_; }
    const std::vector<std::string>& importedModules() const { return modules_; }

    // Segments, 1-based numbering as in the NE file (segment(1) is the first).
    size_t segmentCount() const { return segments_.size(); }
    const Segment& segment(int number) const;
    const std::vector<Segment>& segments() const { return segments_; }
    std::vector<uint8_t> segmentData(int number) const;
    // Relocations with fix-up chains expanded: one entry per patched site.
    std::vector<Relocation> relocations(int number) const;
    // Number of records in the segment's relocation table (before chain expansion).
    size_t relocationRecordCount(int number) const;

    std::optional<EntryPoint> entry(uint16_t ordinal) const;
    const std::vector<EntryPoint>& entries() const { return entries_; }

    // Resources.
    uint16_t resourceAlignShift() const { return rsrcShift_; }
    const std::vector<Resource>& resources() const { return resources_; }
    const Resource* findResource(uint16_t typeId, uint16_t id) const;  // integer type and id
    std::vector<uint8_t> readResource(const Resource& r) const;          // rounded length, zero-padded

    // The type-15 name table (empty if the file has none).
    const std::vector<NameTableEntry>& nameTable() const { return nameTable_; }
    // Resolves (Mac type name, id) through the name table, case-insensitively. nullptr if the
    // pair is not listed or its NE resource does not exist.
    const Resource* findByTypeName(std::string_view typeName, uint16_t id) const;
    // Ids listed in the name table for a type name, in table order.
    std::vector<uint16_t> idsForTypeName(std::string_view typeName) const;

private:
    NeImage() = default;
    void parseResources(const std::vector<uint8_t>& hdr);
    void parseNameTable();
    void parseEntries(const std::vector<uint8_t>& hdr, uint16_t entOff, uint16_t entLen);

    std::shared_ptr<vfs::File> file_;
    std::string name_;
    uint32_t neOffset_ = 0;
    uint16_t flags_ = 0;
    uint16_t autoData_ = 0;
    uint16_t heap_ = 0;
    uint16_t stack_ = 0;
    uint16_t alignShift_ = 0;
    uint8_t targetOs_ = 0;
    uint16_t rsrcShift_ = 0;
    std::string moduleName_;
    std::vector<std::string> modules_;
    std::vector<uint8_t> importNames_;  // imported-names table bytes
    std::vector<Segment> segments_;
    std::vector<EntryPoint> entries_;
    std::vector<Resource> resources_;
    std::map<std::pair<uint16_t, uint16_t>, size_t> resourceIndex_;
    std::vector<NameTableEntry> nameTable_;
    std::map<std::pair<std::string, uint16_t>, size_t> nameIndex_;  // (UPPER type name, id) -> resource
};

}  // namespace tmport::ne
