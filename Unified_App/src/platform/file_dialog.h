#pragma once

#include <functional>

// Async native save dialog (SDL3). Replaces the blocking nfd calls from the
// Monash codebase. on_done runs on the MAIN THREAD during a later frame,
// with the chosen path, or nullptr if the user cancelled.
//
// Call-site transform from nfd:
//   nfdchar_t* path; if (NFD_SaveDialog("csv", ...) == NFD_OKAY) { use(path); }
// becomes:
//   ShowSaveFileDialog("csv", [=](const char* path) { if (path) use(path); });
void ShowSaveFileDialog(const char* extension, std::function<void(const char* path)> on_done);

// Async native OPEN dialog (SDL3), mirror of ShowSaveFileDialog. Lets the user
// pick a single existing file. on_done runs on the MAIN THREAD during a later
// frame with the chosen path, or nullptr if the user cancelled.
//
//   ShowOpenFileDialog("txt", [=](const char* path) { if (path) load(path); });
void ShowOpenFileDialog(const char* extension, std::function<void(const char* path)> on_done);

// Drain completed dialog callbacks onto the main thread. AppBase calls this
// once per frame; nothing else should.
void PumpFileDialogResults();
