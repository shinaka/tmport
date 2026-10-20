#include "gg/data/data_cli.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <format>
#include <optional>
#include <stdexcept>
#include <string_view>

#include "gg/data/cli_args.h"
#include "gg/data/gg_files.h"
#include "gg/data/resources.h"
#include "util/file_util.h"
#include "util/png_writer.h"

namespace tmport::gg {

namespace {

using cli::hasFlag;
using cli::insideFolder;
using cli::openFolder;

constexpr std::string_view kCommands[] = {"--gg-info", "--gg-list", "--gg-selftest", "--gg-extract"};

void out(const std::string& s) { std::fwrite(s.data(), 1, s.size(), stdout); }
void err(const std::string& s) { std::fwrite(s.data(), 1, s.size(), stderr); }

uint16_t parseId(std::string_view s) {
    if (s.starts_with("0x") || s.starts_with("0X")) s.remove_prefix(2);
    if (s.empty() || s.size() > 4) throw std::runtime_error("bad resource id");
    unsigned v = 0;
    for (const char c : s) {
        const int d = (c >= '0' && c <= '9')   ? c - '0'
                      : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                      : (c >= 'A' && c <= 'F') ? c - 'A' + 10
                                               : -1;
        if (d < 0) throw std::runtime_error("bad resource id");
        v = v * 16 + static_cast<unsigned>(d);
    }
    return static_cast<uint16_t>(v);
}

int cmdInfo(const GgFiles& files) {
    const auto& arc = files.archive();
    out(std::format("folder\t{}\n", pathToUtf8(files.folder())));
    out(std::format("ssg.exe\t{} bytes, {} overlays, DGROUP {:04x} ({:#x} bytes)\n", files.exe().file().size(),
                    files.exe().overlays().size(), GgExe::kDgroupSeg, GgExe::kDgroupSize));
    out(std::format("PLAYERS.DAT\t{}\n", files.playersDatPath() ? pathToUtf8(*files.playersDatPath()) : "(none)"));
    out("volume\tentries\tmethod3\tstored\tstored_bytes\tunpacked_bytes\n");
    size_t all[5] = {};
    for (int v = 0; v < Archive::kVolumes; ++v) {
        size_t n3 = 0, n0 = 0, storedBytes = 0, unpacked = 0;
        for (int k = 0; k < arc.entryCount(v); ++k) {
            const auto e = arc.info(static_cast<uint16_t>((v << 12) | k));
            (e.method == kMethodStored ? n0 : n3)++;
            storedBytes += e.storedSize;
            unpacked += arc.unpackedSize(e.id);
        }
        out(std::format("{}\t{}\t{}\t{}\t{}\t{}\n", Archive::volumeName(v), arc.entryCount(v), n3, n0, storedBytes, unpacked));
        all[0] += static_cast<size_t>(arc.entryCount(v));
        all[1] += n3;
        all[2] += n0;
        all[3] += storedBytes;
        all[4] += unpacked;
    }
    out(std::format("all\t{}\t{}\t{}\t{}\t{}\n", all[0], all[1], all[2], all[3], all[4]));
    return 0;
}

int cmdList(const GgFiles& files, bool leaves) {
    const auto& arc = files.archive();
    out("id\tvol\tindex\tfile_off\ttype\tmethod\tcsize\tusize\tkind\tcrc32\n");
    for (const uint16_t id : arc.ids()) {
        const auto e = arc.info(id);
        const auto r = arc.load(id);
        out(std::format("{:04x}\t{}\t{}\t0x{:06x}\t{}\t{}\t{}\t{}\t{}\t{:08x}\n", id, e.volume, e.index, e.fileOffset,
                        typeName(e.type), e.method, e.storedSize, r.bytes.size(), classify(e.type, r.bytes, id),
                        png::crc32(r.bytes)));
        if (leaves && r.type == static_cast<uint8_t>(ResType::Bundle)) {
            for (const auto& m : parseBundle(r.bytes))
                out(std::format("{:04x}/{}\t{}\t{}\t-\t{}\t{}\t-\t{}\t{}\t{:08x}\n", id, m.index, e.volume, e.index,
                                typeName(m.type), m.method, m.payload.size(), classify(m.type, m.payload, id, m.index),
                                png::crc32(m.payload)));
        }
    }
    return 0;
}

int cmdSelftest(const GgFiles& files) {
    const auto& arc = files.archive();
    size_t entries = 0, lzh = 0, stored = 0, unpacked = 0, largest = 0, window13 = 0, defaults = 0, tokenWords = 0;
    size_t bundles = 0, members = 0;
    int errors = 0;
    const auto fail = [&](const std::string& msg) {
        ++errors;
        out("ERROR " + msg + "\n");
    };
    for (const uint16_t id : arc.ids()) {
        ++entries;
        const auto e = arc.info(id);
        try {
            LzhInfo info;
            const auto r = arc.load(id, &info);
            if (e.method == kMethodStored) {
                ++stored;
            } else {
                ++lzh;
                if (info.windowBits == 13) ++window13;
                if (!info.explicitDistances) ++defaults;
                if (info.ignoredWord == ((info.literals + info.matches) & 0xFFFF)) ++tokenWords;
                if (info.usedBytes != e.storedSize)
                    fail(std::format("{:04x}: {} of {} bytes used", id, info.usedBytes, e.storedSize));
                if (info.sizeHighBits != 3) fail(std::format("{:04x}: size word high bits {}", id, info.sizeHighBits));
            }
            unpacked += r.bytes.size();
            largest = std::max(largest, r.bytes.size());
            if (r.bytes.size() >= 0xFFFF)
                fail(std::format("{:04x}: {} bytes do not fit the 0xFFFF-byte buffer", id, r.bytes.size()));
            if (r.type == static_cast<uint8_t>(ResType::Bundle)) {
                ++bundles;
                for (const auto& m : parseBundle(r.bytes)) {
                    ++members;
                    if (m.type == static_cast<uint8_t>(ResType::Bundle))
                        fail(std::format("{:04x}/{}: nested bundle", id, m.index));
                }
            }
        } catch (const std::exception& ex) {
            fail(std::format("{:04x}: {}", id, ex.what()));
        }
    }
    out(std::format("gg selftest: {} volumes, {} entries ({} method 3, {} stored), {} bytes unpacked, largest {}; "
                    "method 3: window13 {}, default distances {}, ignored word = token count {}; {} bundles with {} "
                    "members; {} error{}\n",
                    Archive::kVolumes, entries, lzh, stored, unpacked, largest, window13, defaults, tokenWords, bundles,
                    members, errors, errors == 1 ? "" : "s"));
    return errors ? 1 : 0;
}

int cmdExtract(const GgFiles& files, const std::vector<std::string>& args) {
    const auto it = std::find(args.begin(), args.end(), "--gg-extract");
    if (args.end() - it < 3) {
        err("usage: --gg-extract ID OUT [--data DIR]\n");
        return 2;
    }
    const std::string spec = *(it + 1);
    const auto outPath = pathFromUtf8(*(it + 2));
    if (insideFolder(outPath, files.folder())) {
        err("refusing to write into the game folder: " + pathToUtf8(outPath) + "\n");
        return 2;
    }
    std::vector<uint8_t> bytes;
    const auto slash = spec.find('/');
    if (slash == std::string::npos) {
        bytes = files.archive().read(parseId(spec));
    } else {
        const auto bundle = Bundle::load(files.archive(), parseId(std::string_view(spec).substr(0, slash)));
        const auto m = bundle.member(static_cast<size_t>(std::stoul(spec.substr(slash + 1))));
        bytes.assign(m.payload.begin(), m.payload.end());
    }
    if (!writeWholeFile(outPath, bytes)) {
        err("cannot write " + pathToUtf8(outPath) + "\n");
        return 1;
    }
    out(std::format("{} -> {} ({} bytes)\n", spec, pathToUtf8(outPath), bytes.size()));
    return 0;
}

}  // namespace

bool dataCli(const std::vector<std::string>& args, int& exitCode) {
    const auto cmd = std::find_if(args.begin(), args.end(), [](const std::string& a) {
        return std::find(std::begin(kCommands), std::end(kCommands), a) != std::end(kCommands);
    });
    if (cmd == args.end()) return false;
    try {
        const GgFiles files = openFolder(args);
        if (*cmd == "--gg-info") exitCode = cmdInfo(files);
        else if (*cmd == "--gg-list") exitCode = cmdList(files, hasFlag(args, "--leaves"));
        else if (*cmd == "--gg-selftest") exitCode = cmdSelftest(files);
        else exitCode = cmdExtract(files, args);
    } catch (const std::exception& e) {
        err(std::string("ggport: ") + e.what() + "\n");
        exitCode = 1;
    }
    std::fflush(stdout);
    return true;
}

}  // namespace tmport::gg
