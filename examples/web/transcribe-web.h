// transcribe-web.h - C API of the browser demo. See transcribe-web.cpp.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Load the ASR model and the Silero VAD model. n_threads 0 uses the
// default. Returns 0 on success, else a transcribe_status (see tw_error).
int tw_load(const char * asr_path, const char * vad_path, int n_threads);

// Split 16 kHz mono audio into chunks. Returns the number of chunks, or
// -1 on error (see tw_error).
int tw_split(const float * pcm, int n_samples);

// Transcribe chunk i of the last split. Returns the text without spaces
// at the start and end, or NULL on error (see tw_error). The text is
// valid until the next call.
const char * tw_transcribe_chunk(int i);

// The message of the last error.
const char * tw_error(void);

// Free the models and the chunks.
void tw_free(void);

#ifdef __cplusplus
}
#endif
