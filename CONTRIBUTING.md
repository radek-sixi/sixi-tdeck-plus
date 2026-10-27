# Contributing to Faro

## Boundaries

- `app/core/` and `app/ui/` never include Arduino, MeshCore or SD headers; they talk to the mesh only through
  `IMeshActions` (`app/core/MeshActions.h`) and the `AppStore`. That is what lets the simulator and the
  desktop console run the same UI.
- `external/MeshCore` is unmodified upstream MeshCore. Faro changes go in this repository; changes that belong
  in MeshCore go upstream as a separate pull request. `scripts/verify_upstream_clean.sh` checks the submodule.
- Radio settings must stay inside the rules of the selected region (`app/core/RadioRegions.h`). A new region
  needs its sub-bands, power ceiling and duty cycle from the national rules (with a link in the pull request),
  presets that fit it, and a duty cycle MeshCore can enforce (10 % or more).

## Before you open a pull request

- Format C/C++ with `clang-format` (`.clang-format`).
- Host build with warnings and the unit tests: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build`.
- Build the firmware envs you touched: `pio run -d external/MeshCore -e faro_tdeck` (and the others in `envs/faro.ini`).
- `scripts/license_audit.sh`: no GPL/AGPL/LGPL source in the tree, notices complete.

Contributions are accepted under Apache-2.0 with a DCO sign-off (`git commit -s`).
