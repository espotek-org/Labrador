#pragma once

// Android system-UI queries (JNI into MainActivity). Android builds only.
// The status/navigation bar heights are in physical pixels and can change at
// runtime (rotation, gesture nav), so query them each frame.
#ifdef __ANDROID__
float androidGetDpi();
int androidStatusBarHeight();
int androidNavigationBarHeight();
#endif
