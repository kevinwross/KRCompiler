#include "doctest.h"

#include <exception>
#include <string>
#include <vector>

#include "backend/machine_cfg.h"

using namespace krc::backend::machine;

namespace {

InstructionFlow analyze(std::string_view line) {
    InstructionFlow flow;
    const auto space = line.find(' ');
    const auto mnemonic = line.substr(0, space);
    const auto rest = space == std::string_view::npos ? std::string_view{}
                                                       : line.substr(space + 1);
    const auto number = [](std::string_view text, int& value) {
        if (text.empty()) {
            return false;
        }
        std::size_t consumed = 0;
        try {
            value = std::stoi(std::string(text), &consumed);
        } catch (const std::exception&) {
            return false;
        }
        return consumed == text.size();
    };
    const auto values = [&](std::vector<int>& destination) {
        std::size_t start = 0;
        while (start < rest.size()) {
            const auto end = rest.find(' ', start);
            int value = 0;
            if (number(rest.substr(start, end == std::string_view::npos
                                             ? std::string_view::npos
                                             : end - start),
                       value)) {
                destination.push_back(value);
            }
            start = end == std::string_view::npos ? rest.size() : end + 1;
        }
    };
    if (mnemonic == "DEF") {
        values(flow.defs);
    } else if (mnemonic == "USE") {
        values(flow.uses);
    } else if (mnemonic == "BR") {
        flow.targets.push_back(std::string(rest));
    } else if (mnemonic == "JMP") {
        flow.targets.push_back(std::string(rest));
        flow.falls_through = false;
    } else if (mnemonic == "RET") {
        flow.falls_through = false;
    }
    return flow;
}

}  // namespace

TEST_CASE("A conditional branch has its target and fallthrough as successors") {
    const std::vector<std::string> lines = {
        ".Lentry:",
        "  USE 0",
        "  DEF 1",
        "  DEF 2",
        "  BR .Lthen",
        "  JMP .Lelse",
        ".Lthen:",
        "  USE 1",
        "  JMP .Ljoin",
        ".Lelse:",
        "  USE 1",
        ".Ljoin:",
        "  USE 1 2",
        "  RET",
    };
    auto flow = build_control_flow(lines, analyze);
    REQUIRE(flow.blocks.size() == 5);
    CHECK(flow.blocks[0].label == ".Lentry");
    CHECK(flow.blocks[0].successors.size() == 2);
    CHECK(flow.unresolved_targets == 0);
    CHECK(control_flow_is_consistent(flow));

    compute_liveness(flow);
    CHECK(flow.blocks[0].live_in == std::vector<int>{0});
    CHECK(flow.blocks[0].live_out == std::vector<int>{1, 2});
    CHECK(flow.blocks[3].live_in == std::vector<int>{1, 2});
    CHECK(flow.blocks[4].live_in == std::vector<int>{1, 2});
}

TEST_CASE("Liveness carries a loop-carried value around a back edge") {
    const std::vector<std::string> lines = {
        ".Lentry:",
        "  USE 0",
        "  DEF 1",
        "  JMP .Lhead",
        ".Lhead:",
        "  USE 1",
        "  BR .Lbody",
        "  JMP .Lexit",
        ".Lbody:",
        "  DEF 1",
        "  JMP .Lhead",
        ".Lexit:",
        "  USE 1",
        "  RET",
    };
    auto flow = build_control_flow(lines, analyze);
    REQUIRE(flow.blocks.size() == 5);
    compute_liveness(flow);
    CHECK(flow.blocks[1].live_in == std::vector<int>{1});
    CHECK(flow.blocks[1].live_out == std::vector<int>{1});
    CHECK(flow.blocks[3].live_in.empty());
    CHECK(flow.blocks[3].live_out == std::vector<int>{1});
}

TEST_CASE("Debug labels do not split a block") {
    const std::vector<std::string> lines = {
        ".Lentry:",
        "  DEF 1",
        ".Ldbg.f.10:",
        "  USE 1",
        "  RET",
    };
    auto flow = build_control_flow(lines, analyze);
    REQUIRE(flow.blocks.size() == 1);
    CHECK(flow.blocks[0].instructions.size() == 3);
}

TEST_CASE("An unresolved branch target is counted") {
    const std::vector<std::string> lines = {
        ".Lentry:",
        "  JMP .Lmissing",
    };
    const auto flow = build_control_flow(lines, analyze);
    CHECK(flow.unresolved_targets == 1);
    CHECK(flow.blocks[0].successors.empty());
}
