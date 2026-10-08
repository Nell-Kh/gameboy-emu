#include <iostream>

#include "core/gameboy.h"

int main() {
    std::cout << "gameboy-emu " << core::GameBoy::version() << '\n';
    return 0;
}
