#include <ostream>

#include "doctest.h"
#include "krc/target/target.h"

using krc::target::AggregateRules;
using krc::target::DataModel;
using krc::target::Endianness;
using krc::target::TargetFamily;
using krc::target::TargetId;
using krc::target::TargetProfile;
using krc::target::get_target;
using krc::target::parse_target;
using krc::target::target_profiles;

TEST_CASE("KR232 profile matches the approved data model") {
    const TargetProfile& profile = get_target(TargetId::Kr232);
    CHECK(profile.id == TargetId::Kr232);
    CHECK(profile.name == "kr232");
    CHECK(profile.macro == "__kr232__");
    CHECK(profile.family == TargetFamily::Register);
    CHECK(profile.pointer_bits == 32);
    CHECK(profile.endianness == Endianness::Little);
    CHECK(profile.data_model == DataModel::Ilp32);
    CHECK(profile.char_bits == 8);
    CHECK_FALSE(profile.char_unsigned);
    CHECK(profile.short_bits == 16);
    CHECK(profile.int_bits == 32);
    CHECK(profile.long_bits == 32);
    CHECK(profile.long_long_bits == 0);  // no 64-bit integer type (plan 2.3)
    CHECK(profile.float_bits == 64);  // one floating-point format: float is double
    CHECK(profile.double_bits == 64);
    CHECK(profile.long_double_bits == 64);  // an alias for double (plan 2.5)
    CHECK(profile.size_t_bits == 32);
    CHECK(profile.ptrdiff_bits == 32);
    CHECK(profile.intmax_bits == 32);
    CHECK(profile.wchar_bits == 32);
    CHECK(profile.has_fpu);
    CHECK_FALSE(profile.has_matrix);
    CHECK_FALSE(profile.has_atomics);
    CHECK_FALSE(profile.has_threads);
    CHECK_FALSE(profile.has_eh);
    CHECK_FALSE(profile.has_rtti);
    CHECK_FALSE(profile.has_tls);
    CHECK(profile.has_cpp);
    // kr232 has had a code generator since M3, so this is no longer false.
    CHECK(profile.backend_available);
    CHECK(profile.aggregate_rules == AggregateRules::MemoryOnly);
    CHECK(to_string(TargetId::Kr232) == "kr232");
    CHECK(parse_target("kr232") == TargetId::Kr232);
}

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
    CHECK(profile.has_eh);
    CHECK(profile.backend_available);
}

TEST_CASE("target names parse and enumerate") {
    CHECK(parse_target("kr32") == TargetId::Kr32);
    CHECK(parse_target("kr16") == TargetId::Kr16);
    CHECK(parse_target("kr64") == TargetId::Kr64);
    CHECK(parse_target("kr216") == TargetId::Kr216);
    CHECK_FALSE(parse_target("unknown").has_value());
    CHECK(target_profiles().size() == 5);
}
TEST_CASE("the extension table names every runnable target") {
    // G1 of the build cleanup plan: one table owns the extensions, so no tool
    // carries its own list and a new target cannot ship without a name.
    for (const TargetProfile& profile : target_profiles()) {
        const auto extension = krc::target::image_extension(profile.id);
        REQUIRE_FALSE(extension.empty());
        CHECK(extension.front() == '.');
        CHECK(krc::target::target_for_extension(extension) == profile.id);
        CHECK(krc::target::is_image_extension(extension));
    }
    CHECK(krc::target::image_extension(TargetId::Kr32) == ".kr32");
    CHECK(krc::target::image_extension(TargetId::Kr216) == ".kr216");
    CHECK(krc::target::image_extension(TargetId::Kr232) == ".kr232");

    // An unknown suffix names no target, which is what makes oddball names legal.
    CHECK_FALSE(krc::target::target_for_extension(".backup").has_value());
    // G2 deleted the old names, so they are unknown now: the four that used to
    // be extensions name no target, and neither does a bare stem without a dot.
    CHECK_FALSE(krc::target::target_for_extension(".k32").has_value());
    CHECK_FALSE(krc::target::target_for_extension("k32").has_value());
    CHECK_FALSE(krc::target::target_for_extension("").has_value());
    CHECK_FALSE(krc::target::is_image_extension(".kex"));
    CHECK_FALSE(krc::target::is_image_extension(".k232"));
    CHECK_FALSE(krc::target::is_image_extension(".k216"));
}
