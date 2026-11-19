plugins {
    id("com.android.application")
}

// Output of payload/build.sh (see payload/README.md). Optional at build time:
// without it the APK still builds, and the app reports "no payload".
val payloadOut = rootProject.file(providers.gradleProperty("vstghost.payloadOut").get())

android {
    namespace = "com.vstghost"
    compileSdk = 37  // the AAP library (Phase 3) requires compiling against API 37
    ndkVersion = "29.0.14206865"

    defaultConfig {
        applicationId = "com.vstghost"
        minSdk = 29  // required by the AAP library (Phase 3)
        versionCode = 1
        versionName = "0.0.1-phase0"
        ndk { abiFilters += "arm64-v8a" }
        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++20"
                arguments += "-DANDROID_STL=c++_shared"  // required by Oboe's prefab package
                // Phase 5: build the native plugins from local checkouts instead of pinned archives.
                (project.findProperty("vg.pluginSrcRoot") as String?)?.let { arguments += "-DVG_PLUGIN_SRC_ROOT=$it" }
                (project.findProperty("vg.nativePlugins") as String?)?.let { arguments += "-DVG_NATIVE_PLUGINS=$it" }
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

    buildFeatures {
        buildConfig = true
        prefab = true  // Oboe's native headers/libs
    }

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

dependencies {
    // Low-latency audio (Apache-2.0), named in the project brief.
    implementation("com.google.oboe:oboe:1.11.0")
    // Audio Plugins For Android (MIT): our bridge slots are AAP plugins (Phase 3).
    implementation("org.androidaudioplugin:androidaudioplugin:0.12.0")
    // Phase 5: document-start scripts for native plugins' web UIs (JUCE's Android bridge setup).
    implementation("androidx.webkit:webkit:1.12.1")
    // Player UI (Material 3 components, Apache-2.0).
    implementation("com.google.android.material:material:1.12.0")
    implementation("androidx.appcompat:appcompat:1.7.0")
}

kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
    }
}
