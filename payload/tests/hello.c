/*
 * VST gHost Phase 0: trivial x86_64 Windows console program.
 * SPDX-License-Identifier: LicenseRef-VstGhost-Proprietary
 *
 * Prints what it runs on, then runs the shared DSP benchmark kernel so the app can
 * compare it with the same kernel compiled natively for arm64.
 * Build: x86_64-w64-mingw32-gcc -O2 -ffp-contract=off -I../../shared hello.c -o hello.exe
 */
#include <windows.h>
#include <intrin.h>
#include <stdio.h>

#include "bench/bench_kernel.h"

typedef const char *(CDECL *wine_get_version_t)(void);
typedef void (CDECL *wine_get_host_version_t)(const char **sysname, const char **release);

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("VSTGHOST-HELLO start\n");

    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    wine_get_version_t wgv = (wine_get_version_t)(void *)GetProcAddress(ntdll, "wine_get_version");
    wine_get_host_version_t whv = (wine_get_host_version_t)(void *)GetProcAddress(ntdll, "wine_get_host_version");
    printf("wine: %s\n", wgv ? wgv() : "(not running under Wine)");
    if (whv) {
        const char *sys = "?", *rel = "?";
        whv(&sys, &rel);
        printf("host: %s %s\n", sys, rel);
    }

    int regs[4];
    char brand[49] = {0};
    __cpuid(regs, (int)0x80000000);
    if ((unsigned)regs[0] >= 0x80000004u) {
        __cpuid((int *)(brand + 0), (int)0x80000002);
        __cpuid((int *)(brand + 16), (int)0x80000003);
        __cpuid((int *)(brand + 32), (int)0x80000004);
    }
    printf("cpu: %s\n", brand);
    __cpuidex(regs, 7, 0);
    printf("avx2: %s\n", (regs[1] & (1 << 5)) ? "yes" : "no");

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    printf("cpus: %lu\n", (unsigned long)si.dwNumberOfProcessors);
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof ms;
    GlobalMemoryStatusEx(&ms);
    printf("memory: %llu MB\n", (unsigned long long)(ms.ullTotalPhys >> 20));

    LARGE_INTEGER f, a, b;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&a);
    double sum = vg_bench_run();
    QueryPerformanceCounter(&b);
    printf("BENCH ms=%.1f checksum=%.17g\n", (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)f.QuadPart, sum);

    printf("VSTGHOST-HELLO done\n");
    return 0;
}
