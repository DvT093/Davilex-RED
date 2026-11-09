# Overstappen op de gedeelde menu-engine

`menu/engine/` is één engine voor Vakantie Racer (VR), Europe Racer (ER) en A2 Racer IV (A2). Hij is samengevoegd uit de drie kopieën met een 3-weg-merge op de gemeenschappelijke basis (VR-commit `d27da30`). ER draait er sinds 9 nov 2026 op: alle 54 testshots zijn in de originele look pixelgelijk aan de eigen kopie, op randgladheid van letters na (max 45 px per scherm). VR draait er ook op sinds 9 nov 2026 (zie onder).

## Inbouwen (alle games)
1. Verwijder `src/menu/engine/`.
2. In `src/menu/CMakeLists.txt`:
   ```cmake
   set(DAVILEX_RED_DIR ${CMAKE_SOURCE_DIR}/../Davilex-RED CACHE PATH "Shared Davilex RED code")
   include(${DAVILEX_RED_DIR}/menu/davi_menu.cmake)
   target_link_libraries(<jouw lib of exe> PUBLIC davi_menu_engine)
   ```
   `davi_menu.cmake` regelt SDL3, Freetype, zlib, ffmpeg en `objc` (macOS). Includes blijven `"engine/<x>.h"`.
3. Wijzig je engine-bestanden voortaan alleen hier, en meld het de andere sessies (ze bouwen er ook op).
4. Optioneel: de race-handshake (`launcher/race_launcher.h`, `red::RaceLauncher`: game.ini schrijven in SwARc's overlay, de race op de plek van het menuvenster starten, opnieuw bij "restart", uitslag lezen) staat ook hier, als target `davi_menu_launcher` (brengt `davi_menu_engine` mee). Verwijder dan `src/menu/launcher/`. Een game die zijn eigen kopie houdt, linkt dit target gewoon niet.

## Wat er voor iedereen anders is
- **Tekenen gaat via de Painter van VR** (`paint.h`, `hd.h`): native resolutie, HD-plaatjes, scherpe tekst, F9 origineel/upgraded. `AppConfig::upgraded`, `hd_dirs`, `render_w/h` zijn er voor alle games. `PaintOver` krijgt een transparante overlay: SDL-tekenen daarop wordt geschaald, `davi::Blit`/`davi::DrawText` wordt scherp getekend.
- **Plaatjes met eigen alpha** (32-bit TGA, oppervlak met `SDL_BLENDMODE_BLEND`) worden met alpha getekend. Was een fix voor ER/A2. BMP's van VR blijven ondoorzichtig.
- **`Font::DrawLine` op een canvas met alpha** mengt nu met "over" (randpixels krijgen gedeeltelijke alpha in plaats van zwart). Op een canvas zonder alpha blijft het zoals het was.
- **`Font::embolden`** (synthetisch vet, ER's Hilde) werkt nu ook in de scherpe tekst.
- **Radiogroepen per parent**: een radio zet alleen radio's uit met dezelfde `group` én dezelfde `parent` (Delphi-gedrag). `group` = `GroupIndex`, anders `RadioGroupNumber`, anders 0.
- **Focus**: zet `OnShow` zelf de focus op een zichtbare, ingeschakelde widget, dan blijft die staan (was A2).
- **Ini**: `Keys()` geeft sleutels zoals geschreven, ontdubbeld zonder op hoofdletters te letten (was A2).
- **DFM**: `TDaviWave` zonder lengteprefix (RIFF/BM direct) wordt herkend (was A2).
- **Layout-ini**: elk componenttype neemt `Left/Top/Width/Height/Visible` uit de layout-ini; tekst en lettertype alleen waar dat past (was ER). VR: check of een `TDaviThumb`/slider/`kOther` in `MenuCaps.ini` een positie heeft die je niet wilt.
- **Assets**: `Bitmap("naam")` zonder extensie probeert `.bmp` en dan `.tga`, uit mappen en daarna archieven. Nieuw: `Has(naam)`, `Load(naam)` (bytes), `AddArchive(dir, base)` (RCS `.IND/.IMG`, ruwe en zlib-items allebei), `LoadImage(bytes)` (BMP of TGA), `LoadTga(bytes)`.

## Vakantie Racer
- Overgezet 9 nov 2026: `vracer-menu` linkt `davi_menu_launcher` (engine + launcher uit Davilex-RED), `src/menu/engine/` en `src/menu/launcher/` zijn weg. 136 testshots (origineel en upgraded, 3024x1964 en 800x600) naast de eigen kopie gelegd: gelijk, op twee bedoelde verschillen na. (1) Focus die `OnShow` zelf zet, blijft nu staan (Moeilijkheid op je eigen niveau, OK in Toetsen en Gevorderd, het naamveld bij een record); de oude engine zette er altijd de eerste knop overheen. (2) Radiogroepen per parent: in Opties zette Joystick kiezen eerder ook de Weergave-radio's uit (het monitorplaatje verdween).
- Geen API-wijziging voor TDavi*-componenten, `TDaviThumb`, hd/paint of `RunChild`. Let op de punten hierboven bij radiogroepen en de layout-ini.
- De upscale-tools (`tools/upscale_menu.py`, `menu_text.py`, `menu_lamps.py`) leunen op de plaatjesnamen (`NamePicture`): die zijn ongewijzigd.

## A2 Racer IV
| Was in A2 | Nu |
|---|---|
| `skin.gdi_prefix(key) -> prefix` | `skin.def_link(key, state) -> naam`, met `key` = `"TOwner.def"` en `state` 0-4 (Up, Down, Focused, Hovered, Disabled). Voor A2: `prefix + "_" + {"up","do","fo","hi","di"}[state] + ".tga"`. Een naam die niet bestaat wordt stil overgeslagen. |
| `skin.default_font = 0` | blijft, plus `skin.raw_font_index = true` (anders gelden VR's regels: 1 = font 1, de rest font 2) |
| `assets.AddArchive("…/MenuRcs.IND")` | `assets.AddArchive(dir, "MenuRcs")` |
| `assets.Read(naam)` | `assets.Load(naam)` |
| `LoadPicture(bytes)` | `LoadImage(bytes)` |
| `GraphicDef::shared` | weg: de engine maakt een eigen kopie van archiefplaatjes |
| `Form::Add(..., parent)`, `group = 1000 + parent` | `Widget::parent` (naam) en groepen per parent, zie boven |
| TGDIButton-maat uit `def->w/h` | maat van het Up-plaatje van de definitie |
| `Translucent`/`TranslucencyFactor` op definitie en TGDIGraphic | zelfde velden (`alpha`), getekend via `Painter::SetAlpha` |
- Nieuw voor A2 (uit ER): `skin.graphic_name(form, comp)` voor TGDIGraphic/TGDISlider, `skin.text(value)` voor captions, `skin.font_names` + `FontIndex()`, `GraphicDef` met eigen font (`has_font`, `font`, `font_px`, `font_color`, `left`), `CaptionRect` met `w/h <= 0` = tot de rand min dat, ZValue-volgorde, `BgndImage` onderop.
- Het oude log "script: type, but the focus is on …" is niet meegekomen.
