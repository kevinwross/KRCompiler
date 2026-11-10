#include <doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "linker/archive.h"

namespace {

void append_header(std::vector<std::uint8_t>& archive, const std::string& name,
                   std::size_t size) {
    std::string header(60, ' ');
    header.replace(0, name.size(), name);
    const auto text = std::to_string(size);
    header.replace(48, text.size(), text);
    header[58] = '`';
    header[59] = '\n';
    archive.insert(archive.end(), header.begin(), header.end());
}

void append_member(std::vector<std::uint8_t>& archive, const std::string& name,
                   const std::string& data) {
    append_header(archive, name, data.size());
    archive.insert(archive.end(), data.begin(), data.end());
    if ((data.size() & 1u) != 0) {
        archive.push_back('\n');
    }
}

}  // namespace

TEST_CASE("archive reader returns members and skips padding") {
    std::vector<std::uint8_t> archive = {'!', '<', 'a', 'r', 'c', 'h', '>', '\n'};
    append_member(archive, "one.o/", "abc");
    append_member(archive, "two.o/", "de");

    std::vector<krc::linker::ArchiveMember> members;
    std::string error;
    REQUIRE_MESSAGE(krc::linker::read_archive(archive, members, error), error);
    REQUIRE_EQ(members.size(), 2);
    CHECK_EQ(members[0].name, "one.o");
    CHECK_EQ(std::string(reinterpret_cast<const char*>(members[0].data.data()),
                         members[0].data.size()),
             "abc");
    CHECK_EQ(members[1].name, "two.o");
    CHECK_EQ(members[1].data.size(), 2);
}

TEST_CASE("archive reader rejects a truncated member") {
    std::vector<std::uint8_t> archive = {'!', '<', 'a', 'r', 'c', 'h', '>', '\n'};
    append_header(archive, "bad.o/", 32);
    archive.push_back('x');
    std::vector<krc::linker::ArchiveMember> members;
    std::string error;
    CHECK_FALSE(krc::linker::read_archive(archive, members, error));
    CHECK_FALSE(error.empty());
}
