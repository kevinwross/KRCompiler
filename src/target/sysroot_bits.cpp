#include "krc/target/sysroot_bits.h"

#include <sstream>
#include <string>

namespace krc::target {

namespace {

std::string size_type_spelling(const TargetProfile& profile) {
    return profile.pointer_bits == 16 ? "unsigned int" : "unsigned long";
}

std::string ptrdiff_type_spelling(const TargetProfile& profile) {
    return profile.pointer_bits == 16 ? "int" : "long";
}

std::string int16_spelling(const TargetProfile& profile) {
    return profile.int_bits <= 16 ? "int" : "short";
}

std::string uint16_spelling(const TargetProfile& profile) {
    return profile.int_bits <= 16 ? "unsigned int" : "unsigned short";
}

std::string int32_spelling(const TargetProfile& profile) {
    return profile.int_bits == 32 ? "int" : "long";
}

std::string uint32_spelling(const TargetProfile& profile) {
    return profile.int_bits == 32 ? "unsigned int" : "unsigned long";
}

std::string intptr_spelling(const TargetProfile& profile) {
    return profile.data_model == DataModel::Lp64 ? "long" : "int";
}

std::string uintptr_spelling(const TargetProfile& profile) {
    return profile.data_model == DataModel::Lp64 ? "unsigned long" : "unsigned int";
}

std::string intmax_spelling(const TargetProfile& profile) {
    if (profile.intmax_bits <= 16) {
        return "int";
    }
    if (profile.intmax_bits <= 32) {
        return "long";
    }
    return "long long";
}

std::string uintmax_spelling(const TargetProfile& profile) {
    if (profile.intmax_bits <= 16) {
        return "unsigned int";
    }
    if (profile.intmax_bits <= 32) {
        return "unsigned long";
    }
    return "unsigned long long";
}

}  // namespace

std::string generate_bits_types(const TargetProfile& profile) {
    std::ostringstream out;
    out << "#ifndef KRC_BITS_TYPES_H\n"
        << "#define KRC_BITS_TYPES_H\n"
        << "\n"
        << "typedef " << size_type_spelling(profile) << " size_t;\n"
        << "typedef " << ptrdiff_type_spelling(profile) << " ptrdiff_t;\n"
        << "typedef int wchar_t;\n"
        << "\n"
        << "#endif\n";
    return out.str();
}

std::string generate_bits_stdint(const TargetProfile& profile) {
    std::ostringstream out;
    out << "#ifndef KRC_BITS_STDINT_H\n"
        << "#define KRC_BITS_STDINT_H\n"
        << "\n"
        << "typedef signed char int8_t;\n"
        << "typedef unsigned char uint8_t;\n"
        << "typedef " << int16_spelling(profile) << " int16_t;\n"
        << "typedef " << uint16_spelling(profile) << " uint16_t;\n";
    if (profile.int_bits >= 32 || profile.long_bits >= 32) {
        out << "typedef " << int32_spelling(profile) << " int32_t;\n"
            << "typedef " << uint32_spelling(profile) << " uint32_t;\n";
    }
    if (profile.long_long_bits >= 64) {
        out << "typedef long long int64_t;\n"
            << "typedef unsigned long long uint64_t;\n";
    }
    out << "\n"
        << "typedef " << intptr_spelling(profile) << " intptr_t;\n"
        << "typedef " << uintptr_spelling(profile) << " uintptr_t;\n"
        << "typedef " << intmax_spelling(profile) << " intmax_t;\n"
        << "typedef " << uintmax_spelling(profile) << " uintmax_t;\n"
        << "\n"
        << "#endif\n";
    return out.str();
}

}  // namespace krc::target
