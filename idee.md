# 📘 Progetto IoT: Architettura, Criticità e Soluzioni
> **Guida tecnica per la progettazione del sistema:** ESP32 → Server → MongoDB → UI

---

## 🧩 1. Panoramica del Sistema

Il progetto si compone di quattro elementi principali che collaborano per monitorare dati ambientali e controllare attuatori.

### Componenti
1.  **ESP32**
    * Sensore **AHT20** (temperatura + umidità)
    * Eventuale **BMP280** (temperatura + pressione)
    * Ventola controllata via GPIO
2.  **Server Grezzo (API)**
    * Riceve dati dall’ESP32
    * Li valida e li salva su MongoDB
    * Espone endpoint per UI e comandi
3.  **MongoDB**
    * Storage dei dati dei sensori
    * Storage dello stato della ventola
4.  **Server Interfaccia Utente (UI)**
    * Dashboard e Grafici
    * Controllo ventola

### Flusso Generale
`ESP32` → `Server Grezzo` → `MongoDB` → `Server UI` → `Browser`

---

## 🌡️ 2. Sensori: AHT20 e BMP280

Confronto tra i sensori per definire la strategia di misurazione migliore.

| Caratteristica | AHT20 | BMP280 |
| :--- | :---: | :---: |
| **Temperatura** | ✔️ | ✔️ (più stabile) |
| **Umidità** | ✔️ | ❌ |
| **Pressione** | ❌ | ✔️ |

### Scelta Consigliata
* **Temperatura:** AHT20 (per coerenza con il dato di umidità dello stesso chip).
* **Umidità:** AHT20.
* **Pressione:** BMP280.
* *Nota:* La temperatura del BMP280 può essere usata come dato opzionale di confronto.



---

## 🔌 3. ESP32: Ruolo e Criticità

**Funzioni principali:** Lettura sensori, invio dati al server, ricezione comandi (ventola), gestione GPIO.

### Criticità e Soluzioni

#### 3.1 Perdita di connessione WiFi
* **Problema:** L’ESP32 può perdere la rete o il router può riavviarsi.
* **Soluzioni:**
    * Routine di riconnessione automatica.
    * Buffer locale dei dati (in RAM o SPIFFS) per inviarli al ritorno della linea.
    * Retry con *backoff esponenziale*.

#### 3.2 Dati duplicati o mancanti
* **Problema:** Invii ripetuti per errore o pacchetti persi.
* **Soluzioni:**
    * Generazione **Timestamp** lato ESP.
    * Assegnazione di un **ID univoco** al pacchetto.
    * Implementazione di un server *idempotente*.

#### 3.3 Sicurezza della comunicazione
* **Problema:** Spoofing, intercettazione dati, iniezione di comandi malevoli.
* **Soluzioni:**
    * **HTTPS obbligatorio**.
    * Token JWT a scadenza.
    * API Key lato server o Whitelist dei dispositivi (MAC Address).

#### 3.4 Controllo ventola
* **Problema:** Conflitti tra logica automatica locale e comandi manuali remoti.
* **Soluzioni:**
    * Definire un’unica "fonte di verità" (il Server).
    * Debounce temporale (evitare on/off rapidi).
    * Stato persistente (memorizzare l'ultimo stato noto).

---

## 🖥️ 4. Server Grezzo (API)

**Ruolo:** Punto centrale del sistema. Valida i dati, li salva su DB ed espone le API.

### Endpoint Tipici
* `POST /api/sensori` (Ricezione dati)
* `GET /api/stato` (Stato attuale sistema)
* `POST /api/ventola` (Comando attuatore)
* `GET /api/storico` (Dati per grafici)

### Criticità e Soluzioni

#### 4.1 Carico elevato
* **Problema:** Troppi dispositivi o polling troppo frequente.
* **Soluzioni:** Server asincrono (Node.js, FastAPI, Go), Rate limiting, Caching.

#### 4.2 Sicurezza
* **Problema:** Accessi non autorizzati.
* **Soluzioni:** JWT, API Key, Logging e Audit trail degli accessi.

#### 4.3 Validazione dei dati
* **Problema:** Payload corrotti, incompleti o malevoli.
* **Soluzioni:** Schema JSON rigido, sanitizzazione input, rifiuto (HTTP 400) dei pacchetti non validi.

---

## 🗄️ 5. MongoDB

**Ruolo:** Archivio storico sensori, stato ventola e configurazioni.

### Struttura Consigliata (Collection: `letture`)
```json
{
  "temperatura": 23.4,
  "umidita": 55.1,
  "pressione": 1012.3,
  "ventola": false,
  "timestamp": 1700000000
}