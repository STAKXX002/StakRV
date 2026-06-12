#include "disassembler.hpp"

#include <cstdio>

namespace stakrv {

// ── bit-field helpers (file-local, mirrors cpu.cpp intentionally) ─────────────

static inline uint32_t opcode(uint32_t i) { return i & 0x7F; }
static inline uint32_t rd    (uint32_t i) { return (i >>  7) & 0x1F; }
static inline uint32_t funct3(uint32_t i) { return (i >> 12) & 0x07; }
static inline uint32_t rs1   (uint32_t i) { return (i >> 15) & 0x1F; }
static inline uint32_t rs2   (uint32_t i) { return (i >> 20) & 0x1F; }
static inline uint32_t funct7(uint32_t i) { return (i >> 25) & 0x7F; }

static inline int32_t sign_ext(uint32_t val, uint32_t bits) {
    uint32_t sign = 1u << (bits - 1);
    return (val ^ sign) - sign;
}
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

// ── register ABI names ────────────────────────────────────────────────────────

static const char* reg_name(uint32_t n) {
    static const char* names[32] = {
        "zero","ra", "sp", "gp", "tp", "t0", "t1", "t2",
        "s0",  "s1", "a0", "a1", "a2", "a3", "a4", "a5",
        "a6",  "a7", "s2", "s3", "s4", "s5", "s6", "s7",
        "s8",  "s9", "s10","s11","t3", "t4", "t5", "t6"
    };
    return names[n & 31];
}

// ── disassemble ───────────────────────────────────────────────────────────────

std::string disassemble(uint32_t inst) {
    if (inst == 0x00000013) return "nop";

    char buf[48];

    const uint32_t o   = opcode(inst);
    const uint32_t d   = rd(inst);
    const uint32_t a   = rs1(inst);
    const uint32_t b   = rs2(inst);
    const int32_t  ii  = imm_i(inst);
    const bool     alt = (funct7(inst) == 0x20);

    switch (o) {

    case 0x37: // LUI
        snprintf(buf, sizeof(buf), "lui %s, %d", reg_name(d), imm_u(inst) >> 12);
        return buf;

    case 0x17: // AUIPC
        snprintf(buf, sizeof(buf), "auipc %s, %d", reg_name(d), imm_u(inst) >> 12);
        return buf;

    case 0x6F: { // JAL
        int32_t off = imm_j(inst);
        if (d == 0) { snprintf(buf, sizeof(buf), "j %d",   off); return buf; }
        if (d == 1) { snprintf(buf, sizeof(buf), "jal %d", off); return buf; }
        snprintf(buf, sizeof(buf), "jal %s, %d", reg_name(d), off);
        return buf;
    }

    case 0x67: // JALR
        if (d == 0 && a == 1 && ii == 0) return "ret";
        if (d == 0 && ii == 0) {
            snprintf(buf, sizeof(buf), "jr %s", reg_name(a));
            return buf;
        }
        snprintf(buf, sizeof(buf), "jalr %s, %s, %d", reg_name(d), reg_name(a), ii);
        return buf;

    case 0x63: { // BRANCH
        static const char* bnames[] = {"beq","bne","?","?","blt","bge","bltu","bgeu"};
        int32_t  off = imm_b(inst);
        uint32_t f   = funct3(inst);
        if ((f == 0 || f == 1) && b == 0) {
            snprintf(buf, sizeof(buf), "%sz %s, %d",
                     f == 0 ? "beq" : "bne", reg_name(a), off);
            return buf;
        }
        snprintf(buf, sizeof(buf), "%s %s, %s, %d",
                 bnames[f], reg_name(a), reg_name(b), off);
        return buf;
    }

    case 0x03: { // LOAD
        static const char* lnames[] = {"lb","lh","lw","?","lbu","lhu"};
        snprintf(buf, sizeof(buf), "%s %s, %d(%s)",
                 lnames[funct3(inst)], reg_name(d), ii, reg_name(a));
        return buf;
    }

    case 0x23: { // STORE
        static const char* snames[] = {"sb","sh","sw"};
        snprintf(buf, sizeof(buf), "%s %s, %d(%s)",
                 snames[funct3(inst)], reg_name(b), imm_s(inst), reg_name(a));
        return buf;
    }

    case 0x13: { // OP-IMM
        uint32_t shamt = (uint32_t)ii & 0x1F;
        switch (funct3(inst)) {
        case 0:
            if (a == 0) { snprintf(buf, sizeof(buf), "li %s, %d",    reg_name(d), ii);              return buf; }
            if (ii == 0){ snprintf(buf, sizeof(buf), "mv %s, %s",    reg_name(d), reg_name(a));     return buf; }
            snprintf(buf, sizeof(buf), "addi %s, %s, %d",  reg_name(d), reg_name(a), ii);           return buf;
        case 1: snprintf(buf, sizeof(buf), "slli %s, %s, %d",  reg_name(d), reg_name(a), shamt);    return buf;
        case 2: snprintf(buf, sizeof(buf), "slti %s, %s, %d",  reg_name(d), reg_name(a), ii);       return buf;
        case 3:
            if (ii == 1){ snprintf(buf, sizeof(buf), "seqz %s, %s",  reg_name(d), reg_name(a));     return buf; }
            snprintf(buf, sizeof(buf), "sltiu %s, %s, %d", reg_name(d), reg_name(a), ii);           return buf;
        case 4:
            if (ii == -1){ snprintf(buf, sizeof(buf), "not %s, %s",  reg_name(d), reg_name(a));     return buf; }
            snprintf(buf, sizeof(buf), "xori %s, %s, %d",  reg_name(d), reg_name(a), ii);           return buf;
        case 5:
            snprintf(buf, sizeof(buf), "%s %s, %s, %d",
                     alt ? "srai" : "srli", reg_name(d), reg_name(a), shamt);                       return buf;
        case 6: snprintf(buf, sizeof(buf), "ori %s, %s, %d",   reg_name(d), reg_name(a), ii);       return buf;
        case 7: snprintf(buf, sizeof(buf), "andi %s, %s, %d",  reg_name(d), reg_name(a), ii);       return buf;
        }
        break;
    }

    case 0x33: { // OP
        static const char* rnames[] = {"add","sll","slt","sltu","xor","srl","or","and"};
        static const char* mnames[] = {"mul","mulh","mulhsu","mulhu","div","divu","rem","remu"};
        uint32_t f   = funct3(inst);
        bool     mul = (funct7(inst) == 0x01);
        if (mul) {
            snprintf(buf, sizeof(buf), "%s %s, %s, %s",
                     mnames[f], reg_name(d), reg_name(a), reg_name(b));
            return buf;
        }
        if (f == 0 && alt) {
            if (a == 0) { snprintf(buf, sizeof(buf), "neg %s, %s", reg_name(d), reg_name(b)); return buf; }
            snprintf(buf, sizeof(buf), "sub %s, %s, %s", reg_name(d), reg_name(a), reg_name(b));
            return buf;
        }
        if (f == 5 && alt) {
            snprintf(buf, sizeof(buf), "sra %s, %s, %s", reg_name(d), reg_name(a), reg_name(b));
            return buf;
        }
        if (f == 0 && b == 0) {
            snprintf(buf, sizeof(buf), "mv %s, %s", reg_name(d), reg_name(a));
            return buf;
        }
        snprintf(buf, sizeof(buf), "%s %s, %s, %s",
                 rnames[f], reg_name(d), reg_name(a), reg_name(b));
        return buf;
    }

    case 0x73:
        return (ii == 0) ? "ecall" : "ebreak";
    }

    return "???";
}

} // namespace stakrv