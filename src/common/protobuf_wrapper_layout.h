#pragma once
#include <cstddef>
#include <cstdint>

namespace ProtobufWrapperLayout {
struct Layout { uint32_t header = 0, body = 0; };

// The transport wrapper itself loads both raw bodies before invoking slot 4.
// Decode that paired access and independently verify its header load. This
// handles the old +40/+48 and current +48/+56 layouts without a Steam build ID
// or a guessed pointer dereference. Unknown code is rejected before hooking.
inline bool Detect(const uint8_t* code, size_t size, Layout& layout) {
    layout = {};
    if (!code) return false;
    for (size_t i = 0; i + 8 <= size; ++i) {
        if (code[i] != 0x4D || code[i+1] != 0x8B || code[i+2] != 0x49
            || code[i+4] != 0x4D || code[i+5] != 0x8B || code[i+6] != 0x40
            || code[i+3] != code[i+7]) continue;
        const uint32_t body = code[i+3];
        if (body < 0x28 || body > 0x78 || body % 8) continue;
        const uint32_t header = body - 8;
        for (size_t j = 0; j + 4 <= i; ++j) {
            if (code[j] == 0x49 && code[j+1] == 0x8B && code[j+2] == 0x40
                && code[j+3] == header) {
                layout = {header, body};
                return true;
            }
        }
    }
    return false;
}
struct Metadata { uint32_t connection=0, message=0; };
// Only layouts whose message identity fields have been checked are eligible
// for metadata injection. Unknown layouts still fail closed.
inline bool MetadataFor(const Layout& l, Metadata& m) {
    m = {};
    if (!((l.body==0x30 && l.header==0x28) || (l.body==0x38 && l.header==0x30))) return false;
    m = {l.body-0x14, l.body-0x10};
    return true;
}

}
