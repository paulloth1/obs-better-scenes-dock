# Better Scenes Dock — Ordner, Trenner & Farben für die OBS-Szenenliste

> Living-Document: Ziele, Architektur, Roadmap und Fortschritt für ein OBS-Plugin,
> das ein **verbessertes Szenen-Dock** bereitstellt: die gewohnte OBS-Funktionalität
> plus verschachtelbare **Ordner**, einklappbare **Trenner** und **Farben** für
> Szenen, Ordner und Trenner.

Letzte Aktualisierung: 2026-06-12 · v1.2.0 · GitHub-Repo: **github.com/paulloth1/obs-better-scenes-dock** (öffentlich)

---

## 1. Projektziel & Historie

Erstes Konzept war „Scene Dividers": Trenner direkt in die **native** Szenenliste
hacken (Ansatz A, Marker-Szenen + Qt-Delegate auf dem `scenes`-QListWidget). In der
realen Umgebung des Nutzers erwies sich das als zu fragil (Erkennung/Umfärben
unzuverlässig, Abhängigkeit von OBS-Internas). **Entscheidung 2026-06-11: Pivot auf
Ansatz B** — ein eigenes Dock, das die native Szenenliste **ersetzt** und volle
Kontrolle über Darstellung und Struktur gibt.

Ziel: ein Dock, das sich wie die OBS-Szenenliste bedient, aber zusätzlich echte
Ordnerstruktur, einklappbare Bereiche und Farbcodierung bietet.

---

## 2. Festgelegte Entscheidungen (Stand 2026-06-11)

| Thema | Entscheidung | Konsequenz |
|---|---|---|
| **Ansatz** | **Eigenes Dock** (`QTreeWidget`), **ersetzt** das native Szenen-Dock | Beim Laden wird das native `scenesDock` ausgeblendet; unser Dock „Szenen" tritt an seine Stelle. (Native bleibt über das Docks-Menü reaktivierbar.) |
| **Struktur** | **Echte verschachtelte Ordner + Trenner** (Baum-Datenmodell) | Ordner enthalten Szenen/Unterordner und sind einklappbar; Trenner sind beschriftete Linien. |
| **Farben** | Für **Szenen, Ordner und Trenner** | Pro Knoten `#RRGGBB`; Delegate malt Akzentbalken/-text. |
| **v1-Umfang** | **Minimal zuerst**: anzeigen/wechseln (Studio-Preview-aware), Ordner/Trenner anlegen + einklappen, Farben, Verschieben per Kontextmenü | Szenen anlegen/löschen/umbenennen **und Drag&Drop** kommen direkt danach (Phase 2). |
| **Persistenz** | Eigene JSON-Datei im Modul-Config-Dir, **pro Szenensammlung** | `obs_module_config_path("structure.json")`; Map `Collection → Baum`. Umgeht Timing-Probleme des Save-Callbacks beim Start; voll unter eigener Kontrolle. |
| **Plattform/OBS/Qt** | macOS zuerst; OBS 32.1.2 / Qt 6.8.3 | dev/-Ninja-Build wie bei 2ME; Qt nur Header/MOC (dynamic_lookup). |

---

## 3. Architektur

### 3.1 Verifizierte OBS-APIs (frontend/api/obs-frontend-api.h, OBS 32.1.2)

- **Dock**: `obs_frontend_add_dock_by_id(id, title, QWidget*)` / `obs_frontend_remove_dock(id)`.
- **Szenen**: `obs_frontend_get_scenes`, `obs_frontend_get/set_current_scene`,
  `obs_frontend_get/set_current_preview_scene`, `obs_frontend_preview_program_mode_active`.
- **Events**: `SCENE_CHANGED`, `SCENE_LIST_CHANGED`, `PREVIEW_SCENE_CHANGED`,
  `STUDIO_MODE_ENABLED/DISABLED`, `SCENE_COLLECTION_CHANGING/CHANGED`, `FINISHED_LOADING`, `EXIT`.
- **Persistenz-Helfer**: `obs_module_config_path`, `os_mkdirs`, `obs_data_*`-JSON.
- **Natives Dock**: `QMainWindow::findChild<QDockWidget*>("scenesDock")` → `hide()`.

### 3.2 Datenmodell ([src/bsd-model.hpp](src/bsd-model.hpp))

`Node`-Baum: Typen `Root / Folder / Divider / Scene`. Szenen-Knoten referenzieren
eine echte OBS-Szene über den Namen (`name == Szenenname`); Ordner/Trenner sind
reine Plugin-Metadaten mit eigener `id`, `name`, `color`, `collapsed`.

- `reconcile_with_obs()`: entfernt Szenen-Knoten, deren Szene nicht mehr existiert,
  und hängt neue OBS-Szenen (in OBS-Reihenfolge) ans Root-Ende. Hält den Baum mit
  der realen Szenenliste konsistent.
- Persistenz: `load/save_for_collection(name)` → JSON-Map `{ Collection: { children:[…] } }`.

### 3.3 Dock-UI ([src/bsd-dock.cpp](src/bsd-dock.cpp))

`QTreeWidget` + eigener `QStyledItemDelegate`:
- **Szene anklicken** → `set_current_preview_scene` (Studio-Modus) bzw.
  `set_current_scene`. **Program rot / Preview grün** als linker Balken + fett.
- **Ordner**: einklappbar (Zustand → Modell, persistiert).
- **Trenner**: als Linie mit zentriertem Label gerendert, nicht als Szene wählbar.
- **Farben**: Akzentbalken links + getönter Text.
- **Kontextmenü**: Neuer Ordner / Neuer Trenner / Umbenennen (Ordner+Trenner) /
  Farbe setzen+entfernen / Verschieben nach ▸ (Ordnerbaum) / Entfernen.
- **Event-getrieben**: Rebuild bei Listen-/Collection-Änderung; Highlight bei
  Scene/Preview/Studio-Wechsel; Save bei Collection-Wechsel und EXIT.
- Beim Laden wird das native `scenesDock` ausgeblendet.

### 3.4 Dateien

```
src/
├─ plugin-main.cpp     Modul-Lifecycle (post_load → dock_register)
├─ bsd-model.{hpp,cpp} Baum-Datenmodell + reconcile + JSON-Persistenz (Qt-frei)
└─ bsd-dock.{hpp,cpp}  QTreeWidget-Dock, Delegate, Events, Kontextmenü
data/locale/{en-US,de-DE}.ini
```

---

## 4. Roadmap

- [x] **Phase 0 — Pivot & Walking Skeleton** ✅ (2026-06-11)
      Projekt auf `better-scenes-dock` umgestellt, altes Scene-Dividers-/Native-
      Listen-Coding entfernt. Modell + Dock implementiert, baut grün, nur
      libc++/libSystem gelinkt. *Offen: erster interaktiver Test in OBS.*
- [x] **Phase 1 — v1 minimal verifiziert** ✅ (2026-06-11, vom Nutzer getestet)
      Dock ersetzt native Liste; Szenen anzeigen/wechseln (inkl. Studio-Preview);
      Ordner/Trenner anlegen, einklappen, einfärben; Verschieben per Kontextmenü;
      Struktur überlebt Neustart + Collection-Wechsel.
- [~] **Phase 2 — Volle Szenen-Bedienung** (teilweise)
      ✅ Szene anlegen/löschen/duplizieren/umbenennen aus dem Dock; ✅ Hoch/Runter-
      Sortieren innerhalb des Elternknotens. Rename hält den Knoten an Ort & Stelle
      (id == Szenenname). Offen: **Drag&Drop** (Sortieren + in/aus Ordner ziehen),
      Doppelklick-Umbenennen, Rename-Sync bei externer Umbenennung.
- [x] **Phase 3 — Parität & Politur** ✅ (2026-06-11)
      ✅ Toolbar (`＋`/`－`/Farbe/Filter/`▲`/`▼`); ✅ volle Szenen-Kontextmenü-Parität:
      Projektor (Fenster/Monitor-Vollbild), Übergang überschreiben (inkl. Dauer-
      Spinbox), Szenen-Screenshot, Filter kopieren/einfügen, Filter öffnen.
      Offen (Politur, nicht release-blockierend): Suchfeld, Studio-Modus-Feinschliff.
- [x] **Phase 4 — Release v1.0.0** ✅ (2026-06-11)
      Version 0.2.0 → 1.0.0; Tag `1.0.0` löst Template-CI (macOS/Windows/Ubuntu) +
      Draft-Release mit Downloads aus. README aktualisiert.
- [x] **Phase 5 — Release v1.1.0** ✅ (2026-06-12)
      Studio-Modus-Doppelklick (Preview→Programm), Suchfeld (ein-/ausblendbar),
      Ordner-Szenenzähler, externer Rename-Sync (`source_rename`-Signal),
      **OBS-Szenenreihenfolge immer angeglichen** (kein Schalter), Duplizieren
      refs/Kopien, Farbpaletten-Picker, **Im Multiview anzeigen** im Kontextmenü
      (`show_in_multiview`-Flag + `scenesReordered`-Refresh). Modell in OBS-freie
      `bsd-tree.cpp` + `bsd-model.cpp` getrennt → Unit-Tests (`tests/`, eigene
      CI). CI-Housekeeping (Node-24-Opt-in). macOS-Notarisierung verworfen (kein
      Apple-Dev-Account). Tag `1.1.0`.

---

## 5. Risiken & offene Punkte

1. ~~**Reihenfolge**~~: **Gelöst (v1.1)** — Dock-Reihenfolge wird per
   `findChild<QListWidget>("scenes")` in OBS' (verstecktes) `SceneTree` gespiegelt;
   `obs_frontend_get_scenes`, gespeicherte `scene_order` und Multiview lesen genau
   daraus. Multiview-Refresh via `scenesReordered`-Signal.
2. ~~**Externe Umbenennung**~~: **Gelöst (v1.1)** — globales `source_rename`-Signal
   aktualisiert den Knoten in place (Ordner/Farbe bleiben erhalten).
3. **Natives Dock ausgeblendet**: Wir verstecken `scenesDock` bei jedem Laden.
   Nutzer kann es über das Docks-Menü zurückholen; beim nächsten Start wieder weg.
4. **OBS-Update-Risiko**: Hängt an den Objektnamen `scenesDock` (Ausblenden) und
   `scenes` (Reihenfolge-/Multiview-Sync). Bricht das, laufen schlimmstenfalls beide
   Docks bzw. der Order-Sync wird stillschweigend zum No-op — unkritisch.

---

## 6. Fortschrittslog

- **2026-06-11** — **Pivot auf Ansatz B (eigenes Dock).** Grund: native-Listen-
  Variante (Scene Dividers) war beim Nutzer unzuverlässig (nur eine Szene erkannt,
  Umwandeln/Einfärben fehlerhaft). Neues Konzept „Better Scenes Dock": ersetzt die
  native Liste, echte Ordner + Trenner + Farben. Alle tragenden APIs gegen OBS-32-
  Quellen verifiziert (Dock, Szenenwechsel, Studio-Preview, Events, Persistenz).
  Modell + Dock implementiert (v1 minimal), baut & linkt sauber. Projekt-Identität
  auf `better-scenes-dock`/`0.2.0` umgestellt; alte `sd-*`-Quellen entfernt.
  *Nächster Schritt: OBS neu starten, Dock interaktiv testen.*
- **2026-06-11** — **Toolbar am Dock-Fuß** (`＋`/`－`/`⚙`/`▲`/`▼`, NLE-Look wie das
  native Dock). `＋` ist ein Menü (Neue Szene / Neuer Ordner / Neuer Trenner), `－`
  entfernt den selektierten Knoten (Szene mit Bestätigungsdialog → `obs_source_remove`,
  Ordner/Trenner direkt), `⚙` öffnet die Szenenfilter, `▲`/`▼` sortieren innerhalb
  des Elternknotens (`Model::move_within_parent`). Trenner sind jetzt selektierbar
  (für Toolbar-Aktionen), schalten beim Klick aber keine Szene. `updateHighlight()`
  kapert nicht mehr die Selektion/Scrollposition. Baut & installiert grün.
  Zieht Teile von Phase 2/3 vor. *Noch ungetestet in OBS — Neustart nötig.*
- **2026-06-11** — **Toolbar-Politur + öffentliches Repo.** `＋`-Dropdown-Pfeil
  entfernt, Farb-Button (Live-Swatch der Auswahl) ergänzt, Zahnrad durch selbst
  gezeichnetes Filter-Trichter-Icon ersetzt (konsistente Größe, Theme-Farbe). Repo
  **öffentlich** auf `github.com/paulloth1/obs-better-scenes-dock`; Template-CI baut
  macOS/Windows/Ubuntu.
- **2026-06-11** — **V1-Kontextmenü (Szenen).** Volle Parität zum nativen Dock über
  Frontend-API: Umbenennen (hält Knoten in place), Duplizieren (`obs_scene_duplicate`
  REFS), Szenen-Projektor (Fenster/Monitor-Vollbild via `QGuiApplication::screens`),
  Übergang überschreiben (Privat-Settings `transition`/`transition_duration` + Dauer-
  Spinbox, OBS-Default 300 ms), Szenen-Screenshot, Filter kopieren/einfügen
  (`obs_source_copy_filters`). Alle Signaturen gegen OBS 32.1.2 verifiziert. Vom
  Nutzer getestet & abgenommen → **Release v1.0.0** (Version gebumpt, Tag `1.0.0`).
- **2026-06-12** — **V1.1.** Studio-Doppelklick (Preview→Programm), Suchfeld
  (ein-/ausblendbar, Filter mit Ordner-Reveal), Ordner-Szenenzähler im Delegate,
  externer Rename-Sync via globalem `source_rename`-Signal (auf GUI-Thread
  gemarshallt), **immer aktive** OBS-Szenenreihenfolge-Spiegelung ins versteckte
  `SceneTree` + Multiview-Refresh (`scenesReordered`), Duplizieren refs/Kopien
  (Einstellung), Farbpaletten-Picker (Preset-Swatches + Custom/Clear), **Im
  Multiview anzeigen** (`show_in_multiview`-Privat-Flag). Architektur: pure
  Tree-Logik in `bsd-tree.cpp` (OBS-frei) ausgelagert → Header forward-declared
  obs_data*, eigenständige Unit-Tests unter `tests/` (eigene `tests.yaml`-CI ohne
  OBS-Deps). CI-Housekeeping: `FORCE_JAVASCRIPT_ACTIONS_TO_NODE24` in allen
  Workflows. macOS-Notarisierung bewusst verworfen (kein bezahlter Apple-Account).
  Vom Nutzer getestet → **Release v1.1.0** (Tag `1.1.0`).
- **2026-06-12** — **V1.2 (Politur-Bundle).** Fixes: Such-Collapse-State wird beim
  Leeren wiederhergestellt (`restoreExpansion`), `source_rename`-Disconnect gegen
  null-Handler abgesichert, Ordner-Zähler-Badge kontraststärker (Text-Farbe @ alpha
  150 statt Disabled). Adds: Tastenkürzel (F2/Entf/Strg+↑↓ via `QShortcut`,
  `WidgetShortcut`-Kontext), Alle ein-/ausklappen (Kontextmenü), zuletzt gewählte
  Szene wird pro Collection in `settings.json` gemerkt + wiederhergestellt, Such-
  Treffer-Highlight (Delegate `setQuery`, translucenter Marker via `subElementRect`),
  „Neue Szene" landet im rechtsgeklickten Ordner. #10: Indikator-Punkte an Szenen
  (Filter = teal `obs_source_filter_count`, Übergangs-Override = amber Private-Setting
  `transition`), Refresh nach Override/Paste-Filters. Tag `1.2.0`.
- *(Historie der Scene-Dividers-Phase: siehe git-Log vor diesem Commit.)*

---

## 7. Referenzen

- Plugin-Template: https://github.com/obsproject/obs-plugintemplate
- OBS Frontend-API: https://docs.obsproject.com/reference-frontend-api
- Schwester-Projekt mit Build-Learnings: ../2ME (PROJEKT.md §8)
- Verwandt: obs-scene-tree-view (eigenes Dock mit Baum) — wir ergänzen Farben +
  einklappbare Trenner + native Ersetzung.
