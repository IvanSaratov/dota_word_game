# Runtime deduplication and portable artifact design

## Context

The first native Windows dry-run completed without the ONNX schema-registration
flood and processed 1,597 frames with 16.958 ms mean total latency, 30.208 ms
p95 latency, and 52.593 ms maximum latency. Runtime speed is therefore
acceptable.

The dry-run did expose two correctness failures:

- 21 input proposals were emitted for 8 intended words. Already-sent words were
  proposed again after short detector gaps.
- Stable high-confidence fragments of a sent word were treated as new words:
  `HYPE`, `EMA`, `RN`, and `DIH`.

The current tracker removes a sent track after two missing frames. It also
requires compatible candidate size and a center distance of at most 90 pixels
for association. Those rules are too strict for the observed detector gaps and
partial crops.

The GitHub Actions portable artifact also contains a ZIP inside the ZIP created
by GitHub. GitHub always downloads an Actions artifact as an outer ZIP, while
the workflow currently uploads the CPack portable ZIP as its payload.

One game word can incorrectly appear in Cyrillic because of a confirmed game
bug. The user approved ignoring Cyrillic instead of switching keyboard layouts.

A second dry-run processed 1,156 frames at 12.560 ms mean latency and exposed
one remaining target-word fragment: sent `ABADDON` was followed three frames
later by stable `ADDON`. The crop occupied 74.1% of the retained full-word
area, so it correctly fell outside the existing 65% geometric-only rule even
though its normalized text was a strict substring of the canonical word.

The same run also contained `ALLPICK` and `ALL` from a game notification. The
user classified those observations as an exception for evaluating that run
only. They must not become a product blocklist or a general OCR exception.

## Goals

- Emit at most one input proposal for one visible moving word.
- Suppress geometric and strict-substring OCR fragments derived from an
  already-sent word.
- Allow the same normalized English word to be selected again after the old
  target has genuinely disappeared.
- Reject OCR text containing Cyrillic, including mixed Latin/Cyrillic text.
- Preserve punctuation and whitespace removal for ASCII English words.
- Keep the existing two-frame confirmation latency for valid new words.
- Make the downloaded portable Actions artifact contain application files
  directly, without an inner ZIP.
- Keep the installer as the recommended user distribution.
- Preserve the existing tagged release policy: only a `v*` tag reachable from
  `main` can publish a public GitHub Release.

## Non-goals

- The application will not type Cyrillic, transliterate it, or switch keyboard
  layouts.
- The application will not become a single-file portable executable; runtime
  DLLs, OCR models, configuration, and documentation remain separate files.
- The public release will not be published before native Windows CI and a new
  real-game dry-run pass.
- Detector thresholds and OCR confidence defaults will not be changed.

## Considered approaches

### 1. Retained sent tracks with geometric and substring suppression — selected

Retain sent tracks through realistic short detector gaps, reacquire exact text
while the retained track is alive, consume substantially smaller candidates
near the last full-word bounds, and consume spatially related strict substrings
of the canonical text.

This directly addresses the duplicate and fragment modes observed across both
logs while preserving the ability to accept the same word after the retained
track expires.

### 2. Global text cooldown

Remember every sent string for a fixed time and reject exact repeats. This is
simple but cannot reliably suppress OCR fragments, and it can reject a genuine
new occurrence of the same word.

### 3. More confirmation frames

Require three or more identical OCR frames. This increases latency and still
allows stable fragments that remain visible for multiple frames.

## Tracker design

The default `unlock_missing_frames` value changes from 2 to 15 in both the C++
default and packaged JSON configuration. Fifteen frames covers the short gaps
seen in the dry-run while remaining bounded; after 15 consecutive missing
frames, the sent track is erased and the same word can be selected again.
Explicit user configuration continues to override the default.

A sent track retains:

- the canonical normalized text that was dispatched;
- the most recent full-size bounds;
- its sent state and missing-frame count.

Ordinary association remains one-to-one. Unsent tracks continue to use the
existing distance-and-compatible-size match so that a changed OCR result resets
their confirmation streak. Before ordinary spatial association, every
candidate is offered to live sent tracks using the following authoritative
ownership rules:

1. If its normalized text exactly equals a live sent track's canonical text,
   the candidate is consumed by that sent track even when a detector gap caused
   a position jump. The sent track's full-size bounds and missing count are
   refreshed.
2. Otherwise, a candidate is consumed as a fragment when its normalized text
   is a strict contiguous substring of the live sent track's canonical text and
   its center lies inside the retained full-size bounds expanded on every side
   by `max_center_distance_px`. This rule has no area ceiling, so it covers
   large but unambiguous crops such as `ADDON` from `ABADDON`.
3. Otherwise, a candidate is consumed as a fragment when its area is at most
   65% of a live sent track's last full-size area and its center lies inside
   those bounds expanded on every side by `max_center_distance_px`.

A candidate owned by either sent rule is unavailable to unsent tracks. The
remaining candidates then enter the existing greedy one-to-one
distance-and-compatible-size association. A different full-size word does not
qualify for sent ownership and therefore remains eligible for ordinary
association.

A consumed fragment never replaces canonical text or shrinks the retained
full-size bounds. It only keeps the sent track alive. The substring rule covers
large unambiguous prefix, suffix, and middle crops; the existing geometric rule
continues to cover smaller OCR-error fragments that are not literal
substrings.

Normal unsent tracks still require `confirm_frames` consecutive matching text
observations. Unmatched unsent tracks reset their confirmation streak exactly
as they do now.

The accepted trade-offs are:

- a simultaneous second instance with exactly the same normalized text is
  suppressed while the first sent track is live;
- a second candidate substantially smaller than, and spatially overlapping, a
  sent word is suppressed;
- a spatially overlapping candidate whose text is a strict substring of a sent
  word is suppressed even when its area exceeds 65%.

These choices are safer than duplicate or cropped input. Suppression ends
after 15 consecutive frames contain neither the canonical word nor a
spatially related fragment.

## Cyrillic rejection

`normalize_for_input` will return an empty result if its UTF-8 input contains a
Cyrillic code point in the Russian/common Cyrillic block U+0400–U+04FF.
Therefore:

- a fully Cyrillic word is ignored;
- mixed Latin/Cyrillic text is ignored as a whole instead of producing a Latin
  fragment;
- ASCII punctuation, digits, whitespace, and separators continue to be removed
  from otherwise Latin text.

The application already drops normalized strings shorter than two characters,
so an empty Cyrillic result cannot reach tracking or input dispatch.

## Live-input validation

No second mode variable or command-line override is added. The existing
`live_input` JSON field remains the single source of truth:

- every packaged `config.json` keeps the safe default `"live_input": false`;
- the user explicitly changes it to `true` in the extracted or installed
  configuration for the next real-input test;
- startup must display `LIVE INPUT ENABLED`, and F8 must display
  `RUNNING (LIVE INPUT)` before any input is expected;
- successful dispatch remains visible in the log as `Input sent for <WORD>`.

The live-input acceptance run verifies both sides of the integration: the log
must show one successful dispatch per target, and the game must visibly accept
the typed word. F8 remains the immediate stop control. Notification text such
as the observed `ALL PICK` is not globally ignored; the capture region should
exclude unrelated HUD or notification areas where practical.

## Portable and installer artifacts

CPack will continue to generate and validate
`dota-keyboard-<version>-windows-x64.zip`. ZIP validation will extract that
package into a stable workspace directory such as
`build/windows-release/validated-portable`.

Both Windows workflows will upload the contents of that validated directory as
the `dota-keyboard-windows-x64-portable` Actions artifact. GitHub will create
the only download ZIP, whose root contains `dota_keyboard.exe`, runtime DLLs,
models, `config.json`, and `README.ru.md`.

The installer artifact remains a GitHub-created ZIP containing one
`DotaKeyboardSetup-<version>-windows-x64.exe`. For a tagged public release, the
installer and its SHA-256 file remain direct Release assets without the Actions
artifact wrapper.

README documentation will explain:

- installer: recommended per-user installation with Start Menu shortcut and
  uninstaller;
- portable: diagnostic/manual files with no installation;
- Actions downloads are ZIP-wrapped by GitHub;
- tagged Releases expose the installer EXE directly.

## Testing

Tracker regression tests will reproduce the dry-run patterns:

- an exact sent word remains suppressed across fewer than 15 missing frames and
  a large position jump;
- `HYPE` near sent `HYPERSTONE` is consumed;
- `EMA` near sent `BLADEMAIL` is consumed;
- `RN` and `DIH` inside sent `BLOODTHORN` are consumed;
- `ADDON` inside sent `ABADDON` is consumed even though its area exceeds 65%;
- sent `BANE` owns later `BANE` observations before a competing unsent
  `DECOY` track can claim them spatially;
- a different spatially overlapping word that is neither a strict substring
  nor below the 65% area ceiling remains eligible;
- a distinct full-size nearby word remains eligible;
- the same canonical word becomes eligible after 15 consecutive missing
  frames.

Normalizer tests will cover fully Cyrillic, mixed Cyrillic/Latin, normal ASCII
punctuation, and two-letter English words.

Workflow source tests will require both workflows to upload
`validated-portable` contents rather than the CPack ZIP while retaining package
generation and validation.

Native Windows CI must pass all tests, package validation, installer lifecycle
validation, and both artifact uploads. The final acceptance gate is a real-game
dry-run with no duplicate or fragment proposals in the supplied log.

## Release sequence

1. Implement and review the tracker, normalizer, artifact, and documentation
   changes on `feature/runtime-hardening-release`.
2. Run native Windows CI and download the new portable or installer artifact.
3. Complete a real-game dry-run and inspect its log.
4. Merge the accepted branch into `main` and push `main`.
5. Confirm the `main` Windows workflow is green.
6. Create and push tag `v0.1.0`; the release workflow validates that the tag is
   on `main`, builds again from the cached dependencies, and publishes the
   installer plus checksum.
