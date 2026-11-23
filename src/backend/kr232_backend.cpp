#include "kr232_backend.h"

#include <sstream>

namespace krc::backend {
namespace {

// The ABI's frame is a multiple of the 4-byte stack alignment.
std::uint32_t align_frame(std::uint32_t bytes) { return (bytes + 3u) & ~3u; }

}  // namespace

Kr232Backend::Kr232Backend(const ir::Module& module, const target::TargetProfile& profile,
                           OptimizationLevel level)
    : module_(module), profile_(profile), level_(level) {}

std::uint32_t Kr232Backend::frame_bytes(const ir::Function& function) const {
    (void)function;
    return 0;
}

std::optional<std::string> Kr232Backend::emit(std::string& error) const {
    (void)error;
    debug_info_.clear();
    std::ostringstream out;
    out << "; kr232 backend, optimization level " << static_cast<int>(level_) << "\n";
    for (const auto& function : module_.functions()) {
        const std::uint32_t frame = align_frame(frame_bytes(*function));
        const std::string name = function->name;
        out << ".globl " << name << "\n";
        out << ".text\n";
        out << name << ":\n";
        // The prologue the ABI fixes: ENTER saves FP, pushes the mask and reserves
        // the frame. No stack setup is needed because reset SP is 0x08000000.
        out << "  enter #" << frame << ", #0\n";
        // The epilogue mirrors it; instruction selection fills the body.
        out << "  leave #0\n";
        out << "  ret\n";
    }
    return out.str();
}

}  // namespace krc::backend
