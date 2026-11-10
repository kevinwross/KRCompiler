#include "linker/kr16_image.h"

#include <cstdint>
#include <string>
#include <vector>

#include "doctest.h"
#include "linker/kro_object.h"

TEST_CASE("kr16 image writer emits an EXEC container") {
    const std::vector<std::uint8_t> text{0x00, 0x88};  // HLT at 0x0100
    std::vector<std::uint8_t> bytes;
    std::string error;
    REQUIRE_MESSAGE(krc::linker::write_kr16_image(text, 0x0100, bytes, error),
                    error);
    REQUIRE(bytes.size() > 64);
    CHECK(bytes[0] == 'K');
    CHECK(bytes[1] == 'R');
    CHECK(bytes[2] == '1');
    CHECK(bytes[3] == '6');
    CHECK((bytes[4] | (bytes[5] << 8)) == 0x0001);
    CHECK((bytes[6] | (bytes[7] << 8)) == 2);
    CHECK((bytes[16] | (bytes[17] << 8)) == 0x0100);
    CHECK((bytes[20] | (bytes[21] << 8)) == 2);

    const auto stored =
        static_cast<std::uint32_t>(bytes[52]) |
        static_cast<std::uint32_t>(bytes[53]) << 8 |
        static_cast<std::uint32_t>(bytes[54]) << 16 |
        static_cast<std::uint32_t>(bytes[55]) << 24;
    bytes[52] = 0;
    bytes[53] = 0;
    bytes[54] = 0;
    bytes[55] = 0;
    CHECK(krc::linker::crc32(bytes) == stored);
}
