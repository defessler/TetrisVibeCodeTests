#pragma once
// Minimal single-header test runner — zero external dependencies.
//
// Usage:
//   TEST("description") { EXPECT_TRUE(...); EXPECT_EQ(a, b); }
//   int main() { return run_tests(); }

#include <functional>
#include <iostream>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

namespace testing {

struct Failure {
    std::string message;
    std::source_location loc;
};

struct TestCase {
    std::string_view        name;
    std::function<void()>   fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> v;
    return v;
}

inline thread_local std::vector<Failure> current_failures;

inline int run_tests() {
    int passed = 0, failed = 0;
    for (auto& tc : registry()) {
        current_failures.clear();
        tc.fn();
        if (current_failures.empty()) {
            std::cout << "  \033[92m✓\033[0m  " << tc.name << "\n";
            ++passed;
        } else {
            std::cout << "  \033[91m✗\033[0m  " << tc.name << "\n";
            for (auto& f : current_failures)
                std::cout << "       " << f.loc.file_name() << ':'
                          << f.loc.line() << "  " << f.message << "\n";
            ++failed;
        }
    }
    std::cout << "\n" << (failed == 0 ? "\033[92m" : "\033[91m")
              << passed << " passed, " << failed << " failed\033[0m\n";
    return failed ? 1 : 0;
}

struct Registrar {
    Registrar(std::string_view name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

inline void fail(std::string msg,
                 std::source_location loc = std::source_location::current()) {
    current_failures.push_back({std::move(msg), loc});
}

} // namespace testing

// ── Assertion macros ──────────────────────────────────────────────────────────

// Three-level expansion: _TEST_CAT forces token-paste after uid is expanded,
// _TEST_IMPL2 receives the already-expanded __COUNTER__ value,
// _TEST_IMPL passes __COUNTER__ to trigger that expansion.
#define _TEST_CAT(a, b)    a##b
#define _TEST_IMPL2(name, uid)                                            \
    static void _TEST_CAT(_test_body_, uid)();                            \
    static ::testing::Registrar _TEST_CAT(_reg_, uid){                    \
        name, _TEST_CAT(_test_body_, uid)};                               \
    static void _TEST_CAT(_test_body_, uid)()
#define _TEST_IMPL(name, uid) _TEST_IMPL2(name, uid)
#define TEST(name) _TEST_IMPL(name, __COUNTER__)

#define EXPECT_TRUE(expr)                                                 \
    do { if (!(expr)) testing::fail("EXPECT_TRUE(" #expr ") failed"); } while(0)

#define EXPECT_FALSE(expr)                                                \
    do { if ((expr)) testing::fail("EXPECT_FALSE(" #expr ") failed"); } while(0)

#define EXPECT_EQ(a, b)                                                   \
    do { if (!((a) == (b)))                                               \
        testing::fail("EXPECT_EQ(" #a ", " #b ")  got: "                 \
                      + std::to_string(a) + " != " + std::to_string(b)); \
    } while(0)

#define EXPECT_NE(a, b)                                                   \
    do { if ((a) == (b))                                                  \
        testing::fail("EXPECT_NE(" #a ", " #b ")  both equal: "          \
                      + std::to_string(a));                               \
    } while(0)

#define EXPECT_GE(a, b)                                                   \
    do { if (!((a) >= (b)))                                               \
        testing::fail("EXPECT_GE(" #a ", " #b ")  got: "                 \
                      + std::to_string(a) + " < " + std::to_string(b));  \
    } while(0)
