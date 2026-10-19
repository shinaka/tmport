#include "dos/app/script_input.h"

#include <algorithm>
#include <charconv>
#include <format>

#include "util/file_util.h"

namespace tmport::dos {

namespace {

std::vector<std::string_view> split(std::string_view s, char sep) {
    std::vector<std::string_view> out;
    size_t pos = 0;
    while (true) {
        const size_t e = s.find(sep, pos);
        out.push_back(s.substr(pos, e == std::string_view::npos ? std::string_view::npos : e - pos));
        if (e == std::string_view::npos) break;
        pos = e + 1;
    }
    return out;
}

template <class T>
bool parseNumber(std::string_view s, T& out) {
    const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc() && p == s.data() + s.size() && !s.empty();
}

constexpr uint64_t kPpm = ScriptEvent::kOnTick;

// "N" or "N.F" (at most 6 decimals) as a time in millionths of a game tick.
bool parseTime(std::string_view s, uint64_t& out) {
    const size_t dot = s.find('.');
    uint64_t whole = 0;
    if (!parseNumber(s.substr(0, dot), whole)) return false;
    uint64_t frac = 0;
    if (dot != std::string_view::npos) {
        const std::string_view f = s.substr(dot + 1);
        if (f.empty() || f.size() > 6 || !parseNumber(f, frac)) return false;
        for (size_t i = f.size(); i < 6; ++i) frac *= 10;
    }
    out = whole * kPpm + frac;
    return true;
}

// An event at time `pos` (millionths of a game tick): applied before the first game tick at or
// after that time.
ScriptEvent eventAt(uint64_t pos, ScriptEvent::Kind kind) {
    ScriptEvent e;
    e.kind = kind;
    if (pos % kPpm == 0) {
        e.tick = pos / kPpm;
        e.phasePpm = ScriptEvent::kOnTick;
    } else {
        e.tick = pos / kPpm + 1;
        e.phasePpm = static_cast<uint32_t>(pos % kPpm);
    }
    return e;
}

}  // namespace

void ScriptedInput::add(const ScriptEvent& e) {
    const auto it = std::upper_bound(events_.begin(), events_.end(), e, [](const ScriptEvent& a, const ScriptEvent& x) {
        return a.tick < x.tick || (a.tick == x.tick && a.phasePpm < x.phasePpm);
    });
    const auto idx = static_cast<size_t>(it - events_.begin());
    events_.insert(it, e);
    if (idx < next_) ++next_;  // inserted into the part already taken
}

std::span<const ScriptEvent> ScriptedInput::take(uint64_t n) {
    const size_t first = next_;
    while (next_ < events_.size() && events_[next_].tick <= n) ++next_;
    return std::span<const ScriptEvent>(events_.data() + first, next_ - first);
}

std::optional<ScriptedInput> ScriptedInput::parse(std::string_view text, std::string& error) {
    ScriptedInput s;
    uint64_t prevPos = 0;  // the previous item's time, in millionths of a game tick
    std::string normalized(text);
    std::replace_if(normalized.begin(), normalized.end(), [](char c) { return c == ',' || c == '\n' || c == '\r' || c == '\t'; }, ' ');
    for (std::string_view item : split(normalized, ' ')) {
        if (item.empty()) continue;
        const auto parts = split(item, ':');
        auto fail = [&](std::string_view why) {
            error = std::format("script item '{}': {}", item, why);
            return std::nullopt;
        };
        if (parts.size() < 2) return fail("expected TICK:ACTION");
        uint64_t pos = 0;
        if (parts[0].starts_with('+')) {
            uint64_t d = 0;
            if (!parseTime(parts[0].substr(1), d)) return fail("bad relative tick");
            pos = prevPos + d;
        } else if (!parseTime(parts[0], pos)) {
            return fail("bad tick");
        }
        prevPos = pos;
        const std::string action = toLowerAscii(parts[1]);
        auto key = [&](size_t i) -> std::optional<PcKey> { return i < parts.size() ? pcKeyByName(parts[i]) : std::nullopt; };
        if (action == "down" || action == "up") {
            const auto k = key(2);
            if (!k || parts.size() != 3) return fail("unknown key");
            ScriptEvent e = eventAt(pos, action == "down" ? ScriptEvent::Kind::KeyDown : ScriptEvent::Kind::KeyUp);
            e.key = *k;
            s.add(e);
        } else if (action == "tap") {
            const auto k = key(2);
            uint64_t hold = 1;
            if (!k || parts.size() > 4 || (parts.size() == 4 && (!parseNumber(parts[3], hold) || hold == 0)))
                return fail("expected tap:KEY[:N]");
            ScriptEvent e = eventAt(pos, ScriptEvent::Kind::KeyDown);
            e.key = *k;
            s.add(e);
            e = eventAt(pos + hold * kPpm, ScriptEvent::Kind::KeyUp);
            e.key = *k;
            s.add(e);
        } else if (action == "move" || action == "click") {
            int x = 0, y = 0;
            if (parts.size() != 4 || !parseNumber(parts[2], x) || !parseNumber(parts[3], y)) return fail("expected X:Y");
            ScriptEvent e = eventAt(pos, ScriptEvent::Kind::MouseMove);
            e.x = x;
            e.y = y;
            s.add(e);
            if (action == "click") {
                ScriptEvent b = eventAt(pos, ScriptEvent::Kind::MouseButtons);
                b.buttons = 1;
                s.add(b);
                b = eventAt(pos + kPpm, ScriptEvent::Kind::MouseButtons);
                b.buttons = 0;
                s.add(b);
            }
        } else if (action == "buttons") {
            uint16_t m = 0;
            if (parts.size() != 3 || !parseNumber(parts[2], m)) return fail("expected buttons:MASK");
            ScriptEvent e = eventAt(pos, ScriptEvent::Kind::MouseButtons);
            e.buttons = m;
            s.add(e);
        } else {
            return fail("unknown action");
        }
    }
    return s;
}

}  // namespace tmport::dos
