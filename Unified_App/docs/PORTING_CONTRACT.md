# Porting contract: Monash Labrador_ImGui widgets → Unified_App

Source files: `/Users/chrises/git/Labrador_ImGui/src/<file>`
Destination:  `/Users/chrises/git/labrador/Unified_App/src/instruments/<file>` (same filename)

The goal is a faithful port: behaviour and visual layout IDENTICAL to the Monash
app (its desktop layout is the tested, approved reference). Only make the
mechanical adaptations listed here. Do not refactor, do not rename, do not
"improve" logic, do not add features. Keep the original comments.

## Environment differences

| Was (Monash) | Is (Unified_App) |
|---|---|
| SDL2 | SDL3 (only matters if the file touches SDL directly — most don't) |
| ImGui 1.90.6 | ImGui 1.92.7 (Brent's fork with `custom_imgui.h` available) |
| ImPlot 0.17 | ImPlot fork (~master 2024/25 base, reports version "1.0") |
| FFTW3 | keep `#include "fftw3.h"` UNCHANGED — a compatible shim exists at `src/instruments/fftw3.h` |
| nfd (blocking file dialog) | `platform/file_dialog.h` — async, see transform below |
| `getResourcePath` defined in util.cpp | now in `platform/paths.h` — include that instead; DELETE the Monash definition from util.cpp and the declaration from util.h |
| `LoadTextureFromFile/FromMemory` in AppBase.hpp | now in `app/textures.h` — include that where used (signature: `unsigned int*` instead of `GLuint*`) |
| vendored librador (older API) | merged librador — same core API; header at `librador/librador.h`, just `#include "librador.h"` (include path is set) |

## Mechanical rules

1. Includes among ported files stay unchanged (`"util.h"`, `"OscData.hpp"`, … — all
   files land in the same `src/instruments/` directory).
2. `#include "exprtk.hpp"` stays (deps dir is on the include path).
3. `#include <SDL.h>` (if any) → `#include <SDL3/SDL.h>`.
4. Do NOT define `IMGUI_DEFINE_MATH_OPERATORS` after including imgui — if the file
   defines it, keep it before the first imgui include.
5. ImGui 1.92 differences that may need touching:
   - `io.Fonts->Build()` — delete the call (fonts build lazily now).
   - `io.FontGlobalScale` — does not exist; if a widget references it, replace the
     whole expression with `1.0f` and add a `// 1.92: global scale handled by style` comment.
   - `PushFont(font)` still works. `GetFont()`, `GetFontSize()` still work.
   - `ImGuiKey_*` unchanged.
   - If a call fails to exist in 1.92, check `Unified_App/deps/imgui/imgui.h` for
     the replacement and note it in your report.
6. ImPlot fork differences: API is largely the same. Check calls against
   `Unified_App/deps/implot/implot.h` if unsure. Note anything you had to change.
7. nfd transform (blocking → async). Replace:
   ```cpp
   nfdchar_t* path = nullptr;
   nfdresult_t result = NFD_SaveDialog(fileExtension, nullptr, &path);
   if (result == NFD_OKAY && path) { <BODY using path>; }
   ```
   with:
   ```cpp
   #include "platform/file_dialog.h"
   ShowSaveFileDialog(fileExtension, [=, &state](const char* path) {
       if (path) { <BODY using path>; }
   });
   ```
   Capture heavy data (vectors) by value, long-lived widget members by reference.
   Delete `#include "nfd.h"` and any `NFD_Init/NFD_Quit/NFD_GetError` calls
   (debug prints of dialog results can be dropped).
8. Firmware constants: if the file references `constants::DESIRED_FW_VERSION` /
   `DESIRED_FW_VARIANT`, keep them (util.h defines them; they mirror
   `EXPECTED_FIRMWARE_VERSION`/`DEFINED_EXPECTED_VARIANT` in usbcallhandler.h).
9. Connection API: `librador_setup_usb()` no longer exists. If a widget calls it,
   replace with `librador_connect()` and note it. (App owns connection polling.)
10. C++17, compile-clean under clang. No new dependencies.

## Report format

Return: per file — list of every adaptation you made beyond rule 1-3 boilerplate,
plus anything you were unsure about. No code in the report, just the deltas.
Do NOT attempt to build anything; integration and compiling is handled centrally.
