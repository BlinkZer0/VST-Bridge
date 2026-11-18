// SPDX-License-Identifier: GPL-3.0-or-later
package com.vstghost

import android.app.Application

/**
 * Loads the native library before any activity or service runs. That matters for AAP: an AAP
 * host can start our process just by binding AudioPluginService, and the AAP runtime then
 * dlopen()s libvstghost.so. Loading it from Java first means JNI_OnLoad runs with the app's
 * class loader, so the native AAP glue can call back into [SlotManager].
 */
class VstGhostApp : Application() {
    override fun onCreate() {
        super.onCreate()
        System.loadLibrary("vstghost")
        SlotManager.init(this)
    }
}
