plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// Output of payload/build.sh (see payload/README.md). Optional at build time:
// without it the APK still builds, and the app reports "no payload".
val payloadOut = rootProject.file(providers.gradleProperty("vstghost.payloadOut").get())

android {
    namespace = "com.vstghost"
    compileSdk = 36
    ndkVersion = "29.0.14206865"

    defaultConfig {
        applicationId = "com.vstghost"
        minSdk = 28
        versionCode = 1
        versionName = "0.0.1-phase0"
        ndk { abiFilters += "arm64-v8a" }
        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++20"
                arguments += "-DANDROID_STL=c++_static"
            }
        }
    }

    flavorDimensions += "edition"
    productFlavors {
        // Sideload edition: targetSdk 28 so binaries extracted to filesDir may be exec'd
        // (the Winlator model). Never published to Google Play.
        create("sideload") {
            dimension = "edition"
            applicationIdSuffix = ".sideload"
            targetSdk = 28
            buildConfigField("String", "EDITION", "\"sideload\"")
        }
        // Play edition: current targetSdk. arm64 executables must live in nativeLibraryDir
        // (jniLibs); the x86_64 payload ships inside the APK as an asset and is only ever
        // interpreted/translated by Box64.
        create("play") {
            dimension = "edition"
            targetSdk = 36
            buildConfigField("String", "EDITION", "\"play\"")
        }
    }

    sourceSets {
        getByName("play") {
            jniLibs.srcDir(File(payloadOut, "jnilibs"))
            assets.srcDir(File(payloadOut, "play-assets"))
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.31.6"
        }
    }

    buildFeatures { buildConfig = true }

    packaging {
        // Extract .so files to nativeLibraryDir on install: the play edition execs
        // libldlinux.so / libbox64.so from there.
        jniLibs.useLegacyPackaging = true
        // Ship the payload's glibc/Box64 binaries byte-for-byte (don't let AGP llvm-strip them).
        jniLibs.keepDebugSymbols += listOf("**/libldlinux.so", "**/libbox64.so", "**/lib*_so_*.so")
    }

    androidResources {
        noCompress += listOf("zst")
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    lint {
        // The sideload flavor deliberately targets API 28.
        disable += listOf("ExpiredTargetSdkVersion", "OldTargetApi")
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
    }
}
