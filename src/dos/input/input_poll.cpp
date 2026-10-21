#include "dos/input/input_poll.h"

#include <format>

#include "dos/data/dgroup_offsets.h"
#include "dos/data/dos_exe.h"
#include "mischief/util/error.h"

namespace tmport::dos {

namespace {

constexpr uint16_t kZonePtrs = dg::kZoneLists;     // far[6] zone tables
constexpr uint16_t kKeyListPtrs = dg::kKeyLists;  // far[6] key lists
constexpr uint16_t kCmdListPtrs = dg::kCmdLists;  // far[6] command lists

uint16_t dgroupTarget(const DosExe& exe, uint16_t ptrAt, const char* what) {
    const DosExe::FarPtr p = exe.readFarPtr(ptrAt);
    if (p.seg != DosExe::kDgroupSeg)
        throw mischief::FormatError(std::format("SST.EXE: {} pointer at DS:{:04X} leaves DGROUP ({:04X}:{:04X})", what, ptrAt, p.seg, p.off));
    return p.off;
}

void need(const DosExe& exe, uint16_t off, size_t bytes, const char* what) {
    if (exe.dgroup().size() < static_cast<size_t>(off) + bytes)
        throw mischief::FormatError(std::format("SST.EXE: {} at DS:{:04X} is outside DGROUP", what, off));
}

}  // namespace

InputTables::InputTables() {
    for (size_t c = 0; c < zones_.size(); ++c) {
        zones_[c].records.resize(static_cast<size_t>(kZoneCapacity[c]));
        keys_[c].keys.assign(1, 0);
        keys_[c].commands.assign(1, -1);
    }
}

InputTables InputTables::load(const DosExe& exe) {
    InputTables t;
    for (int ctx = 0; ctx < kInputContexts; ++ctx) {
        const auto c = static_cast<size_t>(ctx);
        const uint16_t z = dgroupTarget(exe, static_cast<uint16_t>(kZonePtrs + 4 * ctx), "zone table");
        const int cap = kZoneCapacity[c];
        need(exe, z, 1 + 8 * static_cast<size_t>(cap), "zone table");
        InputZoneTable& zt = t.zones_[c];
        zt.count = exe.ds8(z);
        if (zt.count > cap) throw mischief::FormatError(std::format("SST.EXE: zone table DS:{:04X} has {} records", z, zt.count));
        zt.records.resize(static_cast<size_t>(cap));
        for (int i = 0; i < cap; ++i) {
            const auto r = static_cast<uint16_t>(z + 1 + 8 * i);
            InputZone& zone = zt.records[static_cast<size_t>(i)];
            zone.x1 = exe.ds8(r);
            zone.y1 = exe.ds8(static_cast<uint16_t>(r + 1));
            zone.x2 = exe.ds8(static_cast<uint16_t>(r + 2));
            zone.y2 = exe.ds8(static_cast<uint16_t>(r + 3));
            zone.hover = static_cast<int8_t>(exe.ds8(static_cast<uint16_t>(r + 4)));
            zone.press = static_cast<int8_t>(exe.ds8(static_cast<uint16_t>(r + 5)));
            zone.held = static_cast<int8_t>(exe.ds8(static_cast<uint16_t>(r + 6)));
            zone.release = static_cast<int8_t>(exe.ds8(static_cast<uint16_t>(r + 7)));
        }
        t.zoneAddr_[c] = z;

        const uint16_t k = dgroupTarget(exe, static_cast<uint16_t>(kKeyListPtrs + 4 * ctx), "key list");
        const uint16_t m = dgroupTarget(exe, static_cast<uint16_t>(kCmdListPtrs + 4 * ctx), "command list");
        need(exe, k, 2, "key list");
        const uint16_t n = exe.ds16(k);
        need(exe, k, 2 + 2 * static_cast<size_t>(n), "key list");
        need(exe, m, 2 + 2 * static_cast<size_t>(n), "command list");
        InputKeyMap& km = t.keys_[c];
        km.keys.assign(static_cast<size_t>(n) + 1, 0);
        km.commands.assign(static_cast<size_t>(n) + 1, -1);
        for (uint16_t i = 0; i <= n; ++i) {
            if (i > 0) km.keys[i] = exe.ds16(static_cast<uint16_t>(k + 2 * i));
            km.commands[i] = exe.dsS16(static_cast<uint16_t>(m + 2 * i));
        }
        t.keyAddr_[c] = k;
    }
    return t;
}

// 4a85:0107 Input_LookupKey.
int inputLookupKey(uint16_t key, const InputKeyMap& map) {
    int i = static_cast<int>(map.keys.size()) - 1;
    while (i != 0 && map.keys[static_cast<size_t>(i)] != key) --i;
    return i;
}

// 4a85:0130 Input_HitZone. Byte coordinates compared unsigned with the words DS:911A / 911E.
int8_t inputHitZone(const InputZoneTable& zones, const MousePointer& mouse) {
    for (int i = static_cast<int>(zones.count) - 1; i >= 0; --i) {
        if (static_cast<size_t>(i) >= zones.records.size()) continue;
        const InputZone& z = zones.records[static_cast<size_t>(i)];
        if (z.x1 <= mouse.x && mouse.x <= z.x2 && z.y1 <= mouse.y && mouse.y <= z.y2) {
            const bool was = mouse.prevButtons != 0, now = mouse.buttons != 0;  // any button counts
            if (was && !now) return z.release;
            if (!was && now) return z.press;
            if (!was) return z.hover;
            return z.held;
        }
    }
    return -1;
}

// 4a85:000E Input_Poll.
void InputPoll::poll(mischief::dos::BiosKeyboard& bios, const MousePointer& mouse, uint8_t uiMode) {
    int16_t c;
    if (bios.peek() != 0) {  // bioskey(1)
        lastKey = bios.read();  // bioskey(0)
        const InputKeyMap& map = tables_->keyMap(context);
        c = map.commands[static_cast<size_t>(inputLookupKey(lastKey, map))];
    } else {
        lastKey = 0;
        c = inputHitZone(tables_->zones(context), mouse);
    }
    if (deliverNext || uiMode == 0) {
        // A queued command goes first; the new one waits in the queue.
        if (queuedCmd != -1) {
            cmd = queuedCmd;
            queuedCmd = c;
        } else {
            cmd = c;
        }
        if (cmd != -1) deliverNext = false;
    } else if (c != -1 && c != cmd) {
        queuedCmd = c;  // a UI mode has not consumed the previous command yet
    }
}

// 4a85:03AC Input_Tick.
InputTickAction InputPoll::tick(mischief::dos::BiosKeyboard& bios, const MousePointer& mouse, uint8_t uiMode, bool scriptedMove,
                                bool slowCpu) {
    poll(bios, mouse, uiMode);
    if (cmd == -1 || uiMode != 0) return InputTickAction::None;
    if (scriptedMove) {  // DS:926C: no menus during a scripted move (Esc and 4..11 are kept)
        if ((cmd >= 1 && cmd <= 3) || cmd > 11) cmd = -1;
        return InputTickAction::None;
    }
    switch (cmd) {  // jump table 4a85:03F9
        case 0:  // Esc
            if (slowCpu) return InputTickAction::ExitDialog;
            cmd = 8;  // the menu code takes the queued Enter: the Exit-and-Save box opens at once
            return InputTickAction::MenuEscape;
        case 1:
        case 2:
        case 3:
            return InputTickAction::MenuOpen;
        default:
            return InputTickAction::None;
    }
}

// 4a85:0208 Input_MouseWalk. Compares are unsigned 16-bit: near the left or top edge px - 8 or
// py - 0x1D wrap to large values and the "left" / "above" branches win.
std::optional<int16_t> InputPoll::mouseWalk(KbdIsr& kbd, const MousePointer& mouse, const MouseWalkParams& p) {
    using namespace keybits;
    std::optional<int16_t> prompt;
    if (p.uiMode != 0) return prompt;
    uint16_t& keys = kbd.keys;
    if (!p.scriptedMove) {
        if ((p.terrainMask & 3) == 0) {  // Down cannot climb here: it drops a coin
            prompt = 4;                  // "Press Down to drop coin"
            if (keys & kDown) keys = static_cast<uint16_t>((keys & 0x00FC) | kCoin);  // 16-bit AND: high byte cleared
            else keys &= 0x00BF;
        } else {
            if (cmd == 11) cmd = -1;  // a status-bar click does nothing where Down climbs
            prompt = 0;
        }
    }
    if (cmd >= 4 && cmd <= 7) cmd = -1;  // arrow-key commands are not used in play
    if (!p.mousePresent) return prompt;
    if (cmd == 9) {  // play-field pressed or held
        keys &= 0xFFD0;
        const uint16_t mx = mouse.x, my = mouse.y;
        const auto sx = static_cast<uint16_t>(p.playerScreenX), sy = static_cast<uint16_t>(p.playerScreenY);
        const auto feet = static_cast<uint16_t>(sy + 0x10);
        const auto top = static_cast<uint16_t>(sy - 0x1D);
        const auto left = static_cast<uint16_t>(sx - 8), right = static_cast<uint16_t>(sx + 0x0C);
        if (!(my < top || feet < my)) {  // beside him
            if (mx < static_cast<uint16_t>(sx - 1)) keys |= kLeft;
            if (static_cast<uint16_t>(sx + 5) < mx) keys |= kRight;
        } else if (!(mx < left || right < mx)) {  // above or below him
            if (my < top) keys |= kUp;
            if (feet < my) keys |= kDown;
        } else if (mx < left && my < top) {  // up-left: jump left
            kbd.jumpFromDiagonal = true;
            keys |= kJump | kLeft;
        } else if (right < mx && my < top) {  // up-right: jump right
            kbd.jumpFromDiagonal = true;
            keys |= kJump | kRight;
        } else if (mx < left && feet < my) {  // down-left: net left
            keys |= kNet | kLeft;
        } else if (right < mx && feet < my) {  // down-right: net right
            keys |= kNet | kRight;
        }
        cmd = -1;
    }
    if (cmd == 11) {  // status bar pressed: drop a coin
        keys |= kCoin;
        cmd = -1;
    }
    if (cmd == 10) {  // button released: stop
        keys = 0;
        cmd = -1;
    }
    return prompt;
}

}  // namespace tmport::dos
