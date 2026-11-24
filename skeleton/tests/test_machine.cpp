// The skeleton's placeholder row. It exists so a tree made from this skeleton has a
// test to run before it has a machine to test, and so the guest-tests option in the
// presets means something the first time it is turned on.
#include "krNN/machine.h"

int main() {
    krNN::Machine machine;
    machine.reset();
    machine.step();
    return machine.program_counter() == 4 ? 0 : 1;
}
