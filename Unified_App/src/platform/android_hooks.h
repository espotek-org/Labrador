#pragma once

// Registers the JNI-backed librador host hooks (firmware dialogs, media scan,
// content-URI DAQ streams, APK asset extraction). Android builds only —
// call from main() before librador is used.
#ifdef __ANDROID__
void librador_register_android_hooks();
#endif
