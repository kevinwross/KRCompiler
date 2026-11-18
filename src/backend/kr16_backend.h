#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "ir/ir.h"
#include "krc/target/target.h"

namespace krc::backend {

// Emits the scalar KR16 assembly subset. Aggregates, globals, and floating
// point are rejected with a diagnostic until their ABI is defined.
class Kr16Backend {
  public:
    Kr16Backend(const ir::Module& module, const target::TargetProfile& profile);

    [[nodiscard]] std::optional<std::string> emit(std::string& error) const;

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

    const ir::Module& module_;
    const target::TargetProfile& profile_;
    // The comparisons of the function currently being emitted whose only use is
    // a conditional branch, so the branch carries the comparison and the boolean
    // is never materialised.
    mutable std::unordered_set<const ir::Instruction*> folded_conditions_;
};

}  // namespace krc::backend
