#include "cpu.hpp"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <cstring>

namespace stakrv {

// ── helpers ───────────────────────────────────────────────────────────────────

// sign-extend a value of `bits` width to 32 bits
static inline int32_t sign_ext(uint32_t val, uint32_t bits) {
    uint32_t sign = 1u << (bits - 1);
    return (val ^ sign) - sign;
}

// instruction field extractors
static inline uint32_t opcode(uint32_t i) { return i & 0x7F; }
static inline uint32_t rd    (uint32_t i) { return (i >> 7)  & 0x1F; }
static inline uint32_t funct3(uint32_t i) { return (i >> 12) & 0x07; }
static inline uint32_t rs1   (uint32_t i) { return (i >> 15) & 0x1F; }
static inline uint32_t rs2   (uint32_t i) { return (i >> 20) & 0x1F; }
static inline uint32_t funct7(uint32_t i) { return (i >> 25) & 0x7F; }

// immediate decoders
static inline int32_t imm_i(uint32_t i) {
    return sign_ext(i >> 20, 12);
}
static inline int32_t imm_s(uint32_t i) {
    return sign_ext(((i >> 25) << 5) | ((i >> 7) & 0x1F), 12);
}
static inline int32_t imm_b(uint32_t i) {
    uint32_t v = ((i >> 31) << 12) | (((i >> 7) & 1) << 11) |
                 (((i >> 25) & 0x3F) << 5) | (((i >> 8) & 0xF) << 1);
    return sign_ext(v, 13);
}
static inline int32_t imm_u(uint32_t i) {
    return (int32_t)(i & 0xFFFFF000);
}
static inline int32_t imm_j(uint32_t i) {
    uint32_t v = ((i >> 31) << 20)       |
                 (((i >> 12) & 0xFF) << 12) |
                 (((i >> 20) & 1) << 11)  |
                 (((i >> 21) & 0x3FF) << 1);
    return sign_ext(v, 21);
}

// ── CPU ───────────────────────────────────────────────────────────────────────

CPU::CPU() : pc_(MEM_BASE), mem_(MEM_SIZE, 0) {
    std::memset(regs_, 0, sizeof(regs_));
    // sp (x2) points to top of memory by convention
    regs_[2] = MEM_BASE + MEM_SIZE;
}

bool CPU::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        std::cerr << "[stakrv] cannot open: " << path << "\n";
        return false;
    }
    size_t size = f.tellg();
    f.seekg(0);
    if (size > MEM_SIZE) {
        std::cerr << "[stakrv] binary too large: " << size << " bytes\n";
        return false;
    }
    f.read(reinterpret_cast<char*>(mem_.data()), size);
    std::cout << "[stakrv] loaded " << size << " bytes from " << path << "\n";
    return true;
}

// ── memory ────────────────────────────────────────────────────────────────────

uint32_t CPU::mem_read32(uint32_t addr) const {
    uint32_t off = addr - MEM_BASE;
    if (off + 4 > MEM_SIZE) throw std::runtime_error("mem_read32 out of bounds: 0x" + std::to_string(addr));
    uint32_t v;
    std::memcpy(&v, &mem_[off], 4);
    return v;
}
uint16_t CPU::mem_read16(uint32_t addr) const {
    uint32_t off = addr - MEM_BASE;
    if (off + 2 > MEM_SIZE) throw std::runtime_error("mem_read16 out of bounds");
    uint16_t v;
    std::memcpy(&v, &mem_[off], 2);
    return v;
}
uint8_t CPU::mem_read8(uint32_t addr) const {
    uint32_t off = addr - MEM_BASE;
    if (off >= MEM_SIZE) throw std::runtime_error("mem_read8 out of bounds");
    return mem_[off];
}
void CPU::mem_write32(uint32_t addr, uint32_t val) {
    uint32_t off = addr - MEM_BASE;
    if (off + 4 > MEM_SIZE) throw std::runtime_error("mem_write32 out of bounds");
    std::memcpy(&mem_[off], &val, 4);
}
void CPU::mem_write16(uint32_t addr, uint16_t val) {
    uint32_t off = addr - MEM_BASE;
    if (off + 2 > MEM_SIZE) throw std::runtime_error("mem_write16 out of bounds");
    std::memcpy(&mem_[off], &val, 2);
}
void CPU::mem_write8(uint32_t addr, uint8_t val) {
    uint32_t off = addr - MEM_BASE;
    if (off >= MEM_SIZE) throw std::runtime_error("mem_write8 out of bounds");
    mem_[off] = val;
}

// ── instruction handlers ──────────────────────────────────────────────────────

void CPU::exec_lui(uint32_t inst) {
    wreg(rd(inst), (uint32_t)imm_u(inst));
    pc_ += 4;
}

void CPU::exec_auipc(uint32_t inst) {
    wreg(rd(inst), pc_ + (uint32_t)imm_u(inst));
    pc_ += 4;
}

void CPU::exec_jal(uint32_t inst) {
    wreg(rd(inst), pc_ + 4);
    pc_ = pc_ + (uint32_t)imm_j(inst);
}

void CPU::exec_jalr(uint32_t inst) {
    uint32_t target = (regs_[rs1(inst)] + (uint32_t)imm_i(inst)) & ~1u;
    wreg(rd(inst), pc_ + 4);
    pc_ = target;
}

void CPU::exec_branch(uint32_t inst) {
    uint32_t a = regs_[rs1(inst)];
    uint32_t b = regs_[rs2(inst)];
    bool taken = false;
    switch (funct3(inst)) {
        case 0x0: taken = (a == b);                           break; // BEQ
        case 0x1: taken = (a != b);                           break; // BNE
        case 0x4: taken = ((int32_t)a <  (int32_t)b);        break; // BLT
        case 0x5: taken = ((int32_t)a >= (int32_t)b);        break; // BGE
        case 0x6: taken = (a <  b);                           break; // BLTU
        case 0x7: taken = (a >= b);                           break; // BGEU
        default:  throw std::runtime_error("unknown branch funct3");
    }
    pc_ = taken ? pc_ + (uint32_t)imm_b(inst) : pc_ + 4;
}

void CPU::exec_load(uint32_t inst) {
    uint32_t addr = regs_[rs1(inst)] + (uint32_t)imm_i(inst);
    uint32_t val  = 0;
    switch (funct3(inst)) {
        case 0x0: val = (uint32_t)sign_ext(mem_read8 (addr), 8);  break; // LB
        case 0x1: val = (uint32_t)sign_ext(mem_read16(addr), 16); break; // LH
        case 0x2: val = mem_read32(addr);                          break; // LW
        case 0x4: val = mem_read8 (addr);                          break; // LBU
        case 0x5: val = mem_read16(addr);                          break; // LHU
        default:  throw std::runtime_error("unknown load funct3");
    }
    wreg(rd(inst), val);
    pc_ += 4;
}

void CPU::exec_store(uint32_t inst) {
    uint32_t addr = regs_[rs1(inst)] + (uint32_t)imm_s(inst);
    uint32_t val  = regs_[rs2(inst)];
    switch (funct3(inst)) {
        case 0x0: mem_write8 (addr, (uint8_t) val); break; // SB
        case 0x1: mem_write16(addr, (uint16_t)val); break; // SH
        case 0x2: mem_write32(addr, val);            break; // SW
        default:  throw std::runtime_error("unknown store funct3");
    }
    pc_ += 4;
}

void CPU::exec_op_imm(uint32_t inst) {
    int32_t  imm  = imm_i(inst);
    uint32_t src  = regs_[rs1(inst)];
    uint32_t shamt = (uint32_t)imm & 0x1F;
    uint32_t val  = 0;
    switch (funct3(inst)) {
        case 0x0: val = src + (uint32_t)imm;                     break; // ADDI
        case 0x1: val = src << shamt;                             break; // SLLI
        case 0x2: val = ((int32_t)src < imm) ? 1 : 0;           break; // SLTI
        case 0x3: val = (src < (uint32_t)imm) ? 1 : 0;          break; // SLTIU
        case 0x4: val = src ^ (uint32_t)imm;                     break; // XORI
        case 0x5:
            if (funct7(inst) == 0x20) val = (uint32_t)((int32_t)src >> shamt); // SRAI
            else                      val = src >> shamt;                        // SRLI
            break;
        case 0x6: val = src | (uint32_t)imm;                     break; // ORI
        case 0x7: val = src & (uint32_t)imm;                     break; // ANDI
    }
    wreg(rd(inst), val);
    pc_ += 4;
}

void CPU::exec_op(uint32_t inst) {
    uint32_t a = regs_[rs1(inst)];
    uint32_t b = regs_[rs2(inst)];
    uint32_t shamt = b & 0x1F;
    uint32_t val = 0;
    bool alt = (funct7(inst) == 0x20);
    switch (funct3(inst)) {
        case 0x0: val = alt ? a - b : a + b;                      break; // ADD/SUB
        case 0x1: val = a << shamt;                                break; // SLL
        case 0x2: val = ((int32_t)a < (int32_t)b) ? 1 : 0;      break; // SLT
        case 0x3: val = (a < b) ? 1 : 0;                          break; // SLTU
        case 0x4: val = a ^ b;                                     break; // XOR
        case 0x5: val = alt ? (uint32_t)((int32_t)a >> shamt)
                            : a >> shamt;                          break; // SRL/SRA
        case 0x6: val = a | b;                                     break; // OR
        case 0x7: val = a & b;                                     break; // AND
    }
    wreg(rd(inst), val);
    pc_ += 4;
}

void CPU::exec_system(uint32_t inst) {
    switch (imm_i(inst)) {
        case 0x0:  // ECALL — minimal: just print a7 (syscall number) and stop
            std::cout << "[stakrv] ECALL a7=" << regs_[17] << "\n";
            throw std::runtime_error("ECALL halt");
        case 0x1:  // EBREAK
            std::cout << "[stakrv] EBREAK at pc=0x"
                      << std::hex << pc_ << std::dec << "\n";
            throw std::runtime_error("EBREAK halt");
        default:
            throw std::runtime_error("unknown system instruction");
    }
}

// ── fetch-decode-execute ──────────────────────────────────────────────────────

bool CPU::step() {
    uint32_t inst = mem_read32(pc_);

    switch (opcode(inst)) {
        case 0x37: exec_lui    (inst); break; // LUI
        case 0x17: exec_auipc  (inst); break; // AUIPC
        case 0x6F: exec_jal    (inst); break; // JAL
        case 0x67: exec_jalr   (inst); break; // JALR
        case 0x63: exec_branch (inst); break; // BRANCH
        case 0x03: exec_load   (inst); break; // LOAD
        case 0x23: exec_store  (inst); break; // STORE
        case 0x13: exec_op_imm (inst); break; // OP-IMM
        case 0x33: exec_op     (inst); break; // OP
        case 0x73: exec_system (inst); break; // SYSTEM
        default:
            std::cerr << "[stakrv] unknown opcode 0x" << std::hex
                      << opcode(inst) << " at pc=0x" << pc_ << std::dec << "\n";
            return false;
    }
    return true;
}

void CPU::run() {
    try {
        while (true) {
            if (!step()) break;
        }
    } catch (const std::runtime_error& e) {
        // color the halt reason: yellow for EBREAK, red for errors
        bool is_break = std::string(e.what()).find("EBREAK") != std::string::npos ||
                        std::string(e.what()).find("ECALL")  != std::string::npos;
        std::cout << (is_break ? "\033[33m" : "\033[31m")
                  << "[stakrv] " << e.what()
                  << "\033[0m\n";
    }
}

// ── register dump ─────────────────────────────────────────────────────────────

// ANSI codes
static constexpr const char* DIM    = "\033[2m";
static constexpr const char* GREEN  = "\033[32m";
static constexpr const char* CYAN   = "\033[36m";
static constexpr const char* YELLOW = "\033[33m";
static constexpr const char* BOLD   = "\033[1m";
static constexpr const char* RESET  = "\033[0m";

void CPU::dump_regs() const {
    static const char* names[32] = {
        "zero","ra",  "sp",  "gp",  "tp",  "t0",  "t1",  "t2",
        "s0",  "s1",  "a0",  "a1",  "a2",  "a3",  "a4",  "a5",
        "a6",  "a7",  "s2",  "s3",  "s4",  "s5",  "s6",  "s7",
        "s8",  "s9",  "s10", "s11", "t3",  "t4",  "t5",  "t6"
    };

    std::cout << "\n" << DIM << " ╭─── " << RESET << BOLD << "CPU STATE" << RESET
              << DIM << " ───────────────────────────────────────────────────╮" << RESET << "\n";

    for (int i = 0; i < 32; i += 2) {
        std::cout << DIM << " │" << RESET;
        for (int j = 0; j < 2; ++j) {
            int idx = i + j;
            bool nz   = (regs_[idx] != 0);
            bool is_a = (idx >= 10 && idx <= 17);
            bool is_s = (idx == 1 || idx == 2);  

            std::cout << " " 
                      << (nz ? (is_a ? CYAN : (is_s ? YELLOW : GREEN)) : DIM)
                      << std::setw(4) << names[idx] << RESET
                      << DIM << " x" << std::setw(2) << std::setfill('0') << idx << std::setfill(' ') << RESET
                      << "  ";

            if (nz) {
                std::cout << (is_a ? CYAN : (is_s ? YELLOW : GREEN))
                          << "0x" << std::hex << std::setw(8) << std::setfill('0')
                          << regs_[idx] << std::dec << std::setfill(' ') << RESET;
            } else {
                std::cout << DIM << "0x00000000" << RESET;
            }

            if (nz && regs_[idx] < 0x10000 && idx != 2) {
                std::string hint = "(" + std::to_string(regs_[idx]) + ")";
                std::cout << DIM << " " << std::setw(9) << std::left << hint << std::right << RESET;
            } else {
                std::cout << "          "; // exactly 10 spaces
            }

            if (j == 0) std::cout << DIM << " │" << RESET;
        }
        std::cout << DIM << " │" << RESET << "\n";
    }

    std::cout << DIM << " ├─────────────────────────────────────────────────────────────────┤" << RESET << "\n";

    uint32_t inst = 0;
    bool inst_ok = false;
    try { inst = mem_read32(pc_); inst_ok = true; } catch(...) {} 
    
    std::cout << DIM << " │" << RESET << BOLD << "    pc" << RESET
              << DIM  << " ---  " << RESET
              << BOLD << CYAN << "0x" << std::hex << std::setw(8) << std::setfill('0') << pc_ << std::dec << std::setfill(' ') << RESET;
    
    if (inst_ok) {
        std::cout << DIM << "  [0x" << std::hex << std::setw(8) << std::setfill('0') << inst << std::dec << std::setfill(' ') << "]" << RESET;
    } else {
        std::cout << DIM << "  [??????????]" << RESET;
    }
    
    std::cout << "                             " << DIM << "│" << RESET << "\n"; 

    std::cout << DIM << " ├─── " << RESET << BOLD << "STACK PEEK" << RESET 
              << DIM << " ──────────────────────────────────────────────────┤" << RESET << "\n";

    uint32_t current_sp = regs_[2];
    for (int i = 0; i < 4; ++i) {
        uint32_t addr = current_sp + (i * 4);
        std::cout << DIM << " │  0x" << std::hex << std::setw(8) << std::setfill('0') << addr << std::dec << std::setfill(' ') << " │ " << RESET;
        try {
            uint32_t val = mem_read32(addr);
            std::cout << CYAN << "0x" << std::hex << std::setfill('0') << std::setw(8) << val << std::dec << std::setfill(' ') << RESET;
        } catch (...) {
            std::cout << DIM << "??????????" << RESET;
        }
        std::cout << "                                        " << DIM << "│" << RESET << "\n";
    }

    std::cout << DIM << " ╰─────────────────────────────────────────────────────────────────╯" << RESET << "\n\n";
}

} // namespace stakrv