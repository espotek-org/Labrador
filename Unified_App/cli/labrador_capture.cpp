// Headless triggered capture: configure the scope, arm a trigger, wait, and
// write the full-rate buffer around the trigger as CSV (t,CH1[,CH2]; t in
// seconds, 0 = trigger sample) plus a JSON sidecar.
//
//   labrador-capture --mode scope2 --gain 4 --trigger ch1:rising:3.0 \
//       --pre 0.2 --post 0.3 --out capture.csv
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include "librador.h"

static void usage(const char *argv0) {
    fprintf(stderr,
        "usage: %s --out FILE [options]\n"
        "  --mode scope1|scope2     scope1 = CH1 only, scope2 = CH1+CH2 (default scope2)\n"
        "  --gain G                 0.5|1|2|4|8|16|32|64 (default 4; 6 V signals need <= 4)\n"
        "  --trigger CH:EDGE:VOLTS  e.g. ch1:rising:3.0 (default ch1:rising:3.0)\n"
        "  --hysteresis V           re-arm distance from the level (default 0.25)\n"
        "  --pre S / --post S       seconds kept before / after the trigger (default 0.2 / 0.3)\n"
        "  --timeout S              give up waiting for the trigger (default 0 = forever)\n"
        "  --settle S               wait after configuring the scope (default 0.5)\n"
        "  --out FILE               CSV path; FILE.json gets the metadata\n", argv0);
}

int main(int argc, char **argv) {
    std::string out, mode = "scope2";
    double gain = 4, pre = 0.2, post = 0.3, timeout = 0, settle = 0.5, hyst = 0.25;
    librador_capture_request req;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        auto next = [&]() -> const char * {
            if (i + 1 >= argc) { usage(argv[0]); exit(2); }
            return argv[++i];
        };
        if (!strcmp(a, "--out")) out = next();
        else if (!strcmp(a, "--mode")) mode = next();
        else if (!strcmp(a, "--gain")) gain = atof(next());
        else if (!strcmp(a, "--pre")) pre = atof(next());
        else if (!strcmp(a, "--post")) post = atof(next());
        else if (!strcmp(a, "--timeout")) timeout = atof(next());
        else if (!strcmp(a, "--settle")) settle = atof(next());
        else if (!strcmp(a, "--hysteresis")) hyst = atof(next());
        else if (!strcmp(a, "--trigger")) {
            char edge[16];
            int ch;
            double level;
            if (sscanf(next(), "ch%d:%15[a-z]:%lf", &ch, edge, &level) != 3 ||
                (ch != 1 && ch != 2) || (strcmp(edge, "rising") && strcmp(edge, "falling"))) {
                fprintf(stderr, "bad --trigger, expected ch1|ch2:rising|falling:VOLTS\n");
                return 2;
            }
            req.trigger_channel = ch;
            req.rising = !strcmp(edge, "rising");
            req.level_v = level;
        } else { usage(argv[0]); return 2; }
    }
    if (out.empty() || (mode != "scope1" && mode != "scope2")) { usage(argv[0]); return 2; }
    req.pre_s = pre;
    req.post_s = post;
    req.timeout_s = timeout;
    req.hysteresis_v = hyst;
    if (mode == "scope1" && req.trigger_channel == 2) {
        fprintf(stderr, "trigger on ch2 needs --mode scope2\n");
        return 2;
    }

    if (librador_init(LABRADOR_TRANSPORT_AUTO) < 0) { fprintf(stderr, "librador_init failed\n"); return 1; }
    int rc = librador_connect();
    if (rc < 0 || !librador_is_connected()) { fprintf(stderr, "librador_connect failed: %d\n", rc); return 1; }

    if (librador_set_oscilloscope_gain(gain) < 0) { fprintf(stderr, "invalid gain %g\n", gain); librador_exit(); return 2; }
    librador_set_device_mode(mode == "scope1" ? 0 : 2);
    std::this_thread::sleep_for(std::chrono::duration<double>(settle));

    fprintf(stderr, "armed: ch%d %s through %.3f V, pre %.3f s, post %.3f s\n",
            req.trigger_channel, req.rising ? "rising" : "falling", req.level_v, pre, post);
    fflush(stderr);

    librador_capture_result res;
    rc = librador_capture_around_trigger(&req, &res);
    if (rc != 0) {
        fprintf(stderr, "capture failed: %d (-3 no stream, -4 bad request, -5 timeout, -6 stream restarted)\n", rc);
        librador_exit();
        return 1;
    }

    FILE *f = fopen(out.c_str(), "w");
    if (!f) { perror(out.c_str()); librador_exit(); return 1; }
    const bool two = !res.ch2.empty();
    fprintf(f, two ? "t,CH1,CH2\n" : "t,CH1\n");
    for (size_t i = 0; i < res.ch1.size(); i++) {
        double t = ((double)i - res.pre_samples) / res.sample_rate_hz;
        if (two) fprintf(f, "%.7f,%.4f,%.4f\n", t, res.ch1[i], res.ch2[i]);
        else     fprintf(f, "%.7f,%.4f\n", t, res.ch1[i]);
    }
    fclose(f);

    FILE *j = fopen((out + ".json").c_str(), "w");
    if (j) {
        fprintf(j,
            "{\n  \"sample_rate_hz\": %.0f,\n  \"samples\": %zu,\n  \"pre_samples\": %d,\n"
            "  \"trigger\": {\"channel\": %d, \"edge\": \"%s\", \"level_v\": %g, \"hysteresis_v\": %g},\n"
            "  \"gain\": %g,\n  \"mode\": \"%s\",\n"
            "  \"frames_bad_checksum\": %llu,\n  \"frames_dropped\": %llu\n}\n",
            res.sample_rate_hz, res.ch1.size(), res.pre_samples, req.trigger_channel,
            req.rising ? "rising" : "falling", req.level_v, req.hysteresis_v, gain, mode.c_str(),
            (unsigned long long)res.frames_bad_checksum, (unsigned long long)res.frames_dropped);
        fclose(j);
    }

    printf("wrote %s: %zu samples at %.0f Hz, trigger at t=0 (sample %d)%s\n", out.c_str(), res.ch1.size(),
           res.sample_rate_hz, res.pre_samples, two ? ", CH1+CH2" : ", CH1 only");
    if (res.frames_bad_checksum || res.frames_dropped)
        printf("warning: %llu bad-checksum and %llu dropped frames during the capture (repeated samples in the window)\n",
               (unsigned long long)res.frames_bad_checksum, (unsigned long long)res.frames_dropped);

    librador_exit();
    return 0;
}
