# Davilex-RED: gedeelde lessen voor de native Mac-ports

Eén plek voor wat we leerden bij Vakantie Racer (8-9 nov 2026), zodat Europe Racer en A2 Racer IV het niet opnieuw hoeven uit te zoeken. Alle drie: Davilex, RED-engine, statische recompilatie met SwARc (x86 → C++ → SDL3/Metal). Details per game staan in hun eigen `CLAUDE.md` en `re/findings.md`; voor Vakantie Racer: `../Vakantieracer/CLAUDE.md` en `re/findings.md`.

Voeg je eigen lessen hier toe (kopje met gamenaam en datum), dan leert de volgende game ervan.

## 1. Ontwerpregels voor de ports

- Origineel 1-op-1 nabouwen; alleen echt nieuwe mod-UI (bv. caravan kiezen) wordt ontworpen.
- Elke beeld-upgrade schakelbaar: F9 wisselt live origineel/upgraded (menu én race, ook in het pauzemenu), F10 AA, F11 filtering, met een korte melding linksboven.
- Spelwereld: een auto hoort bij zijn karakter (geen aparte karakterkeuze); bonusauto's rijdt de presentator. Speldata in het project (`userdata/`), niet in Application Support. 60 fps vast. Geen Wine, geen emulator.
- Stil testen: `SDL_AUDIO_DRIVER=dummy`, venster buiten beeld met `<ID>_WINDOW=-9000,-9000,W,H` en de race direct starten. `<ID>_BACKGROUND=1` alleen opent wél een zichtbaar venster (10 nov 2026 op zijn werk gezien). Eerst zelf testen met screenshots, dan pas melden; zeg wat niet getest is.
- Persoonlijke werkafspraken (takenlijst, notities, toon) staan lokaal in `CLAUDE.local.md` (niet in git).

## 2. Laptop en samenwerking tussen sessies

- **16 GB RAM: één zware job tegelijk op de hele machine** (build, lift, Ghidra `MAXMEM=2G`, Real-ESRGAN-batch). Check vóór elke zware stap `ps` op `ninja|clang -cc1|recomp_lift|analyzeHeadless|realesrgan` en wacht op een stille minuut. Dat werkte beter dan berichten tussen sessies: die worden vaak tegengehouden tot de gebruiker ze goedkeurt (andere permissiemodus) en verlopen dan.
- Agents in dezelfde repo: elk commit alleen zijn eigen paden (`git add <paden>`), nooit andermans onafgemaakte wijzigingen.
- Real-ESRGAN-binary en modellen niet in een scratchpad laten staan (weg na herstart): het script haalt ze zelf op in een gitignored `build/tools/` met sha256-check.

## 3. SwARc: wat al gebouwd is (generiek, branch `legacy-dx5`, lokaal als worktree `../SwARc-dx5`)

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
- Een idee niet als melding van de gebruiker opschrijven.
- Agents die blind gas geven rijden tegen de muur: eerst de wereldpositie uit de data halen, dan de auto ervoor zetten.
- Camping Walhalla (Vakantie Racer, etappe 5) bestaat echt achter een onzichtbare muur, maar loopt over een klif de zee in; gestaakt.

## A2 Racer IV (9 nov 2026)

- **Eigen SwARc-worktree per game.** Zijn besluit: geen gedeelde generieke code meer aanpassen. A2 heeft `../SwARc-a2r4` (branch `a2r4`, afgesplitst van `legacy-dx5`); fixes daar raken VR en ER niet. Wil een ander game een fix, dan kopieert die hem naar zijn eigen branch.
- **Jump tables die Ghidra mist:** 62 bleven open en gaven FATAL "indirect branch to unlifted address". `re/scripts/resolve_jumptables.py` leest `jmp [4*reg + T]` met llvm-objdump, haalt de grens uit `cmp reg,N` + `ja` (anders: lezen zolang de waarden in .text liggen). Twee valkuilen: dubbele doelen eruit (anders "duplicate case value" bij compileren) en nullen overslaan (gaten in de tabel).
- **setjmp/longjmp (MSVC `_setjmp3`/`_longjmp`):** in `lifter.manifest` zowel `setjmp <adres _setjmp3>` als `resume <adres _longjmp>`. Alleen setjmp gaf "returned from a no-return call".
- **Videogeheugen-probe:** de RED-engine maakt bij het starten 256x256-textures tot DDERR_OUTOFVIDEOMEMORY komt. SwARc gaf nooit die fout, dus 19.765 textures en een SIGBUS. Fix in DirectDraw: textures met DDSCAPS_VIDEOMEMORY tellen tegen het geheugen van de kaart (totaal min 8 MB) en geven OUTOFVIDEOMEMORY als het vol is. Daarna `available_texture_memory=58720256` in game.ini. VR/ER hadden dit misschien niet nodig omdat hun probe anders werkt; check bij een SIGBUS in `com::Publish`.
- **Testen zonder echt venster gaat niet:** de SDL dummy-driver heeft geen Metal. Gebruik een echt venster met `<ID>_BACKGROUND=1` (plus buiten beeld zetten).
- **Trace altijd filteren:** `<ID>_TRACE=1` ongefilterd gaf binnen een minuut 250 MB. Altijd door `grep` en `tail`.
- **Nog open:** zonder invoer sluit de race na ~18 s af met "Mouse.cpp(772) Mouse is not initialised". De game post na PostQuitMessage nog WM_CLOSE naar zijn al vernietigde venster en pompt verder; op Windows blijft WM_QUIT dan staan. Waarschijnlijk moet PeekMessage na een opgehaalde quit WM_QUIT blijven geven. Herken je dit bij VR/ER, laat het weten.

## Europe Racer (9 nov 2026)

Details: `../Europe Racer/CLAUDE.md` en `re/menu/findings.md`.

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
- Vervangen (9 nov 2026): het eerdere besluit "ieder project een eigen kopie van de menu-engine" geldt niet meer. Alle games gebruiken de gedeelde engine in `menu/engine/` (zie sectie 5, "Gedeelde menu-engine").

### Ghidra op het Delphi-menu
- Headless heeft `JAVA_HOME` naar Homebrew-openjdk nodig, anders "No version is set for command java" (asdf) en start hij niet.
- **VA = bestandsoffset + 0x400c00** in GameMenu.dat. Met de offset uit `grep -b` zoeken vind je niks.
- Delphi-event-handlers zijn alleen via pointers bereikbaar: Ghidra maakt er geen functies van. Oplossing: functiestarts zoeken (`55 8B EC` op een 4-byte-grens) en die lijst laten decompileren.
- **Let op de DATA-sectie:** daar is bestandsoffset = VA - 0x400e00, niet - 0x400c00 (andere sectie-uitlijning). Lees tabellen (zoals punten per plek) via de sectietabel van de PE, niet met de CODE-formule.
- Functies die alleen via pointers of een `call` bereikbaar zijn: zoek aanroepers met een bytescan op `E8 rel32` naar het doeladres en loop terug naar `55 8B EC` op een 4-byte-grens. Dat is sneller dan Ghidra en vond o.a. de flow na de race.
- Een veld dat nergens met `[reg+disp]` wordt opgehoogd, kan via een samengestelde offset geschreven worden (record-basis + veld, bv. 0x2c + 0x1e4 = 0x210): zoek ook op die som.
- Delphi-stringliterals staan direct na hun functie. Een string zoeken geeft dus meteen de functie ervoor.
- De volgorde van strings koppelt een ini-sectie aan zijn unit: elke `SCORE_LAYOUT-x` staat direct na het `.pas`-pad van zijn control. Snelle eerste kaart vóór Ghidra.
- Niet alles komt uit de ini: in de high scores zijn club (`Random(7)`) en totaaltijd (vaste 23:59:58) bij het laden verzonnen. Lees de laadfunctie, anders zoek je je dood naar een veld dat niet bestaat.

### Valkuilen
- Widget-rechthoeken in de DFM zijn na het inlezen absoluut: de positie van de parent er niet nog eens bij optellen.
- Een film per flow staat nergens in de data (`[MOVIES]` is leeg); nu op bestandsnaam gekoppeld. Gemarkeerd als aanname tot de code het bevestigt.
- **Upscalen bij A2 (9 nov 2026, middag):**
	- Race: A2 laadt een blad van 768x512 heel én in stukken (het vierkant links van 512, soms een kwart rechts). Die stukken hebben een eigen hash, dus het texture-script moet ze apart maken. Gevonden met `<ID>_TEX_DUMP` en dan per stuk hashen. Check dit ook bij Europe Racer.
	- P (pauze) gaf FATAL op `winmm!mixerGetNumDevs`, net als bij VR. A2 lost het op in SwARc (mixer-functies met 0 apparaten); de game slaat het dempen dan over en het originele pauzescherm blijft.
	- Menu: knopstanden zoekt de engine op de definitienaam (`gdfBtnOkDlg.UpLink`). A2 zet per stand een symlink naar de upscale van het archiefbestand (`okay_up.png`), uit de prefixtabel in `skin.cpp`.
	- Twee archiefbestanden met dezelfde naam (`radio_up.bmp` en `radio_up.tga`) krijgen dezelfde HD-naam. Laat de TGA winnen, anders een ondoorzichtig vierkant om de radioknop.
	- Alpha die door het model gaat houdt een waas van bijna-nul over: een vaag vierkant om kleine knoppen. Alpha onder 16 op 0 zetten.
	- Return volgt de TabOrder uit de DFM: op schermen waar Help TabOrder 0 heeft, opent Return de help. Dat is het originele gedrag, geen bug.

## Alle drie: venster dicht = spel dicht (9 nov 2026, regel van Diederick)
- Menu en race zijn twee processen, maar voor de speler is het één spel. Sluit de speler het racevenster (rode knop, Cmd+Q), dan moet het hele spel stoppen, ook het menu. Alleen een normaal einde (finish, Esc in de race, opnieuw starten) gaat terug naar het menu.
- Gebouwd 9 nov 2026 door A2 Racer IV, één keer voor alle drie: SwARc d6cc03d (branch a2r4; op legacy-dx5 4618309) laat `user32_window.cpp` SDL QUIT/CLOSE_REQUESTED onthouden en `host_link.cpp` "closed" sturen vóór "exit"; Davilex-RED 1f6f014 laat `menu/engine/app.cpp` RunChild dan het menu sluiten. Test: race-TEST_INPUT `8 close` vanuit het menu, het menu moet meestoppen. Vakantie Racer getest: menu stopt mee.

## Alle drie: echte macOS-fullscreen, race tekent in het menuvenster (11 nov 2026)
- Vervangt "groene knop = volledig scherm zonder Space" (10 nov). Die werkte slecht: geen animatie, en met de muis naar boven kwam je er niet uit (alleen Cmd+Q).
- Echte fullscreen (eigen Space) kan alleen als macOS één app met één venster ziet: zodra een tweede proces actief wordt, schuift macOS uit de Space (getest met een los programma).
- Daarom tekent de race in het venster van het menu: SwARc `host_link.h` (`<ID>_EMBED=1`). Het racevenster staat randloos buiten beeld en wordt nooit actief. Zijn Metal-laag gaat als `CAContext` naar het menu (regel `layer <id>`), dat hem met een `CALayerHost` over zijn eigen laag toont (`menu/engine/app.cpp` `AttachRaceLayer`). SDL's Metal-view moet uit het venster (`removeFromSuperview`), anders hangt AppKit de laag terug.
- Invoer: het menu stuurt toetsen, muis, focus, `quit` en `size` als regels over de link (`ForwardToRace`), de race maakt er SDL-events van (`PumpEmbedded`). De race stuurt `cursor 0|1` en `relmouse 0|1` terug.
- Het race-proces mag nooit actief worden, anders schuift macOS bij het laden van de baan naar het bureaublad (11 nov 2026 gezien). Daarom: activation policy Prohibited, `ShowWindow` herstelt of minimaliseert het verborgen venster niet, `SetForegroundWindow`/`BringWindowToTop` raisen niet, en het venster staat op alle Spaces (stationary, buiten de cyclus). `app.log` meldt het als het menu toch zijn Space of de focus kwijtraakt ("NOT on the active Space").
- Tijdens de race staan de sneltoetsen van de menubalk uit (Cmd is turbo: Cmd+W/H/M), Cmd+Q en Ctrl+Cmd+F blijven.
- CAContext/CALayerHost zijn privé-API van QuartzCore (Safari, Chrome en Firefox gebruiken ze). Ontbreken ze, dan stuurt de race geen `layer` en valt alles terug op de oude overdracht, met de race in een eigen Space.
- Gemeten (Vakantie Racer, M2 Pro): 60,00 fps, frames 16,64-16,70 ms (direct gestart 4-20 ms). Een hapering van ~0,5-1 s rond 15 s racetijd zit er ook zonder embedding (komt uit het spel).
- Stil testen: `<ID>_MENU_POS=-1046,200` (menuvenster randloos, 1 punt in beeld; helemaal buiten beeld tekent macOS het niet), `<ID>_TEST_FORWARD=bestand` (regels `<ms> key <scancode> d|u`, `<ms> focus 1`; nodig, want zonder focus is de race inactief en stopt hij met "Mouse is not initialised") en `screencapture -l <venster-id>`.
- Een `MessageBox` van de race gaat in deze stand naar het menu (`msgbox ...`, antwoord `msgbox <id>`): een eigen dialoog zou het race-proces actief maken.
- Alle drie de SwARc-forks hebben het (`legacy-dx5`, `er`, `a2r4`, 11 nov 2026; dezelfde patch). Race-deel los testen kan zonder menu met een nep-front-end (socketpair, `<ID>_WINDOW`, `<ID>_LINK_FD`, `<ID>_EMBED=1`, op `layer` antwoorden met `size`/`focus 1`, op `exit` met `x`).
- Open: "Mouse is not initialised" (`Mouse.cpp(772)`, alle drie). De game toont hem bij het afsluiten (na `DestroyWindow`, `PostQuitMessage` en het loslaten van DirectInput, `MessageBoxExA` vanaf 0x491694 in VR), als de race werd gesloten (rode knop, Cmd+Q) of zonder focus stond. Waar de fout wordt vastgelegd is nog niet uitgezocht.
