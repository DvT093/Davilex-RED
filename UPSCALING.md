# Upscalen naar 4K: werkwijze voor elke Davilex-port

Hoe Vakantie Racer (9 nov 2026) zijn race-textures, HUD en menu scherp kreeg tot 4K, zo opgeschreven dat Europe Racer en A2 Racer IV het kunnen volgen. Lees eerst `LEARNINGS.md` (vooral 1, 2 en 6). Referentiecode staat in `../Vakantieracer` (`tools/make_texture_pack.py`, `tools/texture_pack.ini`, `tools/upscale_menu.py`, `tools/menu_text.py`, `tools/menu_lamps.py`, `src/menu/engine/paint.*`, `hd.h`) en in SwARc `legacy-dx5` (`platforms/win32/src/win32/texture_pack.{h,cpp}`).

## Kernprincipes (niet onderhandelbaar)

1. **De game blijft onaangeroerd.** Geen herschreven archief, geen binaire patch. HD-versies liggen ernaast en worden er tijdens het spel overheen gelegd.
2. **Alles komt uit de eigen cd van de gebruiker**, via een script, en gaat nooit in git (`build/…` of `mods/*/local`, gitignored). In git staan alleen het script en de uitzonderingslijsten.
3. **Live schakelbaar:** F9 wisselt origineel/upgraded, ook in het pauzemenu. Zonder pakket of zonder lettertype valt alles terug op het origineel.
4. **Eerst zelf vergelijken, dan melden:** origineel en upgraded naast elkaar op 100%, ook de randen. Zeg eerlijk wat niet beter wordt.

## Deel A: race-textures en HUD (texture pack in SwARc)

### Hoe het werkt

SwARc herkent elke texture aan zijn pixels, zoals de texture packs van console-emulators (Dolphin, PCSX2). Bij elke upload van het bovenste mip-niveau berekent hij een hash en zoekt hij `<hash>.png` in de pakketmap. Gevonden: hij tekent de PNG (eigen mip-keten, eigen alpha) in plaats van de texture, zolang Upgraded aan staat. De game ziet de vervanging nooit.

- Hash: FNV-1a 64 over zeven u32 (breedte, hoogte, bits per pixel, R/G/B/A-maskers uit de DDPIXELFORMAT), gevolgd door de pixels **zoals de game ze in de texture schrijft** (bij de RED-engine na de omzetting naar R5G6B5: de kleursleutel wordt 0x0000, echt zwart 0x0800). Je moet dus de omzetting van de game nabootsen, niet de tga zelf hashen.
- Herkenning bij elke upload, dus ook als de texture-cache van de game een surface hergebruikt.
- Kopieert de game tijdens het spel een paar pixels in een vervangen texture (remlicht, bord), dan blijft de HD-versie en krijgt hij die pixels opgeschaald erbij. Bij een grote wijziging valt hij terug op het origineel.
- PNG's worden op worker-threads gedecodeerd (anders lange laadtijd en een macOS-strandbal). Tot ze klaar zijn tekent de game zijn eigen texture, meestal nog tijdens het laadscherm.
- Configuratie: `GameConfig::texture_pack` of `<ID>_TEX_PACK=map`. Schakelaar: `gfx::Enhancements::replace_textures` (aan F9 hangen).

### Stappenplan voor een nieuwe game

1. **Mechanisme overnemen:** de SwARc-commits van `texture_pack` (a539e23 en de worker-thread-commit 5aa2983) in jouw SwARc-worktree, `GameConfig::texture_pack` zetten en `replace_textures` aan je F9-schakelaar hangen.
2. **Hashes vinden:** speel elke baan een stukje met `<ID>_TEX_DUMP=map`. Dat schrijft elke texture één keer als `<hash>.png`, met de sleutel als doorzichtig. Vergelijk dat met je eigen hashberekening in het script tot ze 100% gelijk zijn. Pas dan kan het script het pakket maken zonder dat je elke baan hoeft te spelen.
3. **Script `tools/make_texture_pack.py`** (kopieer dat van Vakantie Racer en pas het per game aan):
   - textures uit het archief halen (bij Europe Racer eerst zlib uitpakken), alleen de bladen die de game echt als texture laadt;
   - **sleutel naar alpha:** de sleutelkleur (RED: 255,0,127) plus de bijna-magenta rand ertegenaan wordt doorzichtig;
   - doorzichtige texels krijgen de kleur van hun buren, anders roze randen na het upscalen;
   - 16 px rand eromheen: de overkant (wrap) voor herhalende bladen zonder sleutel, anders de rand herhaald; per texture om te draaien in `texture_pack.ini`;
   - 4x met Real-ESRGAN (**4xNomos8kSC**), rand er weer af; de alpha apart als zwart-wit plaatje ook door het model, drempel 0,5 (zachtere bladranden dan Lanczos);
   - `<hash>.png` wegschrijven; alleen wat ontbreekt of veranderd is opnieuw maken (cache met `VERSION`, verhogen als de verwerking verandert);
   - Real-ESRGAN en het model zelf downloaden naar `build/tools/` met sha256-check (nooit in een scratchpad laten staan).
4. **Uitzonderingen in `tools/texture_pack.ini`:** `[skip]` (texture niet vervangen), `[keep] naam = x y b h` (stuk origineel houden, bv. tekst van 5 px zoals "0 Kmh"), `[wrap]`/`[clamp]`.
5. **In de app:** `make_app.sh` roept het script aan; het pakket staat standaard aan in Upgraded.
6. **Controleren:** per baan een shotpaar origineel/upgraded op dezelfde plek, plus 100%-uitsneden van de HUD, auto's, bomen en hekken en herhalend asfalt (naden?). Meet fps (gecapt en ongecapt) en geheugen.

### Wat je kunt verwachten (Vakantie Racer, M2 Pro)

| | |
|---|---|
| Aantal | 124 textures, ~180 MB PNG, eerste run ~5 min, daarna seconden |
| Geheugen | +263 MB |
| fps | ongewijzigd (60 gecapt, 230-260 ongecapt op 2618x1964 met 4x MSAA) |
| Echt beter | HUD-letters en cijfers, auto's, bomen, balustrades, kinderkopjes |
| Vooral rustiger | asfalt, rots: geen blokjes meer, maar ook geen nieuw detail |
| Origineel laten | kleine tekst (5 px), radar en borden die de game zelf tekent, 2D-laadschermen (gaan buiten de textures om) |

## Deel B: menu in 4K

### Hoe het werkt

1. **Tekenen op de echte pixelmaat van het venster** (HiDPI), met de originele layout van 800x600 als coördinaten. Elk onderdeel wordt apart geschaald (`Painter` in `engine/paint.cpp`). Nooit één canvas van 800x600 oprekken: dan blijft alles wazig, hoe scherp de plaatjes ook zijn.
2. **Losse tekst uit TTF** op de geschaalde maat. Regels mogen niet breder worden dan in het origineel, zodat ze op dezelfde plek afbreken.
3. **Plaatjes 4x** met een script (`tools/upscale_menu.py`, model **realesrgan-x4plus**): alle menu-BMP's, de knopstanden uit de formulieren en de plaatjes die het menu zelf maakt. Naamgeving (`engine/hd.h`): `mainscre.png`, `<Form>.<component>.png`, `defBtn.UpLink.png`; `<naam>.alpha.png` voor plaatjes met sleutelkleur. Eigen HD-kunst in `mods/menu/hd/` gaat voor.
4. **Knoppen met ingebakken achtergrond** (Delphi): alpha op de achtergrond, vanaf de rand gevuld, zodat ze op elke ondergrond passen.
5. **Tekst die in plaatjes gebakken zit** (titels, labels): niet AI-upscalen maar uitgummen en opnieuw zetten in het echte lettertype (kleur, dikte, schaduw en bevel gemeten uit het origineel). Lijst met plaatje, vak en tekst in `tools/menu_text.tsv`. Het lettertype komt van de Mac zelf en gaat nooit in git; zonder lettertype blijft de AI-versie staan.
6. **Lampjes en stipjes** (een paar pixels): opnieuw tekenen in code (`tools/menu_lamps.py`), allemaal dezelfde maat en één lichtrichting (linksboven, zoals de originele kunst). Middelpunt nauwkeurig meten, anders steken ze buiten hun balk uit.
7. **Filmpjes** blijven origineel, alleen geschaald.
8. **F9** in het menu, plus een vaste instelling in Opties (`graphics.ini MenuUpgrades`). Testen offscreen met `<ID>_MENU_SIZE=3024x1964`.

### Per game nakijken

- Vakantie Racer: knoppen en plekken uit Delphi-DFM's, titels in Gill Sans Ultra Bold, losse tekst in Impact/Arial.
- Europe Racer: TGDI, TGA's met eigen alpha (dan is stap 4 minder nodig), knopplekken uit code, Hilde-font. Zie de Europe Racer-sectie in `LEARNINGS.md`.
- A2 Racer IV: zelfde menulib als Europe Racer (TGDI), nakijken.
- Elke game: welke tekst is TTF en welke zit in plaatjes, welk lettertype, wat is de sleutelkleur.

## Model kiezen

| Soort | Model of aanpak |
|---|---|
| Menu-plaatjes (3D-renders, logo, achtergronden) | realesrgan-x4plus |
| 3D-textures en HUD in de race | 4xNomos8kSC |
| Lampjes, stipjes | in code tekenen |
| Tekst in plaatjes | opnieuw zetten in het echte lettertype |
| Kleine tekst in textures | origineel laten |

Getest en afgevallen: realesrgan-x4plus-anime (plastic huid, donkere rand om letters), 4x-UltraSharp (verzint wormpjes in asfalt en gezichten), 4x_NMKD-Siax (houdt het korrelige raster), Text2HD (kleurverschuiving, wazig), digital-art (donkere randen). Test een nieuw model altijd op dezelfde uitsneden naast het origineel en de huidige keuze, en schrijf de uitkomst in `LEARNINGS.md` sectie 6.

## Valkuilen

- Roze randen: de sleutel en de bijna-sleutelkleur niet eerst doorzichtig gemaakt en opgevuld.
- Naden op herhalende textures: geen wrap-padding.
- Verkeerde texture op een model: bladen van 768x512 en 384x256 hebben rechts mipmaps, dus knippen tot het vierkante deel. En nakijken welk blad echt bij een model hoort (de DeLorean "paste" op het Countach-blad, maar dat bleek fout).
- Lange laadtijd of strandbal: PNG's op de hoofdthread decoderen.
- Een kleurtest (bv. alleen rood) in een zichtbaar venster: de gebruiker denkt dat het een bug is. Stil en verborgen testen.
- Een HUD die iets doorzichtig is, kan origineel zijn (Vakantie Racer `overlay.ini` RGBA 200). Eerst met F9 vergelijken voordat je iets "repareert".

## Europe Racer: wat anders is (9 nov 2026)

Bekend uit de menu-port. Nog niets ge-upscaled: dit zijn de aandachtspunten.

- **Menu draait al op de gedeelde Painter.** F9 en `ERACER_MENU_UPGRADES`/`ERACER_MENU_SIZE` werken. Er zijn alleen nog geen HD-plaatjes en er is geen upscale-script.
- **Plaatjes komen niet uit DFM's maar uit `Menu/Graphics.IMG`** (322 TGA's, elk item u32 lengte + zlib; de gedeelde `RcsArchive` leest ze). `upscale_menu.py` moet dus het archief lezen in plaats van Menu.exe-formulieren.
- **TGA's hebben echte alpha:** geen sleutelkleur. Stap 4 (achtergrond van knoppen naar alpha) valt weg; de alpha gaat apart door het model, zoals bij de race-textures. **Uitzondering:** sommige 32-bit TGA's hebben overal alpha 0 (`dfimg*`, `opp00`, `res1`) en worden ondoorzichtig getekend. Het script moet die net zo behandelen, anders worden ze onzichtbaar.
- **Bijna alle tekst is TTF:** captions zijn LT_-sleutels uit `Language.ini`, in Hilde (`FONTDATA/HIS_____.TTF` van de cd) en Arial. Stap 5 (tekst opnieuw zetten) is dus waarschijnlijk alleen nodig voor logo's en tekst in foto's. Nog niet per plaatje nagekeken.
- **HD-naam van knopstanden:** de engine noemt een knopstand `<definitie>.<UpLink|…>`, ook al komt het plaatje uit het archief. Meerdere definities delen soms hetzelfde TGA-bestand. Dan zijn er meerdere HD-bestanden voor één plaatje nodig, of de engine moet archiefplaatjes op bestandsnaam noemen. Dat laatste is beter, maar nog niet gebouwd.
- **Zelf getekende tabellen** (high scores, eigenschapsbalken) gebruiken nu `Font::DrawLine` op de PaintOver-canvas en worden dus geschaald, niet scherp. Overzetten naar `davi::DrawText`/`davi::Blit` maakt ze scherp.
- **Race (deel A):** `.d3d` en tga zitten met zlib in `res/rcs.IMG` (eerst 4 bytes lengte). Pak ze uit vóór het hashen. Verder is het dezelfde RED-omzetting, maar de hash is nog niet gecontroleerd, want de race van Europe Racer is nog niet geport.
