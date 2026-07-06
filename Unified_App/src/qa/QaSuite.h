#pragma once

// Dear ImGui Test Engine wiring (QA builds only: cmake -DLABRADOR_QA=ON).
// AppBase::Run drives these hooks; the tests live in QaSuite.cpp.
//
// Modes:
//  - Interactive: run a QA build normally; the Test Engine windows are shown
//    alongside the app, tests can be run/inspected by hand.
//  - Headless: `labrador --qa[=<filter>]` queues the matching tests (default
//    "tests" = everything except perf), runs them at fast speed, prints a
//    summary and exits non-zero on failure. The "hw/" group needs a Labrador
//    board with SG1 wired to OSC1 and SG2 wired to OSC2; those tests skip
//    (with a warning) when no board is connected.
#ifdef LABRADOR_QA

// Create + start the engine, register tests. run_filter == nullptr means
// interactive mode; otherwise queue tests matching the filter and run.
void QaSetup(const char* run_filter);

// Show the interactive Test Engine windows (call inside the ImGui frame).
void QaDrawUI();

// Per-frame post-render hook (call right after SDL_GL_SwapWindow).
void QaPostSwap();

// Headless run: has the queue drained?
bool QaFinished();

// Print the result summary; 0 if every queued test passed, 1 otherwise.
int QaReportAndExitCode();

// Stop the engine (end of Run, while the ImGui context is still alive).
void QaShutdown();

// Prediction-QA capture: a running test asks the render loop to dump the next
// rendered frame's framebuffer to `path` (binary PPM). Lets a scenario grab a
// screenshot before and after an interaction within one session.
void QaRequestFrameDump(const char* path);
// Render loop consumes a pending request: returns the path (once) or nullptr.
const char* QaConsumeFrameDump();

#endif // LABRADOR_QA
