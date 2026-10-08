# Pulse — Him'z DSP

4x-oversampled hybrid even/odd saturator with analog micro-drift (gain + pan) and an optional scalable noise floor.

## Build locally
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

Output: `build/Pulse_artefacts/Release/VST3` (and `AU` on macOS).

## CI
Every push builds Windows (VST3) and macOS (VST3 + AU, universal) via GitHub Actions. Download binaries from the run's Artifacts section.

## Controls
Drive, Character (odd <-> even), Drift, Mix, Output, Noise toggle + level.
