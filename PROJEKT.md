# Scene Dividers — Trenner für die OBS-Szenenliste

> Living-Document: Ziele, Architektur, Roadmap und Fortschritt für ein OBS-Plugin,
> das beschriftbare, einfärbbare Trenner in der **nativen** Szenenliste ermöglicht —
> wie Gruppen/Trenner bei den Quellen, nur für Szenen.

Letzte Aktualisierung: 2026-06-11

---

## 1. Projektziel

Die OBS-Szenenliste ist flach. Bei vielen Szenen (Streams mit Segmenten, Sport-
Produktionen, Multi-Format-Setups) fehlt jede optische Struktur. Dieses Plugin
fügt **Trenner** direkt in die echte Szenenliste ein: eine Linie mit optionaler
Beschriftung und Akzentfarbe, nicht anklickbar, frei positionierbar.

GitHub-Repo (geplant): **obs-scene-dividers** · Anzeigename: **Scene Dividers**

---

## 2. Festgelegte Entscheidungen (Stand 2026-06-11)

| Thema | Entscheidung | Konsequenz |
|---|---|---|
| **Ansatz** | **A: Eingriff in die native Szenenliste** (kein eigenes Dock) | Qt-Zugriff auf das `QListWidget` "scenes" im Hauptfenster; Abgrenzung zu obs-scene-tree-view (eigenes Dock). |
| **Umfang v1** | **Nur Trenner**, keine einklappbaren Gruppen | Kleiner, sicherer Start; Gruppen (Einklappen via `setRowHidden`) als mögliche v2. |
| **Mechanik** | **Marker-Szenen**: jeder Trenner ist eine echte, leere Szene | OBS übernimmt Persistenz, Reihenfolge und Listen-Lebenszyklus gratis; wir stylen nur. Sichtbar in obs-websocket/Streamdeck (dokumentierte Einschränkung). |
| **Erkennung** | **Private Settings** der Szenen-Source (`scene_dividers_marker`, `scene_dividers_color`) | libobs speichert `private_settings` pro Source in der Szenensammlung (obs.c) → überlebt Neustart/Collection-Wechsel, robust gegen Umbenennen. Kein Namens-Präfix nötig. |
| **Bedienung** | **Tools-Menü** → Verwaltungsdialog (hinzufügen/**umwandeln**/umbenennen/Farbe/entfernen/▲▼) | Trenner sind in der Liste bewusst nicht selektierbar, daher läuft Verwaltung über den Dialog. „Umwandeln" adoptiert bestehende Szenen als Trenner (und zurück). Kontextmenü/Hotkey ggf. später. |
| **Multiview** | Trenner werden **automatisch aus dem Multiview ausgeblendet** | Setzt OBS' natives Private-Setting `show_in_multiview=false` (sonst belegt die leere Marker-Szene einen Multiview-Slot). Sofort-Refresh über das `scenesReordered`-Signal → `UpdateMultiviewProjectors`. |
| **Aussehen** | Linie + **zentrierte Beschriftung** + **Akzentfarbe** pro Trenner | Eigener `QStyledItemDelegate`; Label = Szenenname; nur Striche/leer = reine Linie. |
| **Plattform** | Erstmal **nur macOS** (Dev-Maschine); Win/Linux später via Template-CI | dev/-Ninja-Build wie bei 2ME; offizieller Template-Build bleibt für CI/Release. |
| **OBS/Qt** | OBS 32.1.2 (lokal installiert), Qt 6.8.3 (OBS-Runtime) | Header aus obs-studio-32.1.2-Tarball; Qt nur Header/MOC, nicht gelinkt (dynamic_lookup). |

---

## 3. Architektur

### 3.1 Kernidee: Marker-Szenen + Restyling statt Fremd-Zeilen

OBS' Szenenliste (`SceneTree : QListWidget`, objectName `scenes`) enthält **eine
Zeile pro Szene**; die Zeilenreihenfolge IST die Szenenreihenfolge, die OBS als
`scene_order` in der Szenensammlung speichert. Wir fügen **keine fremden Zeilen**
ein (das würde OBS' Drag&Drop/Rebuild-Logik brechen), sondern legen pro Trenner
eine **echte leere Szene** an und stylen deren Zeile um:

- **Delegate** (`DividerDelegate : QStyledItemDelegate`): malt Trenner-Zeilen als
  Linie mit zentriertem Label (Farbe aus Private Settings, sonst Theme-Grau);
  normale Szenen gehen an die Basisklasse.
- **Flags**: Trenner-Zeilen verlieren `ItemIsSelectable|ItemIsEditable`. Wichtig,
  weil `on_scenes_currentItemChanged` → `SetCurrentScene` sonst beim Klick die
  Programmszene auf die leere Trenner-Szene schalten würde.
- **Restyle-Pass** läuft erneut bei: `OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED`,
  `SCENE_COLLECTION_CHANGED`, `FINISHED_LOADING` (Hook) sowie Model-Signal
  `rowsInserted` (OBS baut die Liste bei Events neu bzw. erweitert sie).

### 3.2 Verifizierte OBS-Interna (obs-studio 32.1.2)

- `frontend/forms/OBSBasic.ui`: `SceneTree` objectName **"scenes"** (unverändert seit 31).
- `OBSBasic_SceneCollections.cpp:885`: Reihenfolge wird beim Speichern aus dem
  Widget gelesen (`scene_order`) → programmatisches `takeItem`/`insertItem`
  ändert die echte Szenenreihenfolge; danach `obs_frontend_save()` + Signal
  `scenesReordered` (aktualisiert Multiview-Projektoren).
- `OBSBasic_SceneItems.cpp:178`: `source_create` → `AddScene` via **WaitConnection**
  → nach `obs_scene_create()` existiert das Listenitem **synchron** (UI-Thread);
  neue Szenen landen bei `currentRow + 1`.
- `libobs/obs.c:2342`: `private_settings` werden pro Source in der Collection
  gespeichert → unser Marker-Flag + Farbe persistieren automatisch.
- OBS-eigene Quellfarben nutzen denselben Private-Settings-Mechanismus (Vorbild).

### 3.3 Dateien

```
src/
├─ plugin-main.cpp      Modul-Lifecycle (post_load: Hook + Tools-Menü)
├─ sd-dividers.{hpp,cpp} Kern ohne Qt: Marker-Erkennung, Farbe, create/rename/
│                        remove (Private Settings, eindeutige Szenennamen)
├─ sd-scene-list.{hpp,cpp} Qt-Hook: findChild "scenes", DividerDelegate,
│                        Restyle-Pass, move_scene_row (nur Trenner-Zeilen)
└─ sd-dialog.{hpp,cpp}  Verwaltungsdialog (Tools-Menü), spiegelt die Liste
data/locale/{en-US,de-DE}.ini
dev/                    Schneller Ninja-Build ohne Xcode (siehe dev/README.md)
```

### 3.4 Bekannte Einschränkungen (dokumentieren, v1 akzeptiert)

1. **Marker-Szenen sind echte Szenen**: sichtbar in obs-websocket (Streamdeck/
   Companion), im Vollbild-Projektor-Menü und in Szenen-Dropdowns anderer
   Plugins. Empfehlung an Nutzer: Trenner sprechend benennen.
2. **Hotkey "Szene wechseln"** lässt sich theoretisch auch auf eine Trenner-Szene
   legen → schaltet auf die leere Szene. Nicht verhindert in v1.
3. **Grid-Modus** der Szenenliste: Trenner werden als normale Kachel mit Linie
   gemalt — funktional, aber nicht hübsch. Politur später.
4. **OBS-Update-Risiko**: Wir hängen am objectName "scenes" und am Verhalten von
   SceneTree. Bei jeder neuen OBS-Major-Version gegenprüfen (Abschnitt 3.2).

---

## 4. Roadmap

- [x] **Phase 0 — Setup & Skelett** ✅ (2026-06-11)
      Template-Gerüst von 2ME übernommen, auf `scene-dividers` konfiguriert.
      dev/-Ninja-Build grün (OBS-32-Anpassungen: frontend/api-Pfad, simde,
      generiertes obsconfig.h, data/-Bundling). `otool -L`: nur libc++/libSystem.
- [ ] **Phase 1 — Funktionstest v1** (Code komplett, Test durch Nutzer offen)
      Trenner anlegen/umbenennen/einfärben/verschieben/entfernen über Tools-Menü;
      Trenner-Rendering + Nicht-Auswählbarkeit in der Szenenliste.
      *DoD: Alle Operationen funktionieren live in OBS 32.1.2; Trenner überleben
      Neustart und Collection-Wechsel.*
- [ ] **Phase 2 — Politur**
      Kontextmenü-Eintrag in der Szenenliste, Hotkey, Grid-Modus-Darstellung,
      Schutz vor Szenenwechsel-Hotkeys auf Trenner, Farb-Reset.
- [ ] **Phase 3 — Release**
      Win/Linux-CI-Builds via Template, README-Screenshots/GIF, GitHub-Release,
      ggf. OBS-Forum/Plugin-Portal.
- [ ] **v2-Idee — Gruppen**
      Einklappbare Bereiche (Trenner als Gruppenkopf, Szenen darunter via
      `setRowHidden` verstecken). Erst nach stabiler v1 evaluieren.

---

## 5. Fortschrittslog

- **2026-06-11** — **Nach 1. Nutzer-Feedback.** (1) Trenner werden jetzt per
  `show_in_multiview=false` (OBS-natives Private-Setting, verifiziert in
  `frontend/components/Multiview.cpp:163` + `OBSBasic_Scenes.cpp:595`) automatisch
  aus dem Multiview ausgeblendet; Sofort-Refresh via `scenesReordered`-Signal, da
  `AddScene` das Multiview baut, bevor unser Flag steht. (2) **Umwandeln-Funktion**:
  bestehende Szenen lassen sich als Trenner adoptieren (und zurück) — löst, dass
  die alten, manuell angelegten Strich-Szenen des Nutzers (`------ME1------`) keine
  Marker hatten und daher nicht bearbeitbar waren. (3) Delegate optisch deutlicher
  (dezenter Hintergrund, dickere Linie bei reinen Linien, Akzentfarbe, Label
  uppercase + Strich-Padding entfernt). Baut & lädt. *Offen: erneuter Nutzertest.*
- **2026-06-11** — Projekt initialisiert. Ansatz entschieden (native Liste statt
  eigenem Dock; Abgrenzung zu obs-scene-tree-view), Mechanik Marker-Szenen +
  Private Settings, v1 nur Trenner (Label + Farbe), Bedienung über Tools-Menü,
  erstmal nur macOS. OBS-Interna gegen obs-studio-32.1.2-Quellen verifiziert
  (§3.2). Phase 0 abgeschlossen: Gerüst + kompletter v1-Code + grüner dev-Build,
  Plugin installiert. **Offen: erster Funktionstest in OBS durch Nutzer.**
  - dev-Build-Learnings ggü. 2ME: obs-studio 32 hat `UI/` → `frontend/`
    umstrukturiert (frontend-api unter `frontend/api`), bündelt **simde** nicht
    mehr im Tarball (Kopie liegt in `.deps/simde-include`) und `obs-config.h`
    verlangt ein generiertes `obsconfig.h` (minimal in dev/CMakeLists erzeugt).
  - Qt 6.8.3 (OBS-32-Runtime) == obs-deps Qt von 2ME → per Symlink
    wiederverwendet (`.deps/obs-deps-qt6-2025-07-11-universal`).

---

## 6. Referenzen

- Plugin-Template: https://github.com/obsproject/obs-plugintemplate
- OBS Frontend-API: https://docs.obsproject.com/reference-frontend-api
- Schwester-Projekt mit Build-Learnings: ../2ME (PROJEKT.md §8)
- Abgrenzung: obs-scene-tree-view (eigenes Dock mit Ordnerbaum)
