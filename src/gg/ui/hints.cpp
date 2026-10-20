#include "gg/ui/hints.h"

#include "gg/data/archive.h"
#include "text/text_engine.h"
#include "util/log.h"

namespace tmport::gg {

// 3060:013b Hint_Show: the box
HintBox HintBox::load(const DialogStrings& strings, const Archive& archive, uint16_t id) {
    HintBox box;
    box.text = archive.read(id);  // Res_LoadCopy
    box.desc = messageBoxDesc(strings.hintTitle, text::untilNul(box.text), strings.hintFooter);
    return box;
}

// 3060:0013 Hint_Init, 3060:01fb Hint_ClearQueue
void HintQueue::clear() {
    for (Slot& s : slots_) s = Slot{};
    order_.clear();
}

// 3060:0037 Hint_Queue
void HintQueue::queue(uint16_t id, uint16_t delay, uint32_t now, bool hintsOff, bool seen, Cancel cancel, uint16_t cancelDs) {
    if (hintsOff || seen) return;
    int k = 0;
    while (k < kSlots && slots_[k].id != -1) ++k;
    if (k == kSlots) {
        log::warn("Hint_Queue({:04x}): all {} slots taken; the original writes past its array", id, kSlots);
        if (slots_[k].id != -1) std::erase(order_, k);  // the original would relink the same record
    }
    slots_[k] = Slot{id, now + delay, std::move(cancel), cancelDs};
    // 19c0:00a6 List_InsertSorted with 3060:0000 Hint_CompareDue(cur, new) = new.due - cur.due on the
    // low words: walk on while the result is >= 0.
    auto pos = order_.begin();
    while (pos != order_.end()) {
        const auto diff = static_cast<int16_t>(static_cast<uint16_t>(slots_[k].due) - static_cast<uint16_t>(slots_[*pos].due));
        if (diff < 0) break;
        ++pos;
    }
    order_.insert(pos, k);
}

// 3060:00bc Hint_Update
std::optional<uint16_t> HintQueue::update(uint32_t now, const CancelByDs& cancelled) {
    if (order_.empty()) return std::nullopt;
    Slot& head = slots_[order_.front()];
    if (!(head.due < now)) return std::nullopt;
    std::optional<uint16_t> show;
    const bool byFunction = head.cancel && head.cancel();
    const bool byFlag = head.cancelDs != 0 && cancelled && cancelled(head.cancelDs);
    if (!byFunction && !byFlag) show = static_cast<uint16_t>(head.id);
    order_.erase(order_.begin());
    head = Slot{};
    return show;
}

HintQueue::SlotInfo HintQueue::slot(int k) const {
    if (k < 0 || k >= kSlots) return {};
    return SlotInfo{slots_[k].id, slots_[k].due, slots_[k].cancelDs};
}

void HintQueue::restore(const std::array<SlotInfo, kSlots>& slots, std::vector<int> order) {
    clear();
    for (int k = 0; k < kSlots; ++k) {
        const SlotInfo& s = slots[static_cast<size_t>(k)];
        slots_[k] = Slot{s.id, s.due, {}, s.cancelDs};
    }
    std::erase_if(order, [](int k) { return k < 0 || k >= kSlots; });
    order_ = std::move(order);
}

std::vector<uint16_t> HintQueue::pendingIds() const {
    std::vector<uint16_t> ids;
    for (const int k : order_) ids.push_back(static_cast<uint16_t>(slots_[k].id));
    return ids;
}

}  // namespace tmport::gg
