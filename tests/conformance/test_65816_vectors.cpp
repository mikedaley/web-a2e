/*
 * test_65816_vectors.cpp - Run the SingleStepTests 65816 vectors
 *
 * The unit tests next door say what the core is supposed to do. This says
 * whether it does, one instruction at a time, against 10,000 recorded
 * before-and-after states per opcode per mode — 5,120,000 in all — taken from
 * a real 65816.
 *
 * The vectors are not in the repository: they are about 3GB of JSON. Fetch
 * them and point the test at them:
 *
 *   git clone --depth 1 https://github.com/SingleStepTests/65816 /tmp/65816
 *   A2E_65816_VECTORS=/tmp/65816/v1 ctest -R test_65816_vectors --verbose
 *
 * With the variable unset the test skips, the way the optional ROMs do: a
 * machine without the files is not a machine with a broken CPU.
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "cpu65816.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace a2e;

namespace {

// ---------------------------------------------------------------------------
// Just enough JSON
//
// The vector files are machine-generated and have one shape: objects, arrays,
// numbers and short strings. A parser for that is fifty lines; a dependency for
// it would be a dependency in the build of an emulator that has none.
// ---------------------------------------------------------------------------
struct JValue;
using JObject = std::map<std::string, JValue>;
using JArray = std::vector<JValue>;

struct JValue {
    double num = 0;
    std::string str;
    JArray arr;
    JObject obj;
    long asLong() const { return static_cast<long>(num); }
};

class JsonParser {
public:
    explicit JsonParser(const char *text) : p_(text) {}

    JValue parse() {
        skipWhitespace();
        JValue value;
        if (*p_ == '{') {
            p_++;
            skipWhitespace();
            if (*p_ == '}') { p_++; return value; }
            while (true) {
                skipWhitespace();
                const std::string key = parseString();
                skipWhitespace();
                p_++; // ':'
                value.obj[key] = parse();
                skipWhitespace();
                if (*p_ == ',') { p_++; continue; }
                if (*p_ == '}') { p_++; break; }
                break;
            }
        } else if (*p_ == '[') {
            p_++;
            skipWhitespace();
            if (*p_ == ']') { p_++; return value; }
            while (true) {
                value.arr.push_back(parse());
                skipWhitespace();
                if (*p_ == ',') { p_++; continue; }
                if (*p_ == ']') { p_++; break; }
                break;
            }
        } else if (*p_ == '"') {
            value.str = parseString();
        } else if (*p_ == 'n') {
            p_ += 4; // null — the vectors use it for an internal cycle
        } else if (*p_ == 't') {
            p_ += 4;
            value.num = 1;
        } else if (*p_ == 'f') {
            p_ += 5;
        } else {
            char *end = nullptr;
            value.num = std::strtod(p_, &end);
            p_ = end;
        }
        return value;
    }

private:
    void skipWhitespace() {
        while (*p_ == ' ' || *p_ == '\n' || *p_ == '\t' || *p_ == '\r') p_++;
    }

    std::string parseString() {
        std::string s;
        p_++; // opening quote
        while (*p_ && *p_ != '"') {
            if (*p_ == '\\') p_++;
            s += *p_++;
        }
        p_++;
        return s;
    }

    const char *p_;
};

// The whole of what a 65816 can address, allocated once and shared by every
// vector: a test is a handful of cells written and read back, not a machine.
std::vector<uint8_t> &memory() {
    static std::vector<uint8_t> ram(16 * 1024 * 1024, 0);
    return ram;
}

struct Failure {
    std::string name;
    std::string detail;
};

// Run one file. Returns the failures, and counts what it ran.
std::vector<Failure> runVectorFile(const std::string &path, int &ran) {
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();
    std::vector<Failure> failures;
    if (text.empty()) return failures;

    JsonParser parser(text.c_str());
    const JValue tests = parser.parse();

    auto makeCpu = []() {
        return CPU65816([](uint32_t a) { return memory()[a & 0xFFFFFF]; },
                        [](uint32_t a, uint8_t v) { memory()[a & 0xFFFFFF] = v; });
    };
    CPU65816 cpu = makeCpu();

    for (const JValue &test : tests.arr) {
        const JObject &initial = test.obj.at("initial").obj;
        const JObject &expected = test.obj.at("final").obj;
        ran++;

        // STP and WAI last until a reset, and each vector is a fresh machine.
        if (cpu.isStopped() || cpu.isWaiting()) cpu = makeCpu();

        for (const JValue &cell : initial.at("ram").arr) {
            memory()[cell.arr[0].asLong() & 0xFFFFFF] =
                static_cast<uint8_t>(cell.arr[1].asLong());
        }

        const bool emulation = initial.at("e").asLong() != 0;
        cpu.setEmulation(emulation);
        cpu.setP(static_cast<uint8_t>(initial.at("p").asLong()));
        cpu.setA(static_cast<uint16_t>(initial.at("a").asLong()));
        cpu.setX(static_cast<uint16_t>(initial.at("x").asLong()));
        cpu.setY(static_cast<uint16_t>(initial.at("y").asLong()));
        cpu.setSP(static_cast<uint16_t>(initial.at("s").asLong()));
        cpu.setD(static_cast<uint16_t>(initial.at("d").asLong()));
        cpu.setPC(static_cast<uint16_t>(initial.at("pc").asLong()));
        cpu.setPBR(static_cast<uint8_t>(initial.at("pbr").asLong()));
        cpu.setDBR(static_cast<uint8_t>(initial.at("dbr").asLong()));
        cpu.setP(static_cast<uint8_t>(initial.at("p").asLong()));
        cpu.setEmulation(emulation);

        cpu.executeInstruction();

        std::string problem;
        auto check = [&problem](const char *what, long got, long want) {
            if (got == want || !problem.empty()) return;
            char buffer[128];
            std::snprintf(buffer, sizeof buffer, "%s is %ld, should be %ld", what,
                          got, want);
            problem = buffer;
        };
        check("PC", cpu.getPC(), expected.at("pc").asLong());
        check("A", cpu.getA(), expected.at("a").asLong());
        check("X", cpu.getX(), expected.at("x").asLong());
        check("Y", cpu.getY(), expected.at("y").asLong());
        check("SP", cpu.getSP(), expected.at("s").asLong());
        check("D", cpu.getD(), expected.at("d").asLong());
        check("P", cpu.getP(), expected.at("p").asLong());
        check("PBR", cpu.getPBR(), expected.at("pbr").asLong());
        check("DBR", cpu.getDBR(), expected.at("dbr").asLong());
        check("E", cpu.getEmulation() ? 1 : 0, expected.at("e").asLong());
        for (const JValue &cell : expected.at("ram").arr) {
            check("memory", memory()[cell.arr[0].asLong() & 0xFFFFFF],
                  cell.arr[1].asLong());
        }
        // The cycle *count*, which is what this core models; the vectors also
        // record which address each cycle touched, and that is the pattern a
        // //e's video needs and a IIgs's does not.
        check("cycles", cpu.getCycleCount(),
              static_cast<long>(test.obj.at("cycles").arr.size()));

        if (!problem.empty()) {
            failures.push_back({test.obj.at("name").str, problem});
        }
    }
    return failures;
}

} // namespace

TEST_CASE("The 65816 matches a real one, instruction by instruction",
          "[cpu816][conformance]") {
    const char *directory = std::getenv("A2E_65816_VECTORS");
    if (!directory) {
        WARN("A2E_65816_VECTORS is not set; skipping the vector suite. See the "
             "comment at the top of this file for how to fetch it.");
        return;
    }

    // MVN and MVP are skipped, and the reason is the suite's rather than this
    // core's: a block move is one instruction that runs until its counter
    // empties, and the vectors cap it at a hundred cycles and record the state
    // in the middle. An emulator that moves one byte per execution — which is
    // what the hardware does, rewinding PC over itself so an interrupt can be
    // taken mid-move — cannot land on that state and should not try to.
    static const char *const SKIPPED[] = {"44", "54"};

    int ran = 0;
    int failed = 0;
    std::string firstFailures;

    for (int opcode = 0; opcode < 256; opcode++) {
        char hex[3];
        std::snprintf(hex, sizeof hex, "%02x", opcode);
        bool skip = false;
        for (const char *s : SKIPPED) skip = skip || std::strcmp(s, hex) == 0;
        if (skip) continue;

        for (const char *mode : {"e", "n"}) {
            const std::string path =
                std::string(directory) + "/" + hex + "." + mode + ".json";
            std::ifstream probe(path);
            if (!probe.good()) continue; // A partial download is still useful
            probe.close();

            const auto failures = runVectorFile(path, ran);
            failed += static_cast<int>(failures.size());
            for (size_t i = 0; i < failures.size() && firstFailures.size() < 2000;
                 i++) {
                firstFailures += failures[i].name + ": " + failures[i].detail + "\n";
            }
        }
    }

    INFO("first failures:\n" << firstFailures);
    REQUIRE(ran > 0); // Pointing at an empty directory is a failure, not a pass
    REQUIRE(failed == 0);
}
