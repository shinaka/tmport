#pragma once
// Resource types, id -> type and (type, id) -> DLL routing of the original's Mac Resource
// Manager emulation (re/docs/sys_resources_saves.md §5.2-5.3, 1038:038a / 1038:03da).

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include "gfx/palette.h"

namespace tmport {

enum class ResType : uint8_t { BNDL, RRGB, FFNT, PMAP, SSND, OTHR };
inline constexpr std::array<ResType, 6> kAllResTypes = {ResType::BNDL, ResType::RRGB, ResType::FFNT,
                                                         ResType::PMAP, ResType::SSND, ResType::OTHR};

// The 4-character Mac type name ("BNDL", ...), as passed to FindResource by the original.
std::string_view resTypeName(ResType t);
std::optional<ResType> resTypeFromName(std::string_view name);

// 1038:038a Res_TypeForId: the type is implied by the id range (signed 16-bit compares).
ResType typeForId(int id);

// The resource-only DLLs the game loads (sys_resources_saves.md §4).
enum class DllId : uint8_t {
    TMT,
    TMT256,
    TMT16,
    TM0SOUND, TM1SOUND, TM2SOUND, TM3SOUND, TM4SOUND, TM5SOUND, TM6SOUND, TM7SOUND, TM8SOUND,
};
inline constexpr size_t kDllCount = 12;
std::string_view dllFileName(DllId d);
inline DllId soundDll(int n) { return static_cast<DllId>(static_cast<int>(DllId::TM0SOUND) + n); }

// (type, id) -> DLL: 'P' -> the pixmap DLL of the colour mode, 'S' -> the sound DLL by id range
// (7000-7099 TM0, 7100-7999 TM1, 4000+400k..4399+400k TM(2+k)), anything else -> TMT.DLL.
// nullopt for sound ids no DLL serves (6800-6999, 8000-9999 and anything else).
std::optional<DllId> dllFor(ResType type, int id, gfx::ColorMode mode);

}  // namespace tmport
