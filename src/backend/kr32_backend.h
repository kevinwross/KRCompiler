#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "backend/peephole.h"
#include "ir/ir.h"
#include "krc/target/target.h"

namespace krc::backend {

class Kr32Backend {
public:
    // The level defaults to `-O0`, which is the output a build without the flag
    // produces: no caller has to pass it to mean "the default".
    Kr32Backend(const ir::Module& module, const target::TargetProfile& profile,
                OptimizationLevel level = OptimizationLevel::O0);

    [[nodiscard]] std::optional<std::string> emit(std::string& error) const;
    [[nodiscard]] const std::string& debug_info() const { return debug_info_; }

    // The frame this function needs, in bytes. Public so `--dump-stats` can report
    // the metric S4 is measured by (docs/optimize.md §7) without emitting anything.
    [[nodiscard]] std::uint32_t frame_bytes(const ir::Function& function) const;

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

    const ir::Module& module_;
    const target::TargetProfile& profile_;
    OptimizationLevel level_ = OptimizationLevel::O0;
    mutable std::uint32_t next_debug_source_ = 0;
    mutable std::unordered_map<std::string, std::uint32_t> debug_sources_;
    mutable std::unordered_set<std::string> debug_types_;
    mutable std::string debug_info_;
};

}  // namespace krc::backend
