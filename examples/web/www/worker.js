// worker.js - runs the wasm module off the main thread.
//
// Messages in:  {type: 'load', asrUrl, vadUrl, threads}
//               {type: 'transcribe', pcm: Float32Array (16 kHz mono)}
// Messages out: {type: 'status', text}, {type: 'loaded'},
//               {type: 'chunk', index, count, text}, {type: 'done', ms},
//               {type: 'error', text}

importScripts('transcribe-web.js');

let mod = null;

const post = (msg) => postMessage(msg);

// Get a model file as a Blob. The Cache API keeps it, so the next visit
// does not download it again.
async function fetchModel(url) {
  const cache = await caches.open('transcribe-models');
  let resp = await cache.match(url);
  if (!resp) {
    resp = await fetch(url);
    if (!resp.ok) throw new Error(`${url}: HTTP ${resp.status}`);
    const total = Number(resp.headers.get('Content-Length')) || 0;
    const reader = resp.body.getReader();
    const parts = [];
    let got = 0;
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      parts.push(value);
      got += value.length;
      const pct = total ? ` ${Math.round((100 * got) / total)}%` : '';
      post({ type: 'status', text: `Downloading ${url}${pct}` });
    }
    await cache.put(url, new Response(new Blob(parts)));
    resp = await cache.match(url);
  }
  return resp.blob();
}

async function load({ asrUrl, vadUrl, threads }) {
  const [asr, vad] = await Promise.all([fetchModel(asrUrl), fetchModel(vadUrl)]);
  post({ type: 'status', text: 'Loading models' });
  mod = await createTranscribeModule({
    print: (s) => console.log(s),
    printErr: (s) => console.log(s),
  });
  // MEMFS keeps file data in JS memory, outside the wasm heap. canOwn
  // avoids a second copy. The loader reads in small pieces, which is fast
  // here (WORKERFS took 30 s for 150 MB). Remove the files after the load
  // to release the JS memory.
  const put = async (path, blob) =>
    mod.FS.writeFile(path, new Uint8Array(await blob.arrayBuffer()), { canOwn: true });
  await put('/asr.gguf', asr);
  await put('/vad.gguf', vad);
  const st = mod.ccall('tw_load', 'number', ['string', 'string', 'number'],
                       ['/asr.gguf', '/vad.gguf', threads]);
  mod.FS.unlink('/asr.gguf');
  mod.FS.unlink('/vad.gguf');
  if (st !== 0) throw new Error(mod.UTF8ToString(mod._tw_error()));
  // V8 runs new wasm code with a baseline compiler. It uses the optimized
  // code for a function only from its next call. So a long first run stays
  // slow (20 s of audio with 8 threads: 13 s, not 1.6 s). A short run here
  // costs approximately 1 s and makes the next runs fast.
  post({ type: 'status', text: 'Warming up' });
  transcribe({ pcm: new Float32Array(2 * 16000).map(() => (Math.random() - 0.5) * 0.01) }, () => {});
  post({ type: 'loaded' });
}

function transcribe({ pcm }, report = post) {
  const t0 = performance.now();
  const ptr = mod._malloc(pcm.length * 4);
  mod.HEAPF32.set(pcm, ptr / 4);
  const count = mod._tw_split(ptr, pcm.length);
  mod._free(ptr);
  if (count < 0) throw new Error(mod.UTF8ToString(mod._tw_error()));
  for (let i = 0; i < count; i++) {
    report({ type: 'status', text: `Transcribing chunk ${i + 1} of ${count}` });
    const p = mod._tw_transcribe_chunk(i);
    if (!p) throw new Error(mod.UTF8ToString(mod._tw_error()));
    report({ type: 'chunk', index: i, count, text: mod.UTF8ToString(p) });
  }
  report({ type: 'done', ms: performance.now() - t0 });
}

// In a pthread build, the thread pool starts this script again for each
// thread, with the name 'em-pthread'. There the module handles the
// messages, so do not replace its handler.
if (self.name !== 'em-pthread') {
  onmessage = async (e) => {
    try {
      if (e.data.type === 'load') await load(e.data);
      else if (e.data.type === 'transcribe') transcribe(e.data);
    } catch (err) {
      post({ type: 'error', text: String(err.message || err) });
    }
  };
}
