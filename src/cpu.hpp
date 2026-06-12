#pragma once

#include <cstdint>
#include <vector>
#include <string>

namespace stakrv {

constexpr uint32_t MEM_SIZE  = 1 * 1024 * 1024; // 1 MB
constexpr uint32_t MEM_BASE  = 0x80000000;       // load address
constexpr uint32_t REG_COUNT = 32;

class CPU {
public:
    CPU();

    // Load a flat binary into memory at MEM_BASE.
    bool load(const std::string& path);

    // Single fetch-decode-execute cycle. Returns false to request a halt.
    bool step();

    // Read-only accessors for the TUI.
    uint32_t pc()              const { return pc_; }
    uint32_t reg(uint32_t idx) const { return regs_[idx & 31]; }

    // Safe memory peek used by the TUI (returns false on out-of-bounds).
    bool peek32(uint32_t addr, uint32_t& out) const;

private:
    uint32_t             pc_;
    uint32_t             regs_[REG_COUNT];
    std::vector<uint8_t> mem_;

    // ── memory ────────────────────────────────────────────────────────────────
    uint32_t mem_read32 (uint32_t addr) const;
    uint16_t mem_read16 (uint32_t addr) const;
    uint8_t  mem_read8  (uint32_t addr) const;
    void     mem_write32(uint32_t addr, uint32_t val);
    void     mem_write16(uint32_t addr, uint16_t val);
    void     mem_write8 (uint32_t addr, uint8_t  val);

    // ── instruction handlers ──────────────────────────────────────────────────
    void exec_lui    (uint32_t inst);
    void exec_auipc  (uint32_t inst);
    void exec_jal    (uint32_t inst);
    void exec_jalr   (uint32_t inst);
    void exec_branch (uint32_t inst);
    void exec_load   (uint32_t inst);
    void exec_store  (uint32_t inst);
    void exec_op_imm (uint32_t inst);
    void exec_op     (uint32_t inst);
    void exec_system (uint32_t inst);
};

} // namespace stakrv