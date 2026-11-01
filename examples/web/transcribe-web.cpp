// transcribe-web.cpp - a small C API for the browser demo.
//
// It loads one ASR model and one Silero VAD model, splits long audio into
// chunks at pauses, and transcribes one chunk for each call. One call for
// each chunk lets the page show progress.
//
// The chunking follows the Koyal Hugging Face demo (split_at_pauses):
// audio of 30 s or shorter is one chunk. Longer audio goes through Silero
// get_speech_timestamps (max 30 s, min silence 500 ms, pad 200 ms), and
// the speech segments are joined into chunks of 30 s or shorter. Chunks
// of 0.1 s or shorter are discarded.
//
// The state is global: a browser worker holds one model pair.

#include "transcribe-web.h"

#include "transcribe.h"
#include "transcribe/vad.h"

#include <string>
#include <vector>

namespace {

constexpr int k_sample_rate       = 16000;
constexpr int k_max_chunk_samples = 30 * k_sample_rate;
constexpr int k_min_chunk_samples = k_sample_rate / 10;

transcribe_session *            g_asr       = nullptr;
transcribe_model *              g_vad_model = nullptr;
transcribe_vad_session *        g_vad       = nullptr;
std::vector<std::vector<float>> g_chunks;
std::string                     g_text;
std::string                     g_error;

int fail(const char * what, transcribe_status st) {
    g_error = std::string(what) + ": " + transcribe_status_string(st);
    return st;
}

}  // namespace

extern "C" {

int tw_load(const char * asr_path, const char * vad_path, int n_threads) {
    tw_free();
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    n_threads = 1;  // A wasm build without pthreads cannot start threads.
#endif

    transcribe_model_load_params lp;
    transcribe_model_load_params_init(&lp);
    lp.backend = TRANSCRIBE_BACKEND_CPU;
    lp.no_mmap = true;

    transcribe_session_params sp;
    transcribe_session_params_init(&sp);
    sp.n_threads = n_threads;

    transcribe_status st = transcribe_open(asr_path, &lp, &sp, &g_asr);
    if (st != TRANSCRIBE_OK) {
        return fail("load ASR model", st);
    }

    st = transcribe_model_load_file(vad_path, &lp, &g_vad_model);
    if (st != TRANSCRIBE_OK) {
        tw_free();
        return fail("load VAD model", st);
    }
    transcribe_vad_session_params vp;
    transcribe_vad_session_params_init(&vp);
    vp.n_threads = n_threads;
    st           = transcribe_vad_session_init(g_vad_model, &vp, &g_vad);
    if (st != TRANSCRIBE_OK) {
        tw_free();
        return fail("init VAD session", st);
    }
    return TRANSCRIBE_OK;
}

int tw_split(const float * pcm, int n_samples) {
    g_chunks.clear();
    if (g_asr == nullptr || g_vad == nullptr) {
        g_error = "models are not loaded";
        return -1;
    }
    if (n_samples <= 0) {
        return 0;
    }

    if (n_samples <= k_max_chunk_samples) {
        g_chunks.emplace_back(pcm, pcm + n_samples);
    } else {
        transcribe_vad_params vp;
        transcribe_vad_params_init(&vp);
        vp.max_speech_ms           = 30000;
        vp.min_silence_ms          = 500;
        vp.speech_pad_ms           = 200;
        const transcribe_status st = transcribe_vad_run(g_vad, pcm, n_samples, &vp);
        if (st != TRANSCRIBE_OK) {
            fail("VAD", st);
            return -1;
        }
        transcribe_vad_result res;
        transcribe_vad_result_init(&res);
        transcribe_vad_get_result(g_vad, &res);

        // The same packing as the Python demo, including its empty chunk
        // when a segment does not fit an empty chunk.
        g_chunks.emplace_back();
        for (int i = 0; i < res.n_segments; ++i) {
            transcribe_vad_segment seg;
            transcribe_vad_segment_init(&seg);
            transcribe_vad_get_segment(g_vad, i, &seg);
            const int64_t len = seg.end_sample - seg.start_sample;
            if (static_cast<int64_t>(g_chunks.back().size()) + len > k_max_chunk_samples) {
                g_chunks.emplace_back();
            }
            g_chunks.back().insert(g_chunks.back().end(), pcm + seg.start_sample, pcm + seg.end_sample);
        }
    }

    std::vector<std::vector<float>> kept;
    for (auto & c : g_chunks) {
        if (static_cast<int>(c.size()) > k_min_chunk_samples) {
            kept.push_back(std::move(c));
        }
    }
    g_chunks = std::move(kept);
    return static_cast<int>(g_chunks.size());
}

const char * tw_transcribe_chunk(int i) {
    if (g_asr == nullptr || i < 0 || i >= static_cast<int>(g_chunks.size())) {
        g_error = "no such chunk";
        return nullptr;
    }
    transcribe_run_params rp;
    transcribe_run_params_init(&rp);
    rp.timestamps = TRANSCRIBE_TIMESTAMPS_NONE;

    const std::vector<float> & c  = g_chunks[static_cast<size_t>(i)];
    const transcribe_status    st = transcribe_run(g_asr, c.data(), static_cast<int>(c.size()), &rp);
    if (st != TRANSCRIBE_OK) {
        fail("transcribe", st);
        return nullptr;
    }

    g_text             = transcribe_full_text(g_asr);
    const char * ws    = " \t\n\r";
    const size_t first = g_text.find_first_not_of(ws);
    g_text = first == std::string::npos ? "" : g_text.substr(first, g_text.find_last_not_of(ws) - first + 1);
    return g_text.c_str();
}

const char * tw_error(void) {
    return g_error.c_str();
}

void tw_free(void) {
    g_chunks.clear();
    transcribe_vad_session_free(g_vad);
    g_vad = nullptr;
    transcribe_model_free(g_vad_model);
    g_vad_model = nullptr;
    transcribe_session_free(g_asr);
    g_asr = nullptr;
}

}  // extern "C"
