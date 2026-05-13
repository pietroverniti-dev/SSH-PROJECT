# SSH-PROJECT

## Credenziali WiFi

Per permettere all’ESP32 di connettersi alla tua rete locale, inserisci SSID e password all’interno delle due costanti qui sotto.

Sostituisci le virgolette vuote con i dati esatti della tua rete (rispettando maiuscole e minuscole).

```cpp
// ===================== CREDENZIALI WIFI =====================
#define WIFI_SSID ""
#define WIFI_PASS ""
```

---

## Accesso al sito web

Per accedere all’interfaccia web dell’ESP32, il dispositivo utilizzato (PC, smartphone o tablet) deve essere connesso alla **stessa rete WiFi** dell’ESP32.

Una volta avviato il dispositivo, sarà possibile raggiungere il sito tramite:

- l’indirizzo IP dell’ESP32 mostrato nel **Monitor Seriale**
- oppure tramite il dominio locale:

```text
http://ambiente.local
```

### Esempio

```text
http://192.168.1.42
```

> Assicurati che il monitor seriale sia configurato con il baud rate corretto per visualizzare l’indirizzo IP.