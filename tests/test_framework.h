#pragma once

// ---------------------------------------------------------------------------
// Minimal assert-based test harness — no external framework, no fixtures.
//
// A failed check records file:line, marks the enclosing case failed, and makes
// the process exit non-zero so `ctest` (run inside the Docker build) goes red.
// The point is regression detection, not reporting: one line per failure.
// ---------------------------------------------------------------------------

#include <cstdio>
#include <exception>
#include <mutex>
#include <string>

namespace testing {

class Context {
public:
    void Begin(const char* name) {
        std::lock_guard<std::mutex> lock(mutex_);
        caseName_     = name ? name : "(null)";
        caseFailures_ = 0;
    }

    void End() {
        std::lock_guard<std::mutex> lock(mutex_);
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
        std::lock_guard<std::mutex> lock(mutex_);
        ++caseFailures_;
        ++checkFailures_;
        std::printf("  %s:%d: %s\n", file ? file : "(null)", line, what.c_str());
    }

    // Like Fail, but records that the failure came from an exception escaping
    // the test body, so the suite-level catch in test_main can tell it has
    // already been counted and does not inflate the totals with a second entry
    // for the same incident.
    void FailUncaught(const char* file, int line, const std::string& what) {
        Fail(file, line, what);
        std::lock_guard<std::mutex> lock(mutex_);
        caseThrew_ = true;
    }

    // Consumes the FailUncaught flag: true when the escaping exception has
    // already been recorded (by CaseScope) and must not be counted again.
    bool ConsumeUncaught() {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool threw = caseThrew_;
        caseThrew_ = false;
        return threw;
    }

    // Prints the tail summary and returns the process exit code.
    int Summary() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::printf("\n%d case(s) passed, %d case(s) failed, %d failed check(s)\n",
                    casesPassed_, casesFailed_, checkFailures_);
        return checkFailures_ == 0 ? 0 : 1;
    }

private:
    mutable std::mutex mutex_;
    std::string caseName_;
    int caseFailures_  = 0;
    int casesPassed_   = 0;
    int casesFailed_   = 0;
    int checkFailures_ = 0;
    bool caseThrew_    = false;
};

// RAII case finalizer: ensures End() runs even if a test body breaks/returns.
// An exception escaping the body would otherwise record the case as PASSED
// (no CHECK failed), so detect the in-flight exception and fail the case.
class CaseScope {
public:
    explicit CaseScope(Context& ctx) : ctx_(ctx) {}
    ~CaseScope() {
        if (std::uncaught_exceptions() > 0) {
            try {
                ctx_.FailUncaught(__FILE__, __LINE__,
                                  "uncaught exception escaped the test case");
            } catch (...) {}
        }
        ctx_.End();
    }
    CaseScope(const CaseScope&) = delete;
    CaseScope& operator=(const CaseScope&) = delete;
private:
    Context& ctx_;
};

// Null-safe conversion for CHECK_STR_EQ: constructing std::string from a null
// const char* is UB, so report it instead of crashing the test process.
inline std::string ToString(const std::string& s) { return s; }
inline std::string ToString(const char* s) { return s ? std::string(s) : std::string("(null)"); }

} // namespace testing

// CASE(ctx, "name") { ... } — opens and closes one test case.
#define CASE(ctx, name) \
    for (bool once_ = ((ctx).Begin(name), true); once_; once_ = false) \
        for (testing::CaseScope caseScope_((ctx)); once_; once_ = false)

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
        const std::string a_ = testing::ToString(actual); \
        const std::string e_ = testing::ToString(expected); \
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
