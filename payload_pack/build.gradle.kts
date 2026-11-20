// Play Asset Delivery: the Wine/Box64 payload archive (~84 MB) as an install-time asset pack, so
// the app bundle stays under Play's base-module limit. Install-time packs are part of the app's
// AssetManager, so Payload.openSource() reads it exactly like an ordinary asset. Local APK builds
// (assemble…) merge the pack into the APK.
//
// The archive is payload/build.sh's output (payload/out/play-assets); it is synced into this
// module's assets folder (git-ignored) before the pack is packaged.
plugins {
    id("com.android.asset-pack")
}

assetPack {
    packName.set("payload")
    dynamicDelivery {
        deliveryType.set("install-time")
    }
}

val payloadOut = rootProject.file(providers.gradleProperty("vstghost.payloadOut").get())
val syncPayload = tasks.register<Sync>("syncPayload") {
    from(File(payloadOut, "play-assets"))
    into(layout.projectDirectory.dir("src/main/assets"))
}
tasks.configureEach {
    if (name != "syncPayload") dependsOn(syncPayload)
}
