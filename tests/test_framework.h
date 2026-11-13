#pragma once

// ---------------------------------------------------------------------------
// Minimal assert-based test harness — no external framework, no fixtures.
//
// A failed check records file:line, marks the enclosing case failed, and makes
// the process exit non-zero so `ctest` (run inside the Docker build) goes red.
// The point is regression detection, not reporting: one line per failure.
// ---------------------------------------------------------------------------

#include <cstdio>
#include <string>

namespace testing {

class Context {
public:
    void Begin(const char* name) {
        caseName_     = name;
        caseFailures_ = 0;
    }

    void End() {
        if (caseFailures_ == 0) {
            ++casesPassed_;
        } else {
            ++casesFailed_;
            std::printf("[FAIL] %s (%d failed check%s)\n",
                        caseName_.c_str(), caseFailures_,
                        caseFailures_ == 1 ? "" : "s");
        }
    }

    void Fail(const char* file, int line, const std::string& what) {
        ++caseFailures_;
        ++checkFailures_;
        std::printf("  %s:%d: %s\n", file, line, what.c_str());
    }

    // Prints the tail summary and returns the process exit code.
    int Summary() const {
        std::printf("\n%d case(s) passed, %d case(s) failed, %d failed check(s)\n",
                    casesPassed_, casesFailed_, checkFailures_);
        return checkFailures_ == 0 ? 0 : 1;
    }

private:
    std::string caseName_;
    int caseFailures_  = 0;
    int casesPassed_   = 0;
    int casesFailed_   = 0;
    int checkFailures_ = 0;
};

} // namespace testing

// CASE(ctx, "name") { ... } — opens and closes one test case.
#define CASE(ctx, name) \
    for (bool once_ = ((ctx).Begin(name), true); once_; once_ = false, (ctx).End())

#define CHECK(ctx, cond) \
    do { if (!(cond)) (ctx).Fail(__FILE__, __LINE__, #cond); } while (0)

#define CHECK_EQ_INT(ctx, actual, expected) \
    do { \
        const long long a_ = static_cast<long long>(actual); \
        const long long e_ = static_cast<long long>(expected); \
        if (a_ != e_) { \
            (ctx).Fail(__FILE__, __LINE__, \
                std::string(#actual " == " #expected " (") + \
                std::to_string(a_) + " vs " + std::to_string(e_) + ")"); \
        } \
    } while (0)

#define CHECK_STR_EQ(ctx, actual, expected) \
    do { \
        const std::string a_ = (actual); \
        const std::string e_ = (expected); \
        if (a_ != e_) { \
            (ctx).Fail(__FILE__, __LINE__, \
                std::string(#actual " == " #expected " (\"") + \
                a_ + "\" vs \"" + e_ + "\")"); \
        } \
    } while (0)

#define CHECK_NEAR(ctx, actual, expected, tol) \
    do { \
        const double a_ = (actual); \
        const double e_ = (expected); \
        const double t_ = (tol); \
        const double d_ = (a_ > e_) ? (a_ - e_) : (e_ - a_); \
        if (!(d_ <= t_)) { \
            (ctx).Fail(__FILE__, __LINE__, \
                std::string(#actual " ~= " #expected " (") + \
                std::to_string(a_) + " vs " + std::to_string(e_) + ")"); \
        } \
    } while (0)
