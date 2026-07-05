# Plugin ed Effetti Audio

OQL supporta tre categorie di effetti audio:

| Categoria | Piattaforme | Dipendenze esterne |
|---|---|---|
| **Built-in** | macOS, Windows, Linux | Nessuna |
| **VST2** (`.so` / `.dll`) | Linux, Windows | Plugin di terze parti |
| **LV2** (bundle) | Linux | lilv + plugin di terze parti |

---

## Effetti Built-in

Gli effetti built-in sono implementati direttamente in OQL e **non richiedono l'installazione di plugin esterni**. Sono disponibili su tutte le piattaforme supportate.

### Elenco effetti built-in

| Effetto | Parametri | Descrizione |
|---|---|---|
| **Gain** | Gain (dB, −60..+12) | Regola il volume del segnale |
| **Delay** | Delay ms, Feedback %, Mix % | Eco digitale con feedback regolabile (fino a 2 s) |
| **Reverb** | Room %, Damping %, Wet % | Riverbero algoritmico Freeverb (8 comb + 4 allpass) |
| **EQ 3-band** | Bass dB, Mid dB, Treble dB | Equalizzatore: low shelf 200 Hz, peaking 1 kHz (Q 0.7), high shelf 6 kHz |
| **Compressor** | Threshold dB, Ratio :1, Attack ms, Release ms | Compressore feed-forward con rilevatore di picco stereo |
| **Limiter** | Threshold dB (−30..0) | Limitatore di picco brickwall, attack istantaneo, release 100 ms |
| **Chorus** | Rate Hz, Depth ms, Mix % | Chorus stereofonico: ritardo modulato da LFO, L e R sfasati di 90° |
| **Tremolo** | Rate Hz (0.1..20), Depth % | Modulazione d'ampiezza sinusoidale |
| **Phaser** | Rate Hz, Feedback %, Mix % | Phaser a 4 stadi all-pass con sweep LFO 200–4000 Hz e feedback |
| **Stereo Widener** | Width % (0..200) | Elaborazione mid-side: 0%=mono, 100%=originale, 200%=extra-wide |

### Note tecniche

- Tutti gli effetti sono stereo (2 canali in ingresso, 2 in uscita).
- Il **Compressor** usa la formula `gainDb = (threshold − levelDb) × (1 − 1/ratio)` con smoothing attack/release separato (coefficienti IIR).
- Il **Limiter** usa gain-sharing istantaneo (no lookahead) per protezione speakers.
- Il **Phaser** aggiorna i coefficienti all-pass ogni 64 campioni (control-rate) per ridurre il carico CPU.
- Il **Chorus** usa interpolazione lineare per il ritardo frazionario.
- L'**EQ 3-band** usa i coefficienti del *Audio EQ Cookbook* di Robert Bristow-Johnson (dominio pubblico).

---

## Plugin VST2 e LV2

OQL supporta plugin di terze parti in formato VST2 e LV2. I percorsi di ricerca predefiniti sono:

| Formato | Percorso default (Linux) | Percorso custom |
|---|---|---|
| **VST2** | `/usr/lib/vst/`, `/usr/lib/lxvst/`, `~/.vst/` | Impostazioni → Plugin → Cartelle aggiuntive VST2 |
| **LV2** | `/usr/lib/lv2/`, `~/.lv2/` | Impostazioni → Plugin → Cartelle aggiuntive LV2 |

Le cartelle aggiuntive vengono aggiunte ai percorsi di sistema e hanno effetto al prossimo avvio.

---

## Dove si usano gli effetti

Ogni **Audio cue** ha la propria catena effetti permanente, modificabile dall'Inspector.

Una **Effect cue** porta una catena separata che viene applicata temporaneamente a una Audio cue target al momento dell'esecuzione, e rimossa automaticamente allo scadere della durata (o quando viene eseguita una Reset Effect Cue).

---

## Apertura dell'editor della catena

1. Seleziona una Audio cue o una Effect cue nella cue list.
2. Nell'Inspector Panel clicca **⚙ Effetti…**.
3. Si apre una finestra flottante con la catena del plugin per quella cue.

---

## Aggiungere un effetto

1. Clicca **+ Aggiungi effetto** nell'editor della catena.
2. Si apre un dialogo con tre tab:
   - **Built-in** — effetti integrati, sempre disponibili su tutte le piattaforme.
   - **VST2** — scansiona le cartelle VST configurate. Usa la casella di ricerca per filtrare.
   - **LV2** — elenca tutti i plugin LV2 trovati dal sistema.
3. Seleziona un effetto e clicca **OK**.

L'effetto viene aggiunto in fondo alla catena e preparato immediatamente con il sample rate e il block size correnti dell'engine (48 kHz / 512 frame di default).

---

## Riordino e rimozione

| Pulsante | Azione |
|---|---|
| **▲ / ▼** | Sposta il plugin selezionato su o giù nella catena |
| **−** | Rimuove il plugin selezionato dalla catena |

La catena viene elaborata dall'alto verso il basso: l'output di ogni plugin alimenta l'input del successivo.

---

## Modifica dei parametri

Cliccando un plugin nella lista si visualizzano i suoi parametri come slider. Trascinare uno slider modifica il valore in tempo reale — le variazioni si applicano immediatamente all'audio live.

La checkbox **Attivo** in cima ai parametri bypassa il plugin senza rimuoverlo dalla catena.

---

## Editor grafico nativo (VST2)

I plugin VST2 che forniscono un'interfaccia grafica mostrano il pulsante **Apri editor grafico del plugin**. Cliccandolo si apre l'UI nativa del plugin in una finestra separata.

> **Nota:** Gli editor VST2 nativi richiedono X11. Su Wayland, assicurarsi che XWayland sia in esecuzione. Gli editor basati su OpenGL (es. JUCE) possono richiedere Mesa: `sudo pacman -S mesa` / `sudo apt install mesa-utils`.

---

## Impostazioni dell'engine audio

OQL usa **miniaudio** per la riproduzione a bassa latenza con un block size fisso di **512 frame**. Questo valore viene passato ai plugin VST2 via `effSetBlockSize`, garantendo che plugin ed engine concordino sempre sulla dimensione del buffer. Il sample rate viene scelto automaticamente dal driver.

---

## Salvataggio dello stato

Lo stato dei plugin è salvato come parte del file workspace `.oqlab`.

- I **plugin built-in** salvano i propri parametri come valori JSON.
- I **plugin VST2 chunk-based** (che riportano `effFlagsProgramChunks`) salvano l'intero stato interno come blob binario in base64.
- I **plugin parameter-based** salvano l'array completo dei valori float.

Lo stato viene ripristinato alla riapertura del workspace e quando una Effect cue viene riapplicata.

---

## Esempio di workflow Effect Cue

```
Cue 10  Audio cue  "Ambient loop"             (nessun effetto inizialmente)
Cue 11  Effect cue "Aggiungi reverb"  →10     durata=0 (permanente)
            └─ catena: Reverb  (Room 70%, Damping 40%, Wet 35%)
Cue 12  ResetEffect cue               →10     (ripristina la catena originale)
```

Eseguendo cue 11 si aggiunge il Reverb al loop ambient in tempo reale. Eseguendo cue 12 viene rimosso e ripristinata la catena originale.

---

## Troubleshooting

| Sintomo | Causa probabile | Soluzione |
|---|---|---|
| Plugin VST2/LV2 non in lista | `.so` / bundle non nelle cartelle scansionate | Copia il plugin in `~/.vst/` o `~/.lv2/`, oppure aggiungi la cartella in Impostazioni → Plugin |
| Ronzio / rumore al primo uso | Mismatch block size | Verificare che il block size miniaudio sia 512 (default) |
| Finestra editor nera | OpenGL non disponibile | Installa Mesa: `sudo pacman -S mesa` |
| Crash al cambio parametro | Plugin non thread-safe | Aggiorna all'ultima versione di OQL |
| Parametri persi dopo reload | Plugin usa chunk state | Dovrebbe salvarsi automaticamente; segnala come bug se non funziona |
| Effetto built-in non funziona | Sample rate non inizializzato | Verificare che prepare() venga chiamato prima di process() |
