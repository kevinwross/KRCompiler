#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <map>
#include <string_view>
#include <unordered_set>

#include "backend/machine_ir.h"
#include <vector>

#include "backend/peephole.h"
#include "ir/ir.h"
#include "krc/target/target.h"

namespace krc::backend {

class Kr32Backend {
public:
    // The constructor's default is `-O0` so a direct caller can request the
    // unoptimised path without naming it. `krc` passes the level explicitly, and
    // its own default is `-O1` (ADR 0060).
    Kr32Backend(const ir::Module& module, const target::TargetProfile& profile,
                OptimizationLevel level = OptimizationLevel::O0);

    [[nodiscard]] std::optional<std::string> emit(std::string& error) const;
    [[nodiscard]] const std::string& debug_info() const { return debug_info_; }

    // The frame this function needs, in bytes. Public so `--dump-stats` can report
    // the metric S4 is measured by (docs/optimize.md §7) without emitting anything.
    [[nodiscard]] std::uint32_t frame_bytes(const ir::Function& function) const;

    // The machine-level form of the last emission, per function symbol. R1's plan is
    // to move emission onto this representation in place rather than to write a second
    // selection pass beside the emitter, so the recording grows as more emit sites are
    // converted; a function absent from the map has not been converted at all. Exposed
    // for the tests that hold the conversion to byte-identical output.
    [[nodiscard]] const machine::Function* machine_function(std::string_view symbol) const;

private:
    struct FrameLayout {
        std::unordered_map<const ir::Value*, std::uint32_t> slots;
        std::unordered_map<const ir::Instruction*, std::uint32_t> alloca_offsets;
        std::uint32_t outgoing_size = 0;
        std::uint32_t float_temp = 0;
        // Where the incoming memory-class return destination (R4) is saved.
        std::uint32_t return_pointer = 0;
        bool has_return_pointer = false;
        // Eight bytes a landing pad writes the exception object pointer and its
        // typeinfo into (ADR 0026).
        std::uint32_t eh_scratch = 0;
        bool has_eh_scratch = false;
        std::uint32_t frame_size = 0;
    };

    // One call site that can unwind, as the `.cpp_except` descriptor records
    // it: the call's address, the return address it pushes, and the pad the
    // unwinder enters instead of returning.
    struct EhRegion {
        std::string start;
        std::string end;
        std::string pad;
    };

    [[nodiscard]] FrameLayout layout_function(const ir::Function& function) const;
    [[nodiscard]] std::optional<std::uint32_t> value_slot(
        const ir::Function& function, const FrameLayout& layout, const ir::Value* value,
        std::string& error) const;
    [[nodiscard]] std::uint64_t size_of(ir::TypePtr type, std::string& error) const;
    [[nodiscard]] std::uint64_t align_of(ir::TypePtr type) const;
    // True for an aggregate returned through the hidden destination pointer in
    // R4 rather than in registers.
    [[nodiscard]] bool is_memory_class_return(ir::TypePtr type) const;
    void emit_function(std::string& out, const ir::Function& function,
                       const FrameLayout& layout) const;
    // A `phi` has no code of its own: its value arrives on the incoming edge, so
    // the copy is emitted at the end of each predecessor, before its terminator.
    // An edge that is critical at both ends has nowhere to put that copy, so it
    // is refused loudly rather than miscompiled — see `docs/optimize.md` E6,
    // whose pass splits those edges before inserting any `phi`.
    void emit_phi_copies(std::string& out, const ir::Function& function,
                         const FrameLayout& layout, const ir::BasicBlock& block,
                         const ir::Instruction& terminator) const;
    void emit_instruction(std::string& out, const ir::Function& function,
                          const FrameLayout& layout, const ir::Instruction& instruction) const;
    void load_value(std::string& out, const ir::Function& function, const FrameLayout& layout,
                    const ir::Value* value, std::string_view reg) const;
    // Loads the low 32 bits of an integer value, whether or not the value is
    // wider than a word. Index and condition uses need only the low word.
    void load_low_word(std::string& out, const ir::Function& function,
                       const FrameLayout& layout, const ir::Value* value,
                       std::string_view reg) const;
    // Leaves a 64-bit integer in the register pair R0 (low) and R1 (high).
    void load_wide(std::string& out, const ir::Function& function, const FrameLayout& layout,
                   const ir::Value* value) const;
    // Stores the R0/R1 pair into the two words reserved for the instruction.
    void store_wide(std::string& out, const ir::Function& function, const FrameLayout& layout,
                    const ir::Instruction& instruction) const;
    void emit_wide_binary(std::string& out, const ir::Function& function,
                          const FrameLayout& layout, const ir::Instruction& instruction) const;
    void emit_wide_icmp(std::string& out, const ir::Function& function,
                        const FrameLayout& layout, const ir::Instruction& instruction) const;
    // The 64-bit division helpers this module calls, in declaration order.
    [[nodiscard]] std::vector<std::string> wide_division_helpers() const;
    void load_fp(std::string& out, const ir::Function& function, const FrameLayout& layout,
                 const ir::Value* value, std::string_view reg) const;
    void store_fp(std::string& out, const ir::Function& function, const FrameLayout& layout,
                  const ir::Instruction& instruction, std::string_view reg) const;
    void add_debug_type(ir::TypePtr type) const;
    void store_value(std::string& out, const ir::Function& function,
                     const FrameLayout& layout, const ir::Instruction& instruction,
                     std::string_view reg) const;
    // Lowers an Opcode::Intrinsic to the KR32 tile and transform-stack
    // instructions it names.
    void emit_intrinsic(std::string& out, const ir::Function& function,
                        const FrameLayout& layout, const ir::Instruction& instruction) const;
    [[nodiscard]] std::string block_label(const ir::Function& function,
                                          const ir::BasicBlock& block) const;
    [[nodiscard]] std::string escape_string(const std::vector<std::uint8_t>& bytes) const;
    mutable std::size_t compare_label_counter_ = 0;
    mutable std::size_t wide_label_counter_ = 0;
    // The call sites of the function currently being emitted.
    mutable std::vector<EhRegion> eh_regions_;
    // The comparisons of the function currently being emitted whose only use is
    // a conditional branch, so the branch carries the comparison and the boolean
    // is never materialised.
    mutable std::unordered_set<const ir::Instruction*> folded_conditions_;

    // `-O1` only: the values that live in a register instead of a frame slot.
    // Empty at `-O0`, which is what keeps that level's output byte-identical
    // (ADR 0049). Filled per function by `compute_allocation`.
    mutable std::unordered_map<const ir::Value*, std::uint32_t> allocated_registers_;
    // Machine instructions recorded while emitting, per function symbol. See
    // `machine_function`.
    mutable std::map<std::string, machine::Function> machine_functions_;
    // The function being emitted, so a converted site can record without being told
    // which function it is in.
    mutable machine::Function* recording_ = nullptr;
    // `ADR 0051`: the virtual register each IR value lives in, and the next one to
    // hand out. Cleared per function so numbering is stable within a function.
    mutable std::unordered_map<const ir::Value*, int> virtual_registers_;
    mutable int next_virtual_register_ = 0;

    // Prints `instruction` and records it against the function being emitted, so that
    // the text and the machine form cannot disagree: one call site produces both.
    void emit_machine(std::string& out, const machine::Instruction& instruction) const;

    // `MOV <destination>, <source>`, and `LEA <destination>, [<symbol>]`. Both are
    // frequent enough in `load_value` to be worth naming rather than spelling out at
    // each site.
    void emit_move(std::string& out, const machine::Operand& destination,
                   const machine::Operand& source) const;
    void emit_symbol_address(std::string& out, const machine::Operand& destination,
                             const std::string& symbol) const;
    // The shapes the control-flow and arithmetic sites emit: a compare, a branch to a
    // label, a two-operand ALU instruction, and a call through a symbol.
    void emit_compare(std::string& out, const machine::Operand& left,
                      const machine::Operand& right) const;
    void emit_jump(std::string& out, machine::Opcode opcode, const std::string& label) const;
    void emit_binary(std::string& out, machine::Opcode opcode,
                     const machine::Operand& destination, const machine::Operand& left,
                     const machine::Operand& right) const;
    void emit_call(std::string& out, const std::string& symbol) const;
    void emit_store(std::string& out, const machine::Operand& source,
                    const machine::Operand& address) const;
    // A store whose mnemonic is chosen by the stored type (`STORE` or `STORE8`).
    void emit_store_opcode(std::string& out, std::string_view mnemonic,
                           const machine::Operand& source,
                           const machine::Operand& address) const;
    // A load whose mnemonic is chosen by the loaded type (`LOAD`, `LOAD8S`, ...).
    void emit_load_opcode(std::string& out, std::string_view mnemonic,
                          const machine::Operand& destination,
                          const machine::Operand& address) const;
    // A zero double: two integer zeroes into the float scratch area, then an
    // `FLOAD` of it. `single` selects `FLOAD.W` over `FLOAD.D`.
    void emit_zero_float(std::string& out, const FrameLayout& layout, std::string_view reg,
                         bool single) const;
    // A two-operand float conversion, whose result register differs from its source.
    void emit_convert(std::string& out, machine::Opcode opcode,
                      const machine::Operand& destination,
                      const machine::Operand& source) const;
    // `LEA <destination>, <address>`: a frame slot or a symbol.
    void emit_lea(std::string& out, const machine::Operand& destination,
                  const machine::Operand& address) const;

    // The virtual register for an IR value, assigned on first sight (ADR 0051).
    int vreg_for(const ir::Value* value) const;
    // Annotate the instruction just recorded with the value it defines or reads.
    // Separate from `emit_machine` so that a call site says what it means: these
    // modify the recording only, never the text.
    void annotate_destination(const ir::Value* value) const;
    void annotate_source(const ir::Value* value) const;
    // Runs the register allocator over one function and keeps only the values it
    // is safe to honour: an allocator that mis-handles liveness produces wrong
    // values rather than slow ones, so this stage accepts a value only when its
    // definition and every use are in one block with no call between them — the
    // case where no cross-block or call-clobber reasoning is needed at all.
    void compute_allocation(const ir::Function& function) const;
    // The register an instruction's result belongs in: its allocated register at
    // `-O1`, `R0` otherwise. An operation that writes it directly saves the move
    // that would otherwise carry the result out of `R0`, which is the whole
    // reason allocation is worth wiring.
    [[nodiscard]] std::string result_register(const ir::Instruction& instruction) const;

    // Adds an L record for `label`, registering its file in the source table on
    // first use. The function entry and every statement use the same path.
    void add_debug_line(const std::string& label, const ir::DebugLocation& location) const;

    const ir::Module& module_;
    const target::TargetProfile& profile_;
    OptimizationLevel level_ = OptimizationLevel::O0;
    mutable std::uint32_t next_debug_source_ = 0;
    mutable std::unordered_map<std::string, std::uint32_t> debug_sources_;
    mutable std::unordered_set<std::string> debug_types_;
    mutable std::string debug_info_;
};

}  // namespace krc::backend
