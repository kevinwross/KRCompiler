// The machine-IR printer (R1, `docs/optimize.md` §7).
//
// The printer is what makes the representation verifiable: lowering is only
// trustworthy once printing a lowered function reproduces the backend's current
// text exactly, so the printer's contract is the assembler's syntax and these tests
// pin it against real lines taken from the backend's output.

#include "doctest.h"

#include <string>

#include "backend/machine_ir.h"

using namespace krc::backend::machine;

TEST_CASE("A machine instruction prints in the assembler's syntax") {
    Instruction store;
    store.opcode = Opcode::Store;
    store.operands = {Operand::physical(0), Operand::frame_offset(12)};
    CHECK(print_instruction(store) == "        STORE R0, [R7 + 12]\n");

    Instruction load;
    load.opcode = Opcode::Load;
    load.operands = {Operand::physical(1), Operand::frame_offset(68)};
    CHECK(print_instruction(load) == "        LOAD R1, [R7 + 68]\n");

    // A negative displacement keeps the sign inside the bracket, which is how the
    // backend prints the outgoing-argument area below the frame base.
    Instruction below;
    below.opcode = Opcode::Load;
    below.operands = {Operand::physical(0), Operand::frame_offset(-4)};
    CHECK(print_instruction(below) == "        LOAD R0, [R7 - 4]\n");
}

TEST_CASE("Immediates, symbols and comments print as the backend prints them") {
    Instruction adjust;
    adjust.opcode = Opcode::AdjustStack;
    adjust.operands = {Operand::immediate(-104)};
    CHECK(print_instruction(adjust) == "        ADJSP #-104\n");

    Instruction add;
    add.opcode = Opcode::Add;
    add.operands = {Operand::physical(0), Operand::physical(0), Operand::immediate(4)};
    CHECK(print_instruction(add) == "        ADD R0, R0, #4\n");

    Instruction call;
    call.opcode = Opcode::Call;
    call.operands = {Operand::named("memcpy")};
    CHECK(print_instruction(call) == "        CALL memcpy\n");

    Instruction commented;
    commented.opcode = Opcode::Halt;
    commented.comment = "unsupported";
    CHECK(print_instruction(commented) == "        HALT ; unsupported\n");
}

TEST_CASE("A function prints its symbol, labels and instructions in order") {
    Function function;
    function.symbol = "sum5";
    Block entry;
    entry.label = ".Lsum5.entry";
    Instruction push;
    push.opcode = Opcode::Push;
    push.operands = {Operand::physical(7)};
    entry.instructions.push_back(push);
    Instruction adjust;
    adjust.opcode = Opcode::AdjustStack;
    adjust.operands = {Operand::immediate(-104)};
    entry.instructions.push_back(adjust);
    function.blocks.push_back(entry);

    const auto text = print_function(function);
    CHECK(text ==
          "sum5:\n"
          ".Lsum5.entry:\n"
          "        PUSH R7\n"
          "        ADJSP #-104\n");
}

TEST_CASE("A virtual register prints distinctly from a physical one") {
    // Before allocation the operand is a virtual register; the printer must keep it
    // distinguishable so that a lowered function and an allocated one cannot be
    // confused when compared as text.
    Instruction move;
    move.opcode = Opcode::Mov;
    move.operands = {Operand::physical(0), Operand::virtual_register(3)};
    CHECK(print_instruction(move) == "        MOV R0, %v3\n");
}
