#pragma once

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <functional>
#include <iostream>
#include <cstring>
#include <string>
#include "classifier.h"

namespace kernelguard {

struct ExecutionResult {
    CaseVerdict verdict;
    double wall_time_ms;
    int exit_code;
    int termination_signal;
    bool timed_out;

    ExecutionResult()
        : verdict(CaseVerdict::pass()), wall_time_ms(0.0), exit_code(0),
          termination_signal(0), timed_out(false) {}
};

// Serialized verdict over Unix pipe
struct PipePayload {
    VerdictType verdict_type;
    size_t failing_index;
    float actual_value;
    float expected_value;
    int64_t observed_ulp;
    double max_abs_error;
    double max_rel_error;
    char details[256];
};

inline void write_verdict_to_pipe(int fd, const CaseVerdict& v) {
    PipePayload p;
    p.verdict_type = v.type;
    p.failing_index = v.failing_index;
    p.actual_value = v.actual_value;
    p.expected_value = v.expected_value;
    p.observed_ulp = v.observed_ulp;
    p.max_abs_error = v.max_abs_error;
    p.max_rel_error = v.max_rel_error;
    std::strncpy(p.details, v.details.c_str(), sizeof(p.details) - 1);
    p.details[sizeof(p.details) - 1] = '\0';

    ssize_t written = write(fd, &p, sizeof(p));
    (void)written;
}

inline bool read_verdict_from_pipe(int fd, CaseVerdict& v) {
    PipePayload p;
    ssize_t bytes_read = read(fd, &p, sizeof(p));
    if (bytes_read == sizeof(p)) {
        v.type = p.verdict_type;
        v.failing_index = p.failing_index;
        v.actual_value = p.actual_value;
        v.expected_value = p.expected_value;
        v.observed_ulp = p.observed_ulp;
        v.max_abs_error = p.max_abs_error;
        v.max_rel_error = p.max_rel_error;
        v.details = std::string(p.details);
        return true;
    }
    return false;
}

// Executes a callable test case in a separate forked child process.
// Enforces a strict timeout and isolates against crashes, aborts, and guard-page segfaults.
inline ExecutionResult run_isolated_case(
    const std::function<CaseVerdict()>& test_func,
    int timeout_seconds = 2
) {
    ExecutionResult result;
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        result.verdict = CaseVerdict::fail(VerdictType::CRASH, "Failed to create IPC pipe");
        return result;
    }

    struct timespec ts_start, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        result.verdict = CaseVerdict::fail(VerdictType::CRASH, "Failed to fork isolation process");
        return result;
    }

    if (pid == 0) {
        // Child process
        close(pipe_fds[0]); // Close read end

        // Set alarm for timeout
        alarm(timeout_seconds);

        // Execute test function
        CaseVerdict v = test_func();

        write_verdict_to_pipe(pipe_fds[1], v);
        close(pipe_fds[1]);
        _exit(0);
    }

    // Parent process
    close(pipe_fds[1]); // Close write end

    int status = 0;
    pid_t wpid = waitpid(pid, &status, 0);

    clock_gettime(CLOCK_MONOTONIC, &ts_end);
    result.wall_time_ms = (ts_end.tv_sec - ts_start.tv_sec) * 1000.0 +
                          (ts_end.tv_nsec - ts_start.tv_nsec) / 1000000.0;

    if (wpid == pid) {
        if (WIFEXITED(status)) {
            result.exit_code = WEXITSTATUS(status);
            CaseVerdict child_verdict;
            if (read_verdict_from_pipe(pipe_fds[0], child_verdict)) {
                result.verdict = child_verdict;
            } else {
                result.verdict = CaseVerdict::fail(VerdictType::CRASH,
                    "Child process exited without writing result (exit code: " + std::to_string(result.exit_code) + ")");
            }
        } else if (WIFSIGNALED(status)) {
            result.termination_signal = WTERMSIG(status);
            if (result.termination_signal == SIGALRM) {
                result.timed_out = true;
                result.verdict = CaseVerdict::fail(VerdictType::TIMEOUT,
                    "Kernel execution timed out after " + std::to_string(timeout_seconds) + " seconds");
            } else if (result.termination_signal == SIGSEGV || result.termination_signal == SIGBUS) {
                result.verdict = CaseVerdict::fail(VerdictType::GUARD_PAGE_FAULT,
                    "Segmentation fault / memory fault caught (guard page violation or illegal pointer dereference)");
            } else if (result.termination_signal == SIGFPE) {
                result.verdict = CaseVerdict::fail(VerdictType::CRASH,
                    "Floating-point exception caught (SIGFPE)");
            } else if (result.termination_signal == SIGABRT) {
                result.verdict = CaseVerdict::fail(VerdictType::CRASH,
                    "Process aborted (SIGABRT: assertion failure or sanitizer panic)");
            } else {
                result.verdict = CaseVerdict::fail(VerdictType::CRASH,
                    "Process terminated by signal " + std::to_string(result.termination_signal));
            }
        }
    }

    close(pipe_fds[0]);
    return result;
}

} // namespace kernelguard
