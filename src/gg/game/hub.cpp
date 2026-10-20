#include "gg/game/hub.h"

#include "gg/data/archive.h"
#include "gg/data/dgroup_offsets.h"
#include "gg/data/gg_exe.h"
#include "gg/data/resources.h"
#include "gg/game/pic_cache.h"
#include "gg/gfx/lpx.h"
#include "gg/gfx/video.h"
#include "rtl/borland_rand.h"
#include "util/log.h"

namespace tmport::gg {

namespace {

int16_t s16(std::span<const uint8_t> p, size_t o) {
    return o + 2 <= p.size() ? static_cast<int16_t>(p[o] | (p[o + 1] << 8)) : int16_t{0};
}

constexpr uint8_t kTrophySource[5] = {'S', 'T', 'U', 'V', 'W'};  // the bytes 0x53..0x57 (12e0:0246)

}  // namespace

std::array<HubZone, 8> HubScreen::zones(const GgExe& exe) {
    std::array<HubZone, 8> z{};
    for (size_t i = 0; i < z.size(); ++i) {
        const auto o = static_cast<uint16_t>(dg::kHubZones + dg::kHubZonesStride * i);
        z[i] = {exe.dsS16(o), exe.dsS16(o + 2), exe.dsS16(o + 4), exe.dsS16(o + 6), exe.dsS16(o + 8),
                exe.dsS16(o + 0x0A), exe.dsS16(o + 0x10), exe.dsS16(o + 0x12), exe.dsS16(o + 0x14)};
    }
    return z;
}

HubScreen::~HubScreen() { stopAnims(); }

// 174b:0444 ScreenAnim_Free of both animations: out of the running list, the frame bundles released.
void HubScreen::stopAnims() {
    for (ScreenAnim& a : anims_) {
        running_->stop(a);
        if (cache_) cache_->releaseBundle(a.frameBundle());
    }
    anims_.clear();
}

// 12e0:0148 State_03_Hub, the set-up
void HubScreen::setup(Video& video, const GgExe& exe, const Archive& archive, PicCache& cache,
                      const HubProgress& progress) {
    Screen& screen = video.screen();
    // The title music (DS:0098, bank 0x1020, arrangement 2) is the state's (game/flow.cpp state03Hub).
    std::vector<uint8_t> picture;
    video.loadPicture(archive, kPicture, kPalette, picture);  // Screen_LoadPicture
    screen.lpxDraw(0, 0, picture);
    video.palette().store(parsePaletteRange(archive.read(kPaletteExtra)));  // Pal_LoadRes(0xf6)
    screen.saveAll();

    loadObjects(video, exe, archive, cache);

    // The trophies: Res_Read(0x1003) into the shared buffer (recoloured in place), drawn after the
    // back page was saved (code_world.md §20 quirk 12: an animation crossing one would erase it).
    Resource trophies = archive.load(kTrophies);
    const auto members = parseBundle(trophies.bytes);
    if (members.size() >= 2) {
        const auto xs = members[0].payload, ys = members[1].payload;
        for (int b = 0; b < 3; ++b)
            for (int k = 0; k <= progress.racesWon[static_cast<size_t>(b)]; ++k) {
                const int t = b * 5 + k;
                if (t < 0 || static_cast<size_t>(t + 2) >= members.size() || t >= 15) continue;
                uint8_t to[5];
                for (int i = 0; i < 5; ++i)
                    to[i] = static_cast<uint8_t>(progress.vehicleColor[static_cast<size_t>(t)] * 5 + i + 0x20);
                const BundleMember& m = members[static_cast<size_t>(t + 2)];
                std::span<uint8_t> pic(trophies.bytes.data() + m.offset + 2, m.payload.size());
                recolorPicture(pic, kTrophySource, to);
                screen.lpxDrawKeyed(s16(xs, 2 * static_cast<size_t>(t)), s16(ys, 2 * static_cast<size_t>(t)), pic, 0);
            }
    }
    addRegions(video, exe, archive);
    // Zones_AddLocalArray(8, DS:0132) is the state's (the runtime's zone list, gg/input/zones.h).
    video.sprites().reset();
    complete_ = progress.racesWonTotal == 15;
}

void HubScreen::setupResumed(Video& video, const GgExe& exe, const Archive& archive, PicCache& cache) {
    loadObjects(video, exe, archive, cache);
    addRegions(video, exe, archive);
    complete_ = false;
}

// ScreenAnim_Load(0x1005), ScreenAnim_Load(0x1007), both ScreenAnim_Start (12e0:01b3..01e9).
void HubScreen::loadObjects(Video&, const GgExe&, const Archive&, PicCache& cache) {
    stopAnims();
    cache_ = &cache;
    anims_.reserve(kAnims.size());
    for (const uint16_t id : kAnims) anims_.push_back(ScreenAnim::load(cache, id));  // ScreenAnim_Load
    for (ScreenAnim& a : anims_) running_->start(a);                                // ScreenAnim_Start
}

// The cursor labels (Res_LoadCopy(0x101f), Mouse_LoadCursors of members 0..3) and the hover regions
// over the zones; MouseRegion_AddArray puts them in front of the region list (12e0:02f0..0362).
void HubScreen::addRegions(Video& video, const GgExe& exe, const Archive& archive) {
    labels_ = archive.read(kCursorLabels);
    cursors_.clear();
    const auto labels = parseBundle(labels_);
    for (size_t i = 0; i < 4 && i < labels.size(); ++i) cursors_.push_back(CursorRecord::fromPicture(labels[i].payload));
    zones_ = zones(exe);
    for (size_t i = 0; i < regions_.size(); ++i) {
        const HubZone& z = zones_[i];
        const int label = z.releaseCode - kSignIn;
        regions_[i] = {z.shape, z.x, z.y, z.w, z.h, z.priority,
                       label >= 0 && static_cast<size_t>(label) < cursors_.size() ? cursors_[static_cast<size_t>(label)].get()
                                                                                  : nullptr};
    }
    video.cursor().addRegions(regions_);
}

// 12e0:03bf..03cb: one frame of the hub's animations (ScreenAnim_Frame over the running list)
void HubScreen::frame(Video& video, BorlandRand& rng) { running_->frame(video.screen(), video.sprites(), rng); }

// 12e0:047c..04da: MouseRegion_Clear, Mouse_SetCursorPicture(arrow), the cursors and the 0x101f copy
// freed (with this object), ScreenAnim_Free of both animations.
void HubScreen::leave(Video& video) {
    video.cursor().clearRegions();
    video.cursor().setCursorPicture(video.cursor().arrow());
    stopAnims();
}

}  // namespace tmport::gg
