# PsyDuck

Sidechain / volume shaper VST3 plugin za psytrance produkciju u Bitwig Studiju (Windows i macOS).

## Stanje

Plugin ima četiri režima (meni **Mode** gore desno):

- **Sync**: kriva se ponavlja u ritmu tempa (Rate), kao LFO Tool.
- **Audio trigger**: kick doveden na sidechain ulaz pokreće krivu jednom po udarcu. **Threshold** određuje koliko glasan kick mora biti (narandžasta linija na prikazu), a **Retrigger** je najkraći razmak između dva okidanja.
- **MIDI trigger**: MIDI nota pokreće krivu. U polju **Note** biraš da li važi svaka nota ili samo jedna.
- **Follower**: bas prati glasnoću samog kicka, kao klasičan sidechain kompresor. Kick na nivou **Threshold** potpuno utišava bas, a **Release** određuje koliko brzo se bas vraća. Kriva se u ovom režimu ne koristi.

Dugmad koja u izabranom režimu ne rade ništa su zasivljena.

U Audio i MIDI režimu **Rate** određuje koliko traje jedan prolaz kroz krivu. **Lookahead** kasni zvuk za nekoliko milisekundi (host to sam kompenzuje) da bi se bas spustio pre nego što kick stigne, bez klika.

Ispod krive se vide **kick** (sidechain, narandžasto) i **bas** (izlaz, plavo) raspoređeni po ciklusu, pa se vidi gde se preklapaju.

**Target** (pored Mode) bira šta kriva pomera:

- **Volume**: jačinu (klasičan ducking).
- **Filter**: low-pass filter. Dno krive je frekvencija **Filter low point**, vrh je potpuno otvoren filter, a **Resonance** naglašava prelomnu frekvenciju.
- **Pan**: položaj u stereo slici. Sredina krive je centar, dno levo, vrh desno, a Amount određuje širinu.

**Multiband** (samo uz Volume) deli zvuk na tri pojasa: Low, Mid i High.

- **Low | Mid** i **Mid | High** su frekvencije na kojima se pojasevi dele.
- Dugmad **Low**, **Mid**, **High** određuju koliko se koji pojas utišava (Amount i dalje važi za sve zajedno).
- Svaki pojas ima svoju krivu. Jezičci Low / Mid / High iznad krive biraju koju uređuješ, a ostale dve se vide bledo. Low pojas koristi glavnu krivu.

Faza kicka i basa:

- **Kick / Bass phase** pokazuje da li se niske frekvencije kicka i basa sabiraju ("in phase") ili poništavaju ("cancelling"). Meri se bas pre utišavanja, dok kick i bas sviraju zajedno.
- **Invert** okreće polaritet basa, a **Bass delay** ga pomera unazad za do 10 ms.
- **Align** sam izmeri poslednje dve sekunde zvuka i podesi Bass delay i Invert tako da low-end bude najpuniji.

Alati iznad krive:

- **Undo / Redo**, **Snap** (lepljenje na mrežu, Shift ga privremeno obrće) i **Grid** (4 do 64 koraka).
- **Suggest**: iz kicka koji se vidi ispod krive sam napravi početnu krivu.
- **Copy / Paste**: kopira krivu u drugi pojas ili u drugu instancu plugina.
- **Wave**: kick i bas kao talasni oblik umesto jačine. **Freeze** zamrzava sliku. Točkić miša zumira, Shift + točkić pomera, a **Fit** vraća ceo ciklus.
- **A / B**: dva kompletna podešavanja za poređenje. Dugme "A > B" kopira trenutno u drugo.
- **Presets**: fabrički preseti, tvoji preseti i "Save current curve...". Tvoji preseti su fajlovi `.psycurve` u folderu koji otvara "Open preset folder".

Rate ima i **2 bars** i **4 bars** za duže obrasce (računa se takt 4/4). Veličina prozora se pamti sa projektom.

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

- `src/dsp/` je DSP jezgro bez JUCE-a (kriva, sinhronizacija s tempom, gain), pokriveno testovima u `tests/`. `tests/PluginTests.cpp` dodatno pokreće ceo plugin bez DAW-a (režimi, lookahead, multiband, čuvanje stanja). Sa `psyduck_plugin_tests --screenshot slika.png` crta interfejs u PNG.
- `src/plugin/` je JUCE omotač: parametri, procesor i interfejs.

## Licenca JUCE-a

JUCE je dvojno licenciran (AGPLv3 ili komercijalna licenca). Pre javnog ili komercijalnog izdanja treba izabrati odgovarajuću licencu.
