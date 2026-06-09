#include "cpu.hpp"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <cstring>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <sstream>
#include <thread>
#include <chrono>

namespace stakrv {

// ── helpers ───────────────────────────────────────────────────────────────────

static inline int32_t sign_ext(uint32_t val, uint32_t bits) {
    uint32_t sign = 1u << (bits - 1);
    return (val ^ sign) - sign;
}

static inline uint32_t opcode(uint32_t i) { return i & 0x7F; }
static inline uint32_t rd    (uint32_t i) { return (i >> 7)  & 0x1F; }
static inline uint32_t funct3(uint32_t i) { return (i >> 12) & 0x07; }
static inline uint32_t rs1   (uint32_t i) { return (i >> 15) & 0x1F; }
static inline uint32_t rs2   (uint32_t i) { return (i >> 20) & 0x1F; }
static inline uint32_t funct7(uint32_t i) { return (i >> 25) & 0x7F; }

static inline int32_t imm_i(uint32_t i) { return sign_ext(i >> 20, 12); }
static inline int32_t imm_s(uint32_t i) { return sign_ext(((i >> 25) << 5) | ((i >> 7) & 0x1F), 12); }
static inline int32_t imm_b(uint32_t i) {
    uint32_t v = ((i >> 31) << 12) | (((i >> 7) & 1) << 11) |
                 (((i >> 25) & 0x3F) << 5) | (((i >> 8) & 0xF) << 1);
    return sign_ext(v, 13);
}
static inline int32_t imm_u(uint32_t i) { return (int32_t)(i & 0xFFFFF000); }
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
    regs_[2] = MEM_BASE + MEM_SIZE; // sp
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
    uint32_t target_rd = rd(inst);
    if (target_rd != 0) regs_[target_rd] = (uint32_t)imm_u(inst);
    pc_ += 4;
}

void CPU::exec_auipc(uint32_t inst) {
    uint32_t target_rd = rd(inst);
    if (target_rd != 0) regs_[target_rd] = pc_ + (uint32_t)imm_u(inst);
    pc_ += 4;
}

void CPU::exec_jal(uint32_t inst) {
    uint32_t target_rd = rd(inst);
    if (target_rd != 0) regs_[target_rd] = pc_ + 4;
    pc_ = pc_ + (uint32_t)imm_j(inst);
}

void CPU::exec_jalr(uint32_t inst) {
    uint32_t target = (regs_[rs1(inst)] + (uint32_t)imm_i(inst)) & ~1u;
    uint32_t target_rd = rd(inst);
    if (target_rd != 0) regs_[target_rd] = pc_ + 4;
    pc_ = target;
}

void CPU::exec_branch(uint32_t inst) {
    uint32_t a = regs_[rs1(inst)];
    uint32_t b = regs_[rs2(inst)];
    bool taken = false;
    switch (funct3(inst)) {
        case 0x0: taken = (a == b);                           break; // BEQ
        case 0x1: taken = (a != b);                           break; // BNE
        case 0x4: taken = ((int32_t)a <  (int32_t)b);         break; // BLT
        case 0x5: taken = ((int32_t)a >= (int32_t)b);         break; // BGE
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
        case 0x2: val = mem_read32(addr);                         break; // LW
        case 0x4: val = mem_read8 (addr);                         break; // LBU
        case 0x5: val = mem_read16(addr);                         break; // LHU
        default:  throw std::runtime_error("unknown load funct3");
    }
    uint32_t target_rd = rd(inst);
    if (target_rd != 0) regs_[target_rd] = val;
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
    int32_t  imm  = imm_i(inst);
    uint32_t src  = regs_[rs1(inst)];
    uint32_t shamt = (uint32_t)imm & 0x1F;
    uint32_t val  = 0;
    switch (funct3(inst)) {
        case 0x0: val = src + (uint32_t)imm;                     break; // ADDI
        case 0x1: val = src << shamt;                            break; // SLLI
        case 0x2: val = ((int32_t)src < imm) ? 1 : 0;            break; // SLTI
        case 0x3: val = (src < (uint32_t)imm) ? 1 : 0;           break; // SLTIU
        case 0x4: val = src ^ (uint32_t)imm;                     break; // XORI
        case 0x5:
            if (funct7(inst) == 0x20) val = (uint32_t)((int32_t)src >> shamt); // SRAI
            else                      val = src >> shamt;                      // SRLI
            break;
        case 0x6: val = src | (uint32_t)imm;                     break; // ORI
        case 0x7: val = src & (uint32_t)imm;                     break; // ANDI
    }
    uint32_t target_rd = rd(inst);
    if (target_rd != 0) regs_[target_rd] = val;
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
        case 0x1: val = a << shamt;                               break; // SLL
        case 0x2: val = ((int32_t)a < (int32_t)b) ? 1 : 0;        break; // SLT
        case 0x3: val = (a < b) ? 1 : 0;                          break; // SLTU
        case 0x4: val = a ^ b;                                    break; // XOR
        case 0x5: val = alt ? (uint32_t)((int32_t)a >> shamt)
                            : a >> shamt;                         break; // SRL/SRA
        case 0x6: val = a | b;                                    break; // OR
        case 0x7: val = a & b;                                    break; // AND
    }
    uint32_t target_rd = rd(inst);
    if (target_rd != 0) regs_[target_rd] = val;
    pc_ += 4;
}

void CPU::exec_system(uint32_t inst) {
    switch (imm_i(inst)) {
        case 0x0:  
            throw std::runtime_error("ECALL halt");
        case 0x1:  
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
            throw std::runtime_error("Unknown opcode");
    }
    return true;
}

// ── Terminal TUI Helpers ──────────────────────────────────────────────────────

static struct termios oldt;
static void set_terminal_raw_mode(bool enable) {
    if (enable) {
        tcgetattr(STDIN_FILENO, &oldt);
        struct termios newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO); // Disable buffering & local echo
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK); // Non-blocking read
    } else {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        fcntl(STDIN_FILENO, F_SETFL, 0); // Restore blocking read
    }
}

static int get_char_non_blocking() {
    char ch;
    if (read(STDIN_FILENO, &ch, 1) > 0) return ch;
    return -1;
}

// ── TUI Run Loop & Rendering ──────────────────────────────────────────────────

void CPU::run() {
    bool running = true;
    bool paused = true; 
    int delay_ms = 100; // Start at 100ms minimum
    
    set_terminal_raw_mode(true);
    std::cout << "\033[?1049h\033[?25l\033[2J"; 

    // Create our stopwatch
    auto last_step_time = std::chrono::steady_clock::now();

    while (running) {
        // 1. Instant Input Drainer
        int ch;
        while ((ch = get_char_non_blocking()) != -1) {
            if (ch == 'q' || ch == 27) { 
                running = false;
            } else if (ch == 'p' || ch == ' ') { 
                paused = !paused;
                if (!paused) {
                    // Reset stopwatch when unpausing so it doesn't instantly fire
                    last_step_time = std::chrono::steady_clock::now();
                }
            } else if (ch == 's' && paused) { 
                try { if (!step()) running = false; } catch (...) {}
            } else if (ch == '+' || ch == '=') { // Slower (Add delay)
                delay_ms += 100;
            } else if (ch == '-') {              // Faster (Sub delay)
                if (delay_ms > 100) delay_ms -= 100;
            }
        }

        // 2. Timed CPU Execution
        if (!paused) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_step_time).count();
            
            // Only step the CPU if the required delay has passed
            if (elapsed >= delay_ms) {
                try {
                    if (!step()) {
                        paused = true; 
                    }
                } catch (...) {
                    paused = true; // Auto-pause on EBREAK/Error
                }
                last_step_time = std::chrono::steady_clock::now(); // Reset stopwatch
            }
        }

        // 3. Render Dashboard
        render_dashboard(paused, delay_ms);
        
        // 4. UI Frame Delay (~60 FPS) ensures keys are instantly recognized
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    std::cout << "\033[?25h\033[?1049l"; 
    set_terminal_raw_mode(false);
}

void CPU::render_dashboard(bool paused, int delay_ms) {
    std::stringstream ss;
    
    // Move cursor to top-left (0,0)
    ss << "\033[H"; 

    // ANSI Colors
    static constexpr const char* DIM    = "\033[2m";
    static constexpr const char* GREEN  = "\033[32m";
    static constexpr const char* CYAN   = "\033[36m";
    static constexpr const char* YELLOW = "\033[33m";
    static constexpr const char* BOLD   = "\033[1m";
    static constexpr const char* RESET  = "\033[0m";

    // ABI Register Names
    static const char* names[32] = {
        "zero","ra",  "sp",  "gp",  "tp",  "t0",  "t1",  "t2",
        "s0",  "s1",  "a0",  "a1",  "a2",  "a3",  "a4",  "a5",
        "a6",  "a7",  "s2",  "s3",  "s4",  "s5",  "s6",  "s7",
        "s8",  "s9",  "s10", "s11", "t3",  "t4",  "t5",  "t6"
    };

    // Helper to repeat UTF-8 box drawing characters safely
    auto repeat_char = [](const std::string& str, int count) {
        std::string result;
        for (int i = 0; i < count; ++i) result += str;
        return result;
    };

    // ─── TOP BORDER (Exactly 106 chars wide) ────────────────────────────────────
    ss << DIM << "╭── " << RESET << BOLD << "StakRV rv32i Emulator " << RESET 
       << DIM << repeat_char("─", 79) << "╮\n" << RESET;

    // ─── STATUS LINE (Exactly 106 chars wide) ───────────────────────────────────
    std::string stat_str = paused ? "[||] PAUSED " : "[>>] RUNNING";
    std::string stat_col = paused ? "\033[1;33m" : "\033[1;32m";

    ss << DIM << "│" << RESET << " Status: " << stat_col << stat_str << RESET 
       << " | Delay: " << BOLD << std::setw(3) << std::setfill(' ') << std::left << delay_ms << "ms" << RESET
       << " | PC: " << BOLD << CYAN << "0x" << std::hex << std::setw(8) << std::setfill('0') << std::right << pc_ << RESET
       << std::setfill(' ') << std::setw(51) << " " << DIM << "│\n" << RESET;

    // ─── DIVIDER 1 (┬ aligned exactly at column 64) ─────────────────────────────
    ss << DIM << "├" << repeat_char("─", 62) << "┬" << repeat_char("─", 41) << "┤\n" << RESET;
    ss << DIM << "│  " << RESET << BOLD << "REGISTERS" << RESET << std::string(51, ' ') 
       << DIM << "│  " << RESET << BOLD << "INSTRUCTION PIPELINE" << RESET << std::string(19, ' ') << DIM << "│\n" << RESET;

    // ─── MAIN BODY ──────────────────────────────────────────────────────────────
    uint32_t current_inst = 0;
    try { current_inst = mem_read32(pc_); } catch(...) {}

    for (int i = 0; i < 16; ++i) {
        int r2 = i + 16;
        ss << DIM << "│ " << RESET; // Left Border

        // Lambda to draw a single register with decimal hints (Exactly 29 chars wide)
        auto draw_reg = [&](int idx) {
            bool nz = (regs_[idx] != 0);
            bool is_a = (idx >= 10 && idx <= 17);
            bool is_s = (idx == 8 || idx == 9 || (idx >= 18 && idx <= 27));
            bool is_sp = (idx == 2);
            bool is_ra = (idx == 1);
            
            const char* color = GREEN; 
            if (is_a) color = CYAN;
            else if (is_s || is_sp || is_ra) color = YELLOW;
            
            if (!nz && !is_sp) color = DIM; 

            // Register Name & Number (9 chars)
            ss << color << std::setfill(' ') << std::setw(4) << std::right << names[idx] << RESET
               << DIM << " x" << std::setfill('0') << std::setw(2) << idx << RESET << " ";

            // Hex Value (10 chars)
            if (nz || is_sp) {
                ss << color << "0x" << std::hex << std::setw(8) << std::setfill('0') << regs_[idx] << RESET;
            } else {
                ss << DIM << "0x00000000" << RESET;
            }

            // Decimal Hint (10 chars) - Only show for small numbers, hide for SP/RA
            if (nz && regs_[idx] < 0x10000 && !is_sp && !is_ra) {
                std::string hint = "(" + std::to_string((int32_t)regs_[idx]) + ")";
                ss << DIM << " " << std::setfill(' ') << std::setw(9) << std::left << hint << std::right << RESET;
            } else {
                ss << std::string(10, ' ');
            }
        };

        draw_reg(i);
        ss << "  "; // Center gap between columns
        draw_reg(r2);
        
        // Middle divider (Printed exactly at column 64)
        ss << DIM << " │" << RESET; 

        // Draw Pipeline Right Side (Exactly 41 inner chars)
        if (i == 2) {
            std::string inst_str = disassemble(current_inst);
            if (inst_str.length() < 16) inst_str.append(16 - inst_str.length(), ' ');
            else inst_str = inst_str.substr(0, 16); 

            ss << BOLD << GREEN << " -> 0x" << std::hex << std::setw(8) << std::setfill('0') << pc_
               << "  " << inst_str << RESET << std::string(9, ' '); 
        } else if (i > 2 && i < 12) {
            uint32_t next_pc = pc_ + ((i - 2) * 4);
            uint32_t next_inst = 0;
            bool ok = true;
            try { next_inst = mem_read32(next_pc); } catch(...) { ok = false; }
            
            if (ok) {
                std::string inst_str = disassemble(next_inst);
                if (inst_str.length() < 16) inst_str.append(16 - inst_str.length(), ' ');
                else inst_str = inst_str.substr(0, 16);

                ss << "    0x" << std::hex << std::setw(8) << std::setfill('0') << next_pc
                   << "  " << DIM << inst_str << RESET << std::string(9, ' ');
            } else {
                ss << std::string(41, ' ');
            }
        } else {
            ss << std::string(41, ' ');
        }

        // Right border (Printed exactly at column 106)
        ss << DIM << "│\033[K\n" << RESET;
    }

    // ─── STACK PEEK DIVIDER (┴ aligned exactly at column 64) ────────────────────
    ss << DIM << "├" << repeat_char("─", 62) << "┴" << repeat_char("─", 41) << "┤\n" << RESET;
    ss << DIM << "│  " << RESET << BOLD << "STACK PEEK" << RESET << std::string(92, ' ') << DIM << "│\n" << RESET;

    uint32_t current_sp = regs_[2];
    for (int i = 0; i < 4; ++i) {
        uint32_t addr = current_sp + (i * 4);
        ss << DIM << "│    0x" << std::hex << std::setw(8) << std::setfill('0') << addr << "  │  " << RESET;
        try {
            uint32_t val = mem_read32(addr);
            ss << CYAN << "0x" << std::hex << std::setfill('0') << std::setw(8) << val << RESET;
        } catch (...) {
            ss << DIM << "??????????" << RESET;
        }
        ss << std::string(75, ' ') << DIM << "│\033[K\n" << RESET;
    }

    // ─── BOTTOM BORDER ──────────────────────────────────────────────────────────
    ss << DIM << "╰" << repeat_char("─", 104) << "╯\n" << RESET;
    ss << " Controls: [" << BOLD << "Space" << RESET << "] Play/Pause | [" << BOLD << "S" << RESET << "] Step | [" << BOLD << "+/-" << RESET << "] Speed | [" << BOLD << "Q" << RESET << "] Quit \033[K\n";
    ss << "\033[J";

    std::cout << ss.str() << std::flush;
}

std::string CPU::disassemble(uint32_t inst) const {
    if (inst == 0) return "NOP / INVALID";
    
    uint32_t op = opcode(inst);
    
    switch(op) {
        case 0x37: return "LUI";
        case 0x17: return "AUIPC";
        case 0x6F: return "JAL";
        case 0x67: return "JALR";
        case 0x63: return "BRANCH";
        case 0x03: return "LOAD";
        case 0x23: return "STORE";
        case 0x13: return "OP-IMM";
        case 0x33: return "OP";
        case 0x73: return "SYSTEM / EBREAK";
        default:   return "UNKNOWN";
    }
}

} // namespace stakrv