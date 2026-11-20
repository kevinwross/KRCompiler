#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "ir/ir.h"
#include "krc/target/target.h"
#include "backend/peephole.h"

namespace krc::backend {

// Emits the scalar KR16 assembly subset. Aggregates, globals, and floating
// point are rejected with a diagnostic until their ABI is defined.
class Kr16Backend {
  public:
    // The level defaults to `-O0`, which is the output a build without the flag
    // produces: no caller has to pass it to mean "the default".
    Kr16Backend(const ir::Module& module, const target::TargetProfile& profile,
                OptimizationLevel level = OptimizationLevel::O0);

    [[nodiscard]] std::optional<std::string> emit(std::string& error) const;
    // The textual `.debug_info` bridge for the functions this backend emitted.
    // Same record format as Kr32Backend's; `attach_kdbg` converts it while
    // linking, and a `.kex` carries the result as `.kdbg`.
    [[nodiscard]] const std::string& debug_info() const { return debug_info_; }
    // The frame this function needs, in bytes, for `--dump-stats` (S4's metric).
    [[nodiscard]] std::uint32_t frame_bytes(const ir::Function& function) const;


  private:
    struct FrameLayout {
        std::uint32_t frame_size = 0;
        std::unordered_map<const ir::Value*, std::uint32_t> slots;
        std::unordered_map<const ir::Instruction*, std::uint32_t> allocas;
        // An aggregate return is copied to a caller-owned destination passed as a
        // hidden stack argument (ADR 0048). The callee keeps it here.
        std::uint32_t return_pointer = 0;
        bool has_return_pointer = false;
    };

    [[nodiscard]] FrameLayout layout_function(const ir::Function& function,
                                              std::string& error) const;
    void emit_function(std::string& out, const ir::Function& function,
                       const FrameLayout& layout, std::string& error) const;
    // Adds a T record, and a C record with its M members for a record type, once
    // per type name.
    void add_debug_type(ir::TypePtr type) const;

    const ir::Module& module_;
    const target::TargetProfile& profile_;
    OptimizationLevel level_ = OptimizationLevel::O0;
    // The comparisons of the function currently being emitted whose only use is
    // a conditional branch, so the branch carries the comparison and the boolean
    // is never materialised.
    mutable std::unordered_set<const ir::Instruction*> folded_conditions_;
    // The KDBG bridge being built by the current `emit` call. Mutable because the
    // emitter is const; every field is cleared at the start of `emit`, so two
    // builds in one process cannot observe each other.
    mutable std::string debug_info_;
    mutable std::unordered_map<std::string, std::uint32_t> debug_sources_;
    mutable std::unordered_set<std::string> debug_types_;
    mutable std::uint32_t next_debug_source_ = 0;
};

}  // namespace krc::backend
