# `dev/` — Schneller lokaler Build (macOS, ohne volles Xcode)

Dieser Ordner ist ein **isolierter Dev-Build** für schnelle Iteration auf Apple
Silicon. Er läuft mit **Ninja + Command Line Tools** und braucht **kein volles
Xcode**. Der offizielle, plattformübergreifende Template-Build im Repo-Root
(Xcode-Generator, CI, Signing) bleibt davon unberührt und wird für Releases
genutzt. (Aufbau wie beim 2ME-Projekt, angepasst an obs-studio 32.)

## Funktionsweise

- Kompiliert die Quellen aus [`../src`](../src) gegen die **libobs-Header**.
- Linkt mit `-undefined dynamic_lookup`: libobs-/Qt-Symbole werden erst beim
  Laden durch OBS aufgelöst → **kein kompiliertes libobs nötig**, nur Header.
  Qt wird bewusst **nicht gelinkt** (nur Header/MOC) — vermeidet transitive
  Frameworks wie das entfernte AGL.
- Baut ein `scene-dividers.plugin`-Bundle inkl. `data/` (Locale), signiert es
  ad-hoc und kopiert es nach `~/Library/Application Support/obs-studio/plugins/`.

## Voraussetzungen (einmalig)

1. CMake + Ninja (via Homebrew): `brew install cmake ninja`
2. obs-studio-Quellen passend zur installierten OBS-Version (Standard: **32.1.2**):

   ```sh
   mkdir -p .deps && cd .deps
   curl -fL -o obs-studio-32.1.2.tar.gz \
     https://github.com/obsproject/obs-studio/archive/refs/tags/32.1.2.tar.gz
   tar xzf obs-studio-32.1.2.tar.gz
   ```

   Erwartete Pfade: `.deps/obs-studio-32.1.2/libobs/obs-module.h` und
   `.deps/obs-studio-32.1.2/frontend/api/obs-frontend-api.h`
   (Andere Version? `-DOBS_VERSION=<x.y.z>` an CMake übergeben. Achtung: seit
   OBS 32 liegt die Frontend-API unter `frontend/api`, vorher `UI/obs-frontend-api`.)

3. **simde**-Header (obs-studio 32 bündelt sie nicht mehr im Tarball; libobs'
   `sse-intrin.h` braucht `<simde/x86/sse2.h>`). Entweder aus einem älteren
   OBS-Quellbaum kopieren (`libobs/util/simde` aus ≤ 31.x) oder von
   https://github.com/simd-everywhere/simde holen — Ziel:
   `.deps/simde-include/simde/x86/sse2.h`

4. **Qt6-Header** aus obs-deps, passend zur OBS-Laufzeit (OBS 32.1.2 → Qt 6.8.3):

   ```sh
   cd .deps
   curl -fL -o qt6.tar.xz \
     https://github.com/obsproject/obs-deps/releases/download/2025-07-11/macos-deps-qt6-2025-07-11-universal.tar.xz
   mkdir obs-deps-qt6-2025-07-11-universal
   tar xJf qt6.tar.xz -C obs-deps-qt6-2025-07-11-universal
   ```

   (Auf dieser Maschine: Symlink auf die bereits entpackten Qt-Deps des
   2ME-Projekts.)

## Bauen

```sh
./dev/build.sh
```

Danach OBS starten und im Log prüfen (Hilfe › Logdateien › Aktuelles Log
anzeigen): `[scene-dividers] Scene Dividers loaded successfully` und
`scene list hooked (divider delegate installed)`.

## Grenzen

- Nur für lokale Dev-Iteration (arm64, ad-hoc-signiert, keine Notarisierung/CI).
- Release-Builds laufen über den Template-Build im Repo-Root (GitHub Actions).
