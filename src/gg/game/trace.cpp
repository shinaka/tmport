// The per-frame trace record (trace.h).
#include "gg/game/trace.h"

#include <format>

#include "gg/game/game.h"

namespace tmport::gg {

GgTraceRecord traceSnapshot(GgGame& g) {
    GgTraceRecord r;
    r.interrupts = g.clock().interrupts();
    r.ticks = g.ticks();
    r.state = g.flow.state;
    r.displayRoom = static_cast<int16_t>(g.world.rooms.displayRoom);
    const Actor& p = g.actors.actors[kPlayerActor];
    r.playerRoom = static_cast<int16_t>(p.room);
    r.x = static_cast<int16_t>(p.sprite.x);
    r.y = static_cast<int16_t>(p.sprite.y);
    r.seq = static_cast<int16_t>(p.seq);
    r.randCalls = g.rand.calls();
    r.randState = g.rand.seed();
    r.lastCode = g.input().lastCode();
    r.pictures = g.rt.framesShown();
    return r;
}

std::string_view traceCsvHeader() {
    return "frame,yield,interrupts,ticks,state,displayroom,playerroom,x,y,seq,randcalls,randstate,code,pictures,sound";
}

std::string traceCsvRow(const GgTraceRecord& r) {
    std::string yield = yieldPointName(r.yield);
    if (r.slot >= 0) yield += std::format(".{}", r.slot);
    // The sound column is quoted when it is not empty: its calls hold commas (CSV's rule, quotes doubled).
    std::string sound;
    if (!r.sound.empty()) {
        sound = "\"";
        for (const char c : r.sound) sound += c == '"' ? std::string("\"\"") : std::string(1, c);
        sound += '"';
    }
    return std::format("{},{},{},{},{},{},{},{},{},{},{},{:08x},{:04x},{},{}", r.frame, yield, r.interrupts, r.ticks, r.state,
                       r.displayRoom, r.playerRoom, r.x, r.y, r.seq, r.randCalls, r.randState, r.lastCode, r.pictures, sound);
}

std::string traceChanges(const GgTraceRecord* prev, const GgTraceRecord& r) {
    if (!prev)
        return std::format("f={} t={} state={} room={}/{} x={} y={} seq={} rand={}\n", r.frame, r.ticks, r.state,
                           r.displayRoom, r.playerRoom, r.x, r.y, r.seq, r.randCalls);
    std::string ch;
    auto f = [&](const char* name, long long a, long long b) {
        if (a != b) ch += std::format(" {}:{}->{}", name, a, b);
    };
    f("state", prev->state, r.state);
    f("display", prev->displayRoom, r.displayRoom);
    f("room", prev->playerRoom, r.playerRoom);
    f("x", prev->x, r.x);
    f("y", prev->y, r.y);
    f("seq", prev->seq, r.seq);
    f("rand", static_cast<long long>(prev->randCalls), static_cast<long long>(r.randCalls));
    return ch.empty() ? std::string{} : std::format("f={} t={}{}\n", r.frame, r.ticks, ch);
}

}  // namespace tmport::gg
