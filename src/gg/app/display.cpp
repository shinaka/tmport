#include "gg/app/display.h"

#include "gg/app/gg_runtime.h"
#include "gg/gfx/transitions.h"

namespace tmport::gg {

Display::Display(GgRuntime& runtime, Video& video) : runtime_(runtime), video_(video) {
    runtime_.mouse().setScreen(this);
    // Dirty_Flush from inside the cursor (Mouse_SetCursorPicture) presents too.
    video_.cursor().setFlush([this] { flush(); });
    // Mouse_SetBounds 17ab:017f: INT 33h AX=07h / 08h.
    video_.cursor().setDriverHooks({[this](int minX, int maxX) { runtime_.mouse().driver().setRangeX(minX, maxX); },
                                    [this](int minY, int maxY) { runtime_.mouse().driver().setRangeY(minY, maxY); }});
}

Display::~Display() {
    if (runtime_.mouse().screen() == this) runtime_.mouse().setScreen(nullptr);
    video_.cursor().setDriverHooks({});
    video_.cursor().setFlush([v = &video_] { v->flush(); });
}

// ------------------------------------------------------------------------------------ MouseScreen

bool Display::drawing() const { return video_.screen().mouseHideState() != 0; }
int16_t Display::hotX() const { return static_cast<int16_t>(video_.cursor().hotX()); }
int16_t Display::hotY() const { return static_cast<int16_t>(video_.cursor().hotY()); }
int16_t Display::cursorX() const { return static_cast<int16_t>(video_.cursor().x()); }
int16_t Display::cursorY() const { return static_cast<int16_t>(video_.cursor().y()); }
void Display::setCursorPosition(int16_t x, int16_t y) { video_.cursor().setPosition(x, y); }

void Display::moveCursor(int16_t x, int16_t y) {
    // 17a0:009b: Mouse_RestoreUnder, g_mouseX/Y, Dirty_Flush (Video::moveCursor), then what VRAM shows.
    const uint64_t before = video_.screen().dirty().flushes;
    video_.moveCursor(x, y);
    if (video_.screen().dirty().flushes != before) present();
}

// ---------------------------------------------------------------------------------------- start-up

void Display::init(const GgExe& exe, const Archive& archive) {
    const auto& d = runtime_.mouse().driver();
    runLoad(video_.init(exe, archive, d.x(), d.y()));
}

// ----------------------------------------------------------------------------------------- output

void Display::present() { runtime_.showFrame(video_.frameRgba()); }

void Display::flush() {
    const uint64_t before = video_.screen().dirty().flushes;
    video_.flush();
    if (video_.screen().dirty().flushes != before) present();
}

void Display::runLoad(PaletteLoad load) {
    // 1399:05d6: per group, wait for the start of the next retrace, then 128 colours.
    while (load.pending()) {
        runtime_.waitRetrace();
        load.onRetrace(video_.dac());
        present();
    }
}

void Display::runFade(PalFade fade) {
    // 1473:015c Pal_Fade: every step is Pal_SetRange(cur, 0, 2).
    Rgb6 cur{};
    while (fade.next(cur)) runLoad(PaletteLoad(cur, 0, 2));
}

void Display::fadeOutIn() {
    fadeOut();
    flush();
    fadeIn();
}

void Display::show(const Archive& archive, BorlandRand& rng, const std::function<void(int, bool)>& effect) {
    ScreenShow s(video_, archive, rng);
    if (s.fading()) {
        present();  // the flush ScreenShow made (the DAC is still black)
        runFade(s.fade());
        return;
    }
    // 1473:0479 Screen_Wipe: the effect (loop 1, priority 10), then the steps on timer slot 2.
    Wipe& w = s.wipe();
    if (effect) effect(w.soundEffect(), true);
    while (!w.finished()) {
        runtime_.timers().set(2, static_cast<uint16_t>(w.period()));
        w.step(video_.screen(), video_.vram());
        present();
        runtime_.timerWait(2);
    }
    if (effect) effect(w.soundEffect(), false);
    s.finish(video_);
}

}  // namespace tmport::gg
