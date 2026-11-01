// native-main.cpp - run the browser demo code on a desktop.
//
//     transcribe-web-native asr.gguf vad.gguf audio.wav [n_threads]

#include "transcribe-web.h"
#include "wav.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int main(int argc, char ** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s asr.gguf vad.gguf audio.wav [n_threads]\n", argv[0]);
        return 2;
    }
    std::vector<float> pcm;
    std::string        err;
    if (!transcribe_cli::load_wav_mono_16k(argv[3], pcm, err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    if (tw_load(argv[1], argv[2], argc > 4 ? std::atoi(argv[4]) : 0) != 0) {
        std::fprintf(stderr, "error: %s\n", tw_error());
        return 1;
    }
    const int n_chunks = tw_split(pcm.data(), static_cast<int>(pcm.size()));
    if (n_chunks < 0) {
        std::fprintf(stderr, "error: %s\n", tw_error());
        tw_free();
        return 1;
    }
    std::string text;
    for (int i = 0; i < n_chunks; ++i) {
        const char * t = tw_transcribe_chunk(i);
        if (t == nullptr) {
            std::fprintf(stderr, "error: %s\n", tw_error());
            tw_free();
            return 1;
        }
        std::fprintf(stderr, "chunk %d/%d: %s\n", i + 1, n_chunks, t);
        if (*t != '\0') {
            text += text.empty() ? "" : " ";
            text += t;
        }
    }
    std::printf("%s\n", text.c_str());
    tw_free();
    return 0;
}
