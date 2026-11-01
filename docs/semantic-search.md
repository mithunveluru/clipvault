# Semantic search

Optional, local, off by default. When it is on, a search for `grocery list`
also finds the clip `Remember to buy milk, eggs and bread on the way home`,
even though no word matches.

## Goals and non goals

- Find clips by meaning as well as by text, entirely on this computer.
- Never download anything. The user fetches the model files and points
  ClipVault at them. ClipVault checks their hashes and refuses any other file.
- Never embed a secret.
- Leave plain search exactly as it is. Lexical results keep their order and
  come first. Semantic hits are only appended after them.
- Cost nothing when it is off: no thread, no model in memory, no extra work per
  capture.
- Non goals: question answering, summarising, any remote model, and automatic
  model updates.

## Two gates

1. **Build time.** The CMake option `CLIPVAULT_SEMANTIC` defaults to `OFF`.
   When it is `ON` it requires `-DONNXRUNTIME_ROOT=<extracted onnxruntime
   release>` and stops configuration with a fatal error otherwise. Only that
   build compiles `src/services/OnnxEmbeddingProvider.cpp` and links
   `libonnxruntime.so`. A default build has no ONNX Runtime code or library at
   all. Its Settings group says that the build has no semantic search.
2. **Run time.** The setting `semantic_enabled` defaults to false and
   `semantic_model_dir` defaults to empty. The worker thread and the model
   exist only while both are set and the files verify.

The `clip_embeddings` table, added in schema v3, exists in every build, so the
schema never forks between builds.

## Model provenance

| | |
|---|---|
| Repository | `sentence-transformers/all-MiniLM-L6-v2` |
| Revision | `1110a243fdf4706b3f48f1d95db1a4f5529b4d41` |
| Model file | `onnx/model.onnx`, saved as `model.onnx` |
| Model sha256 | `6fd5d72fe4589f189f8ebc006442dbb529bb7ce38f8082112682524616046452` |
| Vocabulary file | `vocab.txt` |
| Vocabulary sha256 | `07eced375cec144d27c900241f3e339478dec958f92fddbc551f295c992038a3` |
| Licence | Apache 2.0 |

Both hashes are pinned in `src/services/OnnxEmbeddingProvider.h`
(`semantic_model::kModelSha256` and `kVocabSha256`). The constructor accepts
other hashes, but only tests use that.

The input names `input_ids`, `attention_mask` and `token_type_ids` (int64,
batch by sequence) and the output `last_hidden_state` (batch by sequence by
384) were checked on a separate local export of the same model. They are
believed to match the official file, but **this was not verified against the
official file itself**. If they differ, the provider refuses the model and the
status reads "the model could not be loaded"; it never guesses.

## How a user turns it on

This is a manual step, and ClipVault never performs it:

1. Use a build configured with `-DCLIPVAULT_SEMANTIC=ON`.
2. Download the two files above at that exact revision, for example from the
   Hugging Face page of the repository, using a browser or any tool you trust.
3. Put them in one folder, named `model.onnx` and `vocab.txt`.
4. Open Settings, then Semantic search, choose the folder and tick the box.
   The status moves from "verifying" to "ready", or to "hash mismatch" or
   "files missing".

You can check the files yourself first:

```
sha256sum model.onnx vocab.txt
```

## Embedding pipeline

- **Tokenizer** (`src/core/WordPieceTokenizer.*`). This is BERT uncased basic
  tokenisation followed by WordPiece, written to match the reference Python
  `BertTokenizer` with `do_lower_case=true`, `tokenize_chinese_chars=true` and
  `strip_accents` unset (so accents are stripped because lowercasing is on).
  The steps:
  - Drop NUL, U+FFFD and other control characters, and map whitespace to
    spaces.
  - Put spaces around CJK ideographs, then apply NFC.
  - Split on whitespace.
  - Per word: lowercase, apply NFD and drop non-spacing marks, then split out
    each punctuation character. ASCII symbols count as punctuation too.
  - WordPiece: greedy longest match with `##` continuations. A word longer than
    100 characters, or a word that cannot be matched, becomes `[UNK]` (100).
  - Wrap the ids in `[CLS]` (101) and `[SEP]` (102) and truncate to 128 ids.

  The repository does not carry the full vocabulary.
  `tests/fixtures/minilm_vocab_subset.tsv` lists, as id tab token, only the
  258 pieces that the reference cases can reach. The tokenizer test rebuilds a
  full length `vocab.txt` from it and fills every other line with `[filler]`,
  which can never match because brackets are split off as punctuation.

  One known difference: the reference splits literal special tokens such as
  `[SEP]` out of the input text, and this tokenizer does not.
- **Model.** ONNX Runtime 1.20.1 on the CPU. Each text runs as its own sequence
  because padding a short clip to a long neighbour cost more than the extra
  calls. With eight mixed clips one at a time took 162 ms against 394 ms
  padded, in a Debug build.
- **Pooling.** Mean over the attention mask, then L2 normalisation, as
  sentence transformers does for this model. Cosine similarity is therefore a
  dot product.
- The service sends at most the first 4000 characters of a clip, since 126
  word pieces never need more.

## Threading

The app is single threaded by design, and this feature adds exactly one
thread, justified as follows:

- Verifying the model hashes about 90 MB, and one embedding costs tens of
  milliseconds. Either would freeze the window and clipboard capture if it ran
  on the GUI thread.
- `EmbeddingEngine` owns one `QThread`. The `EmbeddingWorker` on that thread
  owns the provider and does exactly one thing: turn copied `QString`s into
  vectors. It never touches `QClipboard`, `QSqlDatabase` or any service.
- Results come back through queued signals. The main thread
  (`SemanticIndex`) does every database write.
- Hash verification and session creation also run on the worker.
- ONNX Runtime is set to one intra-op and one inter-op thread with spinning
  off, so it creates no thread pools. The real model test counts
  `/proc/self/task` before and after loading and embedding, and the count is
  unchanged.
- Shutdown: an atomic stop flag makes queued jobs return at once, then
  `quit()` and `wait()` run, and only then is the worker deleted. Posted events
  for a deleted object are discarded by Qt. Tests destroy the engine with 50
  jobs queued and before the load job has run.
- The thread exists only between `SemanticIndex::start` and `stop`.

## Storage

`src/storage/EmbeddingRepository.*` holds all embedding SQL. The table is
`clip_embeddings(item_id PRIMARY KEY REFERENCES clipboard_items(id) ON DELETE
CASCADE, model, dim, content_hash, vector)`.

- Vectors are little-endian float32 blobs (384 times 4 bytes).
- `PRAGMA foreign_keys = ON` is set on the connection, and a test checks it.
  A test also shows the vector is gone after each delete path: remove, clear
  all with and without pinned, retention expiry and the max items trim. No
  explicit embedding delete is needed.
- The model id is `all-MiniLM-L6-v2/` followed by the first 16 hex digits of
  the verified model hash. A different file therefore re-embeds instead of
  mixing vector spaces.
- A vector is served only when its model matches, its `content_hash` equals
  the item's current hash, and the item is not a secret.
- Vectors are as sensitive as the clips they come from. They live in the same
  owner-only database, die with their row, and are covered by the same
  secure delete and checkpoint.

## Never embed secrets

- In-memory secrets have negative ids and never reach the database, so the
  index never sees them.
- Rows with `secret_kind` set, including pinned secrets, are excluded by the
  pending query and by every load.
- A row that becomes a secret after it was embedded (the same text pinned
  later) loses its vector on the next sync through `removeSecrets()`.
- The main thread checks the row again before it writes a result, in case it
  was deleted or marked secret while the worker ran.
- Blending skips secret rows, so a meaning match cannot surface a masked pinned
  secret.
- Tests cover all of these, including a check that no secret text was ever
  handed to the provider.

## Index and backfill

`SemanticIndex` keeps `item id -> vector` in memory. Every history change
schedules a sync, coalesced over 50 ms. A sync:

- purges secret vectors;
- drops in-memory vectors whose row is gone;
- embeds pending rows newest first, in batches of 8, with one batch in flight
  at a time.

A query therefore waits behind at most one batch. A batch that stores nothing
stops the backfill until the next history change, so a bad batch cannot spin.

## Blending and threshold

`SearchService::searchBlended(query, semanticIds)`:

- Parses the query once with `SearchService::parse`.
- Runs the unchanged lexical search first, under the full parsed filter.
- Appends the semantic ids in similarity order. It fetches them with one
  `ClipboardRepository::findFiltered(ids, filter)` call, which applies the same
  `app:`/`from:` (app or window class substring), `title:` and `date:` SQL
  as the lexical search, because both use the same helper. It skips
  duplicates, missing rows and secrets. The free text part does not filter the
  semantic hits, since matching it by meaning is the whole point.
- Caps the total at the existing limit of 200.

`SearchService::semanticText()` is the one hook for which part of the query
semantic matching sees. It returns the free text part of `parse`, so
`app:firefox meeting notes` embeds only `meeting notes`. A query made only of
prefixes runs no semantic query at all.

The query is embedded asynchronously. `MainWindow` shows lexical results at
once, then refreshes once when hits arrive for the still-current text.

The minimum similarity is **0.35** (`semantic_model::kMinSimilarity`), tuned on
`tests/fixtures/semantic_relevance.json`: 20 realistic clips and 16 queries
whose answer shares no keyword with them. With the local export:

- every query ranks its own clip first;
- the lowest relevant score is 0.380 and the highest unrelated score is 0.388;
- at 0.35, 16 of 16 relevant clips pass and 1 of 304 unrelated pairs passes.

Semantic hits only ever appear after the lexical ones, so recall is worth more
than a rare loosely related extra. The test fails if any relevant clip falls
below the threshold or more than one unrelated pair passes it.

## Performance

Measured on 1 November 2026 on an 8 core x86 64 laptop that was busy with other
work (load average 7 to 15 during the runs), so treat every number as rough.

With the real model, from a Release build with `CLIPVAULT_SEMANTIC=ON`,
running `clipvault_semantic_bench <model dir> <model sha256> <vocab sha256> 1000`
against the local export:

| | |
|---|---|
| verify hashes and load the model | 985 ms, on the worker |
| embed one clip, mean of 100 | 10.7 ms |
| embed a batch of 8 mixed clips | 80 ms |
| backfill 1000 clips, including model load | 12.9 s, about 78 clips per second |
| query, from `query()` to ranked ids, 1000 vectors | 4.0 ms mean over 20 queries |
| resident memory added by the index, model and session | about 141 MB (141032 kB) |

The memory is spent only while semantic search is enabled and ready. Turning
it off stops the thread and frees the model.

With semantic search compiled out, `tools/bench.sh build/clipvault_bench`
(Release, median of 5, 10000 entries) shows no regression. The insert path is
unchanged code, so the differences below are noise.

| | before (main) | after, run 1 | after, run 2 |
|---|---|---|---|
| database init | 6.06 ms | 7.25 ms | 5.80 ms |
| insert, mean | 0.235 ms | 0.298 ms | 0.231 ms |
| search recent | 0.680 ms | 0.609 ms | 0.627 ms |
| search github | 0.935 ms | 0.860 ms | 0.817 ms |
| search employees | 0.963 ms | 0.834 ms | 0.950 ms |
| search with no match | 2.271 ms | 1.969 ms | 2.535 ms |
| search searchable | 0.927 ms | 0.793 ms | 0.998 ms |
| resident memory | 30312 kB | 30216 kB | 30116 kB |

## What was verified

Commands, run in the worktree:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j
(cd build && xvfb-run -a ctest --output-on-failure)
# 100% tests passed, 0 tests failed out of 21

cmake -S . -B build-sem -DCMAKE_BUILD_TYPE=Debug -DCLIPVAULT_SEMANTIC=ON \
      -DONNXRUNTIME_ROOT=<onnxruntime-linux-x64-1.20.1>
cmake --build build-sem -j
(cd build-sem && CLIPVAULT_TEST_MODEL_DIR=<model dir> \
    CLIPVAULT_TEST_MODEL_SHA256=<sha256 of that model.onnx> \
    xvfb-run -a ctest --output-on-failure)
# 100% tests passed, 0 tests failed out of 22   (no_network included)
```

With the model, `test_onnx_embedding` reported:

- the largest difference from `tests/fixtures/embedding_reference.jsonl` is
  5.32e-07, and the limit is 1e-4;
- the model is refused with the wrong hash;
- the thread count is unchanged after loading and embedding;
- 16 of 16 relevant clips pass the threshold, with 1 of 304 false positives.

Without `CLIPVAULT_TEST_MODEL_DIR` the three real model cases skip and say why.

The fake provider tests (`test_embedding_engine`, `test_semantic_index`) also
ran under ThreadSanitizer with `setarch -R`, and all passed. They printed 80
reports. Every one is inside Qt's own event posting
(`QMetaObject::invokeMethod`, `QMetaCallEvent`, `sendPostedEvents`) or glib's
eventfd wakeups. Those come from the distribution's uninstrumented Qt, and none
of them is in this feature's state. A clean result would need a Qt built with
ThreadSanitizer.

**Not verified:**

- The official `onnx/model.onnx` at the pinned revision was not available
  offline here. The tests used a local export of the same checkpoint, whose
  sha256 is `96bca6910f51efe1c8ac3af3d9f7085dedca6a4d8ab64ea881be7c2ff426be3e`, through the test only hash override. The pinned
  model hash, and the input and output names of that exact file, have
  therefore not been checked on this machine. The vocabulary hash was checked, because
  the real model test loads `vocab.txt` from the model folder against the
  pinned hash.
- The app was run with semantic search enabled and pointed at the local
  export. It refused the file with "model.onnx sha256 does not match the pinned
  hash", as designed, and logged no path. The ready path in the real app was
  not exercised with the official file.
- The `semantic` CI job was written but has not run, because nothing here is
  pushed.
