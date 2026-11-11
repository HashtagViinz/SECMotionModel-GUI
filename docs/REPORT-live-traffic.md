# GUI live: carico satelliti e traffico sui link

Questa è la relazione della prima versione "viva" della GUI. La GUI non esegue
SimPy in tempo reale: registra una run reale del simulatore e la riproduce
frame per frame. Posizioni, carico e forward sono dati reali; l'unica
approssimazione è la durata visiva delle onde, allungata quando la trasmissione
reale è troppo breve per essere vista a 60 FPS.

## Cosa è stato aggiunto

- `NavalOrbit/gui_monitor.py`: scrive `gui_state.csv`, `gui_links.csv` e
  `gui_run.json` durante una run navale.
- `naval_main.py --gui-export`: attiva il monitor senza cambiare il
  comportamento normale della simulazione.
- `EdgeServer.transmit_task`: registra i forward reali (sorgente,
  destinazione, task, dimensione, banda, durata).
- `tools/export_gui_scene.py --run-dir`: unisce la configurazione orbitale con
  i CSV e produce una scena v2 con `loads.jsonl` e `traffic.jsonl`.
- GUI: heatmap del carico, onde sui link, HUD con carico massimo/medio,
  telemetria del satellite selezionato, layer `load` e `traffic`.
- I satelliti che non sono più presenti in un frame vengono esclusi anche dai
  dati di carico: non vengono rappresentati né interpolati.

## Come vederlo

```bash
cd /home/vinz/SEC/SECMotionModel

# 1. run reale con export per la GUI
python3 naval_main.py --load-only --run-sim --gui-export --use-case IN_NETWORK

# 2. apertura della run
secgui --run result/naval/IN_NETWORK/optimal

# alternativa con camera sul vessel
secgui --run result/naval/IN_NETWORK/optimal --camera vessel --target V-ATLANTIC
```

La prima apertura esporta la scena e la mette in `~/.cache/secgui/runs`; le
successive riusano la cache. Per il traffico più denso usare `TO_OBSERVER`:

```bash
python3 naval_main.py --load-only --run-sim --gui-export --use-case TO_OBSERVER
secgui --run result/naval/TO_OBSERVER/optimal --camera vessel --target V-ATLANTIC
```

## Cosa guardare nella GUI

### Carico dei satelliti

Layer **Load heatmap** (acceso dalle scene v2):

- azzurro chiaro: carico basso;
- giallo: carico medio;
- rosso: carico alto;
- rosso scuro/nero: saturazione, con alone per restare visibile sul fondo.

La metrica è:

```text
cpu_load = (cpu_queue + cpu_service) / cpu_capacity
net_load = (net_queue + net_service) / net_capacity
load     = max(cpu_load, net_load)
```

Il pannello HUD mostra `Load max` e `Load avg`. Selezionando un satellite, la
telemetria mostra code CPU/NET, servizio, task completati ed energia.

### Traffico sui link

Layer **Link traffic**:

- il colore del link ISL indica quanto traffico ha attraversato il link nella
  finestra recente;
- l'onda luminosa gialla scorre dalla sorgente alla destinazione quando c'è un
  forward attivo.

Le onde non hanno etichette e non mostrano il nome del task: servono a far
vedere i flussi, non a tracciare il singolo task. La rete ISL completa è molto
densa: per vedere meglio le onde conviene premere `I` (ISL off) e lasciare
accesi solo `traffic` e `satellites`.

### Layer e scorciatoie

- pannello **Layers**: `load`, `traffic`, `tasks`, ISL, dome, route;
- `K` continua a togglare i task pulse, ma nelle scene v2 i task pulse sono
  spenti di default: il nuovo default è l'onda sul link;
- `Space` play/pausa, `←`/`→` step, `F12` screenshot.

## Da dove arrivano i dati

```text
naval_main.py --gui-export
        │
        ├── gui_state.csv    carico per satellite ogni 0,5 s
        ├── gui_links.csv    un forward reale per riga
        └── gui_run.json     config, durata, capacità, seed
                    │
                    ▼
tools/export_gui_scene.py --run-dir
        │
        ├── frames/*.json    posizioni/AP/ISL (come prima)
        ├── loads.jsonl      carico, filtrato sui satelliti presenti nel frame
        └── traffic.jsonl    forward reali
                    │
                    ▼
GUI v2: heatmap + onde
```

I CSV grezzi sono ispezionabili con qualunque editor o con `column -s, -t`.
Il campionamento del carico è a 0,5 s; la GUI usa il campione più recente
rispetto al tempo corrente. I frame orbitali restano a 2 s.

Un satellite che non compare più in un frame viene escluso anche dai campioni
di carico: l'exporter filtra per ID presenti nel frame e non fa carry-forward.
Non viene quindi né disegnato né colorato dopo l'uscita dall'orbita/corridoio.

## Risultati della run

### IN_NETWORK

- frame: 150 (300 s);
- satelliti nella scena: 760 complessivi, 454–470 per frame;
- campioni di carico: 597 (0,5 s);
- forward reali: 444 ISL + 532 downlink (976 totali);
- carico massimo osservato: 40% (2 task CPU in servizio su capacità 5);
- attivazioni osservate: 344 righe satellite con carico > 0.

Con la configurazione attuale il carico CPU/NET è basso: i task vengono
serviti rapidamente e le code quasi non si formano. La heatmap mostra quindi
soprattutto attivazioni brevi, non una saturazione continua.

### Stress control (solo per validare la scala rossa/nera)

Per verificare che la scala arrivi davvero al nero è stata usata una copia
temporanea della configurazione con arrivo più frequente
(`arrival_time_exponential: 0.1`) e capacità 2 invece di 5:

- campioni: 597;
- carico massimo: 150% (1 task in coda + 2 in servizio su capacità 2);
- righe satellite oltre il 50%: 219;
- righe satellite oltre il 100%: 4;
- forward registrati: 2.099 ISL + 2.721 downlink (4.820 totali).

Questa run stress non è la configurazione di default; serve solo come prova
visiva della saturazione.

### TO_OBSERVER

- forward registrati: 11.343 ISL + 1.142 downlink (12.485 totali);
- il caso è più adatto a mostrare le onde, perché ogni task attraversa più
  satelliti;
- il carico CPU dei satelliti è quasi nullo per costruzione (relay).

## Limiti noti

- Il campionamento a 0,5 s può ancora perdere attivazioni più brevi.
- La scena v2 è più grande della v1: `loads.jsonl` e `traffic.jsonl` aggiungono
  decine di MB.
- La durata visiva delle onde è allungata a un minimo di 0,35 s; non è una
  misura esatta del tempo di trasmissione.
- I task pulse restano disponibili come layer di debug, ma sono spenti di
  default per non sporcare la scena.
- La run stress per mostrare la saturazione rossa/nera è separata dalla run
  normale e va rigenerata con una configurazione a frequenza di arrivo più alta.

## Evidenze

Le immagini e la GIF sono caricate nella release `agent-media` della repo GUI:

- carico normale (max 40%): [gui-v02-load-normal.png](https://github.com/HashtagViinz/SECMotionModel-GUI/releases/download/agent-media/gui-v02-load-normal.png)
- carico stress (max 150%, nero/rosso): [gui-v02-load-stress.png](https://github.com/HashtagViinz/SECMotionModel-GUI/releases/download/agent-media/gui-v02-load-stress.png)
- onde di traffico con ISL spento: [gui-v02-traffic-waves.png](https://github.com/HashtagViinz/SECMotionModel-GUI/releases/download/agent-media/gui-v02-traffic-waves.png)
- GIF delle onde: [gui-v02-traffic-waves.gif](https://github.com/HashtagViinz/SECMotionModel-GUI/releases/download/agent-media/gui-v02-traffic-waves.gif)
