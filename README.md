# PsyDuck

Sidechain / volume shaper VST3 plugin za psytrance produkciju u Bitwig Studiju (Windows i macOS).

## Stanje

Plugin ima tri režima (meni **Mode** gore desno):

- **Sync**: kriva se ponavlja u ritmu tempa (Rate), kao LFO Tool.
- **Audio trigger**: kick doveden na sidechain ulaz pokreće krivu jednom po udarcu. **Threshold** određuje koliko glasan kick mora biti (narandžasta linija na prikazu), a **Retrigger** je najkraći razmak između dva okidanja.
- **MIDI trigger**: svaka MIDI nota pokreće krivu.

U Audio i MIDI režimu **Rate** određuje koliko traje jedan prolaz kroz krivu. **Lookahead** kasni zvuk za nekoliko milisekundi (host to sam kompenzuje) da bi se bas spustio pre nego što kick stigne, bez klika.

Ispod krive se vide **kick** (sidechain, narandžasto) i **bas** (izlaz, plavo) raspoređeni po ciklusu, pa se vidi gde se preklapaju.

U Bitwigu: sidechain izvor se bira u panelu uređaja (VST3 sidechain ulaz), a za MIDI režim note dovedi na kanal sa pluginom, na primer preko Note Receiver uređaja.

Rad sa krivom:

- vuci tačku da je pomeriš (prva i poslednja idu samo gore-dole)
- dupli klik na prazno mesto dodaje tačku, dupli klik na tačku je briše
- vuci mali prsten na sredini segmenta gore-dole da ga saviješ, dupli klik ga ispravlja

## Gotov build (bez instaliranja alata)

Svaki push na `main` pokreće build na GitHubu. Otvori karticu **Actions**, izaberi poslednji uspešan "Build" i na dnu preuzmi `PsyDuck-Windows-VST3` ili `PsyDuck-macOS-VST3`.

Raspakuj i kopiraj `PsyDuck.vst3` u:

- Windows: `C:\Program Files\Common Files\VST3\`
- macOS: `~/Library/Audio/Plug-Ins/VST3/`

Na macOS-u plugin nije potpisan, pa posle kopiranja pokreni u Terminalu:

```
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/PsyDuck.vst3
```

Zatim u Bitwigu ponovo skeniraj plugine.

## Lokalni build

Potrebni su CMake 3.22+ i C++ kompajler (Visual Studio 2022 na Windowsu, Xcode na macOS-u). JUCE se preuzima automatski.

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
```

Plugin se nalazi u `build/PsyDuck_artefacts/Release/VST3/`. Sa `-DPSYDUCK_COPY_AFTER_BUILD=ON` kopira se sam u sistemski VST3 folder.

## Struktura

- `src/dsp/` je DSP jezgro bez JUCE-a (kriva, sinhronizacija s tempom, gain), pokriveno testovima u `tests/`. `tests/PluginTests.cpp` dodatno pokreće ceo plugin bez DAW-a (režimi, lookahead, čuvanje stanja).
- `src/plugin/` je JUCE omotač: parametri, procesor i interfejs.

## Licenca JUCE-a

JUCE je dvojno licenciran (AGPLv3 ili komercijalna licenca). Pre javnog ili komercijalnog izdanja treba izabrati odgovarajuću licencu.
