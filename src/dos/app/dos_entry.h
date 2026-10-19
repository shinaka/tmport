#pragma once
// Entry point of the 1990 DOS edition, called by the start-up (app/bootstrap.cpp) once edition
// detection (edition/edition.h) chose Dos1990 for `folder`.
//
// The host is ready: its layout is gfx::PresenterLayout::dos1990() (320x200 canvas, 4:3 unless the
// player asked for square pixels, no overlay), its window is open unless options.headless, and
// nothing is installed in host.handlers(). The DOS runtime installs its handlers (keyboard, mouse,
// quit), runs its frame loop (host.pump() per frame, host.presenter().presentFrame(rgba) per
// finished 320x200 frame, the 32-bit RGBA convention of gfx/presenter.h) and returns the process
// exit code (0 = normal end). With options.headless there is no window: the run ends when the
// --keys script is used up. The start-up writes --frame-dump from the host's canvas afterwards
// (a frame-dump folder, DosOptions::frameDumpDir, is filled by the runtime instead).
//
// Implemented in dos_entry.cpp. Wave 2 preview: the composed screens with their music and effects
// (dos_preview.h); replaced by the title/sign-in/game modules in wave 3.

#include <filesystem>

#include "edition/dos_options.h"

namespace tmport::platform {
class Host;
}

namespace tmport::dos {

int runDosEdition(platform::Host& host, const DosOptions& options, const std::filesystem::path& folder);

}  // namespace tmport::dos
