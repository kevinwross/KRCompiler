#include "linker/archive.h"

#include <charconv>
#include <string_view>

namespace krc::linker {
namespace {

constexpr std::string_view kMagic = "!<arch>\n";
constexpr std::size_t kHeaderSize = 60;

std::string_view field(std::string_view header, std::size_t offset, std::size_t length) {
    return header.substr(offset, length);
}

std::string trim(std::string_view text) {
    while (!text.empty() && (text.back() == ' ' || text.back() == '\n')) {
        text.remove_suffix(1);
    }
    while (!text.empty() && text.front() == ' ') {
        text.remove_prefix(1);
    }
    return std::string(text);
}

bool parse_size(std::string_view text, std::uint64_t& value) {
    text = field(text, 0, text.size());
    while (!text.empty() && text.back() == ' ') {
        text.remove_suffix(1);
    }
    if (text.empty()) {
        return false;
    }
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc{} && result.ptr == end;
}

std::string member_name(std::string_view raw, std::string_view long_names,
                        std::span<const std::uint8_t> data) {
    if (raw.starts_with("#1/")) {
        std::uint64_t length = 0;
        if (parse_size(raw.substr(3), length) && length <= data.size()) {
            return std::string(reinterpret_cast<const char*>(data.data()),
                               static_cast<std::size_t>(length));
        }
        return {};
    }
    if (raw.starts_with("/") && raw.size() > 1 && raw[1] >= '0' && raw[1] <= '9') {
        std::uint64_t offset = 0;
        if (!parse_size(raw.substr(1), offset) || offset >= long_names.size()) {
            return {};
        }
        std::size_t end = static_cast<std::size_t>(offset);
        while (end < long_names.size() && long_names[end] != '\n') {
            ++end;
        }
        auto name = std::string(long_names.substr(static_cast<std::size_t>(offset),
                                                  end - static_cast<std::size_t>(offset)));
        if (!name.empty() && name.back() == '/') {
            name.pop_back();
        }
        return name;
    }
    auto name = trim(raw);
    if (!name.empty() && name.back() == '/') {
        name.pop_back();
    }
    return name;
}

}  // namespace

bool is_archive(std::span<const std::uint8_t> bytes) {
    return bytes.size() >= kMagic.size() &&
           std::string_view(reinterpret_cast<const char*>(bytes.data()), kMagic.size()) ==
               kMagic;
}

bool read_archive(std::span<const std::uint8_t> bytes, std::vector<ArchiveMember>& members,
                  std::string& error) {
    members.clear();
    if (!is_archive(bytes)) {
        error = "not a KRO archive";
        return false;
    }
    std::size_t position = kMagic.size();
    std::string_view long_names;
    while (position < bytes.size()) {
        if (position + kHeaderSize > bytes.size()) {
            error = "archive header is truncated";
            return false;
        }
        const std::string_view header(
            reinterpret_cast<const char*>(bytes.data() + position), kHeaderSize);
        if (header[58] != '`' || header[59] != '\n') {
            error = "archive header terminator is invalid";
            return false;
        }
        std::uint64_t size = 0;
        if (!parse_size(field(header, 48, 10), size)) {
            error = "archive member size is invalid";
            return false;
        }
        position += kHeaderSize;
        if (size > bytes.size() - position) {
            error = "archive member exceeds the file";
            return false;
        }
        const auto data = bytes.subspan(position, static_cast<std::size_t>(size));
        position += static_cast<std::size_t>(size);
        if ((size & 1u) != 0 && position < bytes.size()) {
            ++position;
        }
        const auto raw_name = field(header, 0, 16);
        if (raw_name.starts_with("//")) {
            long_names = std::string_view(reinterpret_cast<const char*>(data.data()),
                                          data.size());
            continue;
        }
        if (raw_name.starts_with("/ ") || trim(raw_name) == "/") {
            continue;
        }
        const auto name = member_name(raw_name, long_names, data);
        if (name.empty()) {
            continue;
        }
        std::size_t name_length = 0;
        if (raw_name.starts_with("#1/")) {
            std::uint64_t length = 0;
            if (parse_size(raw_name.substr(3), length) && length <= data.size()) {
                name_length = static_cast<std::size_t>(length);
            }
        }
        members.push_back({name, data.subspan(name_length)});
    }
    return true;
}

}  // namespace krc::linker
