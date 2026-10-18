#include "game/scenes/hint_popup.h"

#include "audio/audio_tables.h"
#include "input/input_codes.h"
#include "platform/engine.h"
#include "res/resource_manager.h"
#include "text/text_renderer.h"

namespace tmport::game::scenes {

namespace {

// 1130:0166 Hint_WaitDismiss(event)
void waitDismiss(SceneIo& io, int event) {
    bool done = false;
    if (io.voiceOn()) done = io.playUntilEvent(hint::kVoiceBase + event, audio::StopList::Hint);
    io.setClueBoxMode(2);
    while (!done) {
        const uint16_t c = io.waitCode();
        if (c == input::kEnter || c == 0x409 || c == input::kEsc || c == hint::kHotspot ||
            c == (hint::kHotspot | input::kRightUpFlag))
            done = true;
    }
    io.flushInput();
}

}  // namespace

void showHint(AppContext& ctx, int event, const SceneHooks& hooks) {
    SceneIo io(ctx, hooks);
    io.tracef("hint {}", event);
    MenuDisable menu(ctx);
    io.setCursorForced(true);
    gfx::FrameBuffer& fb = io.fb();
    fb.drawSprite(hint::kBubbleX, hint::kBubbleY, *io.pixmap(hint::kBubblePmap), false);
    fb.drawSprite(hint::kFlutterX, hint::kFlutterY, *io.pixmap(hint::kFlutterPmap), false);
    // Res_LoadToScratch(327) + Bndl_GetEntry(scratch, event): the bundle's member e.
    std::vector<uint8_t> textBytes;
    if (const auto b = io.res().bundle(hint::kTextBndl); b && event >= 0 && event < static_cast<int>(b->ids.size()))
        textBytes = loadText(io.res(), b->ids[static_cast<size_t>(event)]);
    io.engine().hotspots().set(hint::kHotspot, 0, 0, 512, 384);
    fb.setForeColor(0);
    io.text().drawBox(hint::kTextX, hint::kTextY, hint::kTextW, textBytes);
    io.musicHalt();
    io.present(0, gfx::FrameBuffer::kPlayTop, 512, gfx::FrameBuffer::kPlayHeight);
    io.flushInput();
    waitDismiss(io, event);
    io.engine().hotspots().remove(hint::kHotspot);
    io.musicResume();
    io.setClueBoxMode(0);
    io.setCursorForced(false);
    menu.restore();
}

}  // namespace tmport::game::scenes
