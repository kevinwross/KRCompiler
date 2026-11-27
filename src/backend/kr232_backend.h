#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>

#include "backend/peephole.h"
#include "ir/ir.h"
#include "krc/target/target.h"

namespace krc::backend {

// The kr232 code generator. This first step reaches the backend and emits a
// prologue and epilogue for every function, so the dispatch path from the CLI to
// an object file exists and can be tested before instruction selection lands.
class Kr232Backend {
public:
    Kr232Backend(const ir::Module& module, const target::TargetProfile& profile,
                 OptimizationLevel level = OptimizationLevel::O0);

    // Emits the assembly text for the module, or the reason it could not.
    [[nodiscard]] std::optional<std::string> emit(std::string& error) const;

    // The textual `.debug_info` bridge for the functions this backend emitted.
    [[nodiscard]] const std::string& debug_info() const { return debug_info_; }

    // The frame size the ABI lays out for a function: locals plus alignment.
    [[nodiscard]] std::uint32_t frame_bytes(const ir::Function& function) const;

private:
    // Appends the KDBG type directives for a type and everything it references.
    void add_debug_type(ir::TypePtr type) const;

    const ir::Module& module_;
    OptimizationLevel level_;
    mutable std::string debug_info_;
    mutable std::string debug_directives_;
    mutable std::unordered_set<std::string> debug_types_;
};

}  // namespace krc::backend
