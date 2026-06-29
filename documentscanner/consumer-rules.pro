# Keep the JNI bridge and its native methods so R8 doesn't rename/remove the symbols
# the native libdocumentscanner.so resolves by name.
-keep class com.websitebeaver.documentscanner.NativeOpenCV { *; }
-keepclasseswithmembernames class * {
    native <methods>;
}
