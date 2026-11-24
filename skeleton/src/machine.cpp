#include "krNN/machine.h"

namespace krNN {

// A placeholder body so a tree made from this skeleton builds and runs one row.
// The real machine replaces it; the guide's steps 1-4 are what fills it in.
void Machine::reset() { pc_ = 0; }

void Machine::step() { pc_ += 4; }

}  // namespace krNN
