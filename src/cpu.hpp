#pragma once

#include <cstdint>
#include <vector>
#include <string>

namespace stakrv {

constexpr uint32_t MEM_SIZE   = 1 * 1024 * 1024; // 1MB
constexpr uint32_t MEM_BASE   = 0x80000000;       // where we load the binary
constexpr uint32_t REG_COUNT  = 32;

class CPU {
public:
    CPU();

    // load a flat binary into memory at MEM_BASE
    bool load(const std::string& path);

    // run until EBREAK or error
    void run();

    // dump all registers to stdout
    void dump_regs() const;

private:
    uint32_t              pc_;
    uint32_t              regs_[REG_COUNT];
    std::vector<uint8_t>  mem_;

    // memory access
    uint32_t mem_read32 (uint32_t addr) const;
    uint16_t mem_read16 (uint32_t addr) const;
    uint8_t  mem_read8  (uint32_t addr) const;
    void     mem_write32(uint32_t addr, uint32_t val);
    void     mem_write16(uint32_t addr, uint16_t val);
    void     mem_write8 (uint32_t addr, uint8_t  val);

    // fetch-decode-execute — returns false to stop
    bool step();

    // instruction handlers
    void exec_lui      (uint32_t inst);
    void exec_auipc    (uint32_t inst);
    void exec_jal      (uint32_t inst);
    void exec_jalr     (uint32_t inst);
    void exec_branch   (uint32_t inst);
    void exec_load     (uint32_t inst);
    void exec_store    (uint32_t inst);
    void exec_op_imm   (uint32_t inst);
    void exec_op       (uint32_t inst);
    void exec_system   (uint32_t inst);

    // register write — enforces x0 == 0
    inline void wreg(uint32_t rd, uint32_t val) {
        if (rd != 0) regs_[rd] = val;
    }
};

} // namespace stakrv