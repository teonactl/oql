# Test Fase 1 e Fase 2

Harness standalone per le correzioni del piano di revisione (vedi il doc del
piano in cima al progetto). Nessuna GUI, nessun device audio reale toccato —
`AudioEngine::instance().init()` non viene mai chiamato, quindi è sicuro
eseguirlo anche con un vero show in corso altrove sulla macchina. Il web
server della Fase 2 viene testato su una porta libera scelta dal sistema
(`start(0)`), mai su una porta fissa.

- `test_harness.h` — mini-harness condiviso (PASS/FAIL, cattura qWarning, pumpEvents)
- `fase1_tests.cpp` — contiene anche `main()`
- `fase2_tests.cpp`, `fase3_tests.cpp`, `fase4_tests.cpp` — richiamati da `fase1_tests.cpp`

Due test della Fase 3 usano plugin VST2/LV2 **reali** già installati su questa
macchina di sviluppo (pacchetti Arch `zam-plugins-vst` e `infamousplugins`),
per una verifica end-to-end vera oltre a quella sintetica — si saltano con
una nota (non falliscono) se quel plugin specifico non è installato altrove.

## Build ed esecuzione

```bash
cmake -B build -DOQL_BUILD_TESTS=ON
cmake --build build --target oql_tests -j$(nproc)
./build/oql_tests
```

Build consigliata per questi test (coerente con la sezione AddressSanitizer di
`wiki/Building.md`, utile per i casi che toccano thread/puntatori):

```bash
cmake -B build_asan \
      -DCMAKE_BUILD_TYPE=Debug \
      -DOQL_BUILD_TESTS=ON \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g"
cmake --build build_asan --target oql_tests -j$(nproc)
./build_asan/oql_tests
```

Uscita 0 se tutti i controlli passano, 1 altrimenti.

## Cosa copre

- **C1** — i thread di `AudioCue::applyPluginChain()`/`restorePluginSnapshot()`
  vengono joinati dal distruttore invece di restare `detach()`-ati (test
  `C1+C3`: elimina la cue target subito dopo `EffectCue::go()`).
- **C2** — `AudioEngine::addRenderer()`/`removeRenderer()` non crashano se
  l'engine non è mai stato inizializzato.
- **C3** — `ControlCue::m_target`/`EffectCue::m_activeTarget` (QPointer) non
  dereferenziano una cue target eliminata a metà fade/effetto.
- **C4** — `CueList::go()` non fa stack overflow su una catena ricorsiva, e il
  contatore di profondità interrompe un riferimento ciclico (gruppo
  auto-parentato) con un avviso invece di girare per sempre; una catena
  normale di auto-continue continua a funzionare.
- **C6** — `Workspace::save()` è atomico: un salvataggio che fallisce (es.
  directory non scrivibile) non corrompe il file target preesistente;
  `load()` rifiuta un JSON valido ma non-oggetto invece di svuotare il
  workspace in silenzio.
- **Minori** — `AudioCue::go()/setUserRate()/setPlaybackRate()/setPitchPreserve()`
  tornano a `Idle` (non restano "Playing" fantasma) se il reinit del decoder
  fallisce; `CueList::removeCue()` emette `playheadChanged` quando sposta il
  playhead.

Fase 2:

- **C8** — `WebServer::start(0)` (porta libera), poi richieste HTTP grezze via
  `QTcpSocket` guidate da `QCoreApplication::processEvents()` (non dalle
  `waitForX()` bloccanti di `QTcpSocket`, che non fanno girare l'event loop
  del server sullo stesso processo — un `QTcpSocket` separato che *aspetta*
  e un `WebServer` che deve *rispondere* sono due QObject sullo stesso
  thread: serve l'event loop a far girare entrambi): nessun token → 403,
  token sbagliato → 403, token corretto → 200, OPTIONS (preflight CORS) → 200
  senza token; una richiesta che non manda mai `\r\n\r\n` oltre il limite di
  64 KB fa chiudere la connessione dal server.

Fase 3 (correttezza audio — `fase3_tests.cpp`):

- **A1** — bypass: sintetico su `GainPlugin` (deterministico) + end-to-end su
  un VST2 reale (ZaMaximX2) attraverso `PluginChain::process()`.
- **A2+A9** — un LV2 reale (infamousplugins/Stuck) preparato a due sample
  rate diversi in sequenza: resta valido e produce output non nullo dopo
  ciascun cambio (la correttezza *esatta* della temporizzazione non è
  verificata qui — richiederebbe conoscere il DSP interno del plugin;
  verifica che la ri-istanziazione non rompa nulla).
- **A6** — due istanze identiche di `EqPlugin`, una con un `setParam()`
  ridondante (stesso valore) subito prima dell'ultimo campione: gli output
  devono coincidere esattamente (la memoria del filtro non viene azzerata).
- **A8** — `AudioCue::fadeEnvelope()` (ora pubblica e statica apposta per
  questo) testata direttamente con valori di trim espliciti.
- **A3** non ha un test dedicato: ogni test sopra che carica un plugin reale
  passa comunque più volte dalla stessa guardia sigsetjmp/installXxxOnce — un
  deadlock o una race lì si vedrebbe come un hang/crash in questi test.

Scoperta facendo questi test (non un regressione introdotta qui, pre-esistente):
i plugin LV2 basati su DPF che richiedono `urid:map` (es. Dragonfly Reverb)
non si istanziano in OQL — `Lv2Plugin` non passa nessuna host feature a
`lilv_plugin_instantiate()`. Probabilmente limita l'uso di molti plugin LV2
moderni; non è nel piano attuale, da valutare separatamente.

Fase 4 (thread audio real-time safe — `fase4_tests.cpp`):

- **A4+A5** — un vero thread in loop stretto su `VstPlugin::process()` (lo
  stesso VST2 reale di prima) mentre il main thread fa 2000 `toJson()`/
  `param()` consecutivi: nessun deadlock (il test stesso non finirebbe se ci
  fosse), nessun crash sotto ASan. Non testato qui: il comportamento esatto
  del vettore `toFinish` riusato in `AudioEngine` (richiederebbe accesso a
  uno struct privato solo per osservarlo) — verificato a compilazione + dal
  non-regresso degli altri test.
- **A7** — le stesse macro FTZ/DAZ usate da `ensureDenormalProtectionOnThisThread()`
  (non esportata, quindi testate direttamente) azzerano davvero un
  denormale invece di lasciarlo propagare (solo x86/x86_64; la verifica
  ARM64/AArch64 non è automatizzata qui).
- **P2+P3** — `Lv2Plugin::buildLv2PathValue()` (estratta apposta come
  funzione pura testabile): separatore giusto per piattaforma, default di
  sistema non persi se `LV2_PATH` era vuota, path extra multipli nell'ordine
  giusto.

Non coperto qui (richiede l'app vera / l'orecchio dell'utente / un vero
Mac o Windows): C7, l'inspector, P1 (VST2 reali su macOS), P5 (path accentati
su Windows), P9 (doppio clic su un .oql), C5 (prompt permesso microfono su
macOS), la verifica "a orecchio" di A6/A8/A2 nell'interfaccia reale, **P4**
(SEH su Windows — nessun modo di compilare/eseguire codice MSVC-specifico su
questa macchina: lo verifica solo la CI a compilazione, e un vero Windows a
runtime), **P8** (mic/video sull'uscita configurata — il confronto per nome
fra l'enumerazione di miniaudio e quella di QMediaDevices è "best effort",
serve un vero device non-default per confermare che funzioni davvero). Vedi
la lista di task nel doc del piano.

Fase 5 (prestazioni UI — `fase5_tests.cpp`):

- **E1** — `CueListModel`: la colonna Target per `AudioCue` (ricerca della
  `RecordCue` collegata) e per `ControlCue` (risoluzione del target per id)
  non scansiona più l'intera lista ad ogni singola cella (O(n) per cella,
  O(n²) per un repaint completo con n righe) — usa due cache (`m_cueById`,
  `m_recordSrcByAudioCueId`) invalidate in modo mirato: sui cambi strutturali
  (add/remove/move/reset) e, per la seconda, quando la cue cambiata è
  proprio una `RecordCue`. Testato: risoluzione corretta prima/dopo aver
  sganciato un collegamento RecordCue↔AudioCue e dopo una `removeCue()`.
- **E2** — spostare il playhead (`CueList::setPlayhead()`) emette
  `dataChanged` solo per la riga che perde l'evidenziazione e quella che la
  riceve, non più per `0..rowCount()-1` ad ogni passo. Testato via
  `QSignalSpy` sul segnale `dataChanged` del modello.
- **E3** — `CueListModel::onCuePropertyChanged()` non ricalcola più
  `computeVisibleRows()` (O(n log n) con allocazioni QSet/QMap) per OGNI
  cambio di proprietà di QUALSIASI cue: lo fa solo se il `parentGroupId`
  della cue cambiata è davvero diverso da quello visto all'ultimo rebuild
  (l'unica proprietà che incide sulla disposizione delle righe; il collapse
  di un gruppo arriva già a parte via `cueLayoutChanged`). Testato via
  `QSignalSpy` su `modelReset`: un cambio di nome non lo innesca, un
  cambio reale di `parentGroupId` sì.
- **E4** — `ImageCue::onTick()` non emette più `displayChanged()` 30 volte
  al secondo per l'intera durata di una slideshow: solo mentre è
  effettivamente in transizione (la dissolvenza/slide avanza visivamente ad
  ogni tick) o nel tick in cui la transizione finisce (per mostrare lo stato
  finale) — non più durante i tick di pura attesa (`Holding`), dove non
  cambia nulla da ridisegnare. Chiude anche il caso peggiore di E3 (niente
  più cascata di ricalcoli a 30Hz). Testato chiamando `onTick()` a mano e
  contando le emissioni con `QSignalSpy`.
- **E12** — `JsWorkspace::wrap()` non passa più `this` come parent C++ al
  `JsCue` appena creato: con un parent C++, `QJSEngine` non lo libera MAI
  (anche se lo script non lo referenzia più), quindi ogni
  `workspace.at()/byId()/byNumber()/byName()` restava appeso per sempre a
  `JsWorkspace`. Senza parent, il garbage collector di `QJSEngine` lo libera
  quando lo script non lo usa più — il pattern corretto per i wrapper
  esposti a JS. Testato con 5000 `workspace.at(0)` in loop, verificato pulito
  anche sotto ASan/LeakSanitizer (3 run).
- **E13** — `JsHttp::request()` (dietro `http.get()/post()` negli script) ha
  ora un timeout di sicurezza di 15s e controlla `reply->error()` prima di
  leggere la risposta: prima, un host che non rispondeva mai bloccava lo
  show a tempo indeterminato (event loop bloccante, nessun'altra cue poteva
  avanzare). Testato con una connessione rifiutata locale (fallisce in pochi
  ms, non nei 15s del timeout).

Corrette ma NON testate qui (richiedono GUI/QApplication con un platform
plugin reale, o uno strumento esterno al processo — vedi la checklist nel
doc del piano):

- **E5** — la decodifica della waveform (`WaveformView::startDecode()`) non
  carica più l'intero file in RAM campione per campione: usa
  `ma_decoder_get_length_in_pcm_frames()` per conoscere la lunghezza in
  anticipo e accumula i bucket min/max in streaming, con memoria costante.
  Se la lunghezza non è disponibile (sorgente non seekable), niente
  waveform invece di tornare al vecchio comportamento — caso raro per file
  audio locali.
- **E6** — lo stesso decode: prima, a decodifica completata (non
  interrotta), l'oggetto `QThread` veniva solo scordato (`= nullptr`) senza
  mai essere liberato — un leak per ogni `setFilePath()`. Ora
  `deleteLater()` quando il thread ha già finito da solo.
- **E7** — `ActiveCuesPanel::rebuildCards()` non distrugge più e ricrea
  TUTTE le card ad ogni cambiamento di stato di QUALSIASI cue: sincronizza
  in modo incrementale (aggiunge solo le card nuove, rimuove solo quelle non
  più attive, riposiziona senza ricreare quelle già presenti ma fuori
  ordine).
- **E11** — la cache di `ImageOutputWindow::pixmapFor()` non cresce più
  senza limite: bounded a 48 voci in stile LRU (`m_pixmapOrder`).

Non coperto qui (richiede l'app vera / l'orecchio e l'occhio dell'utente —
vedi la checklist GUI nel doc del piano): la verifica visiva di E5/E6/E7/E11
nell'interfaccia reale, e tutto quanto già elencato per le Fasi 1-4 sopra.

Fase 6 (deduplicazione — `fase6_tests.cpp`):

- **R1** — `AudioCue`: il blocco "uninit + reinit decoder a una nuova sample
  rate + seek + (talvolta) aggiorna il totale frame", ripetuto 4 volte in
  `go()/setUserRate()/setPitchPreserve()/setPlaybackRate()`, è ora
  `AudioCue::reinitDecoderAt()`; il reset di SoundTouch è
  `resetSoundTouchIfNeeded()`. Le differenze pre-esistenti fra i 4 chiamanti
  (es. `setPitchPreserve()` non aggiornava il totale frame, `setPlaybackRate()`
  non tocca SoundTouch) sono state preservate esattamente, non "corrette" —
  non è nello scope di un refactor di deduplicazione. Non ha un test
  dedicato: è esercitato dai test di Fase 1 (`go()`/`setUserRate()`/ecc. già
  testati lì), ripassati senza regressioni.
- **R2** — `AudioCue::restorePluginSnapshot()`/`applyPluginChain()`
  (differivano solo per la fonte del JSON e l'emissione di
  `pluginSnapshotRestored()`) unificate in `swapPluginChainAsync()`. Stesso
  discorso: esercitato dal test C1+C3 di Fase 1, ripassato senza
  regressioni.
- **R3** — le 9 `add*Cue()` "semplici" di `MainWindow` (identiche a meno del
  tipo, del nome di default e di una piccola configurazione extra) unificate
  in `addSimpleCue()`. Nessun test qui: richiede un `QApplication` con
  platform plugin reale.
- **R4** — `CueList::fromJson()`: la catena if/else di 16 rami
  (`make_unique<T>()` + `fromJson` + `move`) sostituita da una tabella
  statica stringa→factory. **Testato**: tutti i 16 cueType risolvono al tipo
  concreto giusto, nell'ordine giusto; un tipo sconosciuto viene saltato
  (non crasha).
- **R5** — le 5 lambda in `CueList::connectCue()` che risalivano da una
  `Cue*` al proprio indice (stesso `std::find_if` ripetuto) usano ora
  `CueList::indexOfCue()`. **Testato**: l'indice riportato resta corretto
  dopo una `removeCue()` precedente nella lista.
- **R6** — `MainWindow::toggleVideo/Text/ImageOutput()` (identiche) e i 3
  rami gemelli in `eventFilter()` unificati in `toggleOutputWindow()` e in
  un piccolo array di coppie finestra/azione. Nessun test: richiede GUI.
- **R7** — `struct Biquad` (duplicata identica in `EqPlugin` e
  `MufflePlugin`, ciascuna con solo il sottoinsieme di metodi che le
  serviva) unificata in un'unica definizione file-scope in
  `BuiltinPlugins.h`; `type()`/`isValid()` e l'apertura di `toJson()`
  (identiche nelle 11 classi builtin) spostate in una base condivisa
  `BuiltinAudioPlugin`. **Testato**: `type()`/`isValid()` ereditati, `toJson()`
  porta il `builtin_id` giusto, round-trip `toJson()`→`fromJson()`→`toJson()`
  identico per `EqPlugin`; `EqPlugin`/`MufflePlugin::process()` su un impulso
  producono solo valori finiti e il segnale viene davvero filtrato (il
  Biquad condiviso funziona per entrambe le classi).
- **R8** — le 5 `QSettings("OQL","OQL")` ad-hoc per i file recenti e l'hint
  "Ultra Dark" in `MainWindow.cpp` sostituite da
  `AppSettings::recentFiles()/setRecentFiles()` e
  `ultraDarkHintShown()/setUltraDarkHintShown()`. **Testato**: round-trip di
  entrambe — lo stato reale di questa macchina viene salvato e ripristinato
  a fine test (il test scrive sul `QSettings` vero di `AppSettings::instance()`,
  non un'istanza isolata).
- **R9** — la stessa logica di dipendenze Linux/Windows e di packaging
  AppImage/installer Windows, ripetuta in `build-linux.yml`/
  `build-windows.yml`/`build-beta.yml`/`release.yml`, è ora in 4 composite
  action sotto `.github/actions/` (`install-linux-deps`,
  `package-linux-appimage`, `install-windows-deps`,
  `package-windows-installer`), parametrizzate dove serve (nome del file
  risultante). **macOS deliberatamente NON toccato**: `build-macos.yml` e
  `build-beta.yml` usano lo stesso approccio (codesign manuale + DMG via
  `hdiutil`/`ditto`, con un passaggio diagnostico extra solo in
  `build-macos.yml`), ma `release.yml` usa un approccio diverso
  (`macdeployqt -dmg` diretto) **e manca del tutto il passo di generazione
  dell'icona** (`.icns`) — quindi le build macOS pubblicate da `release.yml`
  partono probabilmente senza l'icona personalizzata dell'app. Questa è una
  vera divergenza scoperta facendo la deduplicazione (non introdotta qui):
  unificarla significherebbe cambiare il comportamento della pipeline di
  release senza modo di verificarlo su un vero runner macOS — lasciata alla
  tua decisione. Validato con `actionlint` (zero avvisi su tutti i workflow
  toccati).
- **R10** — la scomposizione di `InspectorPanel` (2377 righe, oggetto-dio)
  resta **deliberatamente fuori scope**, come da piano: intervento grosso e
  rischioso, da affrontare da solo quando il resto è stabile. La sola nota
  a parte di R10 — `InspectorPanel::blockSignals(bool)` che nascondeva
  `QObject::blockSignals(bool)` con semantica diversa — è stata applicata:
  rinominata `blockFieldSignals(bool)`.
- **R11** — `VstPlugin::m_active` (nascondeva `AudioPlugin::m_active` con
  significato opposto: "mains on" contro "bypass") rinominato `m_mainsOn`.
  Nessun workaround esplicito (`AudioPlugin::isActive()` qualificato) è
  stato trovato nel codice attuale da aggiornare — probabilmente già
  ripulito quando A1 ha centralizzato il bypass in `PluginChain::process()`
  (Fase 3).

Non coperto qui (richiede GUI reale, o — per R9 — un vero runner CI): R3,
R6 (MainWindow), e la verifica a runtime di R9 sulle tre piattaforme reali
(qui solo validato staticamente con `actionlint`).

## Bug trovati testando A8 a mano (2026-10-24), e relativi fix

Durante il test manuale di A8, sono emersi due problemi distinti dal fix
originale del fade (quello era corretto), aggiunti a `tests/fase3_tests.cpp`:

- **Automazione del volume disattivava il fade** — `AudioCue::renderAudio()`
  (entrambi i rami, con/senza SoundTouch) trattava fade e punti di
  automazione del volume come **mutuamente esclusivi** (`if/else`): la sola
  presenza di un punto di automazione — anche uno creato per sbaglio
  cliccando sulla waveform fuori dalla maniglia del fade — disattivava
  `fadeEnvelope()` del tutto. La linea di anteprima in `WaveformView` li
  mostra invece sempre **combinati** (`userVol(t) * fadeEnv(t)`), quindi
  l'anteprima prometteva un fade che poi non suonava. Ora si moltiplicano
  anche nel motore. **Testato** (`A8c`): un'automazione "piatta" (sempre
  1.0) non impedisce più al fade di sentirsi.
- **`setLoopCount()` ignorato se la cue è già in riproduzione** — scriveva
  solo `m_loopCount` (letto da `go()` all'avvio), mai `m_loopsRemaining`
  (il contatore realmente consultato da `renderAudio()` per decidere se
  ripartire). Cambiarlo da Inspector mentre la cue suonava non aveva alcun
  effetto fino al riavvio. Ora, se la cue è in riproduzione,
  `m_loopsRemaining` viene aggiornato subito. **Testato**: con `loopCount`
  cambiato da 1 a 5 mentre la cue suona, la riproduzione continua oltre la
  durata di un singolo giro invece di fermarsi.

Aggiunto anche `AudioCue::renderAudioForTest()` (wrapper pubblico minimo su
`renderAudio()`, stesso spirito di `fadeEnvelope()` pubblica) e
`writeLoudTestWav()` in `test_harness.h` (segnale a piena scala, non
silenzio) per poter misurare l'ampiezza REALMENTE renderizzata, non solo il
valore di `fadeEnvelope()` in isolamento — è stato proprio ripetendo questa
misura con l'automazione impostata che si è isolato il bug
dell'automazione.

Corretto separatamente (non nei test automatici, serve l'occhio): la
geometria del fade in `WaveformView` (overlay, linea di volume, rilevamento
click sulla maniglia, calcolo della nuova durata quando la si trascina) era
calcolata rispetto alla durata intera del file invece che al trim —
`activeStartNorm()/activeEndNorm()` ora danno l'intervallo giusto.

## Bug trovato testando P8 (uscita audio) a mano (2026-10-24)

Cambiare l'uscita audio da Impostazioni → Hardware non spostava davvero
l'audio, anche se il log diagnostico confermava che miniaudio risolveva
correttamente il nome al device giusto e riportava successo.
`AudioEngine::reinitWithDevice()` faceva stop+uninit+init **sulla stessa
struct `ma_device`** riusata in-place; il percorso "uscita extra per cue"
(`addRenderer()`), che invece crea sempre un `ma_device` nuovo, funzionava
correttamente — confermato dall'utente. **Fix**: `reinitWithDevice()` ora
costruisce una `Output` nuova da zero (stesso schema di `addRenderer()`),
trasferisce i renderer già in riproduzione alla nuova uscita, e solo se
l'apertura riesce sostituisce quella vecchia (altrimenti quella vecchia
resta intatta invece di restare morta). Non testato in automatico: richiede
`AudioEngine::instance().init()` su un vero device audio, che questo
harness non chiama mai di proposito (vedi la nota in cima al file) — build
pulita, ASan+UBSan pulito su 3 run, build CI-style fresca pulita,
verificato da te con un log diagnostico temporaneo (rimasto nel codice,
`qWarning` in `AudioEngine::startOutput()`) prima e dopo il fix.

## Bug P8 più profondo trovato subito dopo (2026-10-24)

Il fix precedente di `reinitWithDevice()` era corretto ma non bastava: le
cue con uscita "Predefinita" (`AudioCue::outputDevice()` vuota) continuavano
a ignorare Impostazioni → Hardware. Causa reale, in `AudioEngine::addRenderer()`:
il confronto `o->name == deviceName` cercava un'uscita con nome
**letteralmente vuoto** per risolvere una richiesta vuota — ma `name`
dell'uscita principale non è un flag "è quella di default", è la stringa
scelta in Impostazioni → Hardware (es. "JBL Xtreme 3"). Appena l'utente
sceglieva lì un device specifico, una richiesta "" non trovava più
corrispondenza e la cue si apriva un'uscita extra NUOVA puntata al device
di sistema predefinito — ignorando in silenzio la scelta fatta in
Impostazioni (che "funzionava" solo per pura coincidenza, quando non era
mai stata toccata). **Fix**: `""` risolve ora sempre direttamente
`m_impl->outputs.front()` (l'uscita principale), prima di qualunque
confronto per nome. Stesso discorso del fix precedente per i test: non
testabile in automatico senza un vero `AudioEngine::init()`.
