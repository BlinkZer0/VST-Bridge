// SPDX-License-Identifier: LicenseRef-VstGhost-Proprietary
#include <time.h>
#include "bench/bench_kernel.h"

// Runs the shared benchmark kernel natively. out[0] = elapsed ms, out[1] = checksum.
void vg_native_bench(double out[2])
{
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    double sum = vg_bench_run();
    clock_gettime(CLOCK_MONOTONIC, &b);
    out[0] = (double)(b.tv_sec - a.tv_sec) * 1e3 + (double)(b.tv_nsec - a.tv_nsec) / 1e6;
    out[1] = sum;
}
