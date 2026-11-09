# Davilex-RED: gedeelde lessen voor de native Mac-ports

Eén plek voor wat we leerden bij Vakantie Racer (8-9 nov 2026), zodat Europe Racer en A2 Racer IV het niet opnieuw hoeven uit te zoeken. Alle drie: Davilex, RED-engine, statische recompilatie met SwARc (x86 → C++ → SDL3/Metal). Details per game staan in hun eigen `CLAUDE.md` en `re/findings.md`; voor Vakantie Racer: `~/Projects/Games/Vakantieracer/CLAUDE.md` en `re/findings.md`.

Voeg je eigen lessen hier toe (kopje met gamenaam en datum), dan leert de volgende game ervan.

## 2. Laptop en samenwerking tussen sessies

- **16 GB RAM: één zware job tegelijk op de hele machine** (build, lift, Ghidra `MAXMEM=2G`, Real-ESRGAN-batch). Check vóór elke zware stap `ps` op `ninja|clang -cc1|recomp_lift|analyzeHeadless|realesrgan` en wacht op een stille minuut. Dat werkte beter dan berichten tussen sessies: die worden vaak tegengehouden tot de gebruiker ze goedkeurt (andere permissiemodus) en verlopen dan.
- Agents in dezelfde repo: elk commit alleen zijn eigen paden (`git add <paden>`), nooit andermans onafgemaakte wijzigingen.
- Real-ESRGAN-binary en modellen niet in een scratchpad laten staan (weg na herstart): het script haalt ze zelf op in een gitignored `build/tools/` met sha256-check.

## 3. SwARc: wat al gebouwd is (generiek, branch `legacy-dx5` in `~/Projects/Games/SwARc-dx5`)

- DirectX 5-adapters op de DX7/8-code: DirectDraw 1/2 + Surface 1/2, Direct3D 2 (Device2/Texture2/Viewport2/Material2), DirectInput 5, DirectSound legacy. DDSURFACEDESC (108) is een prefix van DDSURFACEDESC2 (124); DX7-vtables zijn supersets van DX2.
- `kernel32_profile.cpp`: GetPrivateProfileString/Int, WritePrivateProfileString. **Cache verplicht:** de RED-engine leest bij het laden duizenden ini-sleutels; elke keer het bestand parsen kostte 2-9 s laadtijd en een macOS-strandbal. Nu geparste kopie in geheugen (opnieuw lezen alleen als het bestand verandert of de game schrijft): laden 0,7-1,0 s.
- **Vensterberichten pompen** elke 100 ms vanuit de imports (tijdens laden), anders strandbal. Meten: `<ID>_PUMP_GAP=ms`.
- CD-ROM-stations (`GameConfig::cd_drives`: letter, map, volumelabel) voor de CD-check; `MessageBoxExA`, `WaitMessage`, `CompareStringA/W`, `LocalAlloc/ReAlloc`.
- **`winmm!mixerGetNumDevs` en vrienden:** ontbraken; de game riep ze bij Esc/P (geluid op nul) en stopte met FATAL. Elke ontbrekende import = FATAL by design, dus Esc/P in de race testen.
- Pauze-hooks (audio pauzeren, beeld over het spel tekenen), venster doorgeven van een frontend (menu) naar de race (zelfde plek/maat/fullscreen, geen Dock-icoon).
- **Texture packs:** `texture_pack.cpp` herkent een texture aan zijn pixels (na de R5G6B5-omzetting van de game) en tekent `<hash>.png`; HD-PNG's worden op worker-threads gedecodeerd. Schakelt mee met F9.
- **Toetsen:** `GameConfig::key_aliases`. macOS gebruikt Ctrl+pijltje voor Spaces, dus turbo (Ctrl) naar Cmd. Cmd+spatie opent Spotlight.
- `max_fps = 60` (de RED-simulatie hangt aan het frame, zonder cap rijdt hij te snel); resizable venster; MSAA (M2 Pro max 4x, valt terug), alpha-to-coverage, mipmaps + trilinear + 16x AF.

## 4. RED-engine-kennis (geldt waarschijnlijk voor alle drie)

- Archief `rcs.ind` (u16 aantal, records van 24 bytes: naam[20], u32 offset) + `rcs.IMG`. De game opent eerst in rcs.IMG, anders via fopen: nieuwe bestanden kunnen dus gewoon in de overlay.
- `.d3d`-model: i32 -1, u32 punten, u32 vlakken, 49 B per punt, 9 B per vlak. Alle drie de games gebruiken hetzelfde formaat. **Europe Racer pakt elk `.d3d` (en tga) in met zlib**, eerst 4 bytes lengte.
- **Textuurbladen 768x512 of 384x256 hebben rechts mipmaps;** de UV's gebruiken alleen het vierkante deel links. Altijd knippen.
- Kleursleutel in de race: magenta 255,0,127, plus een rand bijna-magenta die ook als doorzichtig moet tellen.
- Menu en race praten via `game.ini` (`[PLAYER]`, `[ETAPPE]`, `[LEAGUE]`, `[RACERS]`, `[INPUT]` met VK-codes).
- Banen: `etappeN.ini` `[objects]` (mesh, texture, naam, type...), botsmuren in `etappeN.cmp` (een hek botst vaak niet zelf, een onzichtbare muurlijn wel).
- HUD: `overlay.ini` (lettertype `[FONT1]` op overlay.tga; portretten `RGBA = 255;255;255;200`, dus bewust iets doorzichtig, alleen het eigen portret dicht). De lege plek die je soms ziet is de inhaal-animatie.
- Stemmen per autonummer: `RAC<n>P<i>.WAV` (inhalen en T), `RAC<n>C<i>.WAV` (botsen), `HORN<n>`; portretten ook per autonummer.
- Origineel Esc = "Wilt u echt stoppen!? (J/N)", P = pauze; beide stoppen de racetimers en zetten de mixer op nul.
- Bonusauto's en hogere autonummers (bv. CarType 24) werken; tabellen zijn niet op het originele aantal begrensd. **Let op:** de DeLorean in A2 Racer IV en Europe Racer gebruikt het Countach-blad L1racer2; zijn UV's overlappen, dus er bestaat geen echte DeLorean-lak.
- Code vervangen gaat per functie met SwARc `replace <adres>` in de game-repo, niet met een binaire patch.

## 5. Delphi-menu (Vakantie Racer; check of Europe Racer hetzelfde heeft)

- Menu.exe is Delphi: TPF0/DFM-formulieren, `TDaviGraphicButton` met standplaatjes (Up/Down/Focused/Hovered/Disabled) als DIB in de DFM. Teksten in `MenuCaps.ini` + `static.ini`. Native nagebouwd als data-gedreven engine met override-map (`mods/menu/`, layout per formulier).
- Doorzichtigheid Delphi-stijl: de pixel linksonder is de sleutelkleur, tenzij TransparentColor gezet is.
- Knopplaatjes hebben hun achtergrondkleur erin gebakken (gele knoppen voor een geel vlak): op een andere achtergrond geeft dat een gele rand; met echte alpha in de HD-versie opgelost.
- Schermtitels en labels ("Moeilijkheid", "Snelheid") zijn in de BMP's gebakken; het lettertype is **Gill Sans Ultra Bold**. Losse tekst komt uit TTF (Impact, Arial).

### Gedeelde menu-engine: wat generiek is en wat per game (Diederick, 9 nov 2026)

Besluit: één menu-engine in `Davilex-RED/menu/engine/` voor alle games, geen kopieën ("Liever geen duplicated code"). De scheiding is niet het onderwerp (caravans, landen), maar **hoe het eruitziet**: de layout is per game totaal anders.

**Vuistregel: de engine weet niet hoe een scherm eruitziet. Hij tekent wat de game hem aanreikt. Alles wat je ziet en alles wat er gebeurt bij een klik komt van de game.**

| Engine (gedeeld): hoe het werkt | Game (eigen repo): hoe het eruitziet en wat het doet |
|---|---|
| Een knop, plaatje, label of tegel tekenen op de plek die de game opgeeft | Waar alles staat, hoe groot, in welke stijl |
| Klikken, hover, toetsenbord, focus | Welke schermen er zijn en in welke volgorde |
| Schalen naar 4K, F9 origineel/upgraded | Plaatjes, lettertypes, kleuren, knopstijlen |
| Formulier- en layoutbestanden inlezen (DFM, ini) | De formulier- en layoutbestanden zelf (VR: uit de Delphi-DFM's; ER: knopplekken uit de eigen code) |
| Ini, filmpjes, geluid, race starten en uitslag lezen | Wat een klik doet, wat er in `game.ini` gaat |

Voorbeeld: de caravan-rij van Vakantie Racer. De engine levert een generieke tegel (klikbaar plaatje met hover en "gekozen"-rand, `TDaviThumb`); dat het vier caravans zijn, waar de rij staat en dat hij terugspringt bij een andere auto, is Vakantie Racer.

## 6. Upscalen: welk model wanneer

Stap-voor-stap werkwijze voor HUD, race-textures en menu in 4K: zie `UPSCALING.md` in deze map.

| Soort | Beste aanpak | Wat niet werkt |
|---|---|---|
| Menuplaatjes (3D-renders, logo, achtergronden) | realesrgan-x4plus | anime-model: huid wordt plastic |
| 3D-textures in de race | 4xNomos8kSC, trouw | 4x-UltraSharp verzint wormpjes en enge gezichten |
| Lampjes en stipjes van een paar pixels | opnieuw tekenen in code, één lichtrichting (linksboven), zelfde maat | elk AI-model: hoekig, ongelijk; per lampje uit de pixels samplen geeft wisselende schaduwen |
| Tekst in plaatjes | opnieuw zetten in het echte lettertype (kleur, dikte, schaduw, bevel gemeten uit het origineel) | 9 modellen getest, ook Text2HD: rafelig, donkere randen of kleurverschuiving |
| Kleine tekst in textures (5 px) | origineel laten | niks maakt hem leesbaar |

- Altijd eerst de sleutelkleur naar echte alpha, dan pas upscalen (anders roze randen); alpha mag ook door het model (zachte bladranden).
- Herhalende textures (asfalt, gras) eerst met wrap-padding, anders naden.
- Menu: niet één 800x600-canvas oprekken maar elk onderdeel op de echte pixelmaat tekenen, anders blijft alles wazig.
- 124 race-textures 4x = ~180 MB, +263 MB geheugen, fps onveranderd.

## 7. Valkuilen die we zelf maakten

- "Lijkt op" is geen bewijs: de DeLorean-lak "leek" de Back to the Future-versie, maar was het Countach-blad. Toewijzing in de data nakijken.
- Een idee niet als zijn melding opschrijven (zie 1).
- Agents die blind gas geven rijden tegen de muur: eerst de wereldpositie uit de data halen, dan de auto ervoor zetten.
- Camping Walhalla (Vakantie Racer, etappe 5) bestaat echt achter een onzichtbare muur, maar loopt over een klif de zee in; gestaakt.

## A2 Racer IV (9 nov 2026)

- **Eigen SwARc-worktree per game.** Zijn besluit: geen gedeelde generieke code meer aanpassen. A2 heeft `~/Projects/Games/SwARc-a2r4` (branch `a2r4`, afgesplitst van `legacy-dx5`); fixes daar raken VR en ER niet. Wil een ander game een fix, dan kopieert die hem naar zijn eigen branch.
- **Jump tables die Ghidra mist:** 62 bleven open en gaven FATAL "indirect branch to unlifted address". `re/scripts/resolve_jumptables.py` leest `jmp [4*reg + T]` met llvm-objdump, haalt de grens uit `cmp reg,N` + `ja` (anders: lezen zolang de waarden in .text liggen). Twee valkuilen: dubbele doelen eruit (anders "duplicate case value" bij compileren) en nullen overslaan (gaten in de tabel).
- **setjmp/longjmp (MSVC `_setjmp3`/`_longjmp`):** in `lifter.manifest` zowel `setjmp <adres _setjmp3>` als `resume <adres _longjmp>`. Alleen setjmp gaf "returned from a no-return call".
- **Videogeheugen-probe:** de RED-engine maakt bij het starten 256x256-textures tot DDERR_OUTOFVIDEOMEMORY komt. SwARc gaf nooit die fout, dus 19.765 textures en een SIGBUS. Fix in DirectDraw: textures met DDSCAPS_VIDEOMEMORY tellen tegen het geheugen van de kaart (totaal min 8 MB) en geven OUTOFVIDEOMEMORY als het vol is. Daarna `available_texture_memory=58720256` in game.ini. VR/ER hadden dit misschien niet nodig omdat hun probe anders werkt; check bij een SIGBUS in `com::Publish`.
- **Testen zonder echt venster gaat niet:** de SDL dummy-driver heeft geen Metal. Gebruik een echt venster met `<ID>_BACKGROUND=1` (plus buiten beeld zetten).
- **Trace altijd filteren:** `<ID>_TRACE=1` ongefilterd gaf binnen een minuut 250 MB. Altijd door `grep` en `tail`.
- **Nog open:** zonder invoer sluit de race na ~18 s af met "Mouse.cpp(772) Mouse is not initialised". De game post na PostQuitMessage nog WM_CLOSE naar zijn al vernietigde venster en pompt verder; op Windows blijft WM_QUIT dan staan. Waarschijnlijk moet PeekMessage na een opgehaalde quit WM_QUIT blijven geven. Herken je dit bij VR/ER, laat het weten.

## Europe Racer (9 nov 2026)

Details: `~/Projects/Games/Europe Racer/CLAUDE.md` en `re/menu/findings.md`.

### Welke exe
- De CD-exe (v1.0.0.24) en de exe uit Patch 3 zijn allebei **SafeDisc**-verpakt (`.text` versleuteld, secties `stxt*`). De officiële NoCD van Davilex (`dl/nocd/spel.exe`, 15-06-2002) is dezelfde code als Patch 3 zonder verpakking: die porten we. Check bij een volgende game eerst of er zo'n officiële NoCD bestaat voor je met SafeDisc gaat stoeien. Herkennen: zelfde sectiegroottes, entropie van `.text` normaal (6,7) en een echte importtabel.
- Geluid gaat via **Miles** (`mss32`, 51 `AIL_*`-functies), niet via DirectSound. Dat is het grootste nieuwe stuk in SwARc.
- In de exe staan `FPSMULT`-strings: de natuurkunde schaalt misschien mee met de framerate. Eerst uitzoeken voor je op 60 fps vastzet.
- Zes talen in `Langdata/<taal>/`; de installer kopieert er één naast de game. De port moet die samenvoeging zelf doen.

### Delphi-menu: anders dan Vakantie Racer
`Menu/GameMenu.dat` is ook Delphi 5, maar een andere bibliotheek (Davilex Menulib). Wat je uit sectie 5 niet kunt overnemen:
- Componenten zijn de **TGDI-familie** (TGDIButton, TGDILabel, TGDIRadioButton, TGDIGraphic, TGDIPanel, TGDISlider, TGDIMultiProgressBar). De DFM's hebben geen plaatjes: alle GraphicNames zijn leeg en de code zet ze. Knopplaatjes per prefix: `0` Up, `1` Down, `2` Hovered+Focused, `3` Disabled, `4` Focused.
- Plaatjes zitten als **TGA met echte alpha** in `Graphics.IMG` (elk item u32 grootte + zlib), geen sleutelkleur. Let op: sommige 32-bit TGA's hebben overal alpha 0 en worden ondoorzichtig getekend.
- Teksten zijn geen gebakken BMP's maar **LT_-sleutels** uit `Langdata/<taal>/menu/Language.ini`. In de code zijn het `TLanguageText`-enumwaarden; de namen staan als RTTI in de exe (409 stuks), dus uitlezen en je hebt de hele tabel. Lettertype **Hilde** (`FONTDATA/HIS_____.TTF` op de CD, alleen regular, vet is synthetisch).
- Layout uit `Forms.ini` + `FormsA.ini`, flow uit `GameMenu.ini [FS_*]`. **Maar de echte knopplekken zet de code bij elk tonen** (wizard: Volgende rechtsonder, Vorige links ervan; Afsluiten rechtsboven). De ini-waarden zijn vaak ontwerp-standaarden: niet blind vertrouwen.
- Schermen zijn opgebouwd uit kleine herbruikbare controllers (radiogroep, tekst per radio, plaatje per radio, carrousel van drie polaroids, tabs). Eén keer nabouwen en overal inzetten scheelt veel.
- Radiogroepen gelden per container, anders zetten tabs elkaars radio's uit.
- Achtergronden hebben zwarte gaten van 160x160 waar een polaroid hoort.
- Besluit van Diederick: **ieder project een eigen kopie van de menu-engine** (`src/menu/engine/`), geen gedeelde engine.

### Ghidra op het Delphi-menu
- Headless heeft `JAVA_HOME` naar Homebrew-openjdk nodig, anders "No version is set for command java" (asdf) en start hij niet.
- **VA = bestandsoffset + 0x400c00** in GameMenu.dat. Met de offset uit `grep -b` zoeken vind je niks.
- Delphi-event-handlers zijn alleen via pointers bereikbaar: Ghidra maakt er geen functies van. Oplossing: functiestarts zoeken (`55 8B EC` op een 4-byte-grens) en die lijst laten decompileren.
- Delphi-stringliterals staan direct na hun functie. Een string zoeken geeft dus meteen de functie ervoor.
- De volgorde van strings koppelt een ini-sectie aan zijn unit: elke `SCORE_LAYOUT-x` staat direct na het `.pas`-pad van zijn control. Snelle eerste kaart vóór Ghidra.
- Niet alles komt uit de ini: in de high scores zijn club (`Random(7)`) en totaaltijd (vaste 23:59:58) bij het laden verzonnen. Lees de laadfunctie, anders zoek je je dood naar een veld dat niet bestaat.

### Valkuilen
- Widget-rechthoeken in de DFM zijn na het inlezen absoluut: de positie van de parent er niet nog eens bij optellen.
- Een film per flow staat nergens in de data (`[MOVIES]` is leeg); nu op bestandsnaam gekoppeld. Gemarkeerd als aanname tot de code het bevestigt.
