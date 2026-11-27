#include "usbcallhandler.h"
//#include <stdio.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <algorithm>

#include <math.h>
#include "logging_internal.h"
#include "uartstyledecoder.h"
#include <mutex>
#include <thread>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_thread.h>
#include "librador_platform.h"

// ---------------------------------------------------------------------------
// Frame ingestion.  All transports normalise to 750-byte frames dispatched
// into the o1buffers by device mode, with validity accounting fed by the
// per-frame headers (see the AIO_* notes in usbcallhandler.h).
// ---------------------------------------------------------------------------

void usbCallHandler::dispatch_frame(unsigned char *frame750){
    buffer_read_write_mutex.lock();
    switch(deviceMode){
    case 0:
        internal_o1_buffer_375_CHA->addVector((char*) frame750, 375);
        break;
    case 1:
        internal_o1_buffer_375_CHA->addVector((char*) frame750, 375);
        internal_o1_buffer_375_CHB->addVector((unsigned char*) &frame750[375], 375);
        break;
    case 2:
        internal_o1_buffer_375_CHA->addVector((char*) frame750, 375);
        internal_o1_buffer_375_CHB->addVector((char*) &frame750[375], 375);
        break;
    case 3:
        internal_o1_buffer_375_CHA->addVector((unsigned char*) frame750, 375);
        break;
    case 4:
        internal_o1_buffer_375_CHA->addVector((unsigned char*) frame750, 375);
        internal_o1_buffer_375_CHB->addVector((unsigned char*) &frame750[375], 375);
        break;
    case 6:
        internal_o1_buffer_750->addVector((char*) frame750, 750);
        break;
    case 7:
        internal_o1_buffer_375_CHA->addVector((short*) frame750, 375);
        break;
    }
    buffer_read_write_mutex.unlock();
}

static unsigned char xor_csum_750(const unsigned char *p){
    unsigned char c = 0;
    for(int i = 0; i < ISO_PACKET_SIZE; i++) c ^= p[i];
    return c;
}

void usbCallHandler::dispatch_lost_frames(uint64_t count){
    // Sample-and-hold filler for frames that never arrived intact.  Bounded
    // so a long stall (unplug, debugger pause) cannot flood the buffers.
    if(count > 250){
        count = 250;
    }
    for(uint64_t i = 0; i < count; i++){
        dispatch_frame(last_good_frame);
    }
}

void usbCallHandler::note_frame_csum(unsigned char csum, bool valid){
    frame_csum_ring[data_frame_counter % CSUM_RING_SIZE] = csum;
    frame_csum_valid[data_frame_counter % CSUM_RING_SIZE] = valid;
    data_frame_counter++;
    // A newly noted frame may complete the candidate window of a waiting meta.
    score_pending_meta();
}

void usbCallHandler::note_seq(uint16_t seq){
    if(seq_started){
        uint16_t expect = (uint16_t)(last_seq + 1);
        int16_t delta = (int16_t)(seq - expect);
        if(delta > 0){
            frames_dropped += delta;
        }
        // delta < 0: the device re-sent a header it had already sent (its
        // meta endpoint was re-armed before the SOF latched a new one) or
        // the stream restarted.  Neither is a drop; previously this added
        // 65535 to frames_dropped.
    }
    seq_started = true;
    last_seq = seq;
}

void usbCallHandler::reset_frame_state(){
    seq_started = false;
    reset_alignment_state();
}

// Everything reset_frame_state() resets but the sequence tracking, so the
// frames a restart of the transfers misses still count as dropped.
void usbCallHandler::reset_alignment_state(){
    data_frame_counter = 0;
    meta_frame_counter = 0;
    pending_meta.clear();
    bulk_stream.clear();
    for(int i = 0; i < CSUM_RING_SIZE; i++) frame_csum_valid[i] = false;
    for(int k = 0; k < MAX_DATA_ENDPOINTS; k++){
        iso6_queues[k].clear();
        iso6_received[k] = 0;
        iso6_skip[k] = 0;
        for(int i = 0; i < ISO6_HIST; i++) iso6_hist[k][i] = -1;
    }
    recent_meta.clear();
    meta_misses = 0;
    aligned_from_frame = 0;
    iso6_realign_wanted = false;
    iso1_realign_wanted = false;
    iso6_any_order_after = 0;
    meta_lag = -1;
    iso6_qa_overflows = 0;
    iso6_qa_dumps = 0;
}

// QA DIAGNOSTIC: the iso6 alignment state, for finding a misalignment offline: each endpoint's
// last ISO6_HIST packet checksums in packet order (-- for a short slice) and the recent metas.
void usbCallHandler::iso6_qa_dump(const char *why){
    LIBRADOR_LOG(LOG_DEBUG, "iso6 QA dump (%s): meta_lag %ld, metas %llu, frames %llu, aligned_from %llu, "
        "overflows %llu\n", why, meta_lag, (unsigned long long)meta_frame_counter,
        (unsigned long long)data_frame_counter, (unsigned long long)aligned_from_frame,
        (unsigned long long)iso6_qa_overflows);
    for(int k = 0; k < AIO_EP_ISO6_COUNT; k++){
        const uint64_t n = iso6_received[k];
        const uint64_t first = n > 256 ? n - 256 : 0;
        std::string hex;
        char b[4];
        for(uint64_t i = first; i < n; i++){
            const short v = iso6_hist[k][i % ISO6_HIST];
            if(v < 0) hex += "--";
            else { snprintf(b, sizeof b, "%02x", v); hex += b; }
        }
        LIBRADOR_LOG(LOG_DEBUG, "iso6 QA hist %d: received %llu, queued %zu, skip %llu, from %llu: %s\n", k,
            (unsigned long long)n, iso6_queues[k].size(), (unsigned long long)iso6_skip[k],
            (unsigned long long)first, hex.c_str());
    }
    std::string metas;
    char b[40];
    for(size_t i = recent_meta.size() > 256 ? recent_meta.size() - 256 : 0; i < recent_meta.size(); i++){
        const RecentMeta &m = recent_meta[i];
        snprintf(b, sizeof b, " %llu:%02x%02x", (unsigned long long)m.index, m.c0, m.c1);
        metas += b;
    }
    LIBRADOR_LOG(LOG_DEBUG, "iso6 QA metas:%s\n", metas.c_str());
}

void usbCallHandler::reset_frame_stats(){
    frames_ok = 0;
    frames_bad_checksum = 0;
    frames_dropped = 0;
    frames_unvalidated = 0;
}

void usbCallHandler::get_frame_stats(uint64_t *ok, uint64_t *bad_checksum, uint64_t *dropped, uint64_t *unvalidated){
    if(ok) *ok = frames_ok.load();
    if(bad_checksum) *bad_checksum = frames_bad_checksum.load();
    if(dropped) *dropped = frames_dropped.load();
    if(unvalidated) *unvalidated = frames_unvalidated.load();
}

// iso1 data packet: one packet per frame, 750 bytes when healthy.
void usbCallHandler::handle_data_iso_packet(unsigned char *data, int length){
    if(length == ISO_PACKET_SIZE){
        note_frame_csum(xor_csum_750(data), true);
        memcpy(last_good_frame, data, ISO_PACKET_SIZE);
        dispatch_frame(data);
    } else {
        // Device skipped this frame (or partial) - hole in the stream.
        note_frame_csum(0, false);
        if(length != 0) frames_bad_checksum++;
        dispatch_lost_frames(1);
    }
}

// iso6: queue per-endpoint packets, assemble frames in lockstep once all
// six endpoints have delivered their slice of the frame.
void usbCallHandler::handle_iso6_packet(int ep_index, unsigned char *data, int length){
    const uint64_t n = iso6_received[ep_index]++;
    if(length == ISO_PACKET_SIZE / AIO_EP_ISO6_COUNT){
        unsigned char c = 0;
        for(int i = 0; i < length; i++) c ^= data[i];
        iso6_hist[ep_index][n % ISO6_HIST] = c;
    } else {
        iso6_hist[ep_index][n % ISO6_HIST] = -1;
    }
    if(iso6_skip[ep_index]){
        // realign_iso6() moved this endpoint on past this packet.
        iso6_skip[ep_index]--;
        iso6_dropped_packet(ep_index);
        return;
    }
    iso6_queues[ep_index].emplace_back(data, data + length);
    // An endpoint whose queue passes ISO6_QUEUE_LIMIT is further ahead of
    // another than every queued transfer explains: that one stalled.  Drop
    // the oldest frame from every endpoint, so they stay in step; an
    // endpoint with nothing queued drops its slice of that frame on
    // arrival.  (Dropping only the long queue's packet put that endpoint a
    // frame further out of step each time.)
    if(iso6_queues[ep_index].size() > ISO6_QUEUE_LIMIT){
        for(int k = 0; k < AIO_EP_ISO6_COUNT; k++){
            if(iso6_queues[k].empty()){
                iso6_skip[k]++;
            } else {
                iso6_queues[k].pop_front();
                iso6_dropped_packet(k);
            }
        }
        // QA DIAGNOSTIC
        iso6_qa_overflows++;
        if(iso6_qa_overflows <= 5 || iso6_qa_overflows % 1000 == 0){
            LIBRADOR_LOG(LOG_DEBUG, "iso6 QA: endpoint %d's queue passed %d packets, dropped the oldest frame from every endpoint "
                "(overflow %llu); queued %zu %zu %zu %zu %zu %zu, received %llu %llu %llu %llu %llu %llu\n",
                ep_index, (int)ISO6_QUEUE_LIMIT, (unsigned long long)iso6_qa_overflows,
                iso6_queues[0].size(), iso6_queues[1].size(), iso6_queues[2].size(),
                iso6_queues[3].size(), iso6_queues[4].size(), iso6_queues[5].size(),
                (unsigned long long)iso6_received[0], (unsigned long long)iso6_received[1],
                (unsigned long long)iso6_received[2], (unsigned long long)iso6_received[3],
                (unsigned long long)iso6_received[4], (unsigned long long)iso6_received[5]);
        }
    }
}

// A packet left an endpoint's queue without being assembled.  On endpoint 0
// that is a lost frame: noting it keeps data_frame_counter equal to endpoint
// 0's packet number, which meta pairing and realign_iso6() rely on.
void usbCallHandler::iso6_dropped_packet(int ep_index){
    if(ep_index == 0){
        note_frame_csum(0, false);
        dispatch_lost_frames(1);
    }
}

void usbCallHandler::drain_iso6_queues(){
    while(true){
        for(int k = 0; k < AIO_EP_ISO6_COUNT; k++){
            if(iso6_queues[k].empty()) return;
        }
        unsigned char frame[ISO_PACKET_SIZE];
        bool complete = true;
        int off = 0;
        for(int k = 0; k < AIO_EP_ISO6_COUNT; k++){
            std::vector<unsigned char> &pkt = iso6_queues[k].front();
            if(pkt.size() != ISO_PACKET_SIZE / AIO_EP_ISO6_COUNT){
                complete = false;
            } else if(complete){
                memcpy(&frame[off], pkt.data(), pkt.size());
                off += (int)pkt.size();
            }
            iso6_queues[k].pop_front();
        }
        if(complete){
            note_frame_csum(xor_csum_750(frame), true);
            memcpy(last_good_frame, frame, ISO_PACKET_SIZE);
            dispatch_frame(frame);
        } else {
            note_frame_csum(0, false);
            dispatch_lost_frames(1);
        }
    }
}

// Meta packet (iso transports): EB 58 seqL seqH csum_half0 csum_half1
// usb_state mode, describing the previous frame (lag-1).  Because the meta
// and data URB streams can start a frame or two apart, each meta is matched
// against a small window of recent frame checksums.  The match is deferred
// until that window has been assembled: on iso6 the data slices of the
// meta's own block are often still queued when the meta is processed
// (the ingest thread sees a block's six data URBs and its meta URB in one
// batch, and iso6 frames are only assembled by drain_iso6_queues()), and
// scoring immediately then mis-reported about 0.6% bad and 12%
// unvalidated frames on a healthy stream.
void usbCallHandler::handle_meta_packet(unsigned char *data, int length){
    meta_frame_counter++;
    if(length == 0) return;   // device had no meta armed that frame
    if(length != AIO_META_PACKET_SIZE || data[0] != AIO_HDR_MAGIC0 || data[1] != AIO_HDR_MAGIC1_META){
        frames_bad_checksum++;  // corrupt meta counts against validity
        return;
    }
    note_seq((uint16_t)(data[2] | (data[3] << 8)));
    pending_meta.push_back({meta_frame_counter - 1, data[4], data[5]});
    recent_meta.push_back({meta_frame_counter - 1, data[4], data[5]});
    if(recent_meta.size() > ISO6_HIST) recent_meta.pop_front();
    score_pending_meta();
}

// Scores every pending meta whose candidate frames (meta_lag -2..+2 around
// its slot; meta_lag is -1 nominally) have all been noted, or which would
// otherwise fall out of the checksum ring.  Called whenever a meta or a
// frame arrives.
void usbCallHandler::score_pending_meta(){
    while(!pending_meta.empty()){
        const PendingMeta &p = pending_meta.front();
        const bool window_complete = (long)data_frame_counter > (long)p.index + meta_lag + 2;
        const bool about_to_lap = pending_meta.size() > CSUM_RING_SIZE / 2;
        if(!window_complete && !about_to_lap) return;
        bool matched = false;
        bool any_candidate = false;
        for(long lag = meta_lag - 2; lag <= meta_lag + 2; lag++){
            long idx = (long)p.index + lag;
            if(idx < 0 || idx >= (long)data_frame_counter) continue;
            if((long)data_frame_counter - idx > CSUM_RING_SIZE) continue;
            int slot = idx % CSUM_RING_SIZE;
            if(!frame_csum_valid[slot]) continue;
            any_candidate = true;
            if(frame_csum_ring[slot] == p.c0 || frame_csum_ring[slot] == p.c1){
                matched = true;
                break;
            }
        }
        if(matched){
            frames_ok++;
            meta_misses = 0;
        } else if(any_candidate){
            frames_bad_checksum++;
            // Ask the ingest loop for realign_iso6() rather than calling it
            // here: it drops packets, which notes frames, which re-enters
            // this function.  Metas describing frames assembled before the
            // last realignment don't count.
            if((long)p.index + meta_lag >= (long)aligned_from_frame &&
               ++meta_misses >= ISO6_MISSES_TO_REALIGN){
                if(active_transport == LABRADOR_TRANSPORT_ISO6) iso6_realign_wanted = true;
                else if(active_transport == LABRADOR_TRANSPORT_ISO1) iso1_realign_wanted = true;
            }
        } else {
            frames_unvalidated++;
        }
        pending_meta.pop_front();
    }
}

// The any-order search, for when the start-order one in realign_iso6() finds
// nothing: a stream that loses packets mid-stream leaves each endpoint (and
// the metas) off by its own number of frames, in no particular order (seen in
// Windows and Linux VMs, where the start-order search then failed for the rest
// of the stream).  Endpoint k's packet for meta j is taken as j + d[k], each
// d[k] anywhere in [lo, hi].  Meet in the middle: for every (d0, d1, d2) the
// XOR of endpoints 0-2's checksums on nine key metas, in three groups of
// three, is indexed; for every (d3, d4, d5) the XOR endpoints 0-2 would need
// on a group's three metas (one of two values each: a meta gives both buffer
// halves' checksums) is looked up, and every pair found is judged on all the
// metas like the start-order candidates; the first one with at most
// allowed_misses misses is taken (wrong alignments match about 6% of metas).
// Such an alignment hits every meta of at least one group when
// allowed_misses < 3, so it is always found.  hist is endpoint-major,
// hist_len slots each (packet n at n % hist_len, -1 for a short slice);
// every judged meta's packets index + lo .. index + hi must be in history.
// Returns the best alignment's hits, its offsets in d_out.
namespace {
struct Iso6Meta { uint64_t index; unsigned char c0, c1; };
const int ISO6_KEY_GROUPS = 3;
const int ISO6_KEY_METAS = 3;   // per group: a 24-bit key

int iso6_search_any_order(const short *hist, int hist_len, const std::vector<Iso6Meta> &judge,
                          long lo, long hi, int allowed_misses, long d_out[6], long *candidates){
    const long w = hi - lo + 1;
    const long combos = w * w * w;
    auto at = [&](int k, uint64_t index, long d) -> int {
        return hist[k * hist_len + (long)((index + d) % (uint64_t)hist_len)];
    };
    // XOR of three endpoints' packets for meta j, or -1 if any slice is short.
    auto triple = [&](int k0, long a, long b, long c, const Iso6Meta &m) -> int {
        const int x = at(k0, m.index, a), y = at(k0 + 1, m.index, b), z = at(k0 + 2, m.index, c);
        return (x < 0 || y < 0 || z < 0) ? -1 : (x ^ y ^ z);
    };
    std::vector<uint64_t> table[ISO6_KEY_GROUPS];
    for(long a = 0; a < combos; a++){
        const long d0 = lo + a / (w * w), d1 = lo + (a / w) % w, d2 = lo + a % w;
        for(int g = 0; g < ISO6_KEY_GROUPS; g++){
            uint32_t key = 0;
            bool full = true;
            for(int i = 0; i < ISO6_KEY_METAS && full; i++){
                const int x = triple(0, d0, d1, d2, judge[g * ISO6_KEY_METAS + i]);
                if(x < 0) full = false;
                key = (key << 8) | (uint32_t)x;
            }
            if(full) table[g].push_back(((uint64_t)key << 32) | (uint64_t)a);
        }
    }
    for(int g = 0; g < ISO6_KEY_GROUPS; g++) std::sort(table[g].begin(), table[g].end());
    int best = -1;
    for(long b = 0; b < combos; b++){
        const long d3 = lo + b / (w * w), d4 = lo + (b / w) % w, d5 = lo + b % w;
        for(int g = 0; g < ISO6_KEY_GROUPS; g++){
            int xb[ISO6_KEY_METAS];
            bool full = true;
            for(int i = 0; i < ISO6_KEY_METAS && full; i++){
                xb[i] = triple(3, d3, d4, d5, judge[g * ISO6_KEY_METAS + i]);
                if(xb[i] < 0) full = false;
            }
            if(!full) continue;
            for(int mask = 0; mask < (1 << ISO6_KEY_METAS); mask++){
                uint32_t target = 0;
                for(int i = 0; i < ISO6_KEY_METAS; i++){
                    const Iso6Meta &m = judge[g * ISO6_KEY_METAS + i];
                    target = (target << 8) | (uint32_t)(xb[i] ^ ((mask >> i) & 1 ? m.c1 : m.c0));
                }
                auto it = std::lower_bound(table[g].begin(), table[g].end(), (uint64_t)target << 32);
                for(; it != table[g].end() && (uint32_t)(*it >> 32) == target; ++it){
                    const long a = (long)(*it & 0xffffffffu);
                    const long d[6] = {lo + a / (w * w), lo + (a / w) % w, lo + a % w, d3, d4, d5};
                    (*candidates)++;
                    int hits = 0, misses = 0;
                    for(const Iso6Meta &m : judge){
                        int x = 0;
                        bool slices = true;
                        for(int k = 0; k < 6 && slices; k++){
                            const int v = at(k, m.index, d[k]);
                            if(v < 0) slices = false;
                            else x ^= v;
                        }
                        if(slices && (x == m.c0 || x == m.c1)) hits++;
                        else if(++misses > allowed_misses) break;
                    }
                    if(hits > best){
                        best = hits;
                        for(int k = 0; k < 6; k++) d_out[k] = d[k];
                        if(best >= (int)judge.size() - allowed_misses) return best;
                    }
                }
            }
        }
    }
    return best;
}
}

// Finds the endpoint shifts and meta lag under which the recent metas match
// the data, and moves the iso6 queues to that alignment.  Frame m is taken
// as endpoint 0's packet m and endpoint k's packet m + shift[k]; a meta with
// index j describes frame j + lag.
//
// submit_iso_transfers() submits each stream's first transfer in order (data
// endpoints 0 to 5, then the meta endpoint) and hosts start isochronous
// transfers as soon as they can, so each stream starts on the same frame as
// the one before it or a later one.  An endpoint that starts d frames after
// endpoint 0 has shift -d, so the shifts never rise from one endpoint to the
// next; and since a meta describes the frame before its own, the lag is
// -1 - shift[5] plus however many frames the metas started after endpoint 5.
// Every such alignment whose seven streams started within ISO6_MAX_SPREAD
// frames is tried against the last ISO6_JUDGE_METAS metas (18,564
// candidates, most rejected at their third mismatch), and one is taken only
// if 90% of those metas match it.  Measured on air: wrong alignments match
// about 6% of metas, the right one 99%.
void usbCallHandler::realign_iso6(){
    iso6_realign_wanted = false;
    meta_misses = 0;
    const auto search_start = std::chrono::steady_clock::now();
    const int n_ep = AIO_EP_ISO6_COUNT;
    // A meta with index j is checked against packet j + lag + shift[k] of
    // each endpoint, which runs from j - 1 (every stream started together)
    // to j + ISO6_MAX_SPREAD - 1 (endpoint 0 alone, with the metas a full
    // spread after it).
    const long first_needed = -1;
    const long last_needed = ISO6_MAX_SPREAD - 1;
    // The most recent metas whose packets are in every endpoint's history
    // for any candidate.
    std::vector<RecentMeta> judge;
    for(auto it = recent_meta.rbegin(); it != recent_meta.rend() && (int)judge.size() < ISO6_JUDGE_METAS; ++it){
        bool in_history = (long)it->index + first_needed >= 0;
        for(int k = 0; k < n_ep && in_history; k++){
            if((long)it->index + first_needed < (long)iso6_received[k] - ISO6_HIST) in_history = false;
            if((long)it->index + last_needed >= (long)iso6_received[k]) in_history = false;
        }
        if(in_history) judge.push_back(*it);
    }
    if((int)judge.size() < ISO6_JUDGE_METAS) return;   // too early; tried again after more misses
    const int allowed_misses = ISO6_JUDGE_METAS / 10;
    int best_hits = -1;
    long best_lag = 0;
    int best_shift[MAX_DATA_ENDPOINTS] = {0};
    // Walks every 0 = shift[0] >= shift[1] >= ... >= shift[5] >= -ISO6_MAX_SPREAD
    // once, starting from all zeros.
    int shift[MAX_DATA_ENDPOINTS] = {0};
    long candidates = 0;
    for(bool more = true; more; ){
        for(long meta_delay = 0; meta_delay <= ISO6_MAX_SPREAD + shift[n_ep - 1]; meta_delay++){
            candidates++;
            const long lag = -1 - shift[n_ep - 1] + meta_delay;
            int hits = 0;
            int misses = 0;
            for(const RecentMeta &m : judge){
                int x = 0;
                bool full = true;
                for(int k = 0; k < n_ep && full; k++){
                    const short v = iso6_hist[k][((long)m.index + lag + shift[k]) % ISO6_HIST];
                    if(v < 0) full = false;
                    else x ^= v;
                }
                if(full && (x == m.c0 || x == m.c1)) hits++;
                else if(++misses > allowed_misses) break;
            }
            if(hits > best_hits){
                best_hits = hits;
                best_lag = lag;
                for(int k = 0; k < n_ep; k++) best_shift[k] = shift[k];
            }
        }
        // Next sequence: lower the last shift that can still go lower, and
        // bring every later one down to it.
        int k = n_ep - 1;
        while(k > 0 && shift[k] == -ISO6_MAX_SPREAD) k--;
        more = k > 0;
        if(more){
            shift[k]--;
            for(int i = k + 1; i < n_ep; i++) shift[i] = shift[k];
        }
    }
    // Nothing in start order: try every order (iso6_search_any_order), over
    // ISO6_ANY_ORDER_SLACK frames more either side, but at most once every
    // ISO6_ANY_ORDER_BACKOFF metas while it keeps failing, since it costs tens
    // of milliseconds of the ingest thread.
    const char *found_by = "start order";
    if(best_hits < ISO6_JUDGE_METAS - allowed_misses && recent_meta.back().index >= iso6_any_order_after){
        const long lo = first_needed - ISO6_ANY_ORDER_SLACK;
        const long hi = last_needed + ISO6_ANY_ORDER_SLACK;
        std::vector<Iso6Meta> judge_any;
        for(auto it = recent_meta.rbegin(); it != recent_meta.rend() && (int)judge_any.size() < ISO6_JUDGE_METAS; ++it){
            bool in_history = (long)it->index + lo >= 0;
            for(int k = 0; k < n_ep && in_history; k++){
                if((long)it->index + lo < (long)iso6_received[k] - ISO6_HIST) in_history = false;
                if((long)it->index + hi >= (long)iso6_received[k]) in_history = false;
            }
            if(in_history) judge_any.push_back({it->index, it->c0, it->c1});
        }
        if((int)judge_any.size() == ISO6_JUDGE_METAS){
            long d[6];
            const int hits = iso6_search_any_order(&iso6_hist[0][0], ISO6_HIST, judge_any, lo, hi, allowed_misses,
                                                   d, &candidates);
            if(hits >= ISO6_JUDGE_METAS - allowed_misses){
                best_hits = hits;
                best_lag = d[0];
                for(int k = 0; k < n_ep; k++) best_shift[k] = (int)(d[k] - d[0]);
                found_by = "any order";
            } else {
                iso6_any_order_after = recent_meta.back().index + ISO6_ANY_ORDER_BACKOFF;
                // Out of every search's reach: after a long stall (a Linux VM
                // paused for 3 s) the endpoints carried on up to 32 frames
                // apart, with the same number of packets each.  Restarting the
                // transfers starts them together again.
                LIBRADOR_LOG(LOG_DEBUG, "iso6: no alignment within reach; restarting the transfers together\n");
                iso6_resync_wanted = true;
            }
        }
    }
    const long search_us = (long)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - search_start).count();
    if(best_hits < ISO6_JUDGE_METAS - allowed_misses){
        LIBRADOR_LOG(LOG_DEBUG, "iso6: no alignment explains the metas (best %d of %d; %ld candidates in %ld us)\n",
            best_hits, ISO6_JUDGE_METAS, candidates, search_us);
        // QA DIAGNOSTIC: the first failures of a stream, and one every few hundred after.
        if(iso6_qa_dumps < 3 || iso6_qa_dumps % 300 == 0) iso6_qa_dump("search failed");
        iso6_qa_dumps++;
        return;
    }
    // Move every queue forward (never back) until the next packet each will
    // give the lockstep sits best_shift[k] after endpoint 0's.
    long next[MAX_DATA_ENDPOINTS];
    long advance = 0;
    for(int k = 0; k < n_ep; k++){
        next[k] = (long)(iso6_received[k] - iso6_queues[k].size() + iso6_skip[k]);
    }
    for(int k = 0; k < n_ep; k++){
        const long d = (next[k] - next[0]) - best_shift[k];
        if(d > advance) advance = d;
    }
    meta_lag = best_lag;
    for(int k = 0; k < n_ep; k++){
        long move = best_shift[k] + advance - (next[k] - next[0]);
        while(move > 0 && !iso6_queues[k].empty()){
            iso6_queues[k].pop_front();
            iso6_dropped_packet(k);
            move--;
        }
        iso6_skip[k] += move;
    }
    aligned_from_frame = data_frame_counter + iso6_skip[0];
    LIBRADOR_LOG(LOG_DEBUG, "iso6 realigned (%s): endpoint shifts %d %d %d %d %d, meta lag %ld, %d of %d metas matched (%ld candidates in %ld us)\n",
        found_by, best_shift[1], best_shift[2], best_shift[3], best_shift[4], best_shift[5], best_lag, best_hits,
        ISO6_JUDGE_METAS, candidates, search_us);
}

// iso1's data and its metas come from two endpoints, and a host that stops
// servicing them for a while can carry on with them some frames apart: in
// a Linux VM paused for 3 s, no meta matched a frame within meta_lag +-2
// for the rest of the stream (84% of frames counted bad).  The lag is
// searched over every frame the checksum ring still holds, and one is taken
// only if 90% of the last ISO6_JUDGE_METAS metas whose frames are in the
// ring match it.
void usbCallHandler::realign_iso1(){
    iso1_realign_wanted = false;
    meta_misses = 0;
    const int allowed_misses = ISO6_JUDGE_METAS / 10;
    int best_hits = -1;
    long best_lag = meta_lag;
    for(long lag = -ISO_INFLIGHT_FRAMES; lag <= ISO_INFLIGHT_FRAMES; lag++){
        int hits = 0;
        int misses = 0;
        for(auto it = recent_meta.rbegin(); it != recent_meta.rend() && hits + misses < ISO6_JUDGE_METAS; ++it){
            const long idx = (long)it->index + lag;
            if(idx < 0 || idx >= (long)data_frame_counter || (long)data_frame_counter - idx > CSUM_RING_SIZE) continue;
            const int slot = idx % CSUM_RING_SIZE;
            if(frame_csum_valid[slot] && (frame_csum_ring[slot] == it->c0 || frame_csum_ring[slot] == it->c1)) hits++;
            else if(++misses > allowed_misses) break;
        }
        if(hits + misses == ISO6_JUDGE_METAS && hits > best_hits){
            best_hits = hits;
            best_lag = lag;
        }
    }
    if(best_hits < ISO6_JUDGE_METAS - allowed_misses){
        LIBRADOR_LOG(LOG_DEBUG, "iso1: no meta lag explains the metas (best %d of %d)\n", best_hits, ISO6_JUDGE_METAS);
        return;
    }
    LIBRADOR_LOG(LOG_DEBUG, "iso1 realigned: meta lag %ld (was %ld), %d of %d metas matched\n",
        best_lag, meta_lag, best_hits, ISO6_JUDGE_METAS);
    meta_lag = best_lag;
    aligned_from_frame = data_frame_counter;
}

// Bulk stream parser: the 750-byte payload, then the header [EB 57 seqL
// seqH lenL lenH csum mode], 9 diagnostic bytes and 1 pad byte; fixed
// 768-byte stride (firmware 0x000D and later).
void usbCallHandler::handle_bulk_bytes(unsigned char *data, int length){
    // Diagnostic: raw stream capture (LABRADOR_DUMP_BULK=<path>, first 1 MB)
    static FILE *dump_fp = nullptr;
    static long dump_left = -1;
    if(dump_left < 0){
        const char *env = getenv("LABRADOR_DUMP_BULK");
        if(env){ dump_fp = fopen(env, "wb"); dump_left = 1024L*1024L; }
        else dump_left = 0;
    }
    if(dump_fp && dump_left > 0){
        long n = (length < dump_left) ? length : dump_left;
        fwrite(data, 1, n, dump_fp);
        dump_left -= n;
        if(dump_left <= 0){ fclose(dump_fp); dump_fp = nullptr; }
    }
    bulk_stream.insert(bulk_stream.end(), data, data + length);
    // pos is a frame start: the payload, then its header at offset 750.
    size_t pos = 0;
    while(true){
        while(bulk_stream.size() - pos >= AIO_BULK_FRAME_XFER &&
              !(bulk_stream[pos+ISO_PACKET_SIZE] == AIO_HDR_MAGIC0 &&
                bulk_stream[pos+ISO_PACKET_SIZE+1] == AIO_HDR_MAGIC1_BULK)){
            pos++;
        }
        if(bulk_stream.size() - pos < AIO_BULK_FRAME_XFER) break;
        const unsigned char *hdr = bulk_stream.data() + pos + ISO_PACKET_SIZE;
        uint16_t len = hdr[4] | (hdr[5] << 8);
        if(len != ISO_PACKET_SIZE){ pos++; continue; }
        uint16_t seq = hdr[2] | (hdr[3] << 8);
        unsigned char csum = hdr[6];
        unsigned char *payload = bulk_stream.data() + pos;
        if(seq_started){
            uint16_t expect = (uint16_t)(last_seq + 1);
            if(seq != expect){
                // Device skipped frames: keep the sample clock honest.
                dispatch_lost_frames((uint16_t)(seq - expect));
            }
        }
        note_seq(seq);
        if(xor_csum_750(payload) == csum){
            frames_ok++;
            memcpy(last_good_frame, payload, ISO_PACKET_SIZE);
            dispatch_frame(payload);
        } else {
            // Stomped mid-flight by the ADC/DMA loop - substitute a hold of
            // the last good frame rather than plot torn data or silently
            // compress the timebase.
            frames_bad_checksum++;
            dispatch_lost_frames(1);
        }
        pos += AIO_BULK_FRAME_XFER;
    }
    bulk_stream.erase(bulk_stream.begin(), bulk_stream.begin() + pos);
}

// ---------------------------------------------------------------------------
// Ingest thread: takes raw bytes from the libusb event thread and does all
// parsing / validation / o1buffer dispatch, so the event thread never
// blocks on buffer_read_write_mutex (blocking there stalls URB
// resubmission, the wire NAK-starves, and the device blows its 1 ms drain
// deadline against the 2 ms ADC overwrite horizon).
// ---------------------------------------------------------------------------

void usbCallHandler::queue_ingest(uint8_t kind, uint8_t ep_index, const unsigned char *data, int length){
    // TEMP A/B diagnostic: dispatch inline on the event thread (the
    // pre-ingest-thread behavior) to measure what the ingest thread buys.
    static int inline_dispatch = -1;
    if(inline_dispatch < 0){
        const char *env = getenv("LABRADOR_INLINE_DISPATCH");
        inline_dispatch = (env && env[0] == '1') ? 1 : 0;
    }
    if(inline_dispatch){
        switch(kind){
        case 0: handle_bulk_bytes((unsigned char *)data, length); return;
        case 1: handle_data_iso_packet((unsigned char *)data, length); return;
        case 2: handle_iso6_packet(ep_index, (unsigned char *)data, length);
                drain_iso6_queues(); return;
        case 3: handle_meta_packet((unsigned char *)data, length); return;
        case 4: reset_alignment_state(); return;
        }
        return;
    }
    IngestItem item;
    item.kind = kind;
    item.ep_index = ep_index;
    item.bytes.assign(data, data + length);
    {
        std::lock_guard<std::mutex> lock(ingest_mutex);
        // Backstop against an unbounded queue if the ingest thread dies:
        // about 10 s of iso6 (seven items a millisecond).  Dropping items
        // leaves iso6's endpoints out of step, so it must not be reached by
        // an ingest thread that was only held up (a Linux VM held it up for
        // about half a second, 3,234 items; this was 4,096).
        if(ingest_queue.size() > 70000){
            // QA DIAGNOSTIC
            ingest_qa_dropped++;
            if(ingest_qa_dropped <= 5 || ingest_qa_dropped % 1000 == 0){
                LIBRADOR_LOG(LOG_DEBUG, "ingest QA: hand-off queue full, dropped its oldest item (kind %d, "
                    "endpoint %d; %llu dropped so far)\n", ingest_queue.front().kind, ingest_queue.front().ep_index,
                    (unsigned long long)ingest_qa_dropped);
            }
            ingest_queue.pop_front();
        }
        ingest_queue.push_back(std::move(item));
    }
    ingest_cv.notify_one();
}

// Windows: the threads that keep transfers queued run ahead of normal-priority
// work, so a burst of background activity (Defender scanning, Windows Update,
// a build) doesn't hold them off while the queued transfers run out.  The
// event thread, which resubmits every transfer, goes to time-critical, as
// does libusb's I/O completion thread (deps/libusb patch 0002); the ingest
// thread, which does the per-frame work, to high, where it still outranks
// normal work but can't take a core from the system.
static void raise_usb_thread_priority(SDL_ThreadPriority priority, const char *thread){
#ifdef _WIN32
    if(!SDL_SetCurrentThreadPriority(priority)){
        LIBRADOR_LOG(LOG_WARNING, "couldn't raise the %s thread's priority: %s\n", thread, SDL_GetError());
    }
#else
    (void)priority;
    (void)thread;
#endif
}

void usbCallHandler::ingest_thread_function(){
    LIBRADOR_LOG(LOG_DEBUG, "ingest thread spawned\n");
    raise_usb_thread_priority(SDL_THREAD_PRIORITY_HIGH, "ingest");
    while(true){
        std::deque<IngestItem> batch;
        {
            std::unique_lock<std::mutex> lock(ingest_mutex);
            ingest_cv.wait(lock, [this]{ return ingest_stop || !ingest_queue.empty(); });
            if(ingest_stop && ingest_queue.empty()){
                break;
            }
            batch.swap(ingest_queue);
        }
        // QA DIAGNOSTIC: about 0.3 s of iso6 items at once means this thread was held up.
        if(batch.size() >= 2000){
            ingest_qa_big_batches++;
            if(ingest_qa_big_batches <= 10 || ingest_qa_big_batches % 100 == 0){
                LIBRADOR_LOG(LOG_DEBUG, "ingest QA: took over a batch of %zu items (big batch %llu)\n",
                    batch.size(), (unsigned long long)ingest_qa_big_batches);
            }
        }
        for(IngestItem &item : batch){
            switch(item.kind){
            case 0:
                handle_bulk_bytes(item.bytes.data(), (int)item.bytes.size());
                break;
            case 1:
                handle_data_iso_packet(item.bytes.data(), (int)item.bytes.size());
                break;
            case 2:
                handle_iso6_packet(item.ep_index, item.bytes.data(), (int)item.bytes.size());
                // Assemble as soon as a frame's six slices are in rather than
                // at the end of the batch, so frames reach the buffers (and
                // the checksum ring) with no batch-sized lag.
                drain_iso6_queues();
                break;
            case 3:
                handle_meta_packet(item.bytes.data(), (int)item.bytes.size());
                break;
            case 4:
                reset_alignment_state();
                break;
            }
            if(iso6_realign_wanted){
                realign_iso6();
            }
            if(iso1_realign_wanted){
                realign_iso1();
            }
        }
    }
    LIBRADOR_LOG(LOG_DEBUG, "ingest thread finished\n");
}

// ---------------------------------------------------------------------------
// libusb async callbacks (event-loop thread)
// ---------------------------------------------------------------------------

void usbCallHandler::rearm_or_retire(struct libusb_transfer *transfer){
    if(!iso_thread_shutdown_requested && iso6_resync_wanted){
        hold_for_resync();
        return;
    }
    if(!iso_thread_shutdown_requested){
        int error = libusb_submit_transfer(transfer);
        if(error){
            LIBRADOR_LOG(LOG_WARNING, "Error re-arming the endpoint!\n");
            iso_thread_shutdown_requested = true;
            iso_thread_shutdown_remaining_transfers--;
            LIBRADOR_LOG(LOG_WARNING, "Transfer not being rearmed!  %d armed transfers remaining\n", iso_thread_shutdown_remaining_transfers.load());
        }
    } else {
        // Normal teardown: every in-flight transfer retires here once.
        iso_thread_shutdown_remaining_transfers--;
        LIBRADOR_LOG(LOG_DEBUG, "Transfer not being rearmed!  %d armed transfers remaining\n", iso_thread_shutdown_remaining_transfers.load());
    }
}

// Restarting iso6's transfers together: each one is held as it comes back
// instead of resubmitted, and once every one is back (about half a second,
// as each covers frames already scheduled) the ingest thread is told to
// start the alignment afresh, after every packet already queued, and they
// are all submitted again in the order a new stream submits them, so the
// endpoints start within a frame or two of each other as they do at a
// stream's start.  A held transfer counts as retired, so a teardown in the
// meantime finishes when the ones still in flight retire.
void usbCallHandler::hold_for_resync(){
    iso_thread_shutdown_remaining_transfers--;
    if(++iso6_resync_held < total_inflight_transfers()) return;
    iso6_resync_held = 0;
    iso6_resync_wanted = false;
    queue_ingest(4, 0, nullptr, 0);
    int submitted = 0;
    const int error = submit_iso_transfers(&submitted);
    iso_thread_shutdown_remaining_transfers = submitted;
    if(error){
        LIBRADOR_LOG(LOG_WARNING, "iso6: restarting the transfers failed after %d of %d; stopping the stream\n",
            submitted, total_inflight_transfers());
        iso_thread_shutdown_requested = true;
        return;
    }
    LIBRADOR_LOG(LOG_DEBUG, "iso6: restarted all %d transfers together\n", submitted);
}

// A transfer that fails (status other than COMPLETED) still
// spanned num_iso_packets frames on its endpoint, so each of its packets is
// queued empty, to be counted as a lost frame.  Dropping the transfer
// outright would leave that endpoint's packet count, or the meta count,
// num_iso_packets behind the others for the rest of the stream: iso6 would
// assemble every later frame from slices of different milliseconds, and meta
// pairing would compare checksums against the wrong frames.
void LIBUSB_CALL isoCallback(struct libusb_transfer * transfer){
    usbCallHandler *usb_driver = (usbCallHandler *)transfer->user_data;
    const bool transfer_ok = transfer->status == LIBUSB_TRANSFER_COMPLETED;
    // Which data endpoint is this?
    int ep_index = 0;
    if(usb_driver->active_transport == LABRADOR_TRANSPORT_ISO6){
        ep_index = transfer->endpoint - AIO_EP_ISO6_FIRST;
    }
    usb_driver->iso_transfers_done[ep_index]++;
    if(!transfer_ok) usb_driver->iso_transfers_failed[ep_index]++;
    for(int i=0;i<transfer->num_iso_packets;i++){
        unsigned char *packetPointer = libusb_get_iso_packet_buffer_simple(transfer, i);
        int actual = transfer->iso_packet_desc[i].actual_length;
        if(!transfer_ok || transfer->iso_packet_desc[i].status != LIBUSB_TRANSFER_COMPLETED){
            actual = 0;
        }
        if(usb_driver->active_transport == LABRADOR_TRANSPORT_ISO6){
            usb_driver->queue_ingest(2, (uint8_t)ep_index, packetPointer, actual);
        } else {
            usb_driver->queue_ingest(1, 0, packetPointer, actual);
        }
    }
    usb_driver->rearm_or_retire(transfer);
}

void LIBUSB_CALL metaIsoCallback(struct libusb_transfer * transfer){
    usbCallHandler *usb_driver = (usbCallHandler *)transfer->user_data;
    const bool transfer_ok = transfer->status == LIBUSB_TRANSFER_COMPLETED;
    usb_driver->iso_transfers_done[MAX_DATA_ENDPOINTS]++;
    if(!transfer_ok) usb_driver->iso_transfers_failed[MAX_DATA_ENDPOINTS]++;
    for(int i=0;i<transfer->num_iso_packets;i++){
        unsigned char *packetPointer = libusb_get_iso_packet_buffer_simple(transfer, i);
        int actual = transfer->iso_packet_desc[i].actual_length;
        if(!transfer_ok || transfer->iso_packet_desc[i].status != LIBUSB_TRANSFER_COMPLETED){
            actual = 0;
        }
        usb_driver->queue_ingest(3, 0, packetPointer, actual);
    }
    usb_driver->rearm_or_retire(transfer);
}

void LIBUSB_CALL bulkCallback(struct libusb_transfer * transfer){
    usbCallHandler *usb_driver = (usbCallHandler *)transfer->user_data;
    if(transfer->status==LIBUSB_TRANSFER_COMPLETED || transfer->status==LIBUSB_TRANSFER_TIMED_OUT)
    {
        if(transfer->actual_length > 0){
            usb_driver->queue_ingest(0, 0, transfer->buffer, transfer->actual_length);
        }
    }
    usb_driver->rearm_or_retire(transfer);
}

const char* usbCallHandler::daq_unit_labels[] = {"Volts", "ADC", "Bits", "None"};// TODO: allow DAQ of decoded chars

// it makes sense to call this iso_polling_function because we only use libusb's asynchronous API for isochronous transfers
std::atomic<uint32_t> g_ctrl_req_counts[256];

void usbCallHandler::iso_polling_function(libusb_context *ctx){
    LIBRADOR_LOG(LOG_DEBUG, "iso_polling_function thread spawned\n");
    raise_usb_thread_priority(SDL_THREAD_PRIORITY_TIME_CRITICAL, "USB event");
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;//ISO_PACKETS_PER_CTX*4000;
    auto last_stats = std::chrono::steady_clock::now();
    while(iso_thread_shutdown_remaining_transfers != 0){
        //printf("iso_polling_function begin loop\n");
        if(libusb_event_handling_ok(ctx)){
            libusb_handle_events_timeout(ctx, &tv);
        }
        auto now = std::chrono::steady_clock::now();
        if(now - last_stats > std::chrono::seconds(10)){
            last_stats = now;
            LIBRADOR_LOG(LOG_DEBUG, "frame stats: ok=%llu bad_csum=%llu dropped=%llu unvalidated=%llu (transport %d)\n",
                (unsigned long long)frames_ok.load(), (unsigned long long)frames_bad_checksum.load(),
                (unsigned long long)frames_dropped.load(), (unsigned long long)frames_unvalidated.load(),
                active_transport);
            // QA DIAGNOSTIC: transfers completed (and failed) per data endpoint, then the metas'.
            LIBRADOR_LOG(LOG_DEBUG, "iso transfers done/failed: %llu/%llu %llu/%llu %llu/%llu %llu/%llu %llu/%llu %llu/%llu meta %llu/%llu\n",
                (unsigned long long)iso_transfers_done[0], (unsigned long long)iso_transfers_failed[0],
                (unsigned long long)iso_transfers_done[1], (unsigned long long)iso_transfers_failed[1],
                (unsigned long long)iso_transfers_done[2], (unsigned long long)iso_transfers_failed[2],
                (unsigned long long)iso_transfers_done[3], (unsigned long long)iso_transfers_failed[3],
                (unsigned long long)iso_transfers_done[4], (unsigned long long)iso_transfers_failed[4],
                (unsigned long long)iso_transfers_done[5], (unsigned long long)iso_transfers_failed[5],
                (unsigned long long)iso_transfers_done[MAX_DATA_ENDPOINTS],
                (unsigned long long)iso_transfers_failed[MAX_DATA_ENDPOINTS]);
            {
                char req_summary[256];
                int off = 0;
                for(int rc = 0; rc < 256; rc++){
                    uint32_t n = g_ctrl_req_counts[rc].load();
                    if(n && off < 200){
                        off += snprintf(&req_summary[off], sizeof(req_summary)-off, "%02x:%u ", rc, n);
                    }
                }
                LIBRADOR_LOG(LOG_DEBUG, "ctrl sends: %s\n", req_summary);
            }
        }
    }
    iso_thread_active = false;
    LIBRADOR_LOG(LOG_DEBUG, "iso_polling_function thread finished\n");
}

usbCallHandler::usbCallHandler(unsigned short VID_in, unsigned short PID_in)
{
    VID = VID_in;
    PID = PID_in;

    internal_o1_buffer_375_CHA = new o1buffer(375e3);
    internal_o1_buffer_375_CHB = new o1buffer(375e3);
    internal_o1_buffer_750 = new o1buffer(750e3);
    m_i2c_decoder = new i2c::i2cDecoder(internal_o1_buffer_375_CHA, internal_o1_buffer_375_CHB);

    //In case it was deleted before; reset the shared variables.
    iso_thread_shutdown_requested = false;
    iso_thread_shutdown_remaining_transfers = NUM_FUTURE_CTX;
}


usbCallHandler::~usbCallHandler(){
    //Kill off iso_polling_thread.  Maybe join then get it to detect its own timeout condition.
    LIBRADOR_LOG(LOG_DEBUG, "Calling destructor for librador USB call handler\n");

    // Also stops streaming (alt setting 0); without it the board keeps its ISO
    // alt setting selected after exit and the next session degrades or fails to connect.
    teardown_connection();

    if(daq_thread && daq_thread->joinable()){
        daq_thread->join();
    }

    if(ctx){
        libusb_exit(ctx);
        LIBRADOR_LOG(LOG_DEBUG, "Libusb exited\n");
    }

    delete m_i2c_decoder;
    LIBRADOR_LOG(LOG_DEBUG, "librador USB call handler deleted\n");
}

void usbCallHandler::set_bootloader_mode_allowed(bool allowed) {
    if(librador_get_host_hooks().set_bootloader_mode_allowed)
        librador_get_host_hooks().set_bootloader_mode_allowed(allowed);
    LIBRADOR_LOG(LOG_DEBUG, "bootloader_mode_allowed set");
}

// call sequence: usbcallhandler.cpp(requestFirmwareFlash) -> MainActivity.java -> librador -> here, all just to get a dialog window before reset_device (below) triggers a usb permission request
void usbCallHandler::initiateFirmwareFlash()
{
    set_bootloader_mode_allowed(true);
    reset_device(true); // will trigger respondToStartupOrUsbStateChange with the device in bootloader mode.
    connected = false;
    libusb_release_interface(handle, 0);
    libusb_close(handle);
    libusb_exit(ctx);
    ctx = nullptr;
    handle=  nullptr;
}

//Initialise libusb
int usbCallHandler::init_libusb(){
    if(ctx){
        // Called every device poll on desktop — stay quiet when already up
        return 1;
    } else LIBRADOR_LOG(LOG_DEBUG, "libusb context is null\n");
#ifdef PLATFORM_ANDROID
    // Android hands us an fd from UsbManager; libusb must not enumerate itself.
    struct libusb_init_option libusb_options[2] = {
        {.option = LIBUSB_OPTION_NO_DEVICE_DISCOVERY},
        {.option = LIBUSB_OPTION_LOG_LEVEL, .value = {.ival = 3}}
    };
    int error = libusb_init_context(&ctx, libusb_options, 2);
#else
    // Desktop: use the classic libusb_init() unconditionally so the binary stays
    // compatible with the older system libusb shipped by long-lived Linux
    // distros (e.g. 1.0.24/1.0.25 on Debian 11-12, Ubuntu 20.04/22.04, Raspberry
    // Pi OS). libusb_init_context() (libusb >= 1.0.27) would only let us pass the
    // log level as an init option, which libusb_set_option() does identically on
    // every libusb >= 1.0.22 — so there is nothing to gain and much to lose by
    // requiring the newer symbol here. (Android still needs the context API for
    // NO_DEVICE_DISCOVERY with fd-passing; that path is above.)
    int error = libusb_init(&ctx);
    if (!error)
        libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL, 3);
#endif
    if(error){
        LIBRADOR_LOG(LOG_WARNING, "libusb_init FAILED\n");
        return -1;
    } else {
        LIBRADOR_LOG(LOG_DEBUG, "Libusb context initialised\n");
        return 0;
    }
}

int usbCallHandler::setup_usb_control(int file_descriptor){
    LIBRADOR_LOG(LOG_DEBUG, "usbCallHandler::setup_usb_control()\n");
    //Get a handle on the Labrador device
    libusb_wrap_sys_device(ctx, file_descriptor, &handle);

    if(!handle){
        LIBRADOR_LOG(LOG_DEBUG, "DEVICE NOT FOUND\n");
        return -2;
    }
    LIBRADOR_LOG(LOG_DEBUG, "Device found!!\n");
    return claim_and_prepare();
}

int usbCallHandler::claim_and_prepare(){
#ifdef __APPLE__
    // Do NOT claim any interface before the firmware check.  Device-level
    // control transfers work unclaimed on Darwin, and claiming interface 0
    // on a board still running pre-AIO firmware (iso endpoints in alt
    // setting 0) kernel-panics macOS Tahoe.  The streaming interface is
    // claimed in start_streaming() once the firmware is known to be AIO.
    iface0_claimed = false;
    return 0;
#else
    if(libusb_kernel_driver_active(handle, 0)) {
        LIBRADOR_LOG(LOG_DEBUG, "KERNEL DRIVER ACTIVE");
    } else {
        LIBRADOR_LOG(LOG_DEBUG, "KERNEL DRIVER INACTIVE");
    }
    if(libusb_kernel_driver_active(handle, 0)){
        libusb_detach_kernel_driver(handle, 0);
    }

    //Claim interface 0.  On AIO firmware its default alt setting has no
    //endpoints, so this is inert; it exists to keep control transfers
    //working on platforms that expect a claimed interface (WinUSB).
    int error = libusb_claim_interface(handle, 0);
    if(error){
        LIBRADOR_LOG(LOG_WARNING, "libusb_claim_interface FAILED\n");
        libusb_close(handle);
        handle = nullptr;
        return -3;
    } else LIBRADOR_LOG(LOG_DEBUG, "Interface claimed!\n");
    iface0_claimed = true;
    return 0;
#endif
}

int usbCallHandler::set_transport(int transport){
    if(transport < LABRADOR_TRANSPORT_AUTO || transport > LABRADOR_TRANSPORT_BULK){
        return -1;
    }
    requested_transport = transport;
    return 0;
}

int usbCallHandler::get_active_transport(){
    return active_transport;
}

int usbCallHandler::resolve_transport(){
    int t = requested_transport;
    // Developer/test override; same values as LABRADOR_TRANSPORT_*.
    const char *env = getenv("LABRADOR_TRANSPORT");
    if(env && env[0] >= '1' && env[0] <= '3' && env[1] == '\0'){
        t = env[0] - '0';
        LIBRADOR_LOG(LOG_WARNING, "transport overridden to %d via LABRADOR_TRANSPORT\n", t);
    }
    if(t == LABRADOR_TRANSPORT_AUTO){
#if defined(__APPLE__)
        t = LABRADOR_TRANSPORT_BULK;
#elif defined(__ANDROID__)
        // Phone USB stacks haven't been exercised with six concurrent iso
        // pipes; hold Android at the single-endpoint transport until iso6
        // is validated on-device.
        t = LABRADOR_TRANSPORT_ISO1;
#else
        // Windows (64- and 32-bit) and Linux/Pi: iso6.  Cross-endpoint
        // arrival order stopped mattering when frames became per-endpoint
        // queues assembled in lockstep and validated against the meta
        // stream's seq+checksum (the ordering problem that once forced the
        // Qt app onto libusbK.dll on Windows).
        t = LABRADOR_TRANSPORT_ISO6;
#endif
    }
#if defined(__APPLE__) && !defined(LIBRADOR_MACOS_ALLOW_ISO)
    // Opening a full-speed isochronous pipe kernel-panics macOS Tahoe
    // (IOUSBHostFamily's getEndpointMult dereferences the NULL SuperSpeed
    // companion descriptor for full-speed iso endpoints).  Bulk only.
    if(t != LABRADOR_TRANSPORT_BULK){
        LIBRADOR_LOG(LOG_WARNING, "iso transports are disabled on macOS (Tahoe kernel panic); using bulk\n");
        t = LABRADOR_TRANSPORT_BULK;
    }
#endif
    active_transport = t;
    return t;
}

int usbCallHandler::configure_transport_endpoints(){
    switch(active_transport){
    case LABRADOR_TRANSPORT_ISO6:
        num_data_endpoints = AIO_EP_ISO6_COUNT;
        data_packet_size = AIO_ISO6_PACKET_SIZE;
        for(int k = 0; k < AIO_EP_ISO6_COUNT; k++) pipeID[k] = AIO_EP_ISO6_FIRST + k;
        meta_pipeID = AIO_EP_ISO6_META;
        claimed_iface = AIO_IFACE_ISO6;
        break;
    case LABRADOR_TRANSPORT_ISO1:
        num_data_endpoints = 1;
        data_packet_size = ISO_PACKET_SIZE;
        pipeID[0] = AIO_EP_ISO1;
        meta_pipeID = AIO_EP_ISO1_META;
        claimed_iface = AIO_IFACE_ISO1;
        break;
    case LABRADOR_TRANSPORT_BULK:
        num_data_endpoints = 0;
        meta_pipeID = 0;
        claimed_iface = AIO_IFACE_BULK;
        break;
    default:
        return -1;
    }
    return 0;
}

// should only be called if iso_polling_thread is not active.  This means either the thread has never been set up OR it was previously set up but has exited iso_polling_function.
int usbCallHandler::setup_usb_iso(){
    LIBRADOR_LOG(LOG_DEBUG, "usbCallHandler::setup_usb_iso()\n");
    if(iso_polling_thread) {
        LIBRADOR_LOG(LOG_DEBUG, "iso polling thead already exists");
        return -1;
    }
    int error = start_streaming();
    if(error) {
        LIBRADOR_LOG(LOG_WARNING, "setup_usb_iso failed (%d)\n", error);
        return error;
    }
    LIBRADOR_LOG(LOG_DEBUG, "streaming started on transport %d", active_transport);
    ingest_stop = false;
    ingest_thread = new std::thread(&usbCallHandler::ingest_thread_function, this);
    iso_polling_thread = new std::thread(&usbCallHandler::iso_polling_function, this, ctx);
    iso_thread_active = true;
    return 0;
}

int usbCallHandler::total_inflight_transfers(){
    if(active_transport == LABRADOR_TRANSPORT_BULK) return NUM_BULK_CTX;
    return (num_data_endpoints + 1) * NUM_FUTURE_CTX;
}

int usbCallHandler::start_streaming(){
    resolve_transport();
    configure_transport_endpoints();
    reset_frame_state();
    iso_thread_shutdown_requested = false;
    iso_thread_shutdown_remaining_transfers = total_inflight_transfers();

    // Claim the transport's interface and select its streaming alt setting.
    // (Interface 0/alt 0 has no endpoints, so the initial claim during
    // connection is always safe - including on macOS Tahoe.)
    if(claimed_iface != 0 || !iface0_claimed){
        if(libusb_kernel_driver_active(handle, claimed_iface) == 1){
            libusb_detach_kernel_driver(handle, claimed_iface);
        }
        int error = libusb_claim_interface(handle, claimed_iface);
        if(error){
            LIBRADOR_LOG(LOG_WARNING, "claim_interface(%d) FAILED: %s\n", claimed_iface, libusb_error_name(error));
            return -3;
        }
        if(claimed_iface == 0){
            iface0_claimed = true;
        }
    }
    int error = libusb_set_interface_alt_setting(handle, claimed_iface, 1);
    if(error){
        LIBRADOR_LOG(LOG_WARNING, "set_interface_alt_setting(%d,1) FAILED: %s\n", claimed_iface, libusb_error_name(error));
        return -4;
    }

    alloc_iso_transfers();
    iso6_resync_wanted = false;
    iso6_resync_held = 0;
    int submitted = 0;
    return submit_iso_transfers(&submitted);
}

void usbCallHandler::alloc_iso_transfers(){
    alloc_transport = active_transport;
    if(active_transport == LABRADOR_TRANSPORT_BULK){
        // qa only (experiment): the bulk transfer size from LIBRADOR_BULK_XFER.
        int bulk_xfer = BULK_CTX_SIZE;
        if(const char *e = getenv("LIBRADOR_BULK_XFER")) bulk_xfer = atoi(e);
        if(bulk_xfer <= 0 || bulk_xfer > BULK_CTX_MAX || bulk_xfer % 64){
            fprintf(stderr, "LIBRADOR_BULK_XFER=%d: must be a multiple of 64 up to %d\n", bulk_xfer, BULK_CTX_MAX);
            abort();
        }
        LIBRADOR_LOG(LOG_WARNING, "bulk transfers of %d bytes (%d x %d queued)\n", bulk_xfer, NUM_BULK_CTX, bulk_xfer);
#ifdef _WIN32
        if(const char *raw = getenv("LIBRADOR_BULK_RAW_IO")){
            int supported = libusb_endpoint_supports_raw_io(handle, AIO_EP_BULK);
            int max_size = libusb_get_max_raw_io_transfer_size(handle, AIO_EP_BULK);
            int err = libusb_endpoint_set_raw_io(handle, AIO_EP_BULK, atoi(raw));
            LIBRADOR_LOG(LOG_WARNING, "bulk RAW_IO=%s: supported %d, max raw transfer %d, set: %s\n",
                         raw, supported, max_size, libusb_error_name(err));
            if(err != LIBUSB_SUCCESS) abort();
        }
#endif
        for(int n=0;n<NUM_BULK_CTX;n++){
            bulkCtx[n] = libusb_alloc_transfer(0);
            // Timeout 0: with a deep queue the tail URBs legitimately wait
            // longer than any finite timeout before data reaches them
            // (libusb counts from submission).  Teardown cancels explicitly.
            libusb_fill_bulk_transfer(bulkCtx[n], handle, AIO_EP_BULK, bulkBuffer[n], bulk_xfer, bulkCallback, this, 0);
        }
        return;
    }
    // Timeout 0: an isochronous transfer ends by itself once its frames have
    // passed (with an error if the device has gone), so a timeout can only
    // cancel one that is merely late.  libusb counts from submission, and a
    // transfer queued behind NUM_FUTURE_CTX - 1 others is due about
    // ISO_PACKETS_PER_CTX * NUM_FUTURE_CTX ms later, so a finite timeout
    // leaves only the slack beyond that for completion to be delivered.
    // Inside a VM that slack is regularly exceeded under iso6, and the
    // cancels did real damage: Windows aborts the whole pipe, restarting the
    // endpoint on an arbitrary frame, and VMware's emulated xHCI under Linux
    // fell out of step with the guest's driver until it was declared dead.
    for(int n=0;n<NUM_FUTURE_CTX;n++){
        for (int k=0;k<num_data_endpoints;k++){
            isoCtx[k][n] = libusb_alloc_transfer(ISO_PACKETS_PER_CTX);
            libusb_fill_iso_transfer(isoCtx[k][n], handle, pipeID[k], dataBuffer[k][n], data_packet_size*ISO_PACKETS_PER_CTX, ISO_PACKETS_PER_CTX, isoCallback, this, 0);
            libusb_set_iso_packet_lengths(isoCtx[k][n], data_packet_size);
        }
        metaCtx[n] = libusb_alloc_transfer(ISO_PACKETS_PER_CTX);
        libusb_fill_iso_transfer(metaCtx[n], handle, meta_pipeID, metaBuffer[n], AIO_META_PACKET_SIZE*ISO_PACKETS_PER_CTX, ISO_PACKETS_PER_CTX, metaIsoCallback, this, 0);
        libusb_set_iso_packet_lengths(metaCtx[n], AIO_META_PACKET_SIZE);
    }
}

// Submits every transfer allocated for the transport; *submitted counts the
// ones now in flight, also when one fails.
int usbCallHandler::submit_iso_transfers(int *submitted){
    *submitted = 0;
    if(active_transport == LABRADOR_TRANSPORT_BULK){
        for(int n=0;n<NUM_BULK_CTX;n++){
            int error = libusb_submit_transfer(bulkCtx[n]);
            if(!error) (*submitted)++;
            if(error){
                LIBRADOR_LOG(LOG_ERROR, "bulk submit #%d FAILED with error %d %s\n", n, error, libusb_error_name(error));
                return error;
            }
        }
        return 0;
    }
    for(int n=0;n<NUM_FUTURE_CTX;n++){
        for (int k=0;k<num_data_endpoints;k++){
            int error = libusb_submit_transfer(isoCtx[k][n]);
            if(!error) (*submitted)++;
            if(error){
                LIBRADOR_LOG(LOG_ERROR, "libusb_submit_transfer #%d:%d FAILED with error %d %s\n", n, k, error, libusb_error_name(error));
                return error;
            }
        }
        int error = libusb_submit_transfer(metaCtx[n]);
        if(!error) (*submitted)++;
        if(error){
            LIBRADOR_LOG(LOG_ERROR, "meta submit #%d FAILED with error %d %s\n", n, error, libusb_error_name(error));
            return error;
        }
    }
    return 0;
}

int usbCallHandler::send_control_transfer(uint8_t RequestType, uint8_t Request, uint16_t Value, uint16_t Index, uint16_t Length, unsigned char *LDATA){
    unsigned char *controlBuffer;

    if(!connected){
        LIBRADOR_LOG(LOG_DEBUG, "Control packet requested before device has connected!\n");
        return -1;
    }

    if (!LDATA){
        controlBuffer = inBuffer;
    }
    else controlBuffer = LDATA;

    // Diagnostic: per-request-code send counters, dumped with frame stats.
    extern std::atomic<uint32_t> g_ctrl_req_counts[256];
    g_ctrl_req_counts[Request]++;
    // EP0 can report a transient stall when a control request races the
    // streaming event thread (observed ~5% of the time on macOS with async
    // bulk transfers in flight; every occurrence recovers on an immediate
    // retry - the stall self-clears at the next SETUP).  All Labrador
    // vendor requests are idempotent, so retrying is safe.
    int error = 0;
    for(int attempt = 0; attempt < 3; attempt++){
        error = libusb_control_transfer(handle, RequestType, Request, Value, Index, controlBuffer, Length, 4000);
        if(error >= 0){
            return 0;
        }
        if(error != LIBUSB_ERROR_PIPE){
            break;
        }
        LIBRADOR_LOG(LOG_DEBUG, "control req 0x%02x stalled, retry %d\n", Request, attempt + 1);
    }
    LIBRADOR_LOG(LOG_WARNING, "send_control_transfer(req 0x%02x, val 0x%04x, idx 0x%04x, len %u) FAILED with error %s", Request, Value, Index, Length, libusb_error_name(error));
    connected = false;
    return error - 100;
}


int usbCallHandler::avrDebug(void){
    send_control_transfer_with_error_checks(0xc0, 0xa0, 0, 0, sizeof(unified_debug), nullptr);

    LIBRADOR_LOG(LOG_DEBUG, "unified debug is of size %lu\n", sizeof(unified_debug));

    unified_debug *udsPtr = (unified_debug *) inBuffer;
    uint16_t trfcnt0 = (udsPtr->trfcntH0 << 8) + udsPtr->trfcntL0;
    uint16_t trfcnt1 = (udsPtr->trfcntH1 << 8) + udsPtr->trfcntL1;
    uint16_t medianTrfcnt = (udsPtr->medianTrfcntH << 8) + udsPtr->medianTrfcntL;
    uint16_t outOfRange = (udsPtr->outOfRangeH << 8) + udsPtr->outOfRangeL;
    uint16_t counter = (udsPtr->counterH << 8) + udsPtr->counterL;
    uint16_t dma_ch0_cnt = (udsPtr->dma_ch0_cntH << 8) + udsPtr->dma_ch0_cntL;
    uint16_t dma_ch1_cnt = (udsPtr->dma_ch1_cntH << 8) + udsPtr->dma_ch1_cntL;

    LIBRADOR_LOG(LOG_DEBUG, "%s", udsPtr->header);
    LIBRADOR_LOG(LOG_DEBUG, "trfcnt0 = %d\n", trfcnt0);
    LIBRADOR_LOG(LOG_DEBUG, "trfcnt1 = %d\n", trfcnt1);
    LIBRADOR_LOG(LOG_DEBUG, "medianTrfcnt = %d\n", medianTrfcnt);
    LIBRADOR_LOG(LOG_DEBUG, "outOfRange = %d\n", outOfRange);
    LIBRADOR_LOG(LOG_DEBUG, "counter = %d\n", counter);
    LIBRADOR_LOG(LOG_DEBUG, "calValNeg = %d\n", udsPtr->calValNeg);
    LIBRADOR_LOG(LOG_DEBUG, "calValPos = %d\n", udsPtr->calValPos);
    LIBRADOR_LOG(LOG_DEBUG, "CALA = %d\n", udsPtr->CALA);
    LIBRADOR_LOG(LOG_DEBUG, "CALB = %d\n", udsPtr->CALB);
    LIBRADOR_LOG(LOG_DEBUG, "dma_ch0_cnt = %d\n", dma_ch0_cnt);
    LIBRADOR_LOG(LOG_DEBUG, "dma_ch1_cnt = %d\n", dma_ch1_cnt);

    return 0;
}

void usbCallHandler::spawn_daq_thread(int channel, int numToGet, int interval_samples, daqUnitOptions units_sel[2], const char* filename) {
    daq_thread_active = true;
    daq_thread = new std::thread(&usbCallHandler::drive_daq, this, channel, numToGet, interval_samples, units_sel, filename);
    daq_thread_active = false;
}

bool usbCallHandler::poll_daq_status() {
    if(daq_thread_active) {
        return true;
    } else {
        if(daq_thread && daq_thread->joinable()) {
            daq_thread->join();
            daq_thread = nullptr;
        }
        return false;
    }
}

void usbCallHandler::daq_for_channel(int channel, int numToGet, int interval_samples, daqUnitOptions unit_sel, SDL_IOStream* iostream) {
    o1buffer* buffer_for_daq;
    if(channel==1) {
        // TODO: mutex needed for deviceMode
        if(deviceMode==6) {
            buffer_for_daq = internal_o1_buffer_750;
        } else {
            buffer_for_daq = internal_o1_buffer_375_CHA;
        }
    } else {
        buffer_for_daq = internal_o1_buffer_375_CHB;
    }
    const char* volts_fmt_template = "%%.%dg ";
    char volts_fmt[8];
    if(deviceMode==7) {
        sprintf(volts_fmt, volts_fmt_template, 4);
    } else {
        sprintf(volts_fmt, volts_fmt_template, 3);
    }

    // Channel names in the app's canonical form (CH1/CH2, matching the
    // scope UI and the OSC1/OSC2 pins). Loaders keep accepting the legacy
    // "CH A"/"CH B" headers written before the rename.
    const char* ch_names[2] = {"CH1", "CH2"};
    SDL_IOprintf(iostream, "%s\n", ch_names[channel-1]);
    if(unit_sel==usbCallHandler::daqUnitOptions::Bits) {
        std::vector<double>* daq_vals = getMany_singleBit(channel, numToGet * 8, interval_samples, 0, true);
        for(auto it = (*daq_vals).rbegin(); it != (*daq_vals).rend(); it++)
            SDL_IOprintf(iostream, "%.0f ", *it);
    } else if (unit_sel==usbCallHandler::daqUnitOptions::Volts) {
        // volts
        std::vector<double>* daq_vals = getMany_double(channel, numToGet, interval_samples, 0, 0, true);
        for(auto it = (*daq_vals).rbegin(); it != (*daq_vals).rend(); it++)
            SDL_IOprintf(iostream, volts_fmt, *it);
    } else if (unit_sel==usbCallHandler::daqUnitOptions::ADC){
        // adc units
        int ix;
        for(int i = 0; i < numToGet; i++) {
            int i2 = buffer_for_daq->mostRecentAddressDAQ + (i - (numToGet-1)) * interval_samples;
            ix = i2 >= 0 ? i2 : i2 + buffer_for_daq->m_bufferLen;
            SDL_IOprintf(iostream, "%.0f ", buffer_for_daq->get_filtered_sample(ix, -1, 0, 0.0, false, true));
        }
    }
}

void usbCallHandler::drive_daq(int channel, int numToGet, int interval_samples, daqUnitOptions units_sel[2], const char * filepath) {
    LIBRADOR_LOG(LOG_DEBUG, "filepath: %s", filepath);
    SDL_IOStream* iostream = open_file(filepath);
    buffer_read_write_mutex.lock(); 
    if((channel == 1) || (channel == 3)) {
        if(deviceMode==6) {
            internal_o1_buffer_750->copy_to_daq();
        } else {
            internal_o1_buffer_375_CHA->copy_to_daq();
        }
    }
    if((channel == 2) || (channel == 3)) {
        internal_o1_buffer_375_CHB->copy_to_daq();
    }
    buffer_read_write_mutex.unlock(); 

    if((channel == 1) || (channel == 3)) {
        daq_for_channel(1, numToGet, interval_samples, units_sel[0], iostream);
    }
    if(channel == 3) {
        SDL_IOprintf(iostream, "\n");
    }
    if((channel == 2) || (channel == 3)) {
        daq_for_channel(2, numToGet, interval_samples, units_sel[1], iostream);
    }

    SDL_CloseIO(iostream);

    if(librador_get_host_hooks().daq_file_written)
        librador_get_host_hooks().daq_file_written(filepath);
}

std::vector<double>* usbCallHandler::getMany_double(int channel, int numToGet, double interval_samples, int delay_sample, int filter_mode, bool daq) {
    std::vector<double>* temp_to_return = nullptr;
    if(!daq)
        buffer_read_write_mutex.lock(); //daq uses its own buffer (TODO: same for paused state?)
    int delay_including_trigger;
    bool single_shot_reached = false;
    int trigger_delay = 0;
    switch(deviceMode){
    case 0:
        if(channel == 1) {
            delay_including_trigger = internal_o1_buffer_375_CHA->getDelayIncludingFromTrigger(delay_sample, round(interval_samples * numToGet), daq);
            temp_to_return = internal_o1_buffer_375_CHA->getMany_double(numToGet, interval_samples, delay_including_trigger, filter_mode, current_scope_gain, false, daq);
        }
        break;
    case 1:
        if(channel == 1) {
            delay_including_trigger = internal_o1_buffer_375_CHA->getDelayIncludingFromTrigger(delay_sample, round(interval_samples * numToGet), daq, &single_shot_reached, &trigger_delay);
            internal_o1_buffer_375_CHB->setPaused(single_shot_reached,-trigger_delay, true); // dont multiply trigger_delay by 8 b/c each sample of the buffer is 8 subsamples
            temp_to_return = internal_o1_buffer_375_CHA->getMany_double(numToGet, interval_samples, delay_including_trigger, filter_mode, current_scope_gain, false, daq);
        }
        break;
    case 2:
        if(internal_o1_buffer_375_CHA->isTriggeringEnabled() && ((channel==1) || ((channel == 2) && !internal_o1_buffer_375_CHB->getPaused()))) {
            delay_including_trigger = internal_o1_buffer_375_CHA->getDelayIncludingFromTrigger(delay_sample, round(interval_samples * numToGet), daq, &single_shot_reached, &trigger_delay);
            internal_o1_buffer_375_CHB->setPaused(single_shot_reached,-trigger_delay,true); // only relevant when single-shot triggering
        } else if (internal_o1_buffer_375_CHB->isTriggeringEnabled() && ((channel==2) || ((channel == 1) && !internal_o1_buffer_375_CHA->getPaused()))) {
            delay_including_trigger = internal_o1_buffer_375_CHB->getDelayIncludingFromTrigger(delay_sample, round(interval_samples * numToGet), daq, &single_shot_reached, &trigger_delay);
            internal_o1_buffer_375_CHA->setPaused(single_shot_reached,-trigger_delay,true);// only relevant when single-shot triggering
        } else {
            delay_including_trigger = delay_sample;
        }
        if(channel == 1) temp_to_return = internal_o1_buffer_375_CHA->getMany_double(numToGet, interval_samples, delay_including_trigger, filter_mode, current_scope_gain, false, daq);
        else if (channel == 2) temp_to_return = internal_o1_buffer_375_CHB->getMany_double(numToGet, interval_samples, delay_including_trigger, filter_mode, current_scope_gain, false, daq);
        break;
    case 6:
        if(channel == 1){
            delay_including_trigger = internal_o1_buffer_750->getDelayIncludingFromTrigger(delay_sample, round(interval_samples * numToGet), daq);
            temp_to_return = internal_o1_buffer_750->getMany_double(numToGet, interval_samples, delay_including_trigger, filter_mode, current_scope_gain, false, daq);
        }
        break;
    case 7:
        if(channel == 1) {
            delay_including_trigger = internal_o1_buffer_375_CHA->getDelayIncludingFromTrigger(delay_sample, round(interval_samples * numToGet), daq);
            temp_to_return = internal_o1_buffer_375_CHA->getMany_double(numToGet, interval_samples, delay_including_trigger, filter_mode, current_scope_gain, true, daq);
        }
        break;
    }
    if(!daq)
        buffer_read_write_mutex.unlock();
    return temp_to_return;
}

std::vector<double> * usbCallHandler::getMany_singleBit(int channel, int numToGet, double interval_subsamples, int delay_subsamples, bool daq){
    std::vector<double>* temp_to_return = nullptr;
    if(!daq) // daq uses its own buffer
        buffer_read_write_mutex.lock();
    switch(deviceMode){
    case 1:
        if(channel == 2) 
        {
            int delay_including_trigger;
            if(internal_o1_buffer_375_CHA->isTriggeringEnabled()) {
                bool single_shot_reached = false;
                int trigger_delay = 0;
                delay_including_trigger = internal_o1_buffer_375_CHA->getDelayIncludingFromTrigger(static_cast<int>(round(delay_subsamples/8.)), round(interval_subsamples/8. * numToGet), daq, &single_shot_reached, &trigger_delay) * 8;
                internal_o1_buffer_375_CHB->setPaused(single_shot_reached,-trigger_delay, true); // dont multiply trigger_delay by 8 b/c each sample of the buffer is 8 subsamples
            } else {
                delay_including_trigger = delay_subsamples;
            }

            temp_to_return = internal_o1_buffer_375_CHB->getMany_singleBit(numToGet, interval_subsamples, delay_including_trigger, daq);
            internal_o1_buffer_375_CHB->UartDecode();
        }
        break;
    case 3:
        if(channel == 1)
        {
            temp_to_return = internal_o1_buffer_375_CHA->getMany_singleBit(numToGet, interval_subsamples, delay_subsamples, daq);
            internal_o1_buffer_375_CHA->UartDecode();
        }
        break;
    case 4:
        if(channel == 1){
            temp_to_return = internal_o1_buffer_375_CHA->getMany_singleBit(numToGet, interval_subsamples, delay_subsamples, daq);
            internal_o1_buffer_375_CHA->UartDecode();
        }
        else if (channel == 2){
            temp_to_return = internal_o1_buffer_375_CHB->getMany_singleBit(numToGet, interval_subsamples, delay_subsamples, daq);
            internal_o1_buffer_375_CHB->UartDecode();
        }
        try
        {
            m_i2c_decoder->run();
        }
        catch(...)
        {
            LIBRADOR_LOG(LOG_DEBUG, "Resetting I2C");
            m_i2c_decoder->reset();
        }
        break;
    }
    if(!daq)
        buffer_read_write_mutex.unlock();
    return temp_to_return;
}

char * usbCallHandler::getUart_String(int channel, bool* parity_check)
{
    char * temp_to_return = nullptr;
    if(channel == 1)
        temp_to_return = internal_o1_buffer_375_CHA->getUart_String(parity_check);
    else if (channel == 2)
        temp_to_return = internal_o1_buffer_375_CHB->getUart_String(parity_check);
    return temp_to_return;
}

std::vector<double> *usbCallHandler::getMany_sincelast(int channel, int feasible_window_begin, int feasible_window_end, int interval_samples, int filter_mode){
    std::vector<double>* temp_to_return = nullptr;
    buffer_read_write_mutex.lock();
        switch(deviceMode){
        case 0:
            if(channel == 1) temp_to_return = internal_o1_buffer_375_CHA->getSinceLast(feasible_window_begin, feasible_window_end, interval_samples, filter_mode, current_scope_gain, false);
            break;
        case 1:
            if(channel == 1) temp_to_return = internal_o1_buffer_375_CHA->getSinceLast(feasible_window_begin, feasible_window_end, interval_samples, filter_mode, current_scope_gain, false);
            break;
        case 2:
            if(channel == 1) temp_to_return = internal_o1_buffer_375_CHA->getSinceLast(feasible_window_begin, feasible_window_end, interval_samples, filter_mode, current_scope_gain, false);
            else if (channel == 2) temp_to_return = internal_o1_buffer_375_CHB->getSinceLast(feasible_window_begin, feasible_window_end, interval_samples, filter_mode, current_scope_gain, false);
            break;
        case 6:
            if(channel == 1) temp_to_return = internal_o1_buffer_750->getSinceLast(feasible_window_begin, feasible_window_end, interval_samples, filter_mode, current_scope_gain, false);
            break;
        case 7:
            if(channel == 1) temp_to_return = internal_o1_buffer_375_CHA->getSinceLast(feasible_window_begin, feasible_window_end, interval_samples, filter_mode, current_scope_gain, false);
            break;
        }
        buffer_read_write_mutex.unlock();
        return temp_to_return;

}

int usbCallHandler::send_device_reset(){
    libusb_reset_device(handle);
    return 0;
}


int usbCallHandler::set_device_mode(int mode){
    if((mode < 0) || (mode > MAX_SUPPORTED_DEVICE_MODE)){
        return -1;
    }
    deviceMode = mode;
    send_control_transfer_with_error_checks(0x40, 0xa5, (mode == 5 ? 0 : mode), gainMask, 0, nullptr);

    send_function_gen_settings(1);
    send_function_gen_settings(2);

    // Under the same lock as dispatch_frame(), so no frame lands between two
    // of the resets.  One that did (a frame added to CH1's buffer just after
    // its reset and to CH2's just before) left CH2's sample count a frame or
    // more behind CH1's until the next mode change: a window copied from both
    // at the same start index was then out of step by that much, or, when it
    // reached past CH2's newest sample, failed ("copying CH2's samples failed
    // (code -2)" in the QA).
    {
        std::lock_guard<std::mutex> lock(buffer_read_write_mutex);
        internal_o1_buffer_375_CHA->reset(false);
        internal_o1_buffer_375_CHA->resetTrigger(current_scope_gain, deviceMode==7);
        internal_o1_buffer_375_CHB->reset(false);
        internal_o1_buffer_375_CHB->resetTrigger(current_scope_gain, deviceMode==7);
        internal_o1_buffer_750->reset(false);
        internal_o1_buffer_750->resetTrigger(current_scope_gain, deviceMode==7);
    }

    return 0;
}

// Oscilloscope calibration, ported from the Qt app's 3-stage wizard
// (Desktop_Interface/mainwindow.cpp on_actionCalibrate_triggered / calibrateStage2/3).
// vref is the measured reference-bias voltage exactly as the Qt app persists it
// (QSettings CalibrateVrefCHx, neutral 1.65); the value entering the conversion
// is 3.3 - vref, matching Qt's readSettingsFile (mainwindow.cpp:1510-1516).
// gain_scale multiplies the nominal R4/(R3+R4) = 75k/1075k frontend divider
// gain (neutral 1.0); Qt persists the absolute product as CalibrateGainCHx.
// Channel 1 owns both the 375 kSps CHA buffer and the 750 kSps buffer (Qt
// mirrors the CH1 values onto internalBuffer750); channel 2 owns the CHB
// buffer.  No USB traffic: safe to call while disconnected (e.g. restoring
// stored calibration at app startup).
int usbCallHandler::set_channel_calibration(int channel, double vref, double gain_scale){
    const double nominal_frontend_gain = 75.0/1075.0; // R4/(R3+R4), xmega.h
    double new_voltage_ref = 3.3 - vref;
    double new_frontend_gain = nominal_frontend_gain * gain_scale;

    buffer_read_write_mutex.lock();
    if(channel == 1){
        internal_o1_buffer_375_CHA->voltage_ref = new_voltage_ref;
        internal_o1_buffer_375_CHA->frontendGain = new_frontend_gain;
        internal_o1_buffer_750->voltage_ref = new_voltage_ref;
        internal_o1_buffer_750->frontendGain = new_frontend_gain;
    } else if(channel == 2){
        internal_o1_buffer_375_CHB->voltage_ref = new_voltage_ref;
        internal_o1_buffer_375_CHB->frontendGain = new_frontend_gain;
    } else {
        buffer_read_write_mutex.unlock();
        return -1; //Invalid channel
    }
    buffer_read_write_mutex.unlock();

    // ADC-unit trigger levels are derived from the conversion constants
    // (inverseSampleConvert); recompute them like setSettingsForChannel does.
    if(channel == 1){
        internal_o1_buffer_375_CHA->resetTrigger(current_scope_gain, deviceMode==7);
        internal_o1_buffer_750->resetTrigger(current_scope_gain, deviceMode==7);
    } else {
        internal_o1_buffer_375_CHB->resetTrigger(current_scope_gain, deviceMode==7);
    }
    return 0;
}

// PSU calibration offset (Qt genericusbdriver psu_offset / QSettings
// CalibratePsu).  Stored here and applied on the next set_psu_voltage call,
// exactly like Qt.  No USB traffic: safe to call while disconnected.
int usbCallHandler::set_psu_calibration_offset(double offset){
    psu_offset = offset;
    return 0;
}

#define CAL_PAGE_SIZE 32
#define CAL_MAGIC0 0xCA
#define CAL_MAGIC1 0x1B
#define CAL_VERSION 1

int usbCallHandler::save_calibration_to_device(double vref_ch1, double gain_scale_ch1,
        double vref_ch2, double gain_scale_ch2, double psu_offset_v){
    unsigned char page[CAL_PAGE_SIZE];
    float vals[5] = {(float)vref_ch1, (float)gain_scale_ch1,
                     (float)vref_ch2, (float)gain_scale_ch2, (float)psu_offset_v};
    memset(page, 0xFF, sizeof page);
    page[0] = CAL_MAGIC0;
    page[1] = CAL_MAGIC1;
    page[2] = CAL_VERSION;
    page[3] = 0;
    memcpy(&page[4], vals, sizeof vals);
    unsigned char cs = 0;
    for(int i = 0; i < 24; i++) cs ^= page[i];
    page[24] = cs;
    send_control_transfer_with_error_checks(0x40, 0xac, 0, 0, CAL_PAGE_SIZE, page);
    return 0;
}

int usbCallHandler::load_calibration_from_device(double *vref_ch1, double *gain_scale_ch1,
        double *vref_ch2, double *gain_scale_ch2, double *psu_offset_v){
    unsigned char page[CAL_PAGE_SIZE];
    memset(page, 0, sizeof page);
    send_control_transfer_with_error_checks(0xc0, 0xad, 0, 0, CAL_PAGE_SIZE, page);
    if(page[0] != CAL_MAGIC0 || page[1] != CAL_MAGIC1 || page[2] != CAL_VERSION){
        return 1;   // nothing (valid) stored - e.g. fresh EEPROM after a DFU erase
    }
    unsigned char cs = 0;
    for(int i = 0; i < 24; i++) cs ^= page[i];
    if(cs != page[24]){
        return 1;
    }
    float vals[5];
    memcpy(vals, &page[4], sizeof vals);
    if(vref_ch1) *vref_ch1 = vals[0];
    if(gain_scale_ch1) *gain_scale_ch1 = vals[1];
    if(vref_ch2) *vref_ch2 = vals[2];
    if(gain_scale_ch2) *gain_scale_ch2 = vals[3];
    if(psu_offset_v) *psu_offset_v = vals[4];
    return 0;
}

double usbCallHandler::get_scope_gain(){
    return current_scope_gain;
}

// Caller must hold buffer_read_write_mutex.
static o1buffer* scope_buffer_for_channel(int channel, int mode, o1buffer* cha, o1buffer* chb, o1buffer* b750){
    if(channel == 1){
        if(mode == 6) return b750;
        if(mode == 0 || mode == 1 || mode == 2) return cha;
    } else if(channel == 2 && mode == 2){
        return chb;
    }
    return nullptr;
}

int usbCallHandler::get_analog_sample_count(int channel, uint64_t *count){
    std::lock_guard<std::mutex> lock(buffer_read_write_mutex);
    o1buffer *buf = scope_buffer_for_channel(channel, deviceMode, internal_o1_buffer_375_CHA, internal_o1_buffer_375_CHB, internal_o1_buffer_750);
    if(!buf) return -1;
    *count = buf->total_samples_added;
    return 0;
}

int usbCallHandler::get_analog_window(int channel, uint64_t start, int num, double *volts_out){
    std::vector<int> raw(num > 0 ? num : 0);
    std::lock_guard<std::mutex> lock(buffer_read_write_mutex);
    o1buffer *buf = scope_buffer_for_channel(channel, deviceMode, internal_o1_buffer_375_CHA, internal_o1_buffer_375_CHB, internal_o1_buffer_750);
    if(!buf) return -1;
    if(!buf->copyWindow(start, num, raw.data())) return -2;
    for(int i = 0; i < num; i++){
        volts_out[i] = buf->toVolts(raw[i], current_scope_gain);
    }
    return 0;
}

int usbCallHandler::set_gain(double newGain){
    LIBRADOR_LOG(LOG_DEBUG, "set_gain(%f) -> 0xa5 DMA rebuild\n", newGain);
    //See XMEGA_AU Manual, page 359.  ADC.CTRL.GAIN.
    if(newGain==0.5) gainMask = 0x07;
    else if (newGain == 1) gainMask = 0x00;
    else if (newGain == 2) gainMask = 0x01;
    else if (newGain == 4) gainMask = 0x02;
    else if (newGain == 8) gainMask = 0x03;
    else if (newGain == 16) gainMask = 0x04;
    else if (newGain == 32) gainMask = 0x05;
    else if (newGain == 64) gainMask = 0x06;
    else{
      LIBRADOR_LOG(LOG_ERROR, "Invalid gain value.  Valid values are 0.1, 1, 2, 4, 8, 16, 32, 64\n");
      return -1;
    }

    gainMask = gainMask << 2;
    gainMask |= (gainMask << 8);
    send_control_transfer_with_error_checks(0x40, 0xa5, deviceMode, gainMask, 0, nullptr);
    current_scope_gain = newGain;
    return 0;
}

int usbCallHandler::update_function_gen_settings(int channel, unsigned char *sampleBuffer, int numSamples, double usecs_between_samples, double amplitude_v, double offset_v){


    //Parse the channel
    fGenSettings *fGenSelected;
    if(channel == 1){
        fGenSelected = &functionGen_CH1;
    } else if (channel == 2){
        fGenSelected = &functionGen_CH2;
    } else {
        return -1;  //Invalid channel
    }

    //Update number of samples.
    fGenSelected->numSamples = numSamples;

    //Does the output amplifier need to be enabled?
    if ((amplitude_v+offset_v) > FGEN_LIMIT){
        amplitude_v = amplitude_v / 3;
        offset_v = offset_v / 3;
        if(channel == 1){
            fGenTriple |= 0b00000001;
        } else {
            fGenTriple |= 0b00000010;
        }
    }
    else {
        if(channel == 1){
            fGenTriple &= 0b11111110;
        } else {
            fGenTriple &= 0b11111101;
        }
    }

    //Fiddle with the waveform to deal with the fact that the Xmega has a minimum DAC output value.
    double amplitude_sample = (amplitude_v * 255) / FGEN_LIMIT;
    double offset_sample = (offset_v * 255) / FGEN_LIMIT;
    if (offset_sample < FGEN_SAMPLE_MIN){  //If the offset is in the range where the XMEGA output cannot physically drive the signal that low...
        if (amplitude_sample > FGEN_SAMPLE_MIN){  //And the amplitude of the signal can go above this minimum range
            amplitude_sample -= FGEN_SAMPLE_MIN;  //Then reduce the amplitude and add a small offset
            }
        else {
            amplitude_sample = 0;
        }
        offset_sample = FGEN_SAMPLE_MIN;
    }

    //Apply amplitude and offset scaling to all samples.
    double tempDouble;
    for (int i=0;i<numSamples;i++){
        tempDouble = (double) sampleBuffer[i];
        tempDouble *= amplitude_sample;
        tempDouble /= 255.0;
        tempDouble += offset_sample;
        fGenSelected->samples[i] = (uint8_t) tempDouble;
    }

    //Calculate timer values
    int validClockDivs[7] = {1, 2, 4, 8, 64, 256, 1024};

    int clkSetting;
    for(clkSetting = 0; clkSetting<7; clkSetting++){
        if ( ((XMEGA_MAIN_FREQ * usecs_between_samples)/(1000000 * validClockDivs[clkSetting])) < 65535)
             break;
    }
    fGenSelected->timerPeriod = (usecs_between_samples * XMEGA_MAIN_FREQ) / (1000000 * validClockDivs[clkSetting]) - 0.5;
    fGenSelected->clockDividerSetting = clkSetting + 1;

    return 0;
}

int usbCallHandler::get_function_gen_output(int channel, double *frequency_hz, double *v_min, double *v_max){
    const fGenSettings *fGen;
    bool triple;
    if(channel == 1){
        fGen = &functionGen_CH1;
        triple = fGenTriple & 0b00000001;
    } else if (channel == 2){
        fGen = &functionGen_CH2;
        triple = fGenTriple & 0b00000010;
    } else {
        return -1;  //Invalid channel
    }
    if(fGen->numSamples == 0){
        return -2; //Channel not initialised
    }

    // Mirrors update_function_gen_settings: the timer overflows every
    // timerPeriod + 1 ticks of XMEGA_MAIN_FREQ / divider, one sample per overflow.
    const int validClockDivs[7] = {1, 2, 4, 8, 64, 256, 1024};
    const double ticks = (double)validClockDivs[fGen->clockDividerSetting - 1] * (fGen->timerPeriod + 1.0);
    *frequency_hz = XMEGA_MAIN_FREQ / (ticks * fGen->numSamples);

    uint8_t lo = 255, hi = 0;
    for(int i = 0; i < fGen->numSamples; i++){
        lo = std::min(lo, fGen->samples[i]);
        hi = std::max(hi, fGen->samples[i]);
    }
    const double volts_per_sample = FGEN_LIMIT / 255.0 * (triple ? 3.0 : 1.0);
    *v_min = lo * volts_per_sample;
    *v_max = hi * volts_per_sample;
    return 0;
}

int usbCallHandler::send_function_gen_settings(int channel){
    if(channel == 1){
        if(functionGen_CH1.numSamples == 0){
            return -1; //Channel not initialised
        }
        send_control_transfer_with_error_checks(0x40, 0xa2, functionGen_CH1.timerPeriod, functionGen_CH1.clockDividerSetting, functionGen_CH1.numSamples, functionGen_CH1.samples);
    } else if (channel == 2){
        if(functionGen_CH2.numSamples == 0){
            return -1; //Channel not initialised
        }
        send_control_transfer_with_error_checks(0x40, 0xa1, functionGen_CH2.timerPeriod, functionGen_CH2.clockDividerSetting, functionGen_CH2.numSamples, functionGen_CH2.samples);
    } else {
        return -2; //Invalid channel
    }
    send_control_transfer_with_error_checks(0x40, 0xa4, fGenTriple, 0, 0, nullptr);
    return 0;
}

int usbCallHandler::set_psu_voltage(double voltage){
    // psu_offset mirrors the Qt app's PSU calibration
    // (Desktop_Interface/genericusbdriver.cpp setPsu: vinp = (voltage - psu_offset)/11).
    double vinp = (voltage - psu_offset)/11;
    double vinn = 0;

    uint8_t dutyPsu = (uint8_t) ((vinp - vinn)/vref_psu * gain_psu * PSU_ADC_TOP);


    if ((dutyPsu>106) || (dutyPsu<21)){
        return -1;  //Out of range
    }
    send_control_transfer_with_error_checks(0x40, 0xa3, dutyPsu, 0, 0, nullptr);
    return 0;
}

int usbCallHandler::set_digital_state(uint8_t digState){
    send_control_transfer_with_error_checks(0x40, 0xa6, digState, 0, 0, nullptr);
    return 0;
}

int usbCallHandler::reset_device(bool goToBootloader){
    LIBRADOR_LOG(LOG_DEBUG, "resetting device: unimportant error LIBUSB_ERROR_NO_DEVICE for send_control_transfer expected"); // only if goToBootloader?
    send_control_transfer_with_error_checks(0x40, 0xa7, (goToBootloader ? 1 : 0), 0, 0, nullptr);
    return 0;
}

// Version and variant are immutable for the lifetime of a connection (and
// the wedge state their 179/176 sentinels probe for is a connect-time
// condition), so they are fetched over USB once and cached until teardown.
// Callers poll these every UI frame; without the cache that was 60-120
// control transfers per second racing the streaming event thread.
uint16_t usbCallHandler::get_firmware_version(){
    if(fw_version_cached){
        return cached_firmver;
    }
    send_control_transfer_with_error_checks(0xc0, 0xa8, 0, 0, 2, nullptr);
    cached_firmver = *((uint16_t *) inBuffer);
    fw_version_cached = true;
    return cached_firmver;
}

uint8_t usbCallHandler::get_firmware_variant(){
    if(fw_variant_cached){
        return cached_variant;
    }
    send_control_transfer_with_error_checks(0xc0, 0xa9, 0, 0, 1, nullptr);
    cached_variant = *((uint8_t *) inBuffer);
    fw_variant_cached = true;
    return cached_variant;
}

double usbCallHandler::get_samples_per_second(){
    switch(deviceMode){
    case 0:
        return (double)(375000.0);
    case 1:
        return (double)(375000.0);
    case 2:
        return (double)(375000.0);
    case 3:
        return (double)(375000.0);
    case 4:
        return (double)(375000.0);
    case 6:
        return (double)(750000.0);
    case 7:
        return (double)(375000.0);
    default:
        return 0;
    }
}

int usbCallHandler::set_synchronous_pause_state(bool newState){
    if(newState && !synchronous_pause_state){
        buffer_read_write_mutex.lock();
        synchronous_pause_state = true;
        return 0;
    }

    if(!newState && synchronous_pause_state){
        buffer_read_write_mutex.unlock();
        return 0;
    }

    //Otherwise you don't want to do anything.  You should never set the state twice.
    return 1;
}

int usbCallHandler::setPaused(int channel, bool is_paused)
{
    if(channel==1){
        // CH1 owns two capture buffers (375 kSps and the mode-6 750 kSps).
        // Drive both in lockstep, like setSettingsForChannel, so a pause set
        // in one mode can't leave the other buffer stranded frozen after a
        // device-mode change.
        int ret_750 = internal_o1_buffer_750->setPaused(is_paused);
        int ret_375 = internal_o1_buffer_375_CHA->setPaused(is_paused);
        return ret_750 ? ret_750 : ret_375;
    } else if (channel == 2){
        return internal_o1_buffer_375_CHB->setPaused(is_paused);
    }
    return 1;
}

bool usbCallHandler::getPaused(int channel)
{
    if(channel==1){
        if(deviceMode==6)
            return internal_o1_buffer_750->getPaused();
        else
            return internal_o1_buffer_375_CHA->getPaused();
    } else if (channel == 2){
        return internal_o1_buffer_375_CHB->getPaused();
    }
    return 1;
}

void usbCallHandler::setTriggerSettings(int channel, o1buffer::trigger_settings new_trigger_settings)
{
    setSettingsForChannel(channel, new_trigger_settings, internal_o1_buffer_375_CHA, internal_o1_buffer_375_CHB, internal_o1_buffer_750, &o1buffer::setTriggerSettings);
}

void usbCallHandler::setVirtualTransformSettings(int channel, o1buffer::virtual_transform_settings new_virtual_transform_settings)
{
    setSettingsForChannel(channel, new_virtual_transform_settings, internal_o1_buffer_375_CHA, internal_o1_buffer_375_CHB, internal_o1_buffer_750, &o1buffer::setVirtualTransformSettings);
}

void usbCallHandler::setUartDecodeSettings(int channel, UartSettings new_settings)
{
    if(channel==1)
        internal_o1_buffer_375_CHA->setUartDecodeSettings(new_settings);
    else if (channel==2)
        internal_o1_buffer_375_CHB->setUartDecodeSettings(new_settings);
}
 char *usbCallHandler::getI2c_String()
{
    return m_i2c_decoder->getString();
}

void usbCallHandler::setI2cIsDecoding(bool new_decode_on){
    m_i2c_decoder->setIsDecoding(new_decode_on);
}

bool usbCallHandler::isoThreadIsActive(){
    return iso_thread_active;
}

#ifdef PLATFORM_ANDROID
int usbCallHandler::flashFirmware(int file_descriptor){

    libusb_device *device_ptr;

    init_libusb();
    int error = setup_usb_control(file_descriptor);

    char firmware_filename[64];
    snprintf(firmware_filename, sizeof firmware_filename, "labrafirm_%04X_%02x.hex",
        EXPECTED_FIRMWARE_VERSION, DEFINED_EXPECTED_VARIANT);

    if(!librador_get_host_hooks().prepare_firmware_hex){
        LIBRADOR_LOG(LOG_ERROR, "No prepare_firmware_hex host hook registered; cannot flash");
        return -1;
    }
    const char* firmware_copy_filepath
        = librador_get_host_hooks().prepare_firmware_hex(firmware_filename);
    if(!firmware_copy_filepath){
        LIBRADOR_LOG(LOG_ERROR, "prepare_firmware_hex failed for %s", firmware_filename);
        return -1;
    }

    LIBRADOR_LOG(LOG_DEBUG, "FLASHING %s", firmware_copy_filepath);

    //Set up interface to dfuprog
    int exit_code;
    char command[256];

    //Run stage 1
    LIBRADOR_LOG(LOG_DEBUG, "\n\nFlashing Firmware, stage 1.\n\n");
    snprintf(command, sizeof command, "dfu-programmer atxmega32a4u erase --force --debug 300");
    exit_code = dfuprog_virtual_cmd(command, device_ptr, handle, ctx, 0);
    if (exit_code) {
        LIBRADOR_LOG(LOG_WARNING, "ERROR ERASING FIRMWARE.");
    } else {
        LIBRADOR_LOG(LOG_DEBUG, "ERASED FIRMWARE.");
    }
    ctx = nullptr;
    handle=  nullptr;

    init_libusb();
    error = setup_usb_control(file_descriptor);

    //Run stage 2
    LIBRADOR_LOG(LOG_DEBUG, "\n\nFlashing Firmware, stage 2.\n\n");
    snprintf(command, sizeof command, "dfu-programmer atxmega32a4u flash %s --debug 300", firmware_copy_filepath);
    exit_code = dfuprog_virtual_cmd(command, device_ptr, handle, ctx, 0);
    if (exit_code) {
        LIBRADOR_LOG(LOG_WARNING, "\n\n\nERROR WRITING NEW FIRMWARE TO DEVICE.\n\n\n");
        //return exit_code+200;
    }
    ctx = nullptr;
    handle=  nullptr;

    init_libusb();
    error = setup_usb_control(file_descriptor);

    //Run stage 3
    LIBRADOR_LOG(LOG_DEBUG, "\n\nFlashing Firmware, stage 3.\n\n");
    dfu_launch();
    starting_after_flash = true;
    return 0;
}
#endif

void usbCallHandler::dfu_launch() {
    LIBRADOR_LOG(LOG_DEBUG, "\n\n\nDFU LAUNCH.\n\n\n");
    int exit_code;
    char command[256];
    snprintf(command, sizeof command, "dfu-programmer atxmega32a4u launch");
#ifdef PLATFORM_ANDROID
    libusb_device *device_ptr;
    exit_code = dfuprog_virtual_cmd(command, device_ptr, handle, ctx, 0);
    ctx = nullptr;
    handle=  nullptr;
#else
    // Desktop libdfuprog manages its own libusb context and device discovery
    exit_code = dfuprog_virtual_cmd(command);
#endif
    if (exit_code) {
        LIBRADOR_LOG(LOG_WARNING, "\n\n\n DFU LAUNCH ERROR\n\n\n");
        //return exit_code+300;
    }
}

int usbCallHandler::check_firmware_and_start_iso(){
    uint16_t firmver = get_firmware_version();
    LIBRADOR_LOG(LOG_DEBUG, "BOARD IS RUNNING FIRMWARE VERSION 0x%04hx", firmver);
    LIBRADOR_LOG(LOG_DEBUG, "EXPECTING FIRMWARE VERSION 0x%04hx", EXPECTED_FIRMWARE_VERSION);

    uint8_t variant = get_firmware_variant();
    LIBRADOR_LOG(LOG_DEBUG, "FIRMWARE VARIANT = 0x%02hx", variant);
    LIBRADOR_LOG(LOG_DEBUG, "EXPECTED VARIANT = 0x%02hx", DEFINED_EXPECTED_VARIANT);

    if((firmver != EXPECTED_FIRMWARE_VERSION) || (variant != DEFINED_EXPECTED_VARIANT)){
        LIBRADOR_LOG(LOG_DEBUG, "Unexpected Firmware!!");
        if(librador_get_host_hooks().request_firmware_flash)
            librador_get_host_hooks().request_firmware_flash();
        return 1;
    }
    return setup_usb_iso();
}

void usbCallHandler::teardown_connection(){
    // Idempotent and thread-safe: may be reached concurrently by the flash
    // worker and the main thread's disconnect detection (SIGABRT from a
    // double std::thread::join otherwise).
    std::lock_guard<std::mutex> teardown_lock(teardown_mutex);
    connected = false;
    fw_version_cached = false;
    fw_variant_cached = false;
    if(data_frame_counter || frames_ok || frames_dropped){
        LIBRADOR_LOG(LOG_DEBUG, "frame stats at teardown: ok=%llu bad_csum=%llu dropped=%llu unvalidated=%llu (transport %d)\n",
            (unsigned long long)frames_ok.load(), (unsigned long long)frames_bad_checksum.load(),
            (unsigned long long)frames_dropped.load(), (unsigned long long)frames_unvalidated.load(),
            active_transport);
    }
    if(iso_polling_thread) {
        iso_thread_shutdown_requested = true;
        if(iso_polling_thread->joinable())
            iso_polling_thread->join();
        delete iso_polling_thread;

        // prepare for possible replug
        iso_thread_shutdown_requested = false;
        iso_polling_thread = nullptr;
        free_transfers();
    }
    if(ingest_thread){
        {
            std::lock_guard<std::mutex> lock(ingest_mutex);
            ingest_stop = true;
        }
        ingest_cv.notify_one();
        if(ingest_thread->joinable())
            ingest_thread->join();
        delete ingest_thread;
        ingest_thread = nullptr;
        ingest_queue.clear();
    }
    if(handle){
        if(claimed_iface >= 0){
            // Deselect the streaming alt setting so the device stops
            // producing, then release.  Best effort - the device may
            // already be gone.
            libusb_set_interface_alt_setting(handle, claimed_iface, 0);
            if(claimed_iface != 0 || !iface0_claimed){
                libusb_release_interface(handle, claimed_iface);
            }
            claimed_iface = -1;
        }
        if(iface0_claimed){
            libusb_release_interface(handle, 0);
            iface0_claimed = false;
        }
        LIBRADOR_LOG(LOG_DEBUG, "Interface released\n");
        libusb_close(handle);
        handle = nullptr;
        LIBRADOR_LOG(LOG_DEBUG, "Device Closed\n");
    }
}

void usbCallHandler::free_transfers(){
    if(alloc_transport < 0) return;
    if(alloc_transport == LABRADOR_TRANSPORT_BULK){
        for(int n=0; n<NUM_BULK_CTX; n++){
            libusb_free_transfer(bulkCtx[n]);
        }
    } else {
        int ndata = (alloc_transport == LABRADOR_TRANSPORT_ISO6) ? AIO_EP_ISO6_COUNT : 1;
        for (int i=0; i<NUM_FUTURE_CTX; i++){
            for (int k=0; k<ndata; k++){
                libusb_free_transfer(isoCtx[k][i]);
            }
            libusb_free_transfer(metaCtx[i]);
        }
    }
    alloc_transport = -1;
    LIBRADOR_LOG(LOG_DEBUG, "Transfers freed.\n");
}

#ifdef PLATFORM_ANDROID
void usbCallHandler::respondToStartupOrUsbStateChange(bool is_plugged_in, int file_descriptor, bool bootloader_mode){
    if(is_plugged_in) {
        if(bootloader_mode) {
            LIBRADOR_LOG(LOG_DEBUG, "found in bootloader mode");
            if(starting_after_flash) {
                LIBRADOR_LOG(LOG_DEBUG, "startup after flash");
                starting_after_flash = false;
                init_libusb();
                int error = setup_usb_control(file_descriptor);
                set_bootloader_mode_allowed(false);
                dfu_launch();// firmware >= 0x000A boots after a single launch (bootloader-flag bug fixed)

                if(librador_get_host_hooks().confirm_firmware_flash)
                    librador_get_host_hooks().confirm_firmware_flash();
            } else {
                int flashRet = flashFirmware(file_descriptor);
                LIBRADOR_LOG(LOG_DEBUG, "flashRet: %d", flashRet);
            }
        } else {
            if(connected) {
                return;
            }
            init_libusb();
            int control_setup_success = setup_usb_control(file_descriptor);
            if(control_setup_success==0) {
                connected = true;
                check_firmware_and_start_iso();
                return;
            } else {
                connected = false;
                if(handle){
                    libusb_release_interface(handle, 0);
                    libusb_close(handle);
                    handle = nullptr;
                }
                return;
            }
        }
    } else {
        teardown_connection();
    }
}
#endif

SDL_IOStream* usbCallHandler::open_file(const char * filepath) {
    LIBRADOR_LOG(LOG_DEBUG, "daq filename: %s", filepath);
    if(librador_get_host_hooks().open_daq_stream)
        return librador_get_host_hooks().open_daq_stream(filepath);
    return SDL_IOFromFile(filepath, "w");
}

#ifndef PLATFORM_ANDROID
bool usbCallHandler::desktop_device_present(bool* bootloader_mode_out){
    // Legacy semantics: only the application-firmware and bootloader devices
    // count as present; a Gobindar-state (0xa000) board is NOT reported here
    // (callers of this signature predate Gobindar detection).
    bool app_mode = false;
    bool gobindar_mode = false;
    desktop_device_present_ex(&app_mode, bootloader_mode_out, &gobindar_mode);
    return app_mode || *bootloader_mode_out;
}

bool usbCallHandler::desktop_device_present_ex(bool* app_mode_out, bool* bootloader_mode_out, bool* gobindar_mode_out){
    *app_mode_out = false;
    *bootloader_mode_out = false;
    *gobindar_mode_out = false;
    if(init_libusb() < 0)
        return false;
    libusb_device** list;
    ssize_t n = libusb_get_device_list(ctx, &list);
    if(n < 0)
        return false;
    for(ssize_t i = 0; i < n; i++){
        libusb_device_descriptor desc;
        if(libusb_get_device_descriptor(list[i], &desc) != 0)
            continue;
        if(desc.idVendor != VID)
            continue;
        if(desc.idProduct == PID)
            *app_mode_out = true;
        else if(desc.idProduct == LABRADOR_BOOTLOADER_PID)
            *bootloader_mode_out = true;
        else if(desc.idProduct == LABRADOR_GOBINDAR_PID)
            *gobindar_mode_out = true;
    }
    libusb_free_device_list(list, 1);
    return *app_mode_out || *bootloader_mode_out || *gobindar_mode_out;
}

int usbCallHandler::desktop_connect(){
    if(connected)
        return 1;
    if(init_libusb() < 0)
        return -1;
    handle = libusb_open_device_with_vid_pid(ctx, VID, PID);
    if(!handle){
        LIBRADOR_LOG(LOG_WARNING, "Labrador device not found or could not be opened\n");
        return -2;
    }
    int error = claim_and_prepare();
    if(error)
        return error;
    connected = true;
    return check_firmware_and_start_iso();
}

void usbCallHandler::desktop_disconnect(){
    teardown_connection();
}

int usbCallHandler::desktop_flash_firmware(const char* hex_path){
    LIBRADOR_LOG(LOG_DEBUG, "desktop_flash_firmware: %s\n", hex_path);
    if(connected){
        set_bootloader_mode_allowed(true);
        reset_device(true);
        teardown_connection();
    }

    // Wait for the board to enumerate in bootloader mode.  libusb only lists
    // the device once the OS has configured it, and on Windows that means
    // after PnP has bound libusb0.sys to the freshly appeared 03EB:2FE4
    // device - several seconds the first time a machine sees it - so allow
    // well beyond the ~1 s the board itself needs (#450).
    bool bootloader = false;
    for(int attempt = 0; attempt < 200; attempt++){
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if(desktop_device_present(&bootloader) && bootloader)
            break;
    }
    if(!bootloader){
        LIBRADOR_LOG(LOG_ERROR, "Board never appeared in bootloader mode\n");
        return -1;
    }

    int exit_code = dfuprog_virtual_cmd("dfu-programmer atxmega32a4u erase --force --debug 300");
    if(exit_code){
        // Never program a bootloader that would not erase: the link to it is
        // not working, and pressing on is how a board ends up blank (#450).
        // The old firmware is normally still intact, so hand control back to
        // it and report the failure.
        LIBRADOR_LOG(LOG_ERROR, "ERROR ERASING FIRMWARE (%d); not flashing\n", exit_code);
        dfuprog_virtual_cmd("dfu-programmer atxmega32a4u launch");
        set_bootloader_mode_allowed(false);
        return -3;
    }

    // The flash command goes through dfuprog_virtual_main as argv directly:
    // dfuprog_virtual_cmd tokenises its command string on spaces, which would
    // split hex_path (e.g. the default Windows install directory
    // "C:\Program Files\EspoTek Labrador\...").
    char argv_prog[] = "dfu-programmer";
    char argv_target[] = "atxmega32a4u";
    char argv_flash[] = "flash";
    std::vector<char> argv_hex(hex_path, hex_path + strlen(hex_path) + 1);
    char argv_debug[] = "--debug";
    char argv_level[] = "300";
    char *flash_argv[] = { argv_prog, argv_target, argv_flash, argv_hex.data(), argv_debug, argv_level };
    exit_code = dfuprog_virtual_main(6, flash_argv);
    if(exit_code){
        LIBRADOR_LOG(LOG_ERROR, "ERROR WRITING NEW FIRMWARE TO DEVICE (%d)\n", exit_code);
        return -2;
    }

    exit_code = dfuprog_virtual_cmd("dfu-programmer atxmega32a4u launch");
    if(exit_code)
        LIBRADOR_LOG(LOG_WARNING, "dfu launch reported %d (often benign)\n", exit_code);

    // The board may re-enumerate in bootloader mode once more (EEPROM flag);
    // keep launching (once a second) until it comes up as the application
    // firmware.  Allow as long as the wait for the bootloader above: through a
    // VM's USB passthrough the re-enumeration took longer than the 5 s this
    // used to allow, and the connect below then read the bootloader's answer
    // to the version request ("0xfe5b variant 91") and failed the update.
    for(int attempt = 0; attempt < 200; attempt++){
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if(!desktop_device_present(&bootloader))
            continue;
        if(!bootloader)
            break;
        if(attempt % 10 == 0)
            dfuprog_virtual_cmd("dfu-programmer atxmega32a4u launch");
    }

    if(librador_get_host_hooks().confirm_firmware_flash)
        librador_get_host_hooks().confirm_firmware_flash();
    set_bootloader_mode_allowed(false);
    // The application's device node can appear before udev has applied the
    // rule that lets this user open it: in a Linux VM the connect right after
    // the board came back failed with EACCES ("libusb requires write access
    // to USB device nodes").  Keep trying for up to 5 s while the board is
    // there but can't be opened (-2; nothing is held open then).
    int error = desktop_connect();
    for(int attempt = 0; error == -2 && attempt < 50; attempt++){
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        error = desktop_connect();
    }
    return error;
}

// WARNING: currently unused — calling libusb_reset_device with live iso
// transfers crashed on macOS (2026-07-04). A working version must stop the
// iso thread and cancel transfers BEFORE the port reset. Kept as the anchor
// for that future fix; do not wire this up as-is.
int usbCallHandler::desktop_hard_reset(){
    if(!connected || !handle)
        return -1;
    LIBRADOR_LOG(LOG_DEBUG, "Hard USB reset (uninitialised-state auto-heal)\n");
    libusb_reset_device(handle);
    teardown_connection();
    return 0;
}

int usbCallHandler::desktop_bootloader_recover(const char* hex_path){
    LIBRADOR_LOG(LOG_DEBUG, "Bootloader-mode board found: attempting launch\n");
    dfu_launch();

    // Wait for the board to come back as the application firmware.  The
    // bootloader's device is often still listed just after the launch, so
    // this waits for the application's device itself: stopping at the first
    // Labrador seen sent a board that was launching fine to be reflashed,
    // by when it had left the bootloader, and the recovery then failed
    // though the board was running its firmware.  5 s, as after a flash.
    bool app_mode = false;
    bool bootloader = false;
    bool gobindar = false;
    for(int attempt = 0; attempt < 50 && !app_mode; attempt++){
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        desktop_device_present_ex(&app_mode, &bootloader, &gobindar);
    }
    if(app_mode){
        int error = desktop_connect();
        if(error == 0 || error == 1){
            // 0 = streaming; 1 = connected but wrong firmware (the
            // request_firmware_flash hook has fired, the app takes it from here)
            LIBRADOR_LOG(LOG_DEBUG, "Bootloader recovery via launch succeeded\n");
            return error;
        }
    }

    // Launch didn't bring it back (application flash erased or corrupt):
    // do the full erase + flash + launch cycle.
    LIBRADOR_LOG(LOG_DEBUG, "Launch alone insufficient, reflashing\n");
    return desktop_flash_firmware(hex_path);
}

int usbCallHandler::desktop_gobindar_recover(const char* hex_path){
    LIBRADOR_LOG(LOG_DEBUG, "Gobindar-state board (PID 0x%04x): waiting for user to force bootloader\n", LABRADOR_GOBINDAR_PID);

    // Qt parity (genericusbdriver.cpp deGobindarise -> flashFirmware): the
    // 0xa000 device is never sent anything (Qt's bootloaderJump control
    // transfer is skipped because the driver is not "connected").  The user
    // shorts Digital Out 1 to GND and replugs the board, which forces the
    // hardware bootloader (0x2fe4); Qt then retries the dfu erase every
    // 200 ms until that device answers.  We wait for the bootloader to
    // enumerate at the same cadence, with a generous cap instead of Qt's
    // infinite loop, then reuse the standard erase + flash + launch cycle.
    set_bootloader_mode_allowed(true);

    bool app_mode = false;
    bool bootloader_mode = false;
    bool gobindar_mode = false;
    bool entered_bootloader = false;
    const int poll_interval_ms = 200;               // Qt's retry cadence
    const int max_attempts = 10 * 60 * 1000 / poll_interval_ms;  // ~10 minutes, user-paced
    for(int attempt = 0; attempt < max_attempts; attempt++){
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
        desktop_device_present_ex(&app_mode, &bootloader_mode, &gobindar_mode);
        if(bootloader_mode){
            entered_bootloader = true;
            break;
        }
    }
    if(!entered_bootloader){
        set_bootloader_mode_allowed(false);
        LIBRADOR_LOG(LOG_ERROR, "Gobindar recovery: board never entered bootloader mode\n");
        return -10;
    }

    // Bootloader is up: erase + flash + launch + double-launch, then connect.
    // (desktop_flash_firmware sees the bootloader immediately and resets
    // set_bootloader_mode_allowed(false) itself when done.)
    LIBRADOR_LOG(LOG_DEBUG, "Gobindar recovery: bootloader found, reflashing\n");
    return desktop_flash_firmware(hex_path);
}
#endif


