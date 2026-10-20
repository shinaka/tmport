#pragma once
// New Player Hints (ovl12 3060; re/gg/docs/code_ui.md §13): the hint box and the 5-slot queue of
// delayed hints. The calls around the box (Hud_EscHook, the chime 0x1056 cut by the dialog's sound
// pause, the cursor forced to the arrow, the modal loop) are the game's (wave 3).
//
// Hint_Show(id) 3060:013b: when hints are on and hint id was not shown to this player: Font_Select(0),
// Mouse_ForceDefault(1), the text = a private copy of 'H' resource id (FF 06 designer note FF, then
// the text: Text_Render skips the note), Dlg_MessageBox({"New Player Hint" DS:2462, text, the Enter
// footer DS:2472}). Then, always (also when hints are off): seen[id - 0x1043] = 1.

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "gg/ui/dialog.h"

namespace tmport::gg {

class Archive;

// The hints are 0x1043..0x1055 (kFirstHintId, kHintCount of gg/save/players_dat.h: g_hintSeen DS:45a4).
inline constexpr uint16_t kHintChime = 0x1056;  // speaker; 0x1057 FM; priority 0x32

// The hint box of Hint_Show: the descriptor and the text copy it points into.
struct HintBox {
    std::vector<uint8_t> text;  // the whole 'H' payload (note included), as Res_LoadCopy gives it
    DialogDesc desc;            // Dlg_MessageBox(g_hintBox DS:2456)

    // Throws std::out_of_range for a missing resource.
    static HintBox load(const DialogStrings& strings, const Archive& archive, uint16_t id);
    HintBox() = default;
    HintBox(HintBox&&) noexcept = default;  // desc points into `text`'s heap bytes, which a move keeps
    HintBox& operator=(HintBox&&) noexcept = default;
};

// The queue g_hintSlots DS:a0de (5 records of 18 bytes, linked in due order in g_hintList DS:a0da).
class HintQueue {
public:
    static constexpr int kSlots = 5;
    // A cancel flag (+0xe far pointer to a game word: DS:458a for 0x1046, DS:458c for 0x1048); empty =
    // none. True = cancelled.
    using Cancel = std::function<bool()>;

    // 3060:0013 Hint_Init / 3060:01fb Hint_ClearQueue: every slot free, the list empty.
    void clear();
    // 3060:0037 Hint_Queue(id, delay, cancel): nothing when hints are off or the hint was shown;
    // else the first free slot gets {id, due = now + delay (u32), cancel}, inserted before the first
    // pending hint whose due time is later by Hint_CompareDue (3060:0000: the difference of the low
    // words, signed; equal times keep queue order). With all five slots taken the original writes a
    // sixth record past the array (over DS:a138); the engine keeps it in a sixth slot and logs it.
    void queue(uint16_t id, uint16_t delay, uint32_t now, bool hintsOff, bool seen, Cancel cancel = {});
    // 3060:00bc Hint_Update: if the first pending hint is due (due < now, u32, strict): it is removed
    // and its id returned for Hint_Show unless its cancel flag is set (then it is dropped unseen and
    // can be queued again). At most one per call.
    std::optional<uint16_t> update(uint32_t now);

    int pending() const { return static_cast<int>(order_.size()); }
    // The pending ids in due order (tests).
    std::vector<uint16_t> pendingIds() const;

private:
    struct Slot {
        int id = -1;  // +8: -1 free
        uint32_t due = 0;
        Cancel cancel;
    };
    Slot slots_[kSlots + 1];
    std::vector<int> order_;  // slot indices in list order
};

}  // namespace tmport::gg
