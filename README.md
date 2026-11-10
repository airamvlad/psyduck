# PsyDuck

Sidechain / volume shaper VST3 plugin za psytrance produkciju u Bitwig Studiju (Windows i macOS).

## Stanje

Korak 2 (volume shaper): kriva se menja mišem, ima fabričke presete i čuva se sa projektom. Parametri su **Rate**, **Amount**, **Mix** i **Phase**. MIDI i audio okidanje dolaze u narednom koraku.

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

- `src/dsp/` je DSP jezgro bez JUCE-a (kriva, sinhronizacija s tempom, gain), pokriveno testovima u `tests/`.
- `src/plugin/` je JUCE omotač: parametri, procesor i interfejs.

## Licenca JUCE-a

JUCE je dvojno licenciran (AGPLv3 ili komercijalna licenca). Pre javnog ili komercijalnog izdanja treba izabrati odgovarajuću licencu.
