# Idroponica

Progetto per una torre idroponica a ricircolo controllata da un **Wemos D1 mini (ESP8266)**. La dashboard è servita dalla scheda sulla rete locale e permette di controllare la pompa e registrare misurazioni manuali.

> Stato: prima versione di sviluppo. Il firmware non è ancora stato compilato né verificato sul dispositivo reale. Prima di collegare la pompa, verificare il circuito, il modulo MOSFET e il comportamento del segnale all'avvio e al reset.

## Funzioni previste

- Comando manuale e ciclo automatico: durata in minuti e intervallo in ore tra due accensioni successive.
- Dashboard locale con stato comandato della pompa e rappresentazione della torre.
- Inserimento manuale e grafici per livello del serbatoio, pH, EC e temperatura; fino a 500 misure per parametro nella memoria dell'ESP.
- Funzionamento del ciclo automatico anche senza Wi-Fi. In automatico, dopo un riavvio la pompa riparte dopo cinque secondi; in manuale rimane spenta.

L'impianto non dispone ancora di sensori di livello o di flusso: la dashboard mostra il comando inviato, non conferma che l'acqua circoli.

## Struttura

- `firmware/Idroponica_D1mini/`: sketch Arduino, dashboard per ESP, header compresso e istruzioni.
- `preview/index.html`: anteprima indipendente con controlli simulati; i dati sono salvati nel browser e non vengono trasferiti all'ESP.
- `scripts/build_dashboard.py`: rigenera l'header dopo una modifica alla dashboard per l'ESP.

## Avvio e configurazione

1. Installa Arduino IDE e il [supporto ESP8266](https://arduino-esp8266.readthedocs.io/en/latest/installing.html); seleziona **LOLIN(WEMOS) D1 R2 & mini**.
2. Copia `firmware/Idroponica_D1mini/config.example.h` in `config.h` nella stessa cartella e inserisci le credenziali Wi-Fi e quelle della dashboard. `config.h` è escluso da Git: non caricarlo nella repository.
3. Apri `firmware/Idroponica_D1mini/Idroponica_D1mini.ino`, compila e carica sulla scheda con la **pompa scollegata**. Sul monitor seriale a 115200 baud trovi l'indirizzo IP, se la connessione Wi-Fi riesce.
4. Prima di usare la pompa, esegui le verifiche descritte in `firmware/Idroponica_D1mini/LEGGIMI.txt`. Per conservare i dati, leggi le istruzioni su LittleFS prima di inizializzare o formattare la memoria.

Per avviare l'anteprima locale senza scaricare file, esegui dalla radice della repository `python3 -m http.server 8765 --bind 127.0.0.1 --directory preview`, poi apri http://127.0.0.1:8765/. Le modifiche a `preview/index.html` sono visibili ricaricando la pagina; il server deve restare in esecuzione. Per modificare la dashboard servita dalla scheda, modifica `firmware/Idroponica_D1mini/dashboard.html`, esegui `python3 scripts/build_dashboard.py` e ricompila il firmware. Le due versioni hanno collegamenti diversi: l'anteprima simula le operazioni, mentre quella dell'ESP usa le API della scheda.

La dashboard sulla scheda usa autenticazione HTTP senza HTTPS: usala solo su una rete fidata e non esporla direttamente a Internet.
