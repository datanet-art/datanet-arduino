// A ~100-line test framework so the native suite needs nothing but a C++
// compiler. Tests self-register at static-init time; main() runs them all.

#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace tinytest {

struct TestCase {
    const char*           name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failureCount() {
    static int count = 0;
    return count;
}

inline void reportFailure(const char* file, int line, const std::string& message) {
    failureCount()++;
    std::printf("    FAIL %s:%d\n         %s\n", file, line, message.c_str());
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

template <typename T>
inline std::string show(const T& value) {
    return std::to_string(value);
}

inline std::string show(const std::string& value) { return "\"" + value + "\""; }
inline std::string show(const char* value) {
    return value ? "\"" + std::string(value) + "\"" : "(null)";
}
inline std::string show(bool value) { return value ? "true" : "false"; }

inline int run() {
    int failedTests = 0;
    for (auto& test : registry()) {
        int before = failureCount();
        std::printf("  %s\n", test.name);
        test.fn();
        if (failureCount() != before) failedTests++;
    }

    int total = static_cast<int>(registry().size());
    std::printf("\n%d/%d tests passed (%d assertion failures)\n",
                total - failedTests, total, failureCount());
    return failedTests == 0 ? 0 : 1;
}

}  // namespace tinytest

#define TEST(name)                                                     \
    static void name();                                                \
    static tinytest::Registrar name##_registrar(#name, name);          \
    static void name()

#define CHECK(expr)                                                    \
    do {                                                               \
        if (!(expr)) {                                                 \
            tinytest::reportFailure(__FILE__, __LINE__,                \
                                    "expected: " #expr);               \
        }                                                              \
    } while (0)

#define CHECK_EQ(actual, expected)                                     \
    do {                                                               \
        auto _a = (actual);                                            \
        auto _e = (expected);                                          \
        if (!(_a == _e)) {                                             \
            tinytest::reportFailure(                                   \
                __FILE__, __LINE__,                                    \
                std::string(#actual) + " == " + #expected +            \
                    "\n           actual:   " + tinytest::show(_a) +   \
                    "\n           expected: " + tinytest::show(_e));   \
        }                                                              \
    } while (0)

#define CHECK_STR_EQ(actual, expected)                                 \
    do {                                                               \
        std::string _a = (actual);                                     \
        std::string _e = (expected);                                   \
        if (_a != _e) {                                                \
            tinytest::reportFailure(                                   \
                __FILE__, __LINE__,                                    \
                std::string(#actual) +                                 \
                    "\n           actual:   " + tinytest::show(_a) +   \
                    "\n           expected: " + tinytest::show(_e));   \
        }                                                              \
    } while (0)
