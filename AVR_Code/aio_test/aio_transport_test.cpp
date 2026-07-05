// AIO firmware transport test harness.
//
// Exercises the three transports of the Labrador AIO firmware (variant 0x03)
// and validates frame ordering and payload integrity using the per-frame
// headers:
//   bulk (iface 2):  in-stream header 0xEB 0x57 ahead of each 750-byte payload
//   iso1 (iface 1):  1023-byte iso data EP + 8-byte meta EP (0xEB 0x58, lag-1)
//   iso6 (iface 0):  6x 128-byte iso data EPs + 8-byte meta EP (lag-1)
//
// The board input is unwired (noise); validation is content-agnostic:
//   - sequence continuity (drops appear as gaps)
//   - XOR checksum match (ADC/DMA stomps appear as mismatches)
//
// Build: make   (see Makefile in this directory)
// Run:   ./aio_transport_test [bulk|iso1|iso6|all] [seconds]

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <chrono>
#include <deque>
#include <libusb.h>

#define VID 0x03eb
#define PID 0xba94

#define PACKET_SIZE 750
#define HDR_MAGIC0  0xEB
#define HDR_MAGIC1_BULK 0x57
#define HDR_MAGIC1_META 0x58

static libusb_device_handle *g_h = nullptr;

struct Stats {
    uint64_t frames = 0;
    uint64_t bytes = 0;
    uint64_t seq_gaps = 0;
    uint64_t seq_gap_frames = 0;   // total frames lost to gaps
    uint64_t csum_ok = 0;
    uint64_t csum_bad = 0;
    uint64_t hdr_bad = 0;
    bool have_prev_seq = false;
    uint16_t prev_seq = 0;

    void note_seq(uint16_t seq) {
        if (have_prev_seq) {
            uint16_t expect = (uint16_t)(prev_seq + 1);
            if (seq != expect) {
                seq_gaps++;
                seq_gap_frames += (uint16_t)(seq - expect);
            }
        }
        have_prev_seq = true;
        prev_seq = seq;
    }
    void report(const char *name, double secs) const {
        printf("---- %s results (%.1fs) ----\n", name, secs);
        printf("  frames received : %llu (%.1f/s, %.1f KB/s payload)\n",
               (unsigned long long)frames, frames / secs, bytes / secs / 1024.0);
        printf("  sequence gaps   : %llu (%llu frames skipped)\n",
               (unsigned long long)seq_gaps, (unsigned long long)seq_gap_frames);
        printf("  checksum ok     : %llu\n", (unsigned long long)csum_ok);
        printf("  checksum BAD    : %llu\n", (unsigned long long)csum_bad);
        printf("  bad headers     : %llu\n", (unsigned long long)hdr_bad);
        if (csum_ok + csum_bad > 0)
            printf("  csum pass rate  : %.2f%%\n",
                   100.0 * csum_ok / (double)(csum_ok + csum_bad));
    }
};

static uint8_t xor_csum(const uint8_t *p, size_t n) {
    uint8_t c = 0;
    for (size_t i = 0; i < n; i++) c ^= p[i];
    return c;
}

static void dump_aio_debug(const char *when) {
    uint8_t d[8] = {0};
    int r = libusb_control_transfer(g_h, 0xC0, 0xab, 0, 0, d, 8, 1000);
    if (r != 8) {
        printf("[dbg %s] 0xab failed: %d\n", when, r);
        return;
    }
    printf("[dbg %s] transport=%u armfail=0x%02x iso_cb=%u meta_cb=%u "
           "bulkhdr_cb=%u bulkpay_cb=%u usb_state=%u mode=%u\n",
           when, d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
}

static bool set_mode(uint8_t mode, uint16_t gain_idx) {
    int r = libusb_control_transfer(g_h, 0x40, 0xa5, mode, gain_idx, nullptr, 0, 1000);
    if (r < 0) {
        fprintf(stderr, "set_mode(%u): %s\n", mode, libusb_error_name(r));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- bulk ----

static void test_bulk(int seconds, Stats &st) {
    printf("\n=== BULK transport (iface 2, EP 0x88) ===\n");
    if (libusb_claim_interface(g_h, 2)) { fprintf(stderr, "claim(2) failed\n"); return; }
    if (libusb_set_interface_alt_setting(g_h, 2, 1)) {
        fprintf(stderr, "set_alt(2,1) failed\n");
        libusb_release_interface(g_h, 2);
        return;
    }
    printf("[+] alt setting 1 selected, streaming...\n");
    dump_aio_debug("post-alt");

    std::deque<uint8_t> stream;
    std::vector<uint8_t> buf(16384);
    auto t0 = std::chrono::steady_clock::now();
    auto deadline = t0 + std::chrono::seconds(seconds);
    uint64_t raw_bytes = 0;

    while (std::chrono::steady_clock::now() < deadline) {
        int got = 0;
        int r = libusb_bulk_transfer(g_h, 0x88, buf.data(), (int)buf.size(), &got, 250);
        if (r && r != LIBUSB_ERROR_TIMEOUT) {
            fprintf(stderr, "bulk_transfer: %s\n", libusb_error_name(r));
            break;
        }
        raw_bytes += got;
        stream.insert(stream.end(), buf.begin(), buf.begin() + got);
        // Interleave variant polls like the app's wedge check does.
        {
            static int poll_ctr = 0;
            static int poll_fails = 0;
            if (++poll_ctr % 20 == 0) {
                uint8_t v = 0;
                int pr = libusb_control_transfer(g_h, 0xC0, 0xa9, 0, 0, &v, 1, 1000);
                if (pr != 1 && ++poll_fails <= 5)
                    printf("[!] 0xa9 poll failed: %d (%s)\n", pr, pr < 0 ? libusb_error_name(pr) : "short");
                if (poll_ctr % 400 == 0)
                    printf("[i] 0xa9 polls: %d, failures: %d\n", poll_ctr / 20, poll_fails);
            }
        }

        // Parse frames: [EB 57 seqL seqH lenL lenH csum mode] + payload(len)
        while (true) {
            // resync to magic
            while (stream.size() >= 2 &&
                   !(stream[0] == HDR_MAGIC0 && stream[1] == HDR_MAGIC1_BULK)) {
                stream.pop_front();
                st.hdr_bad++;
            }
            if (stream.size() < 8) break;
            uint16_t seq = stream[2] | (stream[3] << 8);
            uint16_t len = stream[4] | (stream[5] << 8);
            uint8_t csum = stream[6];
            if (len != PACKET_SIZE) { // corrupt header, resync
                stream.pop_front();
                st.hdr_bad++;
                continue;
            }
            if (stream.size() < (size_t)(8 + len)) break;
            uint8_t actual = 0;
            for (size_t i = 0; i < len; i++) actual ^= stream[8 + i];
            st.note_seq(seq);
            st.frames++;
            st.bytes += len;
            if (actual == csum) st.csum_ok++; else st.csum_bad++;
            stream.erase(stream.begin(), stream.begin() + 8 + len);
        }
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("[i] raw bulk bytes: %llu (%.1f KB/s incl. headers)\n",
           (unsigned long long)raw_bytes, raw_bytes / secs / 1024.0);
    st.report("bulk", secs);
    libusb_set_interface_alt_setting(g_h, 2, 0);
    libusb_release_interface(g_h, 2);
}

// ----------------------------------------------------------------- iso -----

struct IsoCtx {
    std::vector<std::vector<uint8_t>> frames;  // per completed frame slot: raw data
    uint64_t packets = 0;
    uint64_t zero_packets = 0;
    uint64_t errors = 0;
    bool dead = false;
};

// Meta layout: EB 58 seqL seqH csum_half0 csum_half1 usb_state mode.
// The device reports the checksum of BOTH double-buffer halves; a frame is
// valid if its payload matches either (which half a frame was armed from is
// phase-dependent), and stomped if it matches neither.
struct MetaRec { uint16_t seq; uint8_t csum0; uint8_t csum1; };

struct IsoStream {
    libusb_transfer *xfer = nullptr;
    std::vector<uint8_t> buf;
    IsoCtx ctx;
    uint8_t ep;
    int pkt_size;
    int npackets;
    volatile bool stopping = false;
    volatile bool done = false;
};

static void iso_cb(libusb_transfer *t) {
    IsoStream *s = (IsoStream *)t->user_data;
    if (t->status != LIBUSB_TRANSFER_COMPLETED || s->stopping) {
        s->done = true;
        return;
    }
    for (int i = 0; i < t->num_iso_packets; i++) {
        auto &pd = t->iso_packet_desc[i];
        s->ctx.packets++;
        std::vector<uint8_t> pkt;
        if (pd.status == LIBUSB_TRANSFER_COMPLETED && pd.actual_length > 0) {
            uint8_t *p = libusb_get_iso_packet_buffer_simple(t, i);
            pkt.assign(p, p + pd.actual_length);
        } else if (pd.status != LIBUSB_TRANSFER_COMPLETED) {
            s->ctx.errors++;
        } else {
            s->ctx.zero_packets++;
        }
        s->ctx.frames.push_back(std::move(pkt));
    }
    if (libusb_submit_transfer(t)) {
        s->ctx.dead = true;
        s->done = true;
    }
}

static bool iso_start(IsoStream &s, uint8_t ep, int pkt_size, int npackets) {
    s.ep = ep;
    s.pkt_size = pkt_size;
    s.npackets = npackets;
    s.buf.resize((size_t)pkt_size * npackets);
    s.xfer = libusb_alloc_transfer(npackets);
    if (!s.xfer) return false;
    libusb_fill_iso_transfer(s.xfer, g_h, ep, s.buf.data(), (int)s.buf.size(),
                             npackets, iso_cb, &s, 1000);
    libusb_set_iso_packet_lengths(s.xfer, pkt_size);
    return libusb_submit_transfer(s.xfer) == 0;
}

// Pair meta records (lag-1) with reassembled data frames by checksum match
// within a small alignment window; report best-window match rate.
static void iso_validate(const char *name, double secs,
                         std::vector<std::vector<uint8_t>> &data_frames,
                         std::vector<MetaRec> &metas, Stats &st) {
    // Sequence continuity from the meta stream itself.
    for (auto &m : metas) st.note_seq(m.seq);
    st.frames = data_frames.size();
    for (auto &f : data_frames) st.bytes += f.size();

    // Alignment search: for lag L in [-3, 3], count checksum matches of
    // meta[i] against data_frame[i + L].
    int best_lag = 0;
    uint64_t best_ok = 0, best_tot = 0;
    for (int lag = -3; lag <= 3; lag++) {
        uint64_t ok = 0, tot = 0;
        for (size_t i = 0; i < metas.size(); i++) {
            long j = (long)i + lag;
            if (j < 0 || (size_t)j >= data_frames.size()) continue;
            if (data_frames[j].empty()) continue;
            tot++;
            uint8_t c = xor_csum(data_frames[j].data(), data_frames[j].size());
            if (c == metas[i].csum0 || c == metas[i].csum1)
                ok++;
        }
        if (ok > best_ok) { best_ok = ok; best_tot = tot; best_lag = lag; }
    }
    st.csum_ok = best_ok;
    st.csum_bad = best_tot - best_ok;
    printf("[i] meta records: %zu, data frames: %zu, best alignment lag: %d\n",
           metas.size(), data_frames.size(), best_lag);
    st.report(name, secs);
}

static void parse_meta(IsoCtx &meta_ctx, std::vector<MetaRec> &metas, Stats &st) {
    for (auto &pkt : meta_ctx.frames) {
        if (pkt.empty()) continue;   // frame with no meta (allowed)
        if (pkt.size() != 8 || pkt[0] != HDR_MAGIC0 || pkt[1] != HDR_MAGIC1_META) {
            st.hdr_bad++;
            continue;
        }
        metas.push_back({(uint16_t)(pkt[2] | (pkt[3] << 8)), pkt[4], pkt[5]});
    }
}

static void run_iso_events(int seconds) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        timeval tv{0, 100000};
        libusb_handle_events_timeout(nullptr, &tv);
    }
}

static void iso_stop(IsoStream &s) {
    if (!s.xfer) return;
    s.stopping = true;
    if (libusb_cancel_transfer(s.xfer) != 0 && s.ctx.dead) {
        s.done = true;  // transfer was never resubmitted, nothing in flight
    }
    for (int i = 0; i < 20 && !s.done; i++) {
        timeval tv{0, 100000};
        libusb_handle_events_timeout(nullptr, &tv);
    }
    libusb_free_transfer(s.xfer);
    s.xfer = nullptr;
}

static void test_iso1(int seconds, Stats &st) {
    printf("\n=== ISO1 transport (iface 1, EP 0x87 + meta 0x8a) ===\n");
    if (libusb_claim_interface(g_h, 1)) { fprintf(stderr, "claim(1) failed\n"); return; }
    if (libusb_set_interface_alt_setting(g_h, 1, 1)) {
        fprintf(stderr, "set_alt(1,1) failed\n");
        libusb_release_interface(g_h, 1);
        return;
    }
    printf("[+] alt setting 1 selected, streaming...\n");

    dump_aio_debug("post-alt");
    IsoStream data, meta;
    auto t0 = std::chrono::steady_clock::now();
    if (!iso_start(data, 0x87, 1023, 32) || !iso_start(meta, 0x8a, 8, 32)) {
        fprintf(stderr, "iso_start failed\n");
    } else {
        run_iso_events(seconds);
    }
    dump_aio_debug("post-run");
    iso_stop(data);
    iso_stop(meta);
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    printf("[i] data: %llu pkts (%llu empty, %llu err)  meta: %llu pkts (%llu empty, %llu err)\n",
           (unsigned long long)data.ctx.packets, (unsigned long long)data.ctx.zero_packets,
           (unsigned long long)data.ctx.errors,
           (unsigned long long)meta.ctx.packets, (unsigned long long)meta.ctx.zero_packets,
           (unsigned long long)meta.ctx.errors);
    std::vector<MetaRec> metas;
    parse_meta(meta.ctx, metas, st);
    iso_validate("iso1", secs, data.ctx.frames, metas, st);
    libusb_set_interface_alt_setting(g_h, 1, 0);
    libusb_release_interface(g_h, 1);
}

static void test_iso6(int seconds, Stats &st) {
    printf("\n=== ISO6 transport (iface 0, EP 0x81-0x86 + meta 0x89) ===\n");
    if (libusb_claim_interface(g_h, 0)) { fprintf(stderr, "claim(0) failed\n"); return; }
    if (libusb_set_interface_alt_setting(g_h, 0, 1)) {
        fprintf(stderr, "set_alt(0,1) failed\n");
        libusb_release_interface(g_h, 0);
        return;
    }
    printf("[+] alt setting 1 selected, streaming...\n");

    dump_aio_debug("post-alt");
    IsoStream data[6], meta;
    auto t0 = std::chrono::steady_clock::now();
    bool ok = true;
    for (int i = 0; i < 6; i++)
        ok = ok && iso_start(data[i], (uint8_t)(0x81 + i), 128, 32);
    ok = ok && iso_start(meta, 0x89, 8, 32);
    if (!ok) {
        fprintf(stderr, "iso_start failed\n");
    } else {
        run_iso_events(seconds);
    }
    dump_aio_debug("post-run");
    for (int i = 0; i < 6; i++) iso_stop(data[i]);
    iso_stop(meta);
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    for (int i = 0; i < 6; i++)
        printf("[i] EP 0x%02x: %llu pkts (%llu empty, %llu err)\n", 0x81 + i,
               (unsigned long long)data[i].ctx.packets,
               (unsigned long long)data[i].ctx.zero_packets,
               (unsigned long long)data[i].ctx.errors);
    printf("[i] meta 0x89: %llu pkts (%llu empty, %llu err)\n",
           (unsigned long long)meta.ctx.packets,
           (unsigned long long)meta.ctx.zero_packets,
           (unsigned long long)meta.ctx.errors);

    // Reassemble frame k = concat(EP1[k] .. EP6[k]).  The six URB streams
    // start within a frame or two of one another; the checksum alignment
    // window in iso_validate absorbs the residual offset.
    size_t nframes = SIZE_MAX;
    for (int i = 0; i < 6; i++)
        nframes = std::min(nframes, data[i].ctx.frames.size());
    if (nframes == SIZE_MAX) nframes = 0;
    std::vector<std::vector<uint8_t>> whole(nframes);
    for (size_t k = 0; k < nframes; k++) {
        bool complete = true;
        size_t total = 0;
        for (int i = 0; i < 6; i++) {
            if (data[i].ctx.frames[k].empty()) { complete = false; break; }
            total += data[i].ctx.frames[k].size();
        }
        if (!complete || total != PACKET_SIZE) continue;  // leave empty -> skipped in validate
        auto &w = whole[k];
        w.reserve(PACKET_SIZE);
        for (int i = 0; i < 6; i++)
            w.insert(w.end(), data[i].ctx.frames[k].begin(), data[i].ctx.frames[k].end());
    }
    std::vector<MetaRec> metas;
    parse_meta(meta.ctx, metas, st);
    iso_validate("iso6", secs, whole, metas, st);
    libusb_set_interface_alt_setting(g_h, 0, 0);
    libusb_release_interface(g_h, 0);
}

// ------------------------------------------------------- signal content ----

// Collect a byte histogram of bulk payloads for `seconds`, print summary.
static void bulk_histogram(int seconds, const char *label) {
    std::vector<uint64_t> hist(256, 0);
    std::deque<uint8_t> stream;
    std::vector<uint8_t> buf(16384);
    uint64_t total = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        int got = 0;
        int r = libusb_bulk_transfer(g_h, 0x88, buf.data(), (int)buf.size(), &got, 250);
        if (r && r != LIBUSB_ERROR_TIMEOUT) break;
        stream.insert(stream.end(), buf.begin(), buf.begin() + got);
        while (true) {
            while (stream.size() >= 2 &&
                   !(stream[0] == HDR_MAGIC0 && stream[1] == HDR_MAGIC1_BULK))
                stream.pop_front();
            if (stream.size() < 8) break;
            uint16_t len = stream[4] | (stream[5] << 8);
            if (len != PACKET_SIZE) { stream.pop_front(); continue; }
            if (stream.size() < (size_t)(8 + len)) break;
            for (size_t i = 0; i < len; i++) { hist[stream[8 + i]]++; total++; }
            stream.erase(stream.begin(), stream.begin() + 8 + len);
        }
    }
    // Percentiles + two-cluster measure
    auto pct = [&](double p) {
        uint64_t target = (uint64_t)(p * total), acc = 0;
        for (int v = 0; v < 256; v++) { acc += hist[v]; if (acc >= target) return v; }
        return 255;
    };
    int p5 = pct(0.05), p50 = pct(0.50), p95 = pct(0.95);
    // fraction of samples within +/-8 of the two extreme percentiles
    uint64_t nearlo = 0, nearhi = 0;
    for (int v = 0; v < 256; v++) {
        if (abs(v - p5) <= 8) nearlo += hist[v];
        if (abs(v - p95) <= 8) nearhi += hist[v];
    }
    printf("[sig %s] samples=%llu p5=%d p50=%d p95=%d spread=%d "
           "near-lo=%.1f%% near-hi=%.1f%%\n",
           label, (unsigned long long)total, p5, p50, p95, p95 - p5,
           100.0 * nearlo / (double)total, 100.0 * nearhi / (double)total);
}

static void test_signal(void) {
    printf("\n=== Signal-gen content check (bulk transport) ===\n");
    if (libusb_claim_interface(g_h, 2)) { fprintf(stderr, "claim(2) failed\n"); return; }
    if (libusb_set_interface_alt_setting(g_h, 2, 1)) {
        fprintf(stderr, "set_alt(2,1) failed\n");
        libusb_release_interface(g_h, 2);
        return;
    }
    bulk_histogram(2, "baseline (sig gen idle)");

    // Square wave on CH1 signal gen: 20 samples alternating low/high,
    // TC period wValue, clock divider in wIndex low nibble (vendor req 0xa1).
    uint8_t wave[20];
    for (int i = 0; i < 20; i++) wave[i] = (i < 10) ? 0x20 : 0xE0;
    int r = libusb_control_transfer(g_h, 0x40, 0xa1, 750, 5, wave, sizeof(wave), 1000);
    if (r != (int)sizeof(wave)) {
        fprintf(stderr, "sig gen setup (0xa1): %d\n", r);
    } else {
        printf("[+] square wave loaded (50%% duty)\n");
        bulk_histogram(2, "square 50% duty");
    }
    // Differential check: 25% duty — the low/high sample split should track.
    for (int i = 0; i < 20; i++) wave[i] = (i < 15) ? 0x20 : 0xE0;
    r = libusb_control_transfer(g_h, 0x40, 0xa1, 750, 5, wave, sizeof(wave), 1000);
    if (r == (int)sizeof(wave)) {
        printf("[+] square wave loaded (25%% duty high)\n");
        bulk_histogram(2, "square 25% duty");
    }
    libusb_set_interface_alt_setting(g_h, 2, 0);
    libusb_release_interface(g_h, 2);
}

// Reproduce the app's threading pattern: async bulk transfers serviced by a
// dedicated event thread, control transfers issued from this thread.
#include <thread>
#include <atomic>
static std::atomic<bool> g_ev_stop{false};
static std::atomic<uint64_t> g_bulk_bytes{0};
static void ev_thread_fn() {
    while (!g_ev_stop) {
        timeval tv{0, 100000};
        libusb_handle_events_timeout(nullptr, &tv);
    }
}
static void bulk_async_cb(libusb_transfer *t) {
    if (t->status == LIBUSB_TRANSFER_COMPLETED || t->status == LIBUSB_TRANSFER_TIMED_OUT) {
        g_bulk_bytes += t->actual_length;
        if (!g_ev_stop) { if (libusb_submit_transfer(t)) {} }
    }
}
static void test_bulk_async_threaded(int seconds) {
    printf("\n=== BULK async + event thread + concurrent control (app pattern) ===\n");
    if (libusb_claim_interface(g_h, 2)) { fprintf(stderr, "claim(2) failed\n"); return; }
    if (libusb_set_interface_alt_setting(g_h, 2, 1)) {
        fprintf(stderr, "set_alt failed\n");
        libusb_release_interface(g_h, 2);
        return;
    }
    static unsigned char bufs[4][16384];
    libusb_transfer *xf[4];
    for (int i = 0; i < 4; i++) {
        xf[i] = libusb_alloc_transfer(0);
        libusb_fill_bulk_transfer(xf[i], g_h, 0x88, bufs[i], sizeof bufs[i], bulk_async_cb, nullptr, 250);
        libusb_submit_transfer(xf[i]);
    }
    g_ev_stop = false;
    std::thread ev(ev_thread_fn);
    int fails = 0, polls = 0, retr_ok = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        uint8_t v = 0;
        int pr = libusb_control_transfer(g_h, 0xC0, 0xa9, 0, 0, &v, 1, 1000);
        polls++;
        if (pr != 1) {
            fails++;
            if (fails <= 5) printf("[!] poll %d failed: %s\n", polls, pr < 0 ? libusb_error_name(pr) : "short");
            // immediate retry - EP0 stall self-clears on next SETUP
            pr = libusb_control_transfer(g_h, 0xC0, 0xa9, 0, 0, &v, 1, 1000);
            if (pr == 1) retr_ok++;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    g_ev_stop = true;
    for (int i = 0; i < 4; i++) libusb_cancel_transfer(xf[i]);
    ev.join();
    for (int i = 0; i < 4; i++) libusb_free_transfer(xf[i]);
    printf("[i] polls=%d failures=%d retry-successes=%d, bulk bytes=%llu (%.0f KB/s)\n",
           polls, fails, retr_ok, (unsigned long long)g_bulk_bytes.load(),
           g_bulk_bytes.load() / 1024.0 / seconds);
    libusb_set_interface_alt_setting(g_h, 2, 0);
    libusb_release_interface(g_h, 2);
}

// ---------------------------------------------------------------- main ----

int main(int argc, char **argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::string which = argc > 1 ? argv[1] : "all";
    int seconds = argc > 2 ? atoi(argv[2]) : 4;
    int mode = argc > 3 ? atoi(argv[3]) : 0;

    if (libusb_init(nullptr)) { fprintf(stderr, "libusb_init failed\n"); return 1; }
    g_h = libusb_open_device_with_vid_pid(nullptr, VID, PID);
    if (!g_h) { fprintf(stderr, "Device %04x:%04x not found\n", VID, PID); return 1; }

    uint16_t fw = 0;
    uint8_t variant = 0;
    libusb_control_transfer(g_h, 0xC0, 0xa8, 0, 0, (uint8_t *)&fw, 2, 1000);
    libusb_control_transfer(g_h, 0xC0, 0xa9, 0, 0, &variant, 1, 1000);
    printf("[+] Firmware 0x%04x, variant 0x%02x\n", fw, variant);
    if (variant != 0x03) {
        fprintf(stderr, "Not AIO firmware (variant 0x03 required)\n");
        return 1;
    }

    // Put the scope into mode 0 (single channel, CH1 ADC free-running) so the
    // ADC/DMA loop is actively writing isoBuf while we stream.
    if (!set_mode(mode, 1)) return 1;
    printf("[+] Scope mode %d set (ADC/DMA loop running)\n", mode);

    Stats sb, s1, s6;
    if (which == "bulk" || which == "all") test_bulk(seconds, sb);
    if (which == "iso1" || which == "all") test_iso1(seconds, s1);
    if (which == "iso6" || which == "all") test_iso6(seconds, s6);
    if (which == "sig"  || which == "all") test_signal();
    if (which == "appthread") test_bulk_async_threaded(seconds);

    libusb_close(g_h);
    libusb_exit(nullptr);
    return 0;
}
