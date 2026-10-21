// Diagnostics of the DOS game: the per-tick CSV trace and DGROUP dumps (trace.h).
#include "dos/game/trace.h"

#include <format>
#include <ostream>

#include "dos/game/game.h"
#include "mischief/util/file_util.h"

namespace tmport::dos {

// ---- TraceSound ---------------------------------------------------------------------------------

void TraceSound::sfxPlay(int id) {
    note(std::format("sfx={}", id));
    inner_.sfxPlay(id);
}
void TraceSound::sfxStop() {
    note("sfxstop");
    inner_.sfxStop();
}
void TraceSound::musicPlay(int tune) {
    note(std::format("music={}", tune));
    inner_.musicPlay(tune);
}
void TraceSound::musicStop() {
    note("musicstop");
    inner_.musicStop();
}
void TraceSound::silenceAll() {
    note("silence");
    inner_.silenceAll();
}
void TraceSound::setSoundOn(bool on) {
    note(std::format("soundon={}", on ? 1 : 0));
    inner_.setSoundOn(on);
}
void TraceSound::setMusicOn(bool on) {
    note(std::format("musicon={}", on ? 1 : 0));
    inner_.setMusicOn(on);
}
void TraceSound::setSfxLoop(bool on) {
    note(std::format("sfxloop={}", on ? 1 : 0));
    inner_.setSfxLoop(on);
}
void TraceSound::setMusicLoop(bool on) {
    note(std::format("musicloop={}", on ? 1 : 0));
    inner_.setMusicLoop(on);
}
void TraceSound::setMusicGate(uint16_t m) {
    note(std::format("gate={}", m));
    inner_.setMusicGate(m);
}

std::vector<std::string> TraceSound::takeEvents() {
    std::vector<std::string> e;
    e.swap(events_);
    return e;
}

// ---- DosTrace -----------------------------------------------------------------------------------

DosTrace::DosTrace(DosGame& g, std::ostream& out, TraceSound* sound) : g_(g), out_(out), sound_(sound) {
    out_ << "# tmport DOS trace 1: one row per game tick that ran Game_Tick; x..coins at the start of the tick"
            " (framelog.py STEP), the rest after its frame\n"
         << kHeader << '\n';
    prev_ = watch();
    if (sound_) (void)sound_->takeEvents();  // calls made before the first tick (start-up) are not traced
}

DosTrace::~DosTrace() {
    if (row_.valid) writeRow();
    out_.flush();
}

DosTrace::Watched DosTrace::watch() const {
    Watched w;
    w.uiMode = g_.core.uiMode;
    w.level = g_.core.level;
    w.scene = g_.world.loadedScene;
    w.area = g_.core.area;
    w.titlePhase = g_.titleRunning ? g_.ui.titlePhase : 0xFFFF;
    w.transition = g_.world.transition;
    w.gameInProgress = g_.core.gameInProgress;
    w.endState = g_.core.endState;
    w.screen = g_.ui.screen;
    w.context = g_.input.context;
    w.dissolve = g_.dissolveRunning;
    w.quit = g_.core.quit != 0;
    w.writes = g_.users.writes();
    return w;
}

void DosTrace::soundTick() { g_.soundTick(); }
void DosTrace::frameBegin() { g_.frameBegin(); }
uint8_t DosTrace::uiMode() const { return g_.uiMode(); }
bool DosTrace::quitRequested() const { return g_.quitRequested(); }

void DosTrace::gameTick(const TickInfo& info) {
    if (row_.valid) writeRow();  // a tick without its own frame (slow-machine mode)
    row_ = Row{};
    row_.valid = true;
    row_.events.swap(pendingEvents_);
    row_.tick = info.gameTick;
    row_.ticks = info.ticks;
    row_.gated = info.gated;
    // framelog.py's STEP record: taken at 130f:0112, after the gate test and before Mouse_Poll.
    row_.x = g_.player.obj.x;
    row_.y = g_.player.obj.y;
    row_.camX = g_.world.cameraX;
    row_.camY = g_.world.cameraY;
    row_.state = g_.player.playerState;
    row_.keys = g_.keys();
    row_.coins = g_.core.coins;
    const int16_t cmdBefore = g_.input.cmd;
    g_.gameTick(info);
    if (g_.input.lastKey != 0) row_.events.push_back(std::format("key={:04x}", g_.input.lastKey));
    if (g_.input.cmd != -1 && g_.input.cmd != cmdBefore) row_.events.push_back(std::format("cmd={}", g_.input.cmd));
}

void DosTrace::frameBody() {
    g_.frameBody();
    // A frame whose own tick fell into its I/O (g_quit) has no row: its events start the next one.
    const bool own = row_.valid;
    auto& ev = own ? row_.events : pendingEvents_;
    if (!own && sound_)
        for (std::string& e : sound_->takeEvents()) ev.push_back(std::move(e));
    const Watched w = watch();
    if (w.uiMode != prev_.uiMode) ev.push_back(std::format("ui={:#04x}", w.uiMode));
    if (w.level != prev_.level) ev.push_back(std::format("level={}", w.level));
    if (w.scene != prev_.scene) ev.push_back(std::format("scene={}", w.scene));
    if (w.area != prev_.area) ev.push_back(std::format("area={}", w.area));
    if (w.screen != prev_.screen) ev.push_back(std::format("screen={}", w.screen));
    if (w.context != prev_.context) ev.push_back(std::format("ctx={}", w.context));
    if (w.titlePhase != prev_.titlePhase && w.titlePhase != 0xFFFF) ev.push_back(std::format("title={}", w.titlePhase));
    if (w.transition != prev_.transition) ev.push_back(std::format("transition={}", w.transition));
    if (w.dissolve != prev_.dissolve) ev.push_back(w.dissolve ? "dissolve=on" : "dissolve=off");
    if (w.gameInProgress != prev_.gameInProgress) ev.push_back(std::format("game={}", w.gameInProgress));
    if (w.endState != prev_.endState) ev.push_back(std::format("end={}", w.endState));
    if (w.writes != prev_.writes && g_.users.lastMoment()) ev.push_back(std::format("save={}", describe(*g_.users.lastMoment())));
    if (w.quit && !prev_.quit) ev.push_back("quit");
    prev_ = w;
    if (own) writeRow();
}

void DosTrace::writeRow() {
    if (sound_)
        for (std::string& e : sound_->takeEvents()) row_.events.push_back(std::move(e));
    std::string events;
    for (const std::string& e : row_.events) {
        if (!events.empty()) events += ';';
        events += e;
    }
    out_ << std::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{:#04x},{},{}\n", row_.tick, row_.ticks, row_.gated ? 1 : 0,
                        row_.x, row_.y, row_.camX, row_.camY, row_.state, row_.keys, row_.coins, g_.core.nets, g_.core.level,
                        g_.world.loadedScene, g_.core.uiMode, g_.core.area, events);
    row_.valid = false;
    ++rows_;
}

// ---- DGROUP dumps -------------------------------------------------------------------------------

bool writeDgroupDump(const DosGame& g, const std::filesystem::path& file) {
    const std::vector<uint8_t> bytes = dumpToDgroup(g);
    if (!mischief::writeWholeFile(file, bytes)) return false;
    std::string tsv = "# name\tds\tsize\tfarPointer\tsegWordOffset\tstride\tskipLen\tservice\n";
    auto add = [&tsv](const DgroupField& f) {
        tsv += std::format("{}\t{:04x}\t{}\t{}\t{}\t{}\t{}\t{}\n", f.name, f.ds, f.size, f.farPointer ? 1 : 0, f.segWordOffset,
                           f.stride, f.skipLen, f.service ? 1 : 0);
    };
    for (const DgroupField& f : gameStateFields()) add(f);
    for (const DgroupField& f : serviceFields()) add(f);
    auto tsvPath = file;
    tsvPath += ".fields.tsv";
    return mischief::writeWholeFile(tsvPath, std::vector<uint8_t>(tsv.begin(), tsv.end()));
}

}  // namespace tmport::dos
