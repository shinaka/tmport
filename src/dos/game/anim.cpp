// anim.c (segment 4306): sprite slots, the animation script interpreter and the actor drawing
// (code_player.md §3). The scripts are read from the writable copy PlayerState::animScriptData
// (animScriptWord), so the end sequence's patch of script 0x88's frame word shows.
#include "dos/game/anim.h"

#include "dos/game/game.h"
#include "dos/game/sound.h"

namespace tmport::dos {

namespace {

// Script op codes (code_player.md §3.1). Any other negative value at the start of an entry is
// taken as its first frame code, not as an op.
constexpr int16_t kOpPlayerDX = -2;   // g_playerDX += v
constexpr int16_t kOpPlayerDY = -3;   // g_playerDY += v
constexpr int16_t kOpObjDX = -4;      // slot.obj->dx += v (elf scripts)
constexpr int16_t kOpSfx = -5;        // Sfx_Play(v)
constexpr int16_t kOpStrike = -6;     // g_creatureStrike += 1
constexpr int16_t kOpMoveSlot = -7;   // slot.x += a; slot.y += b
constexpr int16_t kOpEnd = -10;       // end of the script

bool validSlot(int s) { return s >= 0 && s < kSpriteSlots; }

int16_t scriptWord(const DosGame& g, int anim, int i) { return animScriptWord(g, g.tables, anim, i); }

// The object of op -4: an elf record (or the player). A null or stray pointer would write to
// whatever memory it addresses in the original; no shipped script reaches that, so it is ignored.
Obj* opObject(DosGame& g, DsPtr p) {
    if (p.off == kPlayerObjOff) return &g.player.obj;
    const int rel = static_cast<int>(p.off) - static_cast<int>(dg::kElves);
    if (p.off == 0 || rel < 0 || rel % 0x20 != 0 || rel / 0x20 >= kElfCount) return nullptr;
    return &g.elves.elves[static_cast<size_t>(rel / 0x20)].obj;
}

}  // namespace

// 4306:0022 Anim_Tick
void animTick(DosGame& g) {
    if (g.world.transition != 0) return;
    const int count = (g.core.uiMode & 0x7F) == 0 ? kSpriteSlots : 1;  // only the pointer in UI modes
    const bool held = g.stepDone();  // copied once: every slot due this tick advances together
    for (int i = 0; i < count; ++i) {
        SpriteSlot& s = g.player.slots[static_cast<size_t>(i)];
        if (s.anim == 0) continue;
        const int anim = s.anim;
        int16_t delay = static_cast<int16_t>(s.delay - 1);
        if (delay == -1) {
            if (held) {
                delay = 0;  // a step is pending: retry next tick
            } else {
                const int n = scriptWord(g, anim, 1);
                delay = scriptWord(g, anim, 0);
                int k = 2 + s.pos;  // the entry to process
                int pos = s.pos + n;
                switch (scriptWord(g, anim, k)) {
                    case kOpPlayerDX:
                        g.player.obj.dx = static_cast<int16_t>(g.player.obj.dx + scriptWord(g, anim, k + 1));
                        k += 2;
                        pos += 2;
                        break;
                    case kOpPlayerDY:
                        g.player.obj.dy = static_cast<int16_t>(g.player.obj.dy + scriptWord(g, anim, k + 1));
                        k += 2;
                        pos += 2;
                        break;
                    case kOpObjDX:
                        if (Obj* o = opObject(g, s.obj)) o->dx = static_cast<int16_t>(o->dx + scriptWord(g, anim, k + 1));
                        k += 2;
                        pos += 2;
                        break;
                    case kOpSfx: {
                        const int id = scriptWord(g, anim, k + 1);
                        k += 2;
                        pos += 2;
                        sfxPlay(g, id);
                        break;
                    }
                    case kOpStrike:
                        k += 1;
                        pos += 1;
                        ++g.world.creatureStrike;
                        break;
                    case kOpMoveSlot:
                        s.x = static_cast<int16_t>(s.x + scriptWord(g, anim, k + 1));
                        s.y = static_cast<int16_t>(s.y + scriptWord(g, anim, k + 2));
                        k += 3;
                        pos += 3;
                        break;
                    case kOpEnd:  // the last frames stay; no step is flagged
                        s.prev = s.anim;
                        s.anim = 0;
                        delay = 0;
                        pos = 0;
                        break;
                    default:
                        break;
                }
                s.pos = static_cast<uint16_t>(pos);
                if (pos != 0) {
                    for (int j = 0; j < n; ++j) {
                        if (!validSlot(i + j)) break;
                        SpriteSlot& t = g.player.slots[static_cast<size_t>(i + j)];
                        t.frame = static_cast<uint16_t>(scriptWord(g, anim, k + j));
                        t.z = s.z;
                        t.owner = static_cast<uint16_t>(i);
                    }
                    g.stepGate().mark();
                }
            }
        }
        s.delay = delay;
    }
}

// 4306:02c0 Anim_InitSlots
void animInitSlots(DosGame& g) {
    for (int i = 0; i < kSpriteSlots; ++i) {
        SpriteSlot& s = g.player.slots[static_cast<size_t>(i)];
        const DsPtr obj = s.obj;  // +0x12 is not touched
        s = SpriteSlot{};
        s.frame = 0xFFFF;
        s.owner = static_cast<uint16_t>(i);
        s.obj = obj;
    }
    g.sprites.reset();  // the draw records and order, g_spritesChanged = 0, g_saveHeapReset = 1
    g.player.animInitMark = 0;
}

// 4306:0393 Sprites_Snapshot
void spritesSnapshot(DosGame& g, int from, int to) {
    if (g.world.sceneWanted != g.core.level) return;
    for (int i = from; i < to; ++i) {
        if (!validSlot(i)) break;
        const SpriteSlot& s = g.player.slots[static_cast<size_t>(i)];
        // An ended or stopped script keeps its record frozen at the last position and frame until
        // the slot gets a new script or is cleared (moving such a slot does not move its sprite).
        if (s.anim == 0 && s.prev != 0) continue;
        DrawRecord& r = g.sprites.records[static_cast<size_t>(i)];
        if (r.x != s.x || r.y != s.y || r.z != s.z || r.frame != s.frame) {
            g.sprites.spritesChanged = true;
            r.x = s.x;
            r.y = s.y;
            r.z = s.z;
            r.frame = s.frame;
        }
        r.owner = s.owner;
    }
}

// 4306:0497 Sprites_DrawSorted
void spritesDrawSorted(DosGame& g, int from, int to) {
    if (!g.bank) return;
    const int drawn = g.sprites.drawSorted(g.back, *g.bank, from, to, g.core.uiMode == 0x80, g.core.dirty != 0);
    g.core.dirty = static_cast<uint16_t>(g.core.dirty + drawn);  // g_dirty += 1 per sprite drawn
}

// 4306:06dd Sprites_EraseAll
void spritesEraseAll(DosGame& g, int from, int to) { g.sprites.eraseAll(g.back, from, to); }

// 4306:0784 Anim_SetIfIdle
void animSetIfIdle(DosGame& g, int slot, int anim) {
    if (!validSlot(slot)) return;
    SpriteSlot& s = g.player.slots[static_cast<size_t>(slot)];
    if (s.anim == 0) s.anim = static_cast<uint16_t>(anim);
}

// 4306:07b9 Anim_Queue
void animQueue(DosGame& g, int slot, int anim) {
    if (!validSlot(slot)) return;
    SpriteSlot& s = g.player.slots[static_cast<size_t>(slot)];
    if (s.prev != anim) {
        animStop(g, slot);
        s.anim = static_cast<uint16_t>(anim);
    }
}

// 4306:07fa Anim_Set
void animSet(DosGame& g, int slot, int anim) {
    if (!validSlot(slot)) return;
    const SpriteSlot& s = g.player.slots[static_cast<size_t>(slot)];
    if (s.anim == anim) return;
    if (s.anim != 0) animStop(g, slot);
    animStart(g, slot, anim);
}

// 4306:0844 Anim_Stop
void animStop(DosGame& g, int slot) {
    if (!validSlot(slot)) return;
    SpriteSlot& s = g.player.slots[static_cast<size_t>(slot)];
    if (s.anim == 0) return;
    s.prev = s.anim;  // the caller overwrites anim
    s.delay = 0;
    s.pos = 0;
}

// 4306:088e Anim_Start
void animStart(DosGame& g, int slot, int anim) {
    if (!validSlot(slot)) return;
    g.player.slots[static_cast<size_t>(slot)].anim = static_cast<uint16_t>(anim);
    // The first entry's frames are shown at once; a leading op word below -1 is skipped as a
    // two-word op without being executed (pos and delay stay 0: the first tick processes entry 0
    // again, its op included).
    const int k = scriptWord(g, anim, 2) < -1 ? 4 : 2;
    const int n = scriptWord(g, anim, 1);
    const int16_t z = g.player.slots[static_cast<size_t>(slot)].z;
    for (int j = 0; j < n; ++j) {
        if (!validSlot(slot + j)) break;
        SpriteSlot& t = g.player.slots[static_cast<size_t>(slot + j)];
        t.frame = static_cast<uint16_t>(scriptWord(g, anim, k + j));
        t.z = z;
        t.owner = static_cast<uint16_t>(slot);
    }
}

// 4306:092f Anim_Clear
void animClear(DosGame& g, int slot) {
    if (!validSlot(slot)) return;
    SpriteSlot& s = g.player.slots[static_cast<size_t>(slot)];
    s.anim = 0;
    s.prev = 0;
    s.delay = 0;
    s.pos = 0;
    s.frame = 0xFFFF;
}

}  // namespace tmport::dos
