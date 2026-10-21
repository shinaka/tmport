#pragma once
// The game's window icon, read from the user's TMTWINCD.EXE at run time.
//
// RegisterMainWindowClass (1018:00a8) gives the window class LoadIcon(hInst, MAKEINTRESOURCE(15000))
// (re/docs/code_1008-1030.md §2.2). The EXE holds RT_GROUP_ICON 15000 with a single 32x32
// 16-colour entry (RT_ICON 1). The formats are in mischief/platform/exe_icon.h.

#include <cstdint>

#include "mischief/platform/exe_icon.h"

namespace tmport {

namespace ne {
class NeImage;
}

// 1018:00a8 RegisterMainWindowClass: the class icon id.
inline constexpr uint16_t kWindowIconGroupId = 15000;

// Loads RT_GROUP_ICON groupId from an NE image and decodes its best entry (largest, then the most
// colours). Throws FormatError / Error when missing.
mischief::IconImage loadGroupIcon(const ne::NeImage& image, uint16_t groupId);

}  // namespace tmport
