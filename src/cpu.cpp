#include "cpu.hpp"

#include <fstream>
#include <stdexcept>
#include <cstring>
#include <iostream>

namespace stakrv {

// ── bit-field helpers (file-local) ────────────────────────────────────────────

static inline int32_t sign_ext(uint32_t val, uint32_t bits) {
    uint32_t sign = 1u << (bits - 1);
    return (val ^ sign) - sign;
}

static inline uint32_t opcode(uint32_t i) { return i & 0x7F; }
static inline uint32_t rd    (uint32_t i) { return (i >>  7) & 0x1F; }
static inline uint32_t funct3(uint32_t i) { return (i >> 12) & 0x07; }
static inline uint32_t rs1   (uint32_t i) { return (i >> 15) & 0x1F; }
static inline uint32_t rs2   (uint32_t i) { return (i >> 20) & 0x1F; }
static inline uint32_t funct7(uint32_t i) { return (i >> 25) & 0x7F; }

static inline int32_t imm_i(uint32_t i) { return sign_ext(i >> 20, 12); }
static inline int32_t imm_s(uint32_t i) {
    return sign_ext(((i >> 25) << 5) | ((i >> 7) & 0x1F), 12);
}
static inline int32_t imm_b(uint32_t i) {
    uint32_t v = ((i >> 31) << 12) | (((i >>  7) & 0x01) << 11) |
                 (((i >> 25) & 0x3F) << 5) | (((i >> 8) & 0x0F) << 1);
    return sign_ext(v, 13);
}
static inline int32_t imm_u(uint32_t i) { return (int32_t)(i & 0xFFFFF000); }
static inline int32_t imm_j(uint32_t i) {
    uint32_t v = ((i >> 31) << 20)         |
                 (((i >> 12) & 0xFF) << 12) |
                 (((i >> 20) & 0x01) << 11) |
                 (((i >> 21) & 0x3FF) << 1);
    return sign_ext(v, 21);
}

// ── CPU ───────────────────────────────────────────────────────────────────────

CPU::CPU() : pc_(MEM_BASE), mem_(MEM_SIZE, 0) {
    std::memset(regs_, 0, sizeof(regs_));
    regs_[2] = MEM_BASE + MEM_SIZE; // sp → top of memory
}

bool CPU::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        std::cerr << "[stakrv] cannot open: " << path << "\n";
        return false;
    }
    auto size = static_cast<size_t>(f.tellg());
    f.seekg(0);
    if (size > MEM_SIZE) {
        std::cerr << "[stakrv] binary too large: " << size << " bytes\n";
        return false;
    }
    f.read(reinterpret_cast<char*>(mem_.data()), size);
    return true;
}

bool CPU::peek32(uint32_t addr, uint32_t& out) const {
    uint32_t off = addr - MEM_BASE;
    if (off > MEM_SIZE - 4) return false;
    std::memcpy(&out, &mem_[off], 4);
    return true;
}

// ── memory ────────────────────────────────────────────────────────────────────

uint32_t CPU::mem_read32(uint32_t addr) const {
    uint32_t off = addr - MEM_BASE;
    if (off > MEM_SIZE - 4)
        throw std::runtime_error("mem_read32 OOB @ 0x" + std::to_string(addr));
    uint32_t v; std::memcpy(&v, &mem_[off], 4); return v;
}
uint16_t CPU::mem_read16(uint32_t addr) const {
    uint32_t off = addr - MEM_BASE;
    if (off > MEM_SIZE - 2) throw std::runtime_error("mem_read16 OOB");
    uint16_t v; std::memcpy(&v, &mem_[off], 2); return v;
}
uint8_t CPU::mem_read8(uint32_t addr) const {
    uint32_t off = addr - MEM_BASE;
    if (off >= MEM_SIZE) throw std::runtime_error("mem_read8 OOB");
    return mem_[off];
}
void CPU::mem_write32(uint32_t addr, uint32_t val) {
    uint32_t off = addr - MEM_BASE;
    if (off > MEM_SIZE - 4) throw std::runtime_error("mem_write32 OOB");
    std::memcpy(&mem_[off], &val, 4);
}
void CPU::mem_write16(uint32_t addr, uint16_t val) {
    uint32_t off = addr - MEM_BASE;
    if (off > MEM_SIZE - 2) throw std::runtime_error("mem_write16 OOB");
    std::memcpy(&mem_[off], &val, 2);
}
void CPU::mem_write8(uint32_t addr, uint8_t val) {
    uint32_t off = addr - MEM_BASE;
    if (off >= MEM_SIZE) throw std::runtime_error("mem_write8 OOB");
    mem_[off] = val;
}

// ── instruction handlers ──────────────────────────────────────────────────────

void CPU::exec_lui(uint32_t inst) {
    uint32_t d = rd(inst);
    if (d) regs_[d] = (uint32_t)imm_u(inst);
    pc_ += 4;
}

void CPU::exec_auipc(uint32_t inst) {
    uint32_t d = rd(inst);
    if (d) regs_[d] = pc_ + (uint32_t)imm_u(inst);
    pc_ += 4;
}

void CPU::exec_jal(uint32_t inst) {
    uint32_t d = rd(inst);
    if (d) regs_[d] = pc_ + 4;
    pc_ = pc_ + (uint32_t)imm_j(inst);
}

void CPU::exec_jalr(uint32_t inst) {
    uint32_t target = (regs_[rs1(inst)] + (uint32_t)imm_i(inst)) & ~1u;
    uint32_t d = rd(inst);
    if (d) regs_[d] = pc_ + 4;
    pc_ = target;
}

void CPU::exec_branch(uint32_t inst) {
    uint32_t a = regs_[rs1(inst)], b = regs_[rs2(inst)];
    bool taken = false;
    switch (funct3(inst)) {
        case 0x0: taken = (a == b);                         break; // BEQ
        case 0x1: taken = (a != b);                         break; // BNE
        case 0x4: taken = ((int32_t)a <  (int32_t)b);       break; // BLT
        case 0x5: taken = ((int32_t)a >= (int32_t)b);       break; // BGE
        case 0x6: taken = (a <  b);                         break; // BLTU
        case 0x7: taken = (a >= b);                         break; // BGEU
        default:  throw std::runtime_error("unknown branch funct3");
    }
    pc_ = taken ? pc_ + (uint32_t)imm_b(inst) : pc_ + 4;
}

void CPU::exec_load(uint32_t inst) {
    uint32_t addr = regs_[rs1(inst)] + (uint32_t)imm_i(inst);
    uint32_t val  = 0;
    switch (funct3(inst)) {
        case 0x0: val = (uint32_t)sign_ext(mem_read8 (addr),  8); break; // LB
        case 0x1: val = (uint32_t)sign_ext(mem_read16(addr), 16); break; // LH
        case 0x2: val = mem_read32(addr);                         break; // LW
        case 0x4: val = mem_read8 (addr);                         break; // LBU
        case 0x5: val = mem_read16(addr);                         break; // LHU
        default:  throw std::runtime_error("unknown load funct3");
    }
    uint32_t d = rd(inst);
    if (d) regs_[d] = val;
    pc_ += 4;
}

void CPU::exec_store(uint32_t inst) {
    uint32_t addr = regs_[rs1(inst)] + (uint32_t)imm_s(inst);
    uint32_t val  = regs_[rs2(inst)];
    switch (funct3(inst)) {
        case 0x0: mem_write8 (addr, (uint8_t) val); break; // SB
        case 0x1: mem_write16(addr, (uint16_t)val); break; // SH
        case 0x2: mem_write32(addr, val);           break; // SW
        default:  throw std::runtime_error("unknown store funct3");
    }
    pc_ += 4;
}

void CPU::exec_op_imm(uint32_t inst) {
    int32_t  imm   = imm_i(inst);
    uint32_t src   = regs_[rs1(inst)];
    uint32_t shamt = (uint32_t)imm & 0x1F;
    uint32_t val   = 0;
    switch (funct3(inst)) {
        case 0x0: val = src + (uint32_t)imm;                    break; // ADDI
        case 0x1: val = src << shamt;                           break; // SLLI
        case 0x2: val = ((int32_t)src < imm)         ? 1 : 0;  break; // SLTI
        case 0x3: val = (src < (uint32_t)imm)        ? 1 : 0;  break; // SLTIU
        case 0x4: val = src ^ (uint32_t)imm;                   break; // XORI
        case 0x5:
            val = (funct7(inst) == 0x20)
                ? (uint32_t)((int32_t)src >> shamt)            // SRAI
                : src >> shamt;                                 // SRLI
            break;
        case 0x6: val = src | (uint32_t)imm;                   break; // ORI
        case 0x7: val = src & (uint32_t)imm;                   break; // ANDI
    }
    uint32_t d = rd(inst);
    if (d) regs_[d] = val;
    pc_ += 4;
}

void CPU::exec_op(uint32_t inst) {
    if (funct7(inst) == 0x01)
        throw std::runtime_error("M-extension (mul/div) not implemented");

    uint32_t a     = regs_[rs1(inst)];
    uint32_t b     = regs_[rs2(inst)];
    uint32_t shamt = b & 0x1F;
    bool     alt   = (funct7(inst) == 0x20);
    uint32_t val   = 0;
    switch (funct3(inst)) {
        case 0x0: val = alt ? a - b : a + b;                         break; // ADD/SUB
        case 0x1: val = a << shamt;                                  break; // SLL
        case 0x2: val = ((int32_t)a < (int32_t)b) ? 1 : 0;          break; // SLT
        case 0x3: val = (a < b)                   ? 1 : 0;          break; // SLTU
        case 0x4: val = a ^ b;                                       break; // XOR
        case 0x5: val = alt ? (uint32_t)((int32_t)a >> shamt)
                            : a >> shamt;                            break; // SRL/SRA
        case 0x6: val = a | b;                                       break; // OR
        case 0x7: val = a & b;                                       break; // AND
    }
    uint32_t d = rd(inst);
    if (d) regs_[d] = val;
    pc_ += 4;
}

void CPU::exec_system(uint32_t inst) {
    switch (imm_i(inst)) {
        case 0: throw std::runtime_error("ECALL halt");
        case 1: throw std::runtime_error("EBREAK halt");
        default: throw std::runtime_error("unknown SYSTEM instruction");
    }
}

// ── fetch-decode-execute ──────────────────────────────────────────────────────

bool CPU::step() {
    uint32_t inst = mem_read32(pc_);
    switch (opcode(inst)) {
        case 0x37: exec_lui    (inst); break;
        case 0x17: exec_auipc  (inst); break;
        case 0x6F: exec_jal    (inst); break;
        case 0x67: exec_jalr   (inst); break;
        case 0x63: exec_branch (inst); break;
        case 0x03: exec_load   (inst); break;
        case 0x23: exec_store  (inst); break;
        case 0x13: exec_op_imm (inst); break;
        case 0x33: exec_op     (inst); break;
        case 0x73: exec_system (inst); break;
        default: throw std::runtime_error("unknown opcode");
    }
    return true;
}

} // namespace stakrv