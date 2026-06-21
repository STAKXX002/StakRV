#pragma once

#include "cpu.hpp"
#include <cstdint>
#include <array>
#include <string>

namespace stakrv {

class TUI {
public:
    explicit TUI(CPU& cpu);
    ~TUI();

    void run();

private:
    CPU& cpu_;

    // ── per-frame state for visual effects ───────────────────────────────────
    std::array<uint32_t, 32> prev_regs_{};   // register values last frame
    std::array<int,      32> flash_ttl_{};   // frames remaining for flash
    uint64_t                 cycle_count_ = 0;
    int                      last_rd_     = -1; // register written this step
    std::string              halt_msg_;        // reason the last step() threw, if any

    // ── sci-fi additions ─────────────────────────────────────────────────────
    std::array<int, 32>      heat_{};        // per-register heat counter (decays)
    uint32_t                 sp_init_  = 0;  // SP at startup for stack gauge
    uint64_t                 frame_    = 0;  // raw frame counter for animations

    // ── terminal helpers ──────────────────────────────────────────────────────
    void term_enter_raw();
    void term_leave_raw();
    void term_get_size(int& rows, int& cols) const;
    int  term_read_char() const;

    // ── rendering ─────────────────────────────────────────────────────────────
    void render(bool paused, int delay_ms) const;
    void render_too_small(int rows, int cols) const;

    // ── sub-renderers ─────────────────────────────────────────────────────────
    std::string render_heatmap_row(int reg_row) const;  // reg_row 0-3
    std::string render_stack_gauge() const;
    std::string render_pulse(bool paused) const;
};

} // namespace stakrv