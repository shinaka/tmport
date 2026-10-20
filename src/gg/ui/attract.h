#pragma once
// The attract sequence (state 1, ovl06 2c11; re/gg/docs/code_ui.md §10): the title, the six story
// pages on the airship, the animations between them and the sign-in prompt. This module draws each
// step and runs Attract_PlayAnim's channels; the loop around a step (the 4-tick frames, Timer_Set(1,
// n * 146), the busy polls of Input_GetCode, the fades of Screen_FadeOutIn) belongs to the runtime
// and the game (wave 3), the screen animations themselves to the world area (174b ScreenAnim_*,
// code_world.md §19.1) behind ScreenAnimHost.
//
// State_01_Attract 2c11:0913: Mouse_Hide; effect bank 0x1022 (DS:7a44), music bank 0x1020 played
// (section 0, looping); the full-screen zone DS:0a3a (a click = Enter); g_leaveRequest = 2; Seq_Run of
// the sub-sequence DS:20a8 (step 0 is NULL):
//    1 Attract_TitleAnim    2c11:043f   title: 3 phases of PlayAnim(DS:2090) for 1, 3 and 5 seconds
//    2 Attract_AfterTitle   2c11:05cf   the title background; PlayAnim(DS:2096) watching channel 0
//    3..8 Attract_StoryPage 2c11:0630   texts 0x1019..0x101E on the airship, 30 s each
//    9 Attract_AfterStory   2c11:077f   PlayAnim(DS:209c) watching channel 2
//   10 Attract_BeforePrompt 2c11:07e5   PlayAnim(DS:20a2) watching channel 0
//   11 Attract_SignInPrompt 2c11:084c   the prompt over the sign-in background, 30 s; time-out = 1
//                                       (restart at step 1)
// Each step returns 0 (next), -1 (Space: sign-in; Esc: also g_leaveRequest = -1, quit) or 1.
// Then Pal_FadeOut, Zones_ClearLocal, the banks freed, Mouse_Show; the state returns g_leaveRequest.

#include <cstdint>
#include <span>
#include <vector>

#include "gg/data/resources.h"
#include "gg/gfx/palette.h"
#include "gg/ui/ui_canvas.h"

namespace tmport::gg {

class Archive;
class GgExe;

// Resources and constants of the attract (code_ui.md §10.3, §19).
inline constexpr uint16_t kTitlePicture = 0x1002, kTitlePalette = 0x1000;
inline constexpr uint16_t kTitlePictureSet = 0x1001;   // {I x, y}, G pairs, ended by an I starting with FF
inline constexpr uint16_t kTitleArt = 0x1028;          // the 4 title pictures (Res_LoadCopy)
inline constexpr uint16_t kAfterTitlePalette = 0x1004; // Pal_LoadRes (step 2), Screen_LoadPicture (step 10)
inline constexpr uint16_t kStoryPicture = 0x1009, kStoryPalette = 0x100A;
inline constexpr uint16_t kStoryLine = 0x1024;         // the line under the first page (font 0, spacing 1)
inline constexpr uint16_t kStoryFirstText = 0x1019;    // 6 pages: 0x1019..0x101E
inline constexpr int kStoryPages = 6;
inline constexpr uint16_t kPromptPicture = 0x1026;     // keyed at (93, 33) on the sign-in background
inline constexpr uint16_t kPromptText = 0x1025;        // font 0, colour 15, at (74, 121)
inline constexpr uint16_t kAttractEffects = 0x1022, kAttractMusic = 0x1020;
inline constexpr uint32_t kAttractPageTicks = 0x111C;  // 4380 ticks (30.07 s) per story page and the prompt
inline constexpr int kTitlePhaseSeconds[3] = {1, 3, 5}; // Timer_Set(1, n * 146), tested once per frame
inline constexpr int kTitleEffect[2] = {12, 13};       // effects of bank 0x1022 at phases 2 and 3 (priority 4)

// Attract_PlayAnim's flags (2c11:0000).
inline constexpr int kAnimTimed = 2;     // vararg: seconds; leave when Timer_Expired(1)
inline constexpr int kAnimWatch = 4;     // vararg: a channel; leave when its script reaches -1
inline constexpr int kAnimInput = 8;     // Input_GetCode once per frame; Enter, Esc or Space leave
inline constexpr int kAnimFadeIn = 1;    // fade on the first frame: Pal_FadeIn
inline constexpr int kAnimFadeFromSaved = 2;  // Pal_FadeFromSaved

// An animation descriptor {u16 channel count, far table of far channel scripts} (DS:2090, 2096, 209c,
// 20a2). A script is a u16 list: n > 0 start screen animation n, -2 t wait t frames, -1 end. The
// scripts are kept as DGROUP offsets: a channel whose last animation stops reads on past its -1 into
// the following script, as the original does (code_ui.md §10.2).
struct AttractAnim {
    std::vector<uint16_t> channels;  // DS offset of each channel's script

    static AttractAnim fromExe(const GgExe& exe, uint16_t dsOff);
    // Every animation id of the scripts, in the order Attract_PlayAnim loads them (once per
    // occurrence; a -2 skips its operand; each script up to its first -1).
    std::vector<uint16_t> loadOrder(const GgExe& exe) const;
};

struct AttractAnims {
    AttractAnim title;         // DS:2090: 0x1005, 0x1007 (both loop forever)
    AttractAnim afterTitle;    // DS:2096: wait 50, 0x1015 / 0x1005 / 0x1007
    AttractAnim afterStory;    // DS:209c: 5 channels, 0x100B..0x1013
    AttractAnim beforePrompt;  // DS:20a2: wait 20, 0x1017 / 0x1005 / 0x1007

    static AttractAnims fromExe(const GgExe& exe);
};

// The screen animations Attract_PlayAnim drives (174b; the world area's ScreenAnim). Handles are the
// caller's (one per ScreenAnim_Load).
class ScreenAnimHost {
public:
    virtual ~ScreenAnimHost() = default;
    virtual int load(uint16_t id) = 0;               // 174b:0324 ScreenAnim_Load (a private copy)
    virtual void startAt(int handle, int pos) = 0;    // 174b:02ab ScreenAnim_StartAt: list position, +0x24 = 1
    virtual bool inList(int handle) const = 0;       // +0x24
    virtual bool stopped(int handle) const = 0;      // +0x08 != 0: finished (1) or holding (2)
    virtual void frame() = 0;                        // 174b:0510 ScreenAnim_Frame (erase, step, draw)
    virtual void release(int handle) = 0;            // 174b:0444 ScreenAnim_Free
    virtual void resetSprites() = 0;                 // 1581:0484 Sprites_Reset
};

// 2c11:0000 Attract_PlayAnim(anim, fade, flags, ...) as a stepper. The caller's loop per frame:
//     Timer_Set(0, 4); player.startChannels(); host.frame() (ScreenAnim_Frame); Dirty_Flush;
//     Timer_Wait(0); on the first frame the fade (kAnimFadeIn / kAnimFadeFromSaved); player.advance();
//     then, in this order: flags & 2 and Timer_Expired(1) -> result 0, leave; flags & 8: result =
//     Input_GetCode(), leave on Enter, Esc or Space (other codes are lost); flags & 4 and
//     player.channelAtEnd(watch) -> leave.
// The destructor frees the loaded animations (ScreenAnim_Free, last loaded first).
class AttractAnimPlayer {
public:
    // Loads every animation of the scripts (ScreenAnim_Load) and resets the sprite list
    // (Sprites_Reset); every channel ready at position 0.
    AttractAnimPlayer(const GgExe& exe, const AttractAnim& anim, ScreenAnimHost& host);
    ~AttractAnimPlayer();
    AttractAnimPlayer(const AttractAnimPlayer&) = delete;
    AttractAnimPlayer& operator=(const AttractAnimPlayer&) = delete;

    // The frame's first part (2c11:011b..0206): each ready channel takes its op: n > 0 starts the
    // first loaded instance whose id slot still holds n and is not in the active list
    // (ScreenAnim_StartAt(inst, channel)); -2 t: until = frame + t; -1: nothing.
    void startChannels();
    // After the frame: the frame count + 1 (2c11:021c, right after the flush), then (2c11:0247..02ef) a
    // running channel whose animation has stopped frees its id slot (set to -1, never reused) and
    // moves on one op; a waiting channel whose `until` < the frame count moves on two.
    void advance();
    // One whole frame of the loop above without timers, fades or input (tools, tests).
    void runFrame();

    int frames() const { return frames_; }
    // script[c][pos[c]] == -1 (the watch test).
    bool channelAtEnd(int c) const;
    int16_t op(int c) const;  // script[c][pos[c]]
    int channelCount() const { return static_cast<int>(channels_.size()); }

private:
    struct Channel {
        uint16_t script = 0;  // DS offset
        int pos = 0;          // word index
        bool ready = true;
        int state = -1;       // >= 0: the instance it runs; -2: waiting (the original leaves it unset)
        int until = 0;
    };
    int16_t word(const Channel& ch, int k) const;

    std::span<const uint8_t> dgroup_;
    ScreenAnimHost& host_;
    std::vector<Channel> channels_;
    std::vector<int> ids_;      // the id slots (-1 once an instance stopped)
    std::vector<int> handles_;  // the loaded instances
    int frames_ = 0;
};

// 2c11:03a7 Attract_DrawPictureSet(id): a bundle of {I x, y} / G pairs drawn keyed, up to an I member
// whose first byte is 0xFF.
void attractDrawPictureSet(Screen& screen, const Archive& archive, uint16_t id);

// Step 1, 2c11:043f Attract_TitleAnim, before its first PlayAnim: Screen_LoadPicture(0x1002, 0x1000),
// Lpx_Draw(0, 0), Screen_SaveAll, Attract_DrawPictureSet(0x1001). Returns the palette load.
PaletteLoad attractTitleBackground(Video& video, const Archive& archive);
// The title art of each phase (bundle 0x1028): phase 2 (after the first second) picture 0 keyed at
// (59, 9); phase 3 pictures 1..3 at (52, 51), (99, 138), (0, 181). Draws `phase`'s pictures only
// (the original draws phase 2's before phase 3's); Dirty_Flush follows in the original.
void attractDrawTitleArt(Screen& screen, const Bundle& titleArt, int phase);
// Step 2, 2c11:05cf Attract_AfterTitle, before its PlayAnim: Screen_RestoreAll (the background
// without the title art), Pal_Save, Pal_LoadRes(0x1004) (shadow only).
void attractAfterTitle(Video& video, const Archive& archive);
// Steps 3..8, 2c11:0630 Attract_StoryPage(text): on the first page (0x1019) Screen_LoadPicture(0x1009,
// 0x100A), Lpx_Draw(0, 0), Screen_SaveAll, the line 0x1024 with Font_SetColor(15) on the selected
// font, font 0 selected, font 0's spacing patched to 1 (Font_GetCurrent +6) while it is measured and
// drawn at (160 - half(Text_Width), 190), then 2 again. Every page: colour 0xA3 FillRect(57, 38, 177,
// 53), Font_Select(1), Font_SetColor(0), Text_DrawBox(57, 38, 177, text). The caller then runs
// Screen_FadeOutIn (first page) or Dirty_Flush. Returns the first page's palette load.
PaletteLoad attractStoryPage(Video& video, Fonts& fonts, const Archive& archive, uint16_t textId);
// Step 9, 2c11:077f Attract_AfterStory, before its PlayAnim: Gfx_RestoreRect(57, 38, 177, 53) and
// (0, 190, 320, 10) (the story box and line).
void attractAfterStory(Screen& screen);
// Step 10, 2c11:07e5 Attract_BeforePrompt, before its PlayAnim: Screen_LoadPicture(0x1002, 0x1004),
// Lpx_Draw(0, 0), Screen_SaveAll.
PaletteLoad attractBeforePrompt(Video& video, const Archive& archive);
// Step 11, 2c11:084c Attract_SignInPrompt, before its Screen_FadeOutIn: Screen_LoadPicture(0x1029,
// 0x102A), Lpx_Draw(0, 0), 0x1026 keyed at (93, 33), Font_SetColor(15) on the selected font,
// Font_Select(0), Font_DrawString(74, 121, 0x1025) (white: font 0's colour is still 15 from story
// page 1). No Screen_SaveAll.
PaletteLoad attractSignInPrompt(Video& video, Fonts& fonts, const Archive& archive);

}  // namespace tmport::gg
