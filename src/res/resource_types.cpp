#include "res/resource_types.h"

#include "util/file_util.h"

namespace tmport {

std::string_view resTypeName(ResType t) {
    switch (t) {
        case ResType::BNDL: return "BNDL";
        case ResType::RRGB: return "RRGB";
        case ResType::FFNT: return "FFNT";
        case ResType::PMAP: return "PMAP";
        case ResType::SSND: return "SSND";
        case ResType::OTHR: return "OTHR";
    }
    return "????";
}

std::optional<ResType> resTypeFromName(std::string_view name) {
    for (ResType t : kAllResTypes)
        if (iequalsAscii(resTypeName(t), name)) return t;
    return std::nullopt;
}

ResType typeForId(int id) {
    const int16_t s = static_cast<int16_t>(id);  // the original compares signed 16-bit values
    if (s < 900) return ResType::BNDL;
    if (s < 950) return ResType::RRGB;
    if (s < 1000) return ResType::FFNT;
    if (s < 4000) return ResType::PMAP;
    if (s < 10000) return ResType::SSND;
    return ResType::OTHR;
}

std::string_view dllFileName(DllId d) {
    switch (d) {
        case DllId::TMT: return "TMT.DLL";
        case DllId::TMT256: return "TMT256.DLL";
        case DllId::TMT16: return "TMT16.DLL";
        case DllId::TM0SOUND: return "TM0SOUND.DLL";
        case DllId::TM1SOUND: return "TM1SOUND.DLL";
        case DllId::TM2SOUND: return "TM2SOUND.DLL";
        case DllId::TM3SOUND: return "TM3SOUND.DLL";
        case DllId::TM4SOUND: return "TM4SOUND.DLL";
        case DllId::TM5SOUND: return "TM5SOUND.DLL";
        case DllId::TM6SOUND: return "TM6SOUND.DLL";
        case DllId::TM7SOUND: return "TM7SOUND.DLL";
        case DllId::TM8SOUND: return "TM8SOUND.DLL";
    }
    return "?";
}

std::optional<DllId> dllFor(ResType type, int id, gfx::ColorMode mode) {
    const std::string_view name = resTypeName(type);
    if (name[0] == 'P') return mode == gfx::ColorMode::Colour16 ? DllId::TMT16 : DllId::TMT256;
    if (name[0] == 'S') {
        const int s = static_cast<int16_t>(id);
        if (s >= 7000 && s <= 7099) return DllId::TM0SOUND;
        if (s >= 7100 && s <= 7999) return DllId::TM1SOUND;
        if (s >= 4000 && s <= 6799) return soundDll(2 + (s - 4000) / 400);
        return std::nullopt;  // the original takes no branch: undefined, treated as not found
    }
    return DllId::TMT;
}

}  // namespace tmport
