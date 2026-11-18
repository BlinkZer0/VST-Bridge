// SPDX-License-Identifier: GPL-3.0-or-later
package com.vstghost

fun interface ProgressListener {
    /** Return false to cancel. */
    fun onProgress(done: Long, total: Long): Boolean
}

object NativeBridge {
    init {
        System.loadLibrary("vstghost")
    }

    /** Extracts a tar.zst region of [fd]. Returns stats JSON, or a string starting with "ERROR: ". */
    @JvmStatic external fun extract(
        fd: Int, offset: Long, length: Long,
        dest: String, placeholder: String, realRoot: String,
        listener: ProgressListener?,
    ): String

    /** [elapsed ms, checksum] of shared/bench/bench_kernel.h compiled for arm64. */
    @JvmStatic external fun benchmark(): DoubleArray
}
