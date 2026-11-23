#include "kr232_backend.h"

#include <sstream>

namespace krc::backend {
namespace {

// The ABI's frame is a multiple of the 4-byte stack alignment.
std::uint32_t align_frame(std::uint32_t bytes) { return (bytes + 3u) & ~3u; }

// kr232 sizes: a pointer is 32 bits, an integer or float is `bits` wide, an array
// is its element times its count, a record is its fields laid out at 4-byte
// alignment. `type_size` is private to each backend, so this backend owns its own.
std::uint32_t type_bytes(const ir::Type* type) {
    if (type == nullptr) {
        return 0;
    }
    switch (type->kind) {
        case ir::TypeKind::Void:
            return 0;
        case ir::TypeKind::Pointer:
            return 4;
        case ir::TypeKind::Integer:
        case ir::TypeKind::Float:
            return (type->bits + 7u) / 8u;
        case ir::TypeKind::Array:
            return type_bytes(type->element) * static_cast<std::uint32_t>(type->count);
        case ir::TypeKind::Struct:
        case ir::TypeKind::Union: {
            std::uint32_t bytes = 0;
            for (const ir::Type* field : type->fields) {
                bytes += align_frame(type_bytes(field));
            }
            return bytes;
        }
        case ir::TypeKind::Function:
            return 0;
    }
    return 0;
}

}  // namespace

Kr232Backend::Kr232Backend(const ir::Module& module, const target::TargetProfile& profile,
                           OptimizationLevel level)
    : module_(module), profile_(profile), level_(level) {}

std::uint32_t Kr232Backend::frame_bytes(const ir::Function& function) const {
    // Every local the IR allocates takes its own size in the frame; the frame
    // itself stays a multiple of four.
    std::uint32_t bytes = 0;
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block->instructions) {
            if (instruction->opcode == ir::Opcode::Alloca) {
                bytes += align_frame(type_bytes(instruction->allocated_type));
            }
        }
    }
    return align_frame(bytes);
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
