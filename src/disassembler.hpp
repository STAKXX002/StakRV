#pragma once

#include <cstdint>
#include <string>

namespace stakrv {

// Decode one 32-bit RV32I instruction word into a human-readable mnemonic.
// Pure function — no side effects, no CPU state required.
// Returns "???" for unrecognised encodings.
std::string disassemble(uint32_t inst);

} // namespace stakrv