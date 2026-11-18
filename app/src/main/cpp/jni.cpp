// SPDX-License-Identifier: GPL-3.0-or-later
#include <jni.h>
#include <android/log.h>

#include <cstdio>
#include <string>

#include "extractor.h"

extern "C" void vg_native_bench(double out[2]);

namespace {
std::string str(JNIEnv* env, jstring s) {
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string r(c);
    env->ReleaseStringUTFChars(s, c);
    return r;
}
}  // namespace

// Returns a JSON object with stats on success, or a string starting with "ERROR: ".
extern "C" JNIEXPORT jstring JNICALL
Java_com_vstghost_NativeBridge_extract(JNIEnv* env, jclass, jint fd, jlong offset, jlong length,
                                       jstring dest, jstring placeholder, jstring realRoot,
                                       jobject listener) {
    vg::ExtractOptions opt;
    opt.fd = fd;
    opt.offset = offset;
    opt.length = length;
    opt.destDir = str(env, dest);
    opt.placeholder = str(env, placeholder);
    opt.realRoot = str(env, realRoot);
    jmethodID onProgress = nullptr;
    if (listener) {
        jclass cls = env->GetObjectClass(listener);
        onProgress = env->GetMethodID(cls, "onProgress", "(JJ)Z");
        opt.progress = [&](int64_t done, int64_t total) -> bool {
            return env->CallBooleanMethod(listener, onProgress, (jlong)done, (jlong)total) == JNI_TRUE;
        };
    }
    vg::ExtractStats st;
    std::string err = vg::extractTarZst(opt, st);
    if (!err.empty()) {
        __android_log_print(ANDROID_LOG_ERROR, "VSTGHOST", "extract failed: %s", err.c_str());
        return env->NewStringUTF(("ERROR: " + err).c_str());
    }
    char json[512];
    snprintf(json, sizeof json,
             "{\"files\":%llu,\"dirs\":%llu,\"symlinks\":%llu,\"hardlinks\":%llu,\"bytes\":%llu,"
             "\"interpRewrites\":%llu,\"shebangRewrites\":%llu,\"symlinkRewrites\":%llu,\"ms\":%.1f}",
             (unsigned long long)st.files, (unsigned long long)st.dirs,
             (unsigned long long)st.symlinks, (unsigned long long)st.hardlinks,
             (unsigned long long)st.bytesWritten, (unsigned long long)st.interpRewrites,
             (unsigned long long)st.shebangRewrites, (unsigned long long)st.symlinkRewrites,
             st.elapsedMs);
    return env->NewStringUTF(json);
}

// [elapsed ms, checksum] of the shared benchmark kernel, compiled for arm64.
extern "C" JNIEXPORT jdoubleArray JNICALL
Java_com_vstghost_NativeBridge_benchmark(JNIEnv* env, jclass) {
    double r[2];
    vg_native_bench(r);
    jdoubleArray out = env->NewDoubleArray(2);
    env->SetDoubleArrayRegion(out, 0, 2, r);
    return out;
}
