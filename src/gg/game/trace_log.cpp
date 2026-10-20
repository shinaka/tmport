// The trace logger and the DGROUP dump (trace_log.h). Owner: flow agent.
#include "gg/game/trace_log.h"

#include <format>
#include <ostream>

#include "gg/game/game.h"
#include "gg/game/sound.h"
#include "util/file_util.h"
#include "util/log.h"

namespace tmport::gg {

// ---------------------------------------------------------------------------------- trace

GgTraceLogger::GgTraceLogger(GgGame& g, std::ostream& out, bool polls) : g_(g), out_(out), polls_(polls) {
    out_ << "# Gizmos & Gadgets engine trace (game/trace.h): one row per "
         << (polls ? "frame boundary and input poll" : "frame boundary (Timer_Wait(0))") << '\n';
    out_ << traceCsvHeader() << '\n';
    previous_ = g_.rt.yieldHook();
    g_.rt.setYieldHook([this](YieldPoint p, int slot) {
        onYield(p, slot);
        if (previous_) previous_(p, slot);
    });
}

GgTraceLogger::~GgTraceLogger() {
    g_.rt.setYieldHook(std::move(previous_));
    out_.flush();
}

bool GgTraceLogger::isBoundary(YieldPoint p, int slot, bool polls) {
    if (p == YieldPoint::TimerWait && slot == 0) return true;
    return polls && (p == YieldPoint::GetInputCode || p == YieldPoint::Idle || p == YieldPoint::TimerExpired);
}

void GgTraceLogger::onYield(YieldPoint p, int slot) {
    if (!isBoundary(p, slot, polls_)) return;
    GgTraceRecord r = traceSnapshot(g_);
    r.frame = ++rows_;
    r.yield = p;
    r.slot = (p == YieldPoint::TimerWait || p == YieldPoint::TimerExpired) ? slot : -1;
    // The sound calls since the previous row when the facade records them (tests); the real sound
    // logs its calls with --verbose instead.
    if (auto* rec = dynamic_cast<RecordingSound*>(&g_.sound)) r.sound = RecordingSound::describe(rec->takeCalls());
    out_ << traceCsvRow(r) << '\n';  // (traceCsvRow quotes the sound column)
    if ((rows_ & 0xFF) == 0) out_.flush();
}

// ---------------------------------------------------------------------------------- the dump

GgStateDumper::GgStateDumper(GgGame& g, uint64_t tick, std::filesystem::path file) : g_(g), tick_(tick), file_(std::move(file)) {
    previous_ = g_.rt.yieldHook();
    g_.rt.setYieldHook([this](YieldPoint p, int slot) {
        onYield(p, slot);
        if (previous_) previous_(p, slot);
    });
}

GgStateDumper::~GgStateDumper() { g_.rt.setYieldHook(std::move(previous_)); }

void GgStateDumper::onYield(YieldPoint, int) {
    if (written_ || g_.clock().interrupts() < tick_) return;
    written_ = true;
    if (writeGgDgroupDump(g_, file_))
        log::info("--gg-dump-state: DGROUP at PIT tick {} (g_ticks {}, g_state {}) -> {}", g_.clock().interrupts(), g_.ticks(),
                  g_.flow.state, pathToUtf8(file_));
    else
        log::error("--gg-dump-state: cannot write {}", pathToUtf8(file_));
}

std::string ggDgroupFieldsTsv() {
    std::string out = "name\tds\tsize\tstride\tservice\tmask\n";
    const auto add = [&](const DgroupField& f) {
        std::string mask;
        if (f.stride != 0 && !f.mask.empty())
            for (size_t i = 0; i < f.stride; ++i) mask += f.mask[i] ? '1' : '0';
        out += std::format("{}\t{:04x}\t{}\t{}\t{}\t{}\n", f.name, f.ds, f.size, f.stride, f.service ? 1 : 0, mask);
    };
    for (const DgroupField& f : gameStateFields()) add(f);
    for (const DgroupField& f : serviceFields()) add(f);
    return out;
}

bool writeGgDgroupDump(GgGame& g, const std::filesystem::path& file) {
    const std::vector<uint8_t> image = dumpToDgroup(g);
    if (!writeWholeFile(file, image)) return false;
    std::filesystem::path tsv = file;
    tsv += ".fields.tsv";
    const std::string text = ggDgroupFieldsTsv();
    return writeWholeFile(tsv, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
}

}  // namespace tmport::gg
