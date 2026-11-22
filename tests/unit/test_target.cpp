#include <ostream>

#include "doctest.h"
#include "krc/target/target.h"

using krc::target::DataModel;
using krc::target::TargetId;
using krc::target::get_target;
using krc::target::parse_target;
using krc::target::target_profiles;

TEST_CASE("KR32 profile matches the approved data model") {
    const auto& profile = get_target(TargetId::Kr32);
    CHECK(profile.name == "kr32");
    CHECK(profile.pointer_bits == 32);
    CHECK(profile.data_model == DataModel::Ilp32);
    CHECK(profile.char_bits == 8);
    CHECK_FALSE(profile.char_unsigned);
    CHECK(profile.int_bits == 32);
    CHECK(profile.long_bits == 32);
    CHECK(profile.long_long_bits == 64);
    CHECK(profile.double_bits == 64);
    CHECK(profile.has_fpu);
    CHECK(profile.has_matrix);
    CHECK(profile.has_cpp);
    CHECK(profile.backend_available);
}

TEST_CASE("KR16 profile documents the non-conforming IP16 model") {
    const auto& profile = get_target(TargetId::Kr16);
    CHECK(profile.pointer_bits == 16);
    CHECK(profile.data_model == DataModel::Ip16);
    CHECK(profile.char_unsigned);
    CHECK(profile.int_bits == 16);
    CHECK(profile.long_bits == 16);
    CHECK(profile.long_long_bits == 0);
    CHECK(profile.float_bits == 0);
    CHECK_FALSE(profile.has_cpp);
    CHECK(profile.backend_available);
}

TEST_CASE("KR64 is a placeholder without a backend") {
    const auto& profile = get_target(TargetId::Kr64);
    CHECK(profile.pointer_bits == 64);
    CHECK(profile.data_model == DataModel::Lp64);
    CHECK(profile.long_bits == 64);
    CHECK_FALSE(profile.backend_available);
}

TEST_CASE("KR216 profile matches the approved data model") {
    const auto& profile = get_target(TargetId::Kr216);
    CHECK(profile.name == "kr216");
    CHECK(profile.pointer_bits == 16);
    CHECK(profile.data_model == DataModel::Ip16Long32);
    CHECK_FALSE(profile.char_unsigned);
    CHECK(profile.short_bits == 16);
    CHECK(profile.int_bits == 16);
    CHECK(profile.long_bits == 32);
    CHECK(profile.long_long_bits == 64);
    CHECK(profile.float_bits == 32);
    CHECK(profile.double_bits == 64);
    CHECK(profile.long_double_bits == 64);
    CHECK(profile.size_t_bits == 16);
    CHECK(profile.intmax_bits == 64);
    CHECK(profile.has_fpu);
    CHECK(profile.has_cpp);
    CHECK_FALSE(profile.has_eh);
    CHECK(profile.backend_available);
}

TEST_CASE("target names parse and enumerate") {
    CHECK(parse_target("kr32") == TargetId::Kr32);
    CHECK(parse_target("kr16") == TargetId::Kr16);
    CHECK(parse_target("kr64") == TargetId::Kr64);
    CHECK(parse_target("kr216") == TargetId::Kr216);
    CHECK_FALSE(parse_target("unknown").has_value());
    CHECK(target_profiles().size() == 4);
}
