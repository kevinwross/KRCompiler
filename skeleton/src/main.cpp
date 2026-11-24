#include "krNN/machine.h"

#include <cstdio>

int main() {
    krNN::Machine machine;
    machine.reset();
    machine.step();
    std::printf("krNN: pc=0x%llX\n",
                static_cast<unsigned long long>(machine.program_counter()));
    return 0;
}
