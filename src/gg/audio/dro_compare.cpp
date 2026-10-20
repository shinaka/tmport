#include "gg/audio/dro_compare.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>

#include "gg/audio/gg_audio.h"
#include "gg/audio/snd_driver.h"
#include "util/error.h"

namespace tmport::gg {

namespace {

bool isTimerReg(uint8_t reg) { return reg >= 0x02 && reg <= 0x04; }

// DOSBox-X's dump value of a cached register (the key and drum bits silenced).
uint8_t dumpValue(int reg, uint8_t v) {
    if (reg >= 0xB0 && reg <= 0xB8) return static_cast<uint8_t>(v & ~0x20);
    if (reg == 0xBD) return static_cast<uint8_t>(v & ~0x1F);
    return v;
}

// Writes before each entry in its tick, of its kind (inside the tick, or a game call).
std::vector<int> inTickCounts(std::span<const dos::OplWriteLog::Entry> log) {
    std::vector<int> k(log.size(), 0);
    for (size_t i = 1; i < log.size(); ++i)
        if (log[i].tick == log[i - 1].tick && log[i].command == log[i - 1].command) k[i] = k[i - 1] + 1;
    return k;
}

}  // namespace

DroFile parseDro(std::span<const uint8_t> d) {
    static constexpr char kMagic[] = "DBRAWOPL";
    if (d.size() < 26 || !std::equal(kMagic, kMagic + 8, d.begin())) throw FormatError("DRO: not a DOSBox raw OPL capture");
    const auto u16 = [&](size_t at) { return static_cast<uint16_t>(d[at] | (d[at + 1] << 8)); };
    const auto u32 = [&](size_t at) { return static_cast<uint32_t>(u16(at) | (static_cast<uint32_t>(u16(at + 2)) << 16)); };
    if (u16(8) != 2) throw FormatError(std::format("DRO: version {} (only 2.0 is read)", u16(8)));
    DroFile f;
    const uint32_t pairs = u32(12);
    f.lengthMs = u32(16);
    f.hardware = d[20];
    const uint8_t shortDelay = d[23], longDelay = d[24], mapSize = d[25];
    size_t p = 26 + mapSize;
    if (p > d.size() || mapSize > 128) throw FormatError("DRO: bad codemap");
    const auto map = d.subspan(26, mapSize);
    uint32_t t = 0;
    for (uint32_t k = 0; k < pairs; ++k, p += 2) {
        if (p + 2 > d.size()) throw FormatError("DRO: truncated");
        const uint8_t c = d[p], v = d[p + 1];
        if (c == shortDelay) {
            t += v + 1u;
        } else if (c == longDelay) {
            t += (v + 1u) << 8;
        } else {
            if ((c & 0x7F) >= mapSize) throw FormatError("DRO: register index outside the codemap");
            f.writes.push_back({t, static_cast<uint16_t>((c & 0x80 ? 0x100 : 0) | map[c & 0x7F]), v});
        }
    }
    while (f.dumpSize < f.writes.size() && !(f.writes[f.dumpSize].reg < 0x100 &&
                                              isCaptureTrigger(static_cast<uint8_t>(f.writes[f.dumpSize].reg), f.writes[f.dumpSize].value)))
        ++f.dumpSize;
    return f;
}

std::vector<dos::OplWriteLog::Entry> parseOplLogText(std::string_view text) {
    std::vector<dos::OplWriteLog::Entry> out;
    size_t lineNo = 0;
    while (!text.empty()) {
        const size_t nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) continue;
        std::array<std::string_view, 4> f{};
        size_t n = 0;
        while (!line.empty() && n < f.size()) {
            const size_t a = line.find_first_not_of(' ');
            if (a == std::string_view::npos) break;
            line = line.substr(a);
            const size_t b = line.find(' ');
            f[n++] = line.substr(0, b);
            line = b == std::string_view::npos ? std::string_view{} : line.substr(b);
        }
        dos::OplWriteLog::Entry e;
        unsigned reg = 0, value = 0;
        const auto num = [](std::string_view s, auto& v, int base) {
            const auto r = std::from_chars(s.data(), s.data() + s.size(), v, base);
            return r.ec == std::errc() && r.ptr == s.data() + s.size();
        };
        if (n < 3 || !num(f[0], e.tick, 10) || !num(f[1], reg, 16) || !num(f[2], value, 16) || reg > 0xFF || value > 0xFF)
            throw FormatError(std::format("OPL log line {}: expected 'tick reg value [c]'", lineNo));
        e.reg = static_cast<uint8_t>(reg);
        e.value = static_cast<uint8_t>(value);
        e.command = n == 4 && f[3] == "c";
        out.push_back(e);
    }
    return out;
}

bool isCaptureTrigger(uint8_t reg, uint8_t value) {
    return (reg >= 0xB0 && reg <= 0xB8 && (value & 0x20)) || (reg == 0xBD && (value & 0x3F) > 0x20);
}

SimulatedCapture simulateCapture(std::span<const dos::OplWriteLog::Entry> log, size_t from) {
    SimulatedCapture cap;
    std::array<uint8_t, 256> cache{};
    const auto k = inTickCounts(log);
    bool open = false;
    for (size_t i = 0; i < log.size(); ++i) {
        const auto& w = log[i];
        if (isTimerReg(w.reg)) continue;
        if (!open) {
            if (i >= from && isCaptureTrigger(w.reg, w.value)) {
                open = true;
                for (int r = 0; r < 256; ++r)
                    if (const uint8_t v = dumpValue(r, cache[static_cast<size_t>(r)])) cap.dump.push_back({0, static_cast<uint16_t>(r), v});
                cap.writes.push_back({w.tick, w.reg, w.value, w.command, i, k[i]});
            }
        } else if (cache[w.reg] != w.value) {
            cap.writes.push_back({w.tick, w.reg, w.value, w.command, i, k[i]});
        }
        cache[w.reg] = w.value;
    }
    return cap;
}

namespace {

// The writes DOSBox-X logs once the capture is open at entry `i` (the trigger, then the writes that
// change a register), the filter starting from `cache` (the registers before the trigger).
std::vector<CaptureWrite> captureFrom(std::span<const dos::OplWriteLog::Entry> log, size_t i, std::array<uint8_t, 256> cache,
                                      const std::vector<int>& k, size_t limit = SIZE_MAX) {
    std::vector<CaptureWrite> out;
    for (size_t j = i; j < log.size() && out.size() < limit; ++j) {
        const auto& x = log[j];
        if (isTimerReg(x.reg) || (j > i && cache[x.reg] == x.value)) continue;
        cache[x.reg] = x.value;
        out.push_back({x.tick, x.reg, x.value, x.command, j, k[j]});
    }
    return out;
}

}  // namespace

DroComparison compareWithDro(const DroFile& ref, std::span<const dos::OplWriteLog::Entry> log, size_t fromIndex, double writeMs,
                             CompareBase base) {
    DroComparison res;
    const std::span<const DroWrite> tail = std::span(ref.writes).subspan(ref.dumpSize);
    res.refWrites = tail.size();
    if (tail.empty()) return res;
    std::array<uint8_t, 256> dump{};
    for (size_t d = 0; d < ref.dumpSize; ++d)
        if (ref.writes[d].reg < 0x100) dump[ref.writes[d].reg] = ref.writes[d].value;
    // The filter's registers at a trigger: the engine's own, or the capture's dump with the key and
    // drum bits (masked in the dump) from the engine.
    const auto baseAt = [&](const std::array<uint8_t, 256>& engine) {
        if (base == CompareBase::Engine) return engine;
        std::array<uint8_t, 256> c = dump;
        for (int r = 0xB0; r <= 0xB8; ++r) c[static_cast<size_t>(r)] = static_cast<uint8_t>((dump[static_cast<size_t>(r)] & ~0x20) | (engine[static_cast<size_t>(r)] & 0x20));
        c[0xBD] = static_cast<uint8_t>((dump[0xBD] & ~0x1F) | (engine[0xBD] & 0x1F));
        return c;
    };
    const auto k = inTickCounts(log);
    std::array<uint8_t, 256> cache{};
    size_t bestLen = 0;
    std::array<uint8_t, 256> bestCache{};
    for (size_t i = 0; i < log.size(); ++i) {
        const auto& w = log[i];
        if (isTimerReg(w.reg)) continue;
        if (i >= fromIndex && w.reg == tail[0].reg && w.value == tail[0].value && isCaptureTrigger(w.reg, w.value)) {
            // the run of identical writes when the capture opens here
            std::array<uint8_t, 256> c = baseAt(cache);
            c[w.reg] = w.value;
            size_t n = 1;
            for (size_t j = i + 1; j < log.size() && n < tail.size(); ++j) {
                const auto& x = log[j];
                if (isTimerReg(x.reg) || c[x.reg] == x.value) continue;
                if (x.reg != tail[n].reg || x.value != tail[n].value) break;
                c[x.reg] = x.value;
                ++n;
            }
            if (n > bestLen) {
                bestLen = n;
                res.found = true;
                res.triggerIndex = i;
                res.triggerTick = w.tick;
                bestCache = cache;
                if (n == tail.size()) break;
            }
        }
        cache[w.reg] = w.value;
    }
    if (!res.found) {
        res.firstMismatch = std::format("no engine key-on equals the capture's first one ({:02x} {:02x})", tail[0].reg, tail[0].value);
        return res;
    }
    // The dump against the engine's registers at the trigger.
    for (int r = 0; r < 256; ++r) {
        if (isTimerReg(static_cast<uint8_t>(r))) continue;
        const uint8_t mine = dumpValue(r, bestCache[static_cast<size_t>(r)]);
        const uint8_t theirs = dump[static_cast<size_t>(r)];
        if (mine == 0 && theirs == 0) continue;
        if (mine == theirs) {
            ++res.dumpEqual;
        } else if (++res.dumpDiffer <= 24) {
            res.dumpDiffs += std::format("{}{:02x} {:02x}/{:02x}", res.dumpDiffs.empty() ? "" : ", ", r, mine, theirs);
        }
    }
    const std::vector<CaptureWrite> cap = captureFrom(log, res.triggerIndex, baseAt(bestCache), k);
    res.engineWrites = cap.size();
    const size_t n = std::min(cap.size(), tail.size());
    while (res.matched < n && cap[res.matched].reg == tail[res.matched].reg && cap[res.matched].value == tail[res.matched].value)
        ++res.matched;
    if (res.matched < tail.size()) {
        const DroWrite& r = tail[res.matched];
        res.firstMismatch = std::format(".dro write {} ({} ms: {:02x} {:02x})", res.matched, r.ms, r.reg, r.value);
        if (res.matched < cap.size()) {
            const CaptureWrite& e = cap[res.matched];
            res.firstMismatch += std::format(", engine tick {}{}: {:02x} {:02x}", e.tick, e.command ? " (call)" : "", e.reg, e.value);
        } else {
            res.firstMismatch += ", engine: no more writes";
        }
    }
    for (size_t i = 0; i < res.matched; ++i) res.pairs.push_back({tail[i].ms, cap[i].tick, cap[i].inTick, cap[i].command});
    // Timing of the matched writes made inside a tick.
    const double tickMs = 1000.0 / ggSoundTickHz();
    std::vector<double> x;
    std::vector<size_t> at;
    for (size_t i = 0; i < res.matched; ++i)
        if (!cap[i].command) {
            x.push_back(tail[i].ms - static_cast<double>(cap[i].tick) * tickMs - cap[i].inTick * writeMs);
            at.push_back(i);
        }
    res.matchedOnTime = res.matched;
    if (!x.empty()) {
        std::vector<double> s = x;
        std::nth_element(s.begin(), s.begin() + static_cast<ptrdiff_t>(s.size() / 2), s.end());
        res.offsetMs = s[s.size() / 2];
        res.minDev = 1e9;
        res.maxDev = -1e9;
        for (size_t j = 0; j < x.size(); ++j) {
            const double dv = (x[j] - res.offsetMs) / tickMs;
            if (std::abs(dv) > 0.5 && res.matchedOnTime == res.matched) {
                res.matchedOnTime = at[j];
                res.offGridTick = cap[at[j]].tick;
            }
            res.minDev = std::min(res.minDev, dv);
            res.maxDev = std::max(res.maxDev, dv);
            res.within025 += std::abs(dv) <= 0.25;
            res.within05 += std::abs(dv) <= 0.5;
        }
        res.timed = x.size();
    }
    return res;
}

void seedB0Shadows(SndDriver& driver, const DroFile& capture) {
    for (size_t i = 0; i < capture.dumpSize; ++i)
        if (capture.writes[i].reg >= 0xB0 && capture.writes[i].reg <= 0xB5)
            driver.setOplB0Shadow(capture.writes[i].reg - 0xB0, capture.writes[i].value);
}

std::string DroComparison::summary() const {
    if (!found) return firstMismatch;
    std::string s = std::format("capture opens at engine log entry {} (tick {}); dump: {} registers equal, {} differ{}\n", triggerIndex,
                                triggerTick, dumpEqual, dumpDiffer, dumpDiffs.empty() ? "" : " (engine/.dro: " + dumpDiffs + ")");
    s += std::format("identical in order: {} of {} .dro writes, {} before the first one off the tick grid (engine: {} logged by the "
                     "capture rules)\n",
                     matched, refWrites, matchedOnTime, engineWrites);
    if (!firstMismatch.empty()) s += "first difference: " + firstMismatch + "\n";
    if (matchedOnTime < matched) s += std::format("first write off the tick grid: write {}, engine tick {}\n", matchedOnTime, offGridTick);
    if (timed)
        s += std::format("timing: .dro ms = {:.2f} + tick * {:.4f} + write * k; {} writes in -0.25..+0.25 tick, {} in 0.5, of {} "
                         "(min {:+.3f}, max {:+.3f})\n",
                         offsetMs, 1000.0 / ggSoundTickHz(), within025, within05, timed, minDev, maxDev);
    return s;
}

}  // namespace tmport::gg
