# kosc

`kosc` is a Bela C++ project built around a two-channel harmonic resonator network.
The main DSP implementation lives in `render.cpp`.

Current runtime note: this patch is running at about 33% CPU, so there is room for
further sound design and structural experiments.

## Architecture at a glance

- 15 nodes (`NUM_OSCS`) per channel
- Frequency map per node: subharmonics -> fundamental -> harmonics
- Multipliers: `1/8, 1/7, ..., 1/2, 1, 2, ..., 8`
- Per-node oscillator + per-node bandpass excitation and envelope
- Cross-node coupling matrix for harmonic diffusion (`Spread`)
- Rotating input and output scanners for timbral movement

## Signal flow

Per audio frame, each channel does:

1. Read input sample and update smoothed input envelope
2. Compute input scanner crossfade weights across adjacent nodes
3. For each node:
   - Bandpass filter the injected signal
   - Track node envelope with attack/decay smoothing
   - Advance phase and generate sine output
4. Apply asymmetric coupling diffusion across node envelopes
5. Output scanner selects/interpolates between adjacent nodes
6. Write stereo outputs with global amplitude scaling

## Controls (GuiController)

- `F0 (Hz)`: fundamental used to derive all node frequencies
- `Spread`: amount of envelope diffusion through coupling matrix
- `Input A Rotation`, `Input B Rotation`: where each channel injects energy
- `Output A Rotation`, `Output B Rotation`: where each channel reads out
- `Node Env Attack`, `Node Env Decay`: envelope response per node

## File guide

- `render.cpp`: DSP graph, coupling logic, scanners, and GUI controls
- `settings.json`: Bela runtime/audio/analog configuration
- `.bela-host`: default SSH target host for deploy tooling
- `.vscode/tasks.json`: Cursor tasks for deploy/sync/stop via `bela-tools`
- `.gitignore`: local artifact exclusions

## Development workflow (Cursor + bela-tools)

From the `kosc` folder in Cursor:

- Deploy and run: `Terminal -> Run Task -> Bela: Deploy`
- Sync only: `Terminal -> Run Task -> Bela: Sync only`
- Stop audio: `Terminal -> Run Task -> Bela: Stop`

Equivalent CLI:

```bash
bela deploy .
bela sync .
bela stop
```

## Safe first experiments

If you want to evolve the patch while keeping behavior predictable:

- Change node frequency set (`mult[]`) to alternate harmonic systems
- Modify coupling threshold and weight law in `buildCouplingWeights()`
- Try different envelope curves in the node attack/decay branch
- Replace sine output with lightweight waveshaping per node
- Add optional per-node damping tied to frequency region

## Notes

- Keep per-frame allocations out of `render()` to preserve realtime stability.
- If CPU climbs, first inspect node loop work and coupling loop complexity.