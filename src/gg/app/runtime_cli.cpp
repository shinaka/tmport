#include "gg/app/runtime_cli.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <exception>
#include <format>
#include <optional>
#include <string_view>

#include "gg/app/gg_runtime.h"
#include "gg/data/cli_args.h"

namespace tmport::gg {

namespace {

using cli::hasFlag;

constexpr std::string_view kCommands[] = {"--gg-clock", "--gg-input-trace"};

void out(const std::string& s) { std::fwrite(s.data(), 1, s.size(), stdout); }

std::vector<std::string> values(const std::vector<std::string>& args, std::string_view name) {
    std::vector<std::string> v;
    for (size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i] == name) v.push_back(args[i + 1]);
    return v;
}

std::optional<std::string> value(const std::vector<std::string>& args, std::string_view name) {
    auto v = values(args, name);
    if (v.empty()) return std::nullopt;
    return v.back();
}

// A decimal number, or hexadecimal with 0x.
template <class T>
T number(std::string_view s, std::string_view what) {
    T v{};
    int base = 10;
    std::string_view d = s;
    if (d.starts_with("0x") || d.starts_with("0X")) {
        d.remove_prefix(2);
        base = 16;
    }
    const auto [p, ec] = std::from_chars(d.data(), d.data() + d.size(), v, base);
    if (d.empty() || ec != std::errc() || p != d.data() + d.size()) throw std::runtime_error(std::format("{}: bad number '{}'", what, s));
    return v;
}

std::vector<int> numberList(std::string_view s, std::string_view what) {
    std::vector<int> v;
    size_t pos = 0;
    while (pos <= s.size()) {
        const size_t e = s.find(',', pos);
        v.push_back(number<int>(s.substr(pos, e == std::string_view::npos ? std::string_view::npos : e - pos), what));
        if (e == std::string_view::npos) break;
        pos = e + 1;
    }
    return v;
}

int cmdClock(const std::vector<std::string>& args) {
    const uint64_t n = number<uint64_t>(value(args, "--ticks").value_or("146"), "--ticks");
    GgClock c;
    uint64_t sound = 0;
    c.setSoundTick([&sound](uint32_t) { ++sound; });
    const uint64_t tN = c.timeOfTick(n);
    uint64_t retraces = 0;
    for (uint64_t t = c.nextRetraceAfter(0); t <= tN; t = c.nextRetraceAfter(t)) ++retraces;
    for (uint64_t i = 0; i < n; ++i) c.tick();
    out(std::format("pit_divisor\t{:#06x}\n", GgClock::kDivisor));
    out(std::format("pit_hz\t{:.6f}\n", c.timeBase().hz()));
    out(std::format("tick_ns\t{}\n", c.timeOfTick(1)));
    out(std::format("sound_hz\t{:.6f}\n", c.timeBase().hz() / 2));
    out(std::format("retrace_hz\t{:.6f}\n", 1e9 * GgClock::kRetraceDen / static_cast<double>(GgClock::kRetraceNum)));
    out(std::format("retrace_ns\t{}\n", c.retraceTimeNs(1)));
    out(std::format("ticks\t{}\n", n));
    out(std::format("time_ns\t{}\n", tN));
    out(std::format("time_s\t{:.9f}\n", static_cast<double>(tN) / 1e9));
    out(std::format("g_ticks\t{}\n", c.ticks()));
    out(std::format("sound_ticks\t{}\n", sound));
    out(std::format("bios_ticks\t{}\n", c.biosTicks()));
    out(std::format("retraces_by_then\t{}\n", retraces));
    return 0;
}

// A cursor with a fixed hot spot that never draws (for --hot); it keeps g_mouseX/Y.
class FixedCursor final : public MouseScreen {
public:
    FixedCursor(int16_t hx, int16_t hy) : hx_(hx), hy_(hy) {}
    bool drawing() const override { return false; }
    int16_t hotX() const override { return hx_; }
    int16_t hotY() const override { return hy_; }
    int16_t cursorX() const override { return x_; }
    int16_t cursorY() const override { return y_; }
    void setCursorPosition(int16_t x, int16_t y) override {
        x_ = x;
        y_ = y;
    }
    void moveCursor(int16_t x, int16_t y) override { setCursorPosition(x, y); }

private:
    int16_t hx_, hy_;
    int16_t x_ = 0, y_ = 0;
};

int cmdInputTrace(const std::vector<std::string>& args) {
    std::string keys, error;
    for (const auto& k : values(args, "--keys")) keys += k + " ";
    for (const auto& k : values(args, "--mouse")) keys += k + " ";
    const auto script = dos::ScriptedInput::parse(keys, error);
    if (!script) throw std::runtime_error(error);
    const uint64_t lastEvent = script->events().empty() ? 0 : script->events().back().tick;
    const uint64_t n = number<uint64_t>(value(args, "--ticks").value_or(std::to_string(lastEvent + 16)), "--ticks");
    const uint64_t every = std::max<uint64_t>(1, number<uint64_t>(value(args, "--poll-every").value_or("4"), "--poll-every"));

    GgRuntime rt;
    rt.installHandlers();
    if (hasFlag(args, "--play-mode")) rt.keyboard().setPlayMode(1);
    std::optional<FixedCursor> cursor;
    if (const auto hot = value(args, "--hot")) {
        const auto v = numberList(*hot, "--hot");
        if (v.size() != 2) throw std::runtime_error("--hot X,Y");
        cursor.emplace(static_cast<int16_t>(v[0]), static_cast<int16_t>(v[1]));
        rt.mouse().setScreen(&*cursor);
    }
    rt.mouse().readDriverPosition();
    std::vector<Zone> zones;
    for (const auto& z : values(args, "--zone")) {
        const auto v = numberList(z, "--zone");
        if (v.size() != 7 && v.size() != 8) throw std::runtime_error("--zone X,Y,W,H,PRI,PRESS,RELEASE[,SHAPE]");
        Zone zone = Zone::rect(static_cast<int16_t>(v[0]), static_cast<int16_t>(v[1]), static_cast<int16_t>(v[2]),
                               static_cast<int16_t>(v[3]), static_cast<int16_t>(v[4]), static_cast<uint16_t>(v[5]),
                               static_cast<uint16_t>(v[6]));
        if (v.size() == 8) zone.shape = static_cast<int16_t>(v[7]);
        zones.push_back(zone);
    }
    for (Zone& z : zones) rt.zones().addLocal(z);  // the vector does not grow any more
    rt.setScript(*script);

    out("tick\tg_ticks\theld\tring_w\tring_r\tbios\tmouse_x\tmouse_y\tbuttons\tpress\tpress_x\tpress_y\tfirst\trelease\trelease_x\t"
        "release_y\tcode\n");
    for (uint64_t i = 1; i <= n; ++i) {
        rt.advanceTicks(1);
        if (i % every != 0) continue;
        // The latches and the ring as the poll finds them, then the poll.
        const InputRecord press = rt.mouse().press(), release = rt.mouse().release();
        const KeyRing& ring = rt.keyboard().ring();
        const int bios = rt.keyboard().bios().size();
        const uint16_t w = ring.writeIndex(), r = ring.readIndex();
        const uint16_t code = rt.input().getCode();
        out(std::format("{}\t{}\t{:#04x}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{:#06x}\n", i, rt.ticks(),
                        rt.keyboard().heldKeys(), w, r, bios, rt.mouse().x(), rt.mouse().y(), rt.mouse().buttons(), press.kind,
                        press.x, press.y, press.first, release.kind, release.x, release.y, code));
    }
    return 0;
}

}  // namespace

bool runtimeCli(const std::vector<std::string>& args, int& exitCode) {
    std::string_view cmd;
    for (const auto& a : args)
        for (const std::string_view c : kCommands)
            if (a == c) cmd = c;
    if (cmd.empty()) return false;
    try {
        exitCode = cmd == "--gg-clock" ? cmdClock(args) : cmdInputTrace(args);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%.*s: %s\n", static_cast<int>(cmd.size()), cmd.data(), e.what());
        exitCode = 1;
    }
    std::fflush(stdout);
    return true;
}

}  // namespace tmport::gg
