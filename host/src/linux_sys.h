// SPDX-License-Identifier: LicenseRef-VstGhost-Proprietary
//
// Raw x86_64 Linux system calls from inside a Windows (PE) program running under Wine.
//
// This works because Wine on x86_64 does not intercept `syscall` instructions in PE code
// (it does not enable syscall user dispatch), and Box64 translates x86_64 Linux syscalls to
// the arm64 kernel. Used only for futex wakeups on the shared-memory file and for getpid /
// getppid. If futex is unavailable, the host falls back to polling (see serve.cpp).
// Note: Windows is LLP64 (`long` is 32-bit), so everything here is `long long`.
#pragma once

#include <cstdint>

namespace vg::linux_sys {

using reg = long long;

inline reg syscall6(reg n, reg a1 = 0, reg a2 = 0, reg a3 = 0, reg a4 = 0, reg a5 = 0, reg a6 = 0) {
    reg ret;
    register reg r10 asm("r10") = a4;
    register reg r8 asm("r8") = a5;
    register reg r9 asm("r9") = a6;
    asm volatile("syscall"
                 : "=a"(ret)
                 : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
                 : "rcx", "r11", "memory");
    return ret;
}

constexpr reg NR_getpid = 39, NR_getppid = 110, NR_futex = 202;
constexpr reg FUTEX_WAIT = 0, FUTEX_WAKE = 1;

struct timespec64 { int64_t sec, nsec; };

inline reg futexWait(uint32_t* addr, uint32_t expected, int64_t timeoutNs) {
    timespec64 ts{timeoutNs / 1000000000, timeoutNs % 1000000000};
    return syscall6(NR_futex, (reg)(intptr_t)addr, FUTEX_WAIT, (reg)expected, (reg)(intptr_t)&ts);
}

inline reg futexWake(uint32_t* addr, int n = 1) { return syscall6(NR_futex, (reg)(intptr_t)addr, FUTEX_WAKE, n); }
inline reg getpid() { return syscall6(NR_getpid); }
inline reg getppid() { return syscall6(NR_getppid); }

}  // namespace vg::linux_sys
