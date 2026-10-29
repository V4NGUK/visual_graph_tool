#pragma once

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

inline void check(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

inline void expect_error(const std::function<void()>& action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Expected an exception");
}

struct TestSuite {
    int passed = 0;
    int failed = 0;

    void run(const std::string& name, const std::function<void()>& action) {
        try {
            action();
            ++passed;
            std::cout << "PASS: " << name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
        }
    }

    int finish() const {
        std::cout << "Passed: " << passed << ", failed: " << failed << '\n';
        return failed == 0 ? 0 : 1;
    }
};
