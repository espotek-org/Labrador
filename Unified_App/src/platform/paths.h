#pragma once
#include <string>
#include <vector>

// Absolute path to a bundled read-only asset (fonts, pinout images, waveforms).
// Desktop only — Android assets live inside the APK and have no path; use
// loadAsset there. Throws std::runtime_error if the asset does not exist — a
// missing asset is a packaging bug, not a recoverable condition.
std::string getResourcePath(const std::string& relative);

// Load a bundled asset fully into memory. Works on every platform — on
// Android this reads from the APK via SDL's asset-manager-aware IO. Throws
// std::runtime_error if the asset is missing.
std::vector<unsigned char> loadAsset(const std::string& relative);

// Per-user writable directory for settings/calibration. Created on first call.
std::string getPrefPath();
