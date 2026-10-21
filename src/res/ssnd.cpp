#include "res/ssnd.h"

#include "mischief/util/byte_reader.h"

namespace tmport {

std::optional<SsndInfo> parseSsnd(std::span<const uint8_t> resource) {
    if (resource.size() < 12) return std::nullopt;
    mischief::ByteReader r(resource, "SSND");
    SsndInfo s;
    s.reserved = r.u32(0);
    s.sampleRate = r.u32(4);    // Wave_Play passes +4 as nSamplesPerSec
    s.sampleCount = r.u32(8);   // and +8 as the buffer length
    s.pcmOffset = 12;
    const size_t avail = resource.size() - 12;
    s.truncated = s.sampleCount > avail;
    s.pcmLength = s.truncated ? avail : s.sampleCount;
    return s;
}

}  // namespace tmport
