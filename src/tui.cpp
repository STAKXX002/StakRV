#include "tui.hpp"
#include "disassembler.hpp"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <cstring>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/ioctl.h>

namespace stakrv {

// ── Layout constants ──────────────────────────────────────────────────────────
//
//  Total visible width  : 106 columns
//  Left panel (regs)    : 62 inner chars  (columns 2–63)
//  Centre divider       : column 64
//  Right panel (pipe)   : 41 inner chars  (columns 65–105)
//  Right border         : column 106
//
//  Register cell = 29 visible chars:
//    name(4) + " x"(2) + idx(2) + " "(1) + "0x"(2) + hex(8) + hint(10) = 29
//  Row: 1(lead) + 29 + 2(gap) + 29 + 1(trail) = 62 = LEFT_INNER ✓
//
//  Pipeline cell = 41 visible chars:
//    prefix(4) + addr(10) + "  "(2) + mnem(16) + pad(9) = 41 ✓

static constexpr int TOTAL_W     = 106;
static constexpr int LEFT_INNER  = 62;
static constexpr int RIGHT_INNER = 41;
static constexpr int MIN_ROWS    = 28;
static constexpr int FLASH_FRAMES = 6;   // frames a changed register stays bright

// ── ANSI palette ─────────────────────────────────────────────────────────────
static constexpr const char* DIM     = "\033[2m";
static constexpr const char* RESET   = "\033[0m";
static constexpr const char* BOLD    = "\033[1m";
static constexpr const char* GREEN   = "\033[32m";
static constexpr const char* CYAN    = "\033[36m";
static constexpr const char* YELLOW  = "\033[33m";
static constexpr const char* MAGENTA = "\033[35m";
static constexpr const char* WHITE   = "\033[97m";   // bright white — flash colour
static constexpr const char* RED     = "\033[31m";

static constexpr const char* ABI_NAMES[32] = {
    "zero","ra", "sp", "gp", "tp", "t0", "t1", "t2",
    "s0",  "s1", "a0", "a1", "a2", "a3", "a4", "a5",
    "a6",  "a7", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8",  "s9", "s10","s11","t3", "t4", "t5", "t6"
};

// ── TUI ───────────────────────────────────────────────────────────────────────

TUI::TUI(CPU& cpu) : cpu_(cpu) {
    prev_regs_.fill(0);
    flash_ttl_.fill(0);
    // Snapshot initial SP so it doesn't flash on first frame
    prev_regs_[2] = cpu_.reg(2);
}

TUI::~TUI() { term_leave_raw(); }

// ── terminal helpers ──────────────────────────────────────────────────────────

static struct termios s_saved_termios;
static bool           s_raw_active = false;

void TUI::term_enter_raw() {
    tcgetattr(STDIN_FILENO, &s_saved_termios);
    struct termios t = s_saved_termios;
    t.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
    s_raw_active = true;
}

void TUI::term_leave_raw() {
    if (!s_raw_active) return;
    tcsetattr(STDIN_FILENO, TCSANOW, &s_saved_termios);
    fcntl(STDIN_FILENO, F_SETFL, 0);
    s_raw_active = false;
}

void TUI::term_get_size(int& rows, int& cols) const {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    rows = w.ws_row;
    cols = w.ws_col;
}

int TUI::term_read_char() const {
    char ch;
    return (read(STDIN_FILENO, &ch, 1) > 0) ? (unsigned char)ch : -1;
}

// ── run loop ──────────────────────────────────────────────────────────────────

void TUI::run() {
    bool running  = true;
    bool paused   = true;
    int  delay_ms = 100;

    term_enter_raw();
    std::cout << "\033[?1049h\033[?25l\033[2J" << std::flush;

    auto last_step = std::chrono::steady_clock::now();

    while (running) {
        // 1. drain input
        int ch;
        while ((ch = term_read_char()) != -1) {
            if (ch == 'q' || ch == 27) {
                running = false;
            } else if (ch == 'p' || ch == ' ') {
                paused = !paused;
                if (!paused) last_step = std::chrono::steady_clock::now();
            } else if (ch == 's' && paused) {
                // Snapshot before step so we can detect changes
                for (int i = 0; i < 32; ++i) prev_regs_[i] = cpu_.reg(i);
                try { cpu_.step(); ++cycle_count_; } catch (...) {}
                // Detect which registers changed and arm their flash
                last_rd_ = -1;
                for (int i = 1; i < 32; ++i) {
                    if (cpu_.reg(i) != prev_regs_[i]) {
                        flash_ttl_[i] = FLASH_FRAMES;
                        last_rd_ = i;
                    }
                }
            } else if (ch == '+' || ch == '=') {
                delay_ms += 100;
            } else if (ch == '-') {
                if (delay_ms > 100) delay_ms -= 100;
            }
        }

        // 2. timed CPU step
        if (!paused) {
            auto now     = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                               now - last_step).count();
            if (elapsed >= delay_ms) {
                for (int i = 0; i < 32; ++i) prev_regs_[i] = cpu_.reg(i);
                try {
                    cpu_.step();
                    ++cycle_count_;
                } catch (...) {
                    paused = true;
                }
                last_rd_ = -1;
                for (int i = 1; i < 32; ++i) {
                    if (cpu_.reg(i) != prev_regs_[i]) {
                        flash_ttl_[i] = FLASH_FRAMES;
                        last_rd_ = i;
                    }
                }
                last_step = std::chrono::steady_clock::now();
            }
        }

        // 3. tick down flash counters every frame
        for (int i = 0; i < 32; ++i)
            if (flash_ttl_[i] > 0) --flash_ttl_[i];

        // 4. render
        render(paused, delay_ms);

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    std::cout << "\033[?25h\033[?1049l" << std::flush;
    term_leave_raw();
}

// ── rendering helpers ─────────────────────────────────────────────────────────

static std::string repeat(const std::string& s, int n) {
    std::string r; r.reserve(s.size() * n);
    for (int i = 0; i < n; ++i) r += s;
    return r;
}

// Returns the mnemonic colour based on instruction category.
static const char* mnem_color(uint32_t inst) {
    uint32_t op = inst & 0x7F;
    switch (op) {
        case 0x6F: case 0x67:        return MAGENTA; // JAL, JALR
        case 0x63:                   return YELLOW;  // branches
        case 0x03: case 0x23:        return CYAN;    // loads / stores
        case 0x13: case 0x33:        return GREEN;   // ALU
        case 0x37: case 0x17:        return GREEN;   // LUI, AUIPC
        case 0x73:                   return RED;     // ECALL/EBREAK
        default:                     return DIM;
    }
}

// Format a 29-char register cell.
// Layout: name(4) + " x"(2) + idx(2) + " "(1) + "0x"(2) + hex(8) + hint(10) = 29
static std::string format_reg_cell(uint32_t idx, uint32_t val,
                                   int flash_ttl, bool is_last_rd) {
    const bool is_zero = (val == 0);
    const bool is_a    = (idx >= 10 && idx <= 17);
    const bool is_s    = (idx == 8 || idx == 9 || (idx >= 18 && idx <= 27));
    const bool is_sp   = (idx == 2);
    const bool is_ra   = (idx == 1);

    // Base colour by register class
    const char* color;
    if      (is_a)                   color = CYAN;
    else if (is_s || is_sp || is_ra) color = YELLOW;
    else                             color = GREEN;
    if (is_zero && !is_sp)           color = DIM;

    // Flash override — bright white while TTL > 0
    if (flash_ttl > 0) color = WHITE;

    std::ostringstream o;

    // Name (4 chars)
    o << color << std::setw(4) << std::right << std::setfill(' ')
      << ABI_NAMES[idx] << RESET;

    // Index " x##" (4 chars)
    o << DIM << " x" << std::setw(2) << std::setfill('0') << idx << RESET;

    // Hex value " 0xHHHHHHHH" (11 chars)
    o << " ";
    if (!is_zero || is_sp)
        o << color << "0x" << std::hex << std::setw(8) << std::setfill('0') << val << RESET;
    else
        o << DIM << "0x00000000" << RESET;

    // Decimal hint (10 chars, always).
    // Show for small values; show "◀" marker on last-written register instead
    // when the value is large (so the column never goes wide).
    if (is_last_rd && flash_ttl > 0) {
        // "  <<      " — 2 ASCII chars + 8 spaces = 10
        o << WHITE << "  <<" << RESET << std::string(6, ' ');
    } else if (!is_zero && !is_sp && !is_ra && val < 0x10000) {
        char hint[12];
        std::snprintf(hint, sizeof(hint), "(%d)", (int32_t)val);
        o << DIM << " " << std::setw(9) << std::left << std::setfill(' ')
          << hint << std::right << RESET;
    } else {
        o << std::string(10, ' ');
    }

    return o.str();
}

// Format a 41-char pipeline cell with mnemonic colouring.
static std::string format_pipe_cell(uint32_t addr, uint32_t inst, bool arrow) {
    std::string mnem = disassemble(inst);
    if ((int)mnem.size() < 16) mnem.append(16 - mnem.size(), ' ');
    else                       mnem = mnem.substr(0, 16);

    char addr_buf[12];
    std::snprintf(addr_buf, sizeof(addr_buf), "0x%08x", addr);

    std::ostringstream o;
    if (arrow) {
        // Current instruction: bright address, category-coloured mnemonic
        o << BOLD << WHITE << " >> " << RESET
          << BOLD << CYAN  << addr_buf << RESET
          << "  "
          << BOLD << mnem_color(inst) << mnem << RESET
          << std::string(9, ' ');
    } else {
        // Lookahead: dim address, softer mnemonic colour
        o << DIM << "    " << addr_buf << RESET
          << "  "
          << mnem_color(inst) << DIM << mnem << RESET
          << std::string(9, ' ');
    }
    return o.str();
}

// ── render_too_small ──────────────────────────────────────────────────────────

void TUI::render_too_small(int rows, int cols) const {
    std::cout << "\033[H\033[2J"
              << "\033[1;31m[!] Terminal too small.\033[0m\n\n"
              << "Required : " << TOTAL_W << " cols x " << MIN_ROWS << " rows\n"
              << "Current  : " << cols    << " cols x " << rows     << " rows\n\n"
              << "Expand the terminal to continue.\n"
              << "(Press Q to quit)\033[J\n" << std::flush;
}

// ── render ────────────────────────────────────────────────────────────────────

void TUI::render(bool paused, int delay_ms) const {
    int rows, cols;
    term_get_size(rows, cols);
    if (cols < TOTAL_W || rows < MIN_ROWS) { render_too_small(rows, cols); return; }

    std::ostringstream ss;
    ss << "\033[H";

    /// ── top border ────────────────────────────────────────────────────────────
    // Emit the entire border in one DIM context, toggling colour only for the
    // bracket contents — never RESET between box-drawing ─ characters.
    // Visible: ╭─[ STAKRV rv32i ]─[ v0.1 ]────…────╮  = TOTAL_W cols
    // Plain:    1 1  16           1  8              1  = 106
    //   title_plain = "─[ STAKRV rv32i ]─[ v0.1 ]" = 26 chars
    //   trail = TOTAL_W - 2 - 26 = 78
    {
        const int trail = TOTAL_W - 2 - 26; // "─[ STAKRV rv32i ]─[ v0.1 ]" = 26 visible chars
        // Never RESET between ─ characters — keep the whole line in DIM context,
        // briefly switching to colour only for the bracket *contents*, then back to DIM.
        ss << DIM << "╭─["
           << RESET << BOLD << CYAN << " STAKRV rv32i " << RESET
           << DIM << "]─["
           << RESET << " v0.1 "
           << DIM << "]" << repeat("─", trail) << "╮" << RESET << "\n";
    }

    // ── status line ──────────────────────────────────────────────────────────
    // ASCII-only icons so every character is exactly 1 terminal column wide.
    // Visible layout: "  || PAUSED  | CYC: 0000000054 | PC: 0x8000002c | 100ms  "
    const char* stat_label = paused ? "PAUSED " : "RUNNING";
    const char* stat_color = paused ? "\033[1;33m" : "\033[1;32m";
    const char* stat_icon  = paused ? "||" : ">>";   // 2 ASCII chars, always

    char pc_buf[12], cyc_buf[20];
    std::snprintf(pc_buf,  sizeof(pc_buf),  "0x%08x", cpu_.pc());
    std::snprintf(cyc_buf, sizeof(cyc_buf), "%010llu", (unsigned long long)cycle_count_);

    // Measure visible length with plain ASCII only — no ANSI, no unicode.
    char status_plain[80];
    std::snprintf(status_plain, sizeof(status_plain),
                  "  %s %s | CYC: %s | PC: %s | %dms",
                  stat_icon, stat_label, cyc_buf, pc_buf, delay_ms);
    int slen = (int)std::strlen(status_plain);

    ss << DIM << "│" << RESET
       << "  " << stat_color << BOLD << stat_icon << " " << stat_label << RESET
       << DIM << " | " << RESET
       << "CYC: " << CYAN << cyc_buf << RESET
       << DIM << " | " << RESET
       << "PC: " << BOLD << CYAN << pc_buf << RESET
       << DIM << " | " << RESET
       << BOLD << std::dec << delay_ms << "ms" << RESET
       << std::string(TOTAL_W - 2 - slen, ' ')
       << DIM << "│\n" << RESET;

    // ── section header row ────────────────────────────────────────────────────
    ss << DIM << "├" << repeat("─", LEFT_INNER) << "┬" << repeat("─", RIGHT_INNER) << "┤\n" << RESET;
    {
        const std::string lhdr = "  REGISTERS";
        const std::string rhdr = "  INSTRUCTION PIPELINE";
        ss << DIM << "│" << RESET << BOLD << lhdr << RESET
           << std::string(LEFT_INNER  - (int)lhdr.size(), ' ')
           << DIM << "│" << RESET << BOLD << rhdr << RESET
           << std::string(RIGHT_INNER - (int)rhdr.size(), ' ')
           << DIM << "│\n" << RESET;
    }

    // ── register rows + pipeline ──────────────────────────────────────────────
    uint32_t cur_pc   = cpu_.pc();
    uint32_t cur_inst = 0;
    cpu_.peek32(cur_pc, cur_inst);

    for (int row = 0; row < 16; ++row) {
        int lo = row;
        int hi = row + 16;

        // Left panel: 1 + 29 + 2 + 29 + 1 = 62 = LEFT_INNER
        ss << DIM << "│" << RESET
           << " "
           << format_reg_cell(lo, cpu_.reg(lo), flash_ttl_[lo], last_rd_ == lo)
           << "  "
           << format_reg_cell(hi, cpu_.reg(hi), flash_ttl_[hi], last_rd_ == hi)
           << " "
           << DIM << "│" << RESET;

        // Right panel
        if (row == 0) {
            ss << format_pipe_cell(cur_pc, cur_inst, true);
        } else if (row < 10) {
            uint32_t npc = cur_pc + row * 4, ni = 0;
            if (cpu_.peek32(npc, ni)) ss << format_pipe_cell(npc, ni, false);
            else                      ss << std::string(RIGHT_INNER, ' ');
        } else {
            ss << std::string(RIGHT_INNER, ' ');
        }

        ss << DIM << "│\033[K\n" << RESET;
    }

    // ── stack peek ────────────────────────────────────────────────────────────
    ss << DIM << "├" << repeat("─", LEFT_INNER) << "┴" << repeat("─", RIGHT_INNER) << "┤\n" << RESET;
    {
        const std::string shdr = "  STACK";
        ss << DIM << "│" << RESET << BOLD << shdr << RESET
           << std::string(TOTAL_W - 2 - (int)shdr.size(), ' ')
           << DIM << "│\n" << RESET;
    }

    uint32_t sp = cpu_.reg(2);
    for (int i = 0; i < 4; ++i) {
        uint32_t addr = sp + (i * 4);
        char abuf[12];
        std::snprintf(abuf, sizeof(abuf), "0x%08x", addr);

        ss << DIM << "│    " << RESET
           << (i == 0 ? YELLOW : "") << abuf << (i == 0 ? RESET : "")
           << DIM << "  │  " << RESET;

        uint32_t val = 0;
        if (cpu_.peek32(addr, val)) {
            ss << (i == 0 ? CYAN : "") << DIM;
            ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << val;
            ss << RESET;
        } else {
            ss << DIM << "??????????" << RESET;
        }

        // Inner: 4 + 10 + 2 + 1 + 2 + 10 = 29
        constexpr int STACK_ROW_USED = 4 + 10 + 2 + 1 + 2 + 10;
        ss << std::string(TOTAL_W - 2 - STACK_ROW_USED, ' ')
           << DIM << "│\033[K\n" << RESET;
    }

    // ── bottom border + controls ──────────────────────────────────────────────
    ss << DIM << "╰" << repeat("─", TOTAL_W - 2) << "╯\n" << RESET;
    ss << DIM << "  "
       << "[" << RESET << BOLD << "Space" << RESET << DIM << "] run/pause  "
       << "[" << RESET << BOLD << "S"     << RESET << DIM << "] step  "
       << "[" << RESET << BOLD << "+/-"   << RESET << DIM << "] speed  "
       << "[" << RESET << BOLD << "Q"     << RESET << DIM << "] quit"
       << RESET << "\033[K\n";
    ss << "\033[J";

    std::cout << ss.str() << std::flush;
}

} // namespace stakrv