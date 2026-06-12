#include "cpu.hpp"
#include "tui.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "StakRV v0.1 — RISC-V rv32i emulator\n"
                  << "usage: stakrv <binary>\n";
        return 1;
    }

    stakrv::CPU cpu;
    if (!cpu.load(argv[1])) return 1;

    stakrv::TUI tui(cpu);
    tui.run();

    return 0;
}