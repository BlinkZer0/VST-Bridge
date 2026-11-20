# VST gHost release build (R8). The app talks to its native code through JNI: keep every class
# that declares native methods or is looked up by name from C++/JavaScript.

# Classes with native methods keep their names and members (JNI registration by name).
-keepclasseswithmembernames,includedescriptorclasses class * {
    native <methods>;
}
-keep class com.vstghost.NativeEngine { *; }
-keep class com.vstghost.NativeBridge { *; }
-keep class com.vstghost.ProgressListener { *; }
# Looked up with FindClass from the JUCE adapter (nested modal loops).
-keep class com.vstghost.JuceModalLoop { *; }
# The virtual MIDI device services and the AAP service are referenced from the manifest (kept by
# default); the AAP library ships its own consumer rules.

# Kotlin metadata is not needed at runtime; stack traces stay readable with the mapping file.
-keepattributes SourceFile,LineNumberTable
-renamesourcefileattribute SourceFile
