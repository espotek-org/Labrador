// End-to-end librador bulk-transport test: connect exactly like the app,
// stream in modes 0 and 2, quiet and with the signal generator driving the
// looped-back scope input, and report the frame-validity counters.
#include <cstdio>
#include <cstdint>
#include <thread>
#include <chrono>
#include "librador.h"

static void stream_phase(const char *label, int seconds) {
    librador_reset_frame_stats();
    uint64_t ok = 0, bad = 0, dropped = 0, unval = 0;
    for (int s = 0; s < seconds; s++) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        librador_get_frame_stats(&ok, &bad, &dropped, &unval);
        printf("  [%s t=%ds] ok=%llu bad=%llu dropped=%llu unvalidated=%llu\n",
               label, s + 1, (unsigned long long)ok, (unsigned long long)bad,
               (unsigned long long)dropped, (unsigned long long)unval);
        fflush(stdout);
    }
    double total = (double)(ok + bad);
    printf("  [%s] FINAL pass rate: %.2f%% (%llu/%llu), dropped=%llu unvalidated=%llu\n",
           label, total > 0 ? 100.0 * ok / total : 0.0,
           (unsigned long long)ok, (unsigned long long)(ok + bad),
           (unsigned long long)dropped, (unsigned long long)unval);
}

int main() {
    if (librador_init(LABRADOR_TRANSPORT_AUTO) < 0) {
        fprintf(stderr, "librador_init failed\n");
        return 1;
    }
    int rc = librador_connect();
    if (rc < 0 || !librador_is_connected()) {
        fprintf(stderr, "librador_connect failed: %d\n", rc);
        return 1;
    }
    printf("[+] connected, active transport = %d (3 = bulk)\n",
           librador_get_active_transport());

    librador_set_device_mode(0);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    stream_phase("mode 0, quiet", 6);

    // Drive the signal generator (scope input is looped back to it): the
    // app's normal condition, and the one that exposed the stride bug.
    unsigned char wave[20];
    for (int i = 0; i < 20; i++) wave[i] = (i < 10) ? 40 : 215;
    librador_update_signal_gen_settings(1, wave, 20, 141.2, 2.5, 0.5);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    stream_phase("mode 0, fgen ", 6);

    librador_set_device_mode(2);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    stream_phase("mode 2, fgen ", 6);

    // Sanity: samples must actually reach the scope buffers.
    std::vector<double> *data = librador_get_analog_data(1, 0.1, 1000, 0.0, 0);
    printf("[+] analog readback: %zu samples%s\n",
           data ? data->size() : 0,
           (data && data->size() > 10) ? " (dispatch path OK)" : " (EMPTY - dispatch broken?)");

    librador_exit();
    return 0;
}
