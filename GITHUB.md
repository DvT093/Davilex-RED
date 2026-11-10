# Een port op GitHub zetten

Het format van [VakantieRacer-Revived](https://github.com/DvT093/VakantieRacer-Revived), voor Europe Racer en A2 Racer IV. Doel: iemand downloadt de ISO, doet `git clone --recursive` en `./setup.sh <iso>`, en speelt precies wat de maintainer speelt: HD, menu in 4K, alle mods.

## Repo's

| Repo | Wat erin zit |
|---|---|
| `<Game>-Revived` (bv. `EuropeRacer-Revived`, `A2RacerIV-Revived`) | De port: `src/`, `mods/`, `tools/`, `symbols/`, `setup.sh`, README, MIT |
| `external/SwARc` (submodule) | De SwARc-fork op DvT093, op de branch van deze game |
| `external/Davilex-RED` (submodule) | Gedeeld menu en launcher |
| Release `hd-v1` | Zip met het HD-pack (textures, menu-4K, mod-plaatjes, icoon), < 2 GB per bestand |

- Repo-naam: `<Game>-Revived`. Titel in de beschrijving: "<Game> Revived: port to macOS (Apple Silicon)".
- Submodules staan ín de repo onder `external/`, niet als losse map ernaast. CMake kijkt eerst in `external/` en valt terug op `../SwARc-<branch>` en `../Davilex-RED`.
- Openbaar, MIT voor onze code.

## Wat nooit in git of in een release staat

- Spelbestanden (`iso/`), de ISO zelf (`*.iso` in .gitignore).
- `generated/` (de omgezette spelcode, afgeleid van de exe). Die maakt `setup.sh` bij de speler.
- Persoonlijke werkafspraken: die staan in `CLAUDE.local.md` (gitignored). Ook niet in oude commits: check vóór de eerste push de hele geschiedenis (`git log -p | grep -iE "vault|Obsidian|Werken met"`) en herschrijf als het moet.
- Commit-e-mail: het GitHub-noreply-adres `6576986+DvT093@users.noreply.github.com` (staat lokaal per repo in `git config user.email`). Nooit een echt adres (werk of Gmail): op een openbare repo leest iedereen het, ook spambots. Check `git log --all --format='%ae %ce' | sort -u` vóór de push.

Wel in git: `symbols/` (alleen adressen en namen, zodat niemand Ghidra nodig heeft), screenshots in `docs/screenshots/`.

## setup.sh

- Neemt een ISO (`hdiutil attach -readonly -nobrowse`, kopie naar `iso/`) of een uitgepakte map.
- Checkt de versie met de sha256 van de exe; een verkeerde ISO geeft een duidelijke fout.
- Checkt Homebrew-pakketten en toont bij ontbreken de exacte `brew install ...`.
- Zet de spelcode om, bouwt, downloadt het HD-pack van de release (met voortgangsbalk en sha256-check, terugval: zelf maken) en maakt de `.app`.
- Vriendelijk: per stap een emoji, `[n/6]`, geschatte duur en een vinkje; kleur, `--plain` zonder. Opnieuw draaien slaat over wat klaar is.
- Gooit de ISO niet weg; zegt aan het eind dat hij weg mag of als back-up kan blijven.
- Emoji: 🏖️ en 🚘 in de banner. Geen busje (🚐) en geen 💨: kinderachtig en er zijn geen bussen.

## README

Volgorde van boven naar onder:
1. Titel `# 🏖️ <Game> Revived 🚘`, één vetgedrukte zin wat het is.
2. Badges (shields.io): macOS Apple Silicon, 60 fps, textures 4x HD, menu 4K, license MIT.
3. Een `> [!IMPORTANT]`-blok: "Je hebt de ISO van <Game> nodig." De game zit niet in de repo; zeg gewoon waar je hem downloadt (bij Vakantie Racer: [Nationaal Archief Educatieve Games](https://nationaalarchiefeducatievegames.nl/archief/vakantie-racer/) met de directe Archive.org-link). Niet "je eigen CD": niemand heeft de CD nog. Check eerst dat die download dezelfde ISO is als de onze (hash).
4. Eerste screenshot: **het hoofdmenu in 4K**. Zonder echte spelersnaam in beeld (bv. "Welkom Vakantieganger!") en zonder zwarte balken (bijsnijden tot 4:3).
5. `*English below.*`
6. "✨ Wat is er nieuw": tabel met emoji per functie (native, HD met F9, menu 4K, mods, pauzemenu, 60 fps).
7. "📸 Screenshots": race in HD, origineel naast HD (uitsnede), F9-paar, de mod-schermen, nog een menuscherm. JPG, 1600 px breed, kwaliteit 85.
   - Race-shots met een originele auto en zijn eigen aanhanger (bij Vakantie Racer: Sjonnie en Anita met de tokkie-caravan). Geen bonusauto's zonder echte lak (Z3, DeLorean).
   - Alleen rijdend: nooit vast tegen een muur, boom of andere auto. Maak veel frames (`<ID>_SHOT_FROM`, `<ID>_SHOT_EVERY`) en kies.
   - Origineel naast HD uit één run met een F9-druk (de twee frames rond de wissel), niet uit twee runs: die lopen net anders.
   - Op hoge rendermaat (`<ID>_RESOLUTION=2618x1964`), anders zie je het verschil nauwelijks.
   - Een "achteruit kijken"-shot (camera voor de auto) laat de auto zelf zien als de aanhanger hem van achteren verbergt.
   - Testen buiten beeld: start de race direct met `<ID>_WINDOW=-9000,-9000,W,H`; `<ID>_BACKGROUND=1` alleen opent wel een zichtbaar venster.
8. "⚠️ Bekende problemen" (bv. auto's die de makers nooit goed van textures hebben voorzien). Over de repo, wat je nodig hebt, installeren (twee commando's), toetsen, opties, credits (SwARc door JT, MIT) en licentie.
9. Daarna hetzelfde in het Engels, met dezelfde kop en hetzelfde ISO-blok.

## Volgorde van publiceren

1. Geschiedenis schoon (e-mail, persoonlijke regels).
2. SwARc-fork: `gh repo fork JTM-rootstorm/SwARc --clone=false`, push de branch van de game.
3. Davilex-RED is al online (DvT093/Davilex-RED); alleen pushen als er iets nieuws is.
4. `gh repo create DvT093/<Game>-Revived --public`, push.
5. `gh release create hd-v1 <zip>`.
6. Proef: verse `git clone --recursive` vanaf GitHub, submodules aanwezig, zip-download geeft de juiste sha256.
