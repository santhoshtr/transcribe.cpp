# Speech to text in the browser

This example runs transcribe.cpp as WebAssembly in a web page. The audio
does not go to a server. The page downloads the models one time and keeps
them in the browser cache. The page transcribes an audio file or a
recording from the microphone.

The page uses one ASR model and the Silero VAD model. It splits long audio
at pauses, as the Koyal demo on Hugging Face does:

- Audio of 30 s or shorter is one chunk.
- For longer audio, Silero VAD finds the speech (maximum segment 30 s,
  minimum silence 500 ms, padding 200 ms). The speech segments are joined
  into chunks of 30 s or shorter.
- Chunks of 0.1 s or shorter are discarded.

## Build

Install [Emscripten](https://emscripten.org/docs/getting_started/downloads.html)
and load its environment (`source emsdk_env.sh`).

Without threads:

```bash
emcmake cmake -B build-wasm -DEMSCRIPTEN_SYSTEM_PROCESSOR=wasm32 \
    -DTRANSCRIBE_BUILD_TESTS=OFF -DTRANSCRIBE_USE_SYSTEM_BLAS=OFF -DTRANSCRIBE_INSTALL=OFF
cmake --build build-wasm --target transcribe-web
```

With threads, add `-DCMAKE_C_FLAGS=-pthread -DCMAKE_CXX_FLAGS=-pthread`
and use a different build folder, for example `build-wasm-mt`.

`EMSCRIPTEN_SYSTEM_PROCESSOR=wasm32` is necessary. Without it, ggml does
not use the WebAssembly SIMD kernels.

## Run

Put the models in a `models` folder in the build output:

```bash
mkdir -p build-wasm/bin/models
cp koyal-ml-120m-1.0-Q8_0.gguf silero-vad-v6.2-F32.gguf build-wasm/bin/models/
```

Then serve the folder and open the page:

```bash
uv run examples/web/serve.py build-wasm/bin 8000
```

A build with threads works only on a cross-origin isolated page.
`serve.py` sends the necessary headers (COOP and COEP). A static host must
send the same headers.

The microphone works only on `localhost` or on an HTTPS page.

## Speed

An 84 s Malayalam clip, Q8_0 model, Chrome on an Intel Core Ultra 7 255U:

| Build | Time | Speed |
| --- | ---: | ---: |
| wasm, 1 thread | 35.8 s | 2.3x realtime |
| wasm, 8 threads | 6.6 s | 12.7x realtime |
| native, 1 thread | 12.9 s | 6.5x realtime |

After loading, the page runs 2 s of audio one time. Without this warm-up,
the first transcription with threads is approximately 11 s slower.

## Memory

The Q8_0 weights need 142 MB. The encoder work memory for the Koyal
model was 91 MB for a 17 s chunk and 277 MB for a 27.6 s chunk. Chunks
are not longer than 30 s, so this stays well below the 4 GB limit of
wasm32.

## Desktop check

`transcribe-web-native` runs the same code on a desktop. It is part of the
normal native build:

```bash
cmake --build build --target transcribe-web-native
./build/bin/transcribe-web-native asr.gguf vad.gguf audio.wav [n_threads]
```
