/*
 * ================================================================
 * STAZIONE AMBIENTE + CONTROLLO VENTOLA  —  Web Dashboard
 * Modifiche: Aggiunti sensori Gas MQ-4 e MQ-7. Sicurezza ventole.
 * Hardware : ESP32 | BMP280 | AHT20 | SSD1306 | L298N (x2) | MQ-4 | MQ-7
 * Librerie : Adafruit_SSD1306, Adafruit_BMP280, Adafruit_AHTX0,
 * WiFi, WebServer, LittleFS (built-in ESP32 Arduino core)
 * ================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <LittleFS.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_AHTX0.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// ===================== CREDENZIALI WIFI =====================
#define WIFI_SSID "sr1777"
#define WIFI_PASS "milanmilan"

// ===================== PIN I2C =====================
#define SDA_PIN 17
#define SCL_PIN 16

// ===================== SENSORI GAS =====================
#define MQ4_AO_PIN 34  // Analog MQ-4 (ADC1 - Sicuro con WiFi)
#define MQ4_DO_PIN 15  // Digital MQ-4
#define MQ7_AO_PIN 35  // Analog MQ-7 (ADC1 - Sicuro con WiFi)
#define MQ7_DO_PIN 4   // Digital MQ-7

int mq4_analog = 0;
int mq4_digital = 1;
int mq7_analog = 0;
int mq7_digital = 1;

// ===================== OLED =====================
#define OLED_RST -1
#define ROW 64
#define COL 128
Adafruit_SSD1306 display(COL, ROW, &Wire, OLED_RST);

// ===================== SENSORI I2C =====================
Adafruit_BMP280 bmp;
Adafruit_AHTX0  aht;

// ===================== TIMER =====================
hw_timer_t *timer0 = NULL;
unsigned long lastMs = 0;
const unsigned int period = 1000;

float currentTemp, currentHum;
unsigned long totSec;
unsigned int  minutes, seconds;

// ===================== MOTORI (L298N) =====================
#define ENA 13
#define IN1 12
#define IN2 14
#define ENB 25
#define IN3 27
#define IN4 26

#define BUTTON_PIN  0
#define LED_PIN    33
#define DEBOUNCE_MS 50
#define CW  1
#define CCW 0

const int PWM_CHANNEL_A   = 0;
const int PWM_CHANNEL_B   = 1;
const int PWM_FREQ        = 20000;
const int PWM_RES         = 8;

bool motorOn   = false;
bool autoMode  = true;
int  direction = CW;

unsigned long lastDebounceTime = 0;
int  lastButtonState = HIGH;
int  buttonState     = HIGH;
unsigned long manualTimer = 0;

// ===================== PARAMETRI CONFIGURABILI =====================
float tempThreshold = 28.0;
float humThreshold  = 70.0;
int   fanSpeedPct   = 100;

// ===================== CLASSE MOTORE =====================
class Robojax_L298N_DC_motor {
public:
  Robojax_L298N_DC_motor(int in1, int in2, int ena, int ch)
    : _in1(in1), _in2(in2), _ena(ena), _ch(ch) {}

  void begin() {
    pinMode(_in1, OUTPUT);
    pinMode(_in2, OUTPUT);
    pinMode(_ena, OUTPUT);
    ledcSetup(_ch, PWM_FREQ, PWM_RES);
    ledcAttachPin(_ena, _ch);
    coast();
  }

  void rotate(int /*id*/, int pct, int dir) {
    uint8_t duty = map(pct, 0, 230, 204, 255);
    digitalWrite(_in1, dir == CW ? HIGH : LOW);
    digitalWrite(_in2, dir == CW ? LOW  : HIGH);
    ledcWrite(_ch, duty);
  }

  void coast() {
    ledcWrite(_ch, 0);
    digitalWrite(_in1, LOW);
    digitalWrite(_in2, LOW);
  }

private:
  int _in1, _in2, _ena, _ch;
};

Robojax_L298N_DC_motor motorA(IN1, IN2, ENA, PWM_CHANNEL_A);
Robojax_L298N_DC_motor motorB(IN3, IN4, ENB, PWM_CHANNEL_B);

void setFanState(bool state, int dir = CW) {
  motorOn   = state;
  direction = dir;
  if (motorOn) {
    motorA.rotate(1, fanSpeedPct, direction);
    motorB.rotate(1, fanSpeedPct, direction);
    digitalWrite(LED_PIN, HIGH);
  } else {
    motorA.coast();
    motorB.coast();
    digitalWrite(LED_PIN, LOW);
  }
}

// ===================== STORICO (buffer circolare) =====================
#define HISTORY_SIZE 120  // 2 minuti a 1 campione/s

struct History {
  float temp [HISTORY_SIZE] = {};
  float hum  [HISTORY_SIZE] = {};
  uint8_t fan[HISTORY_SIZE] = {};
  unsigned long ts[HISTORY_SIZE] = {};
  int head  = 0;
  int count = 0;
} hist;

void histPush(float t, float h, bool fan) {
  hist.temp[hist.head] = t;
  hist.hum [hist.head] = h;
  hist.fan [hist.head] = fan ? 1 : 0;
  hist.ts  [hist.head] = totSec;
  hist.head = (hist.head + 1) % HISTORY_SIZE;
  if (hist.count < HISTORY_SIZE) hist.count++;
}

// ===================== WEB SERVER =====================
WebServer server(80);

void handleRoot() {
  File f = LittleFS.open("/index.html", "r");
  if (!f) {
    server.send(503, "text/plain",
      "index.html non trovato nel filesystem.\n"
      "Carica i file con: Strumenti -> ESP32 Sketch Data Upload");
    return;
  }
  server.streamFile(f, "text/html");
  f.close();
}

void handleNotFound() {
  String path = server.uri();
  if (LittleFS.exists(path)) {
    File f = LittleFS.open(path, "r");
    String contentType = "text/plain";
    if (path.endsWith(".html")) contentType = "text/html";
    else if (path.endsWith(".css"))  contentType = "text/css";
    else if (path.endsWith(".js"))   contentType = "application/javascript";
    else if (path.endsWith(".json")) contentType = "application/json";
    else if (path.endsWith(".ico"))  contentType = "image/x-icon";
    else if (path.endsWith(".png"))  contentType = "image/png";
    else if (path.endsWith(".svg"))  contentType = "image/svg+xml";
    server.streamFile(f, contentType);
    f.close();
  } else {
    server.send(404, "text/plain", "File non trovato: " + path);
  }
}

void handleData() {
  String json = "{";
  json += "\"temp\":"      + String(currentTemp, 2) + ",";
  json += "\"hum\":"       + String(currentHum, 2)  + ",";
  json += "\"fanOn\":"     + String(motorOn   ? "true" : "false") + ",";
  json += "\"autoMode\":"  + String(autoMode  ? "true" : "false") + ",";
  json += "\"tempThresh\":"+ String(tempThreshold, 1) + ",";
  json += "\"humThresh\":" + String(humThreshold, 1) + ",";
  json += "\"fanSpeed\":"  + String(fanSpeedPct) + ",";
  json += "\"uptime\":"    + String(totSec) + ",";
  
  // Aggiunta dati gas JSON
  json += "\"mq4_a\":"     + String(mq4_analog) + ",";
  json += "\"mq4_d\":"     + String(mq4_digital) + ",";
  json += "\"mq7_a\":"     + String(mq7_analog) + ",";
  json += "\"mq7_d\":"     + String(mq7_digital) + ",";

  int n     = hist.count;
  int start = (hist.count < HISTORY_SIZE) ? 0 : hist.head;
  #define HIDX(i) ((start + (i)) % HISTORY_SIZE)

  json += "\"history\":{";
  json += "\"ts\":[";
  for (int i = 0; i < n; i++) { if (i) json += ","; json += String(hist.ts[HIDX(i)]); }
  json += "],";
  json += "\"temp\":[";
  for (int i = 0; i < n; i++) { if (i) json += ","; json += String(hist.temp[HIDX(i)], 1); }
  json += "],";
  json += "\"hum\":[";
  for (int i = 0; i < n; i++) { if (i) json += ","; json += String(hist.hum[HIDX(i)], 1); }
  json += "]";
  json += "}}";
  #undef HIDX

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void handleToggleMode() {
  if (server.hasArg("m")) {
    autoMode = (server.arg("m").toInt() == 1);
    if (!autoMode) manualTimer = millis();
  }
  server.send(200, "text/plain", "OK");
}

void handleToggleFan() {
  autoMode   = false;
  manualTimer = millis();
  setFanState(!motorOn, CW);
  server.send(200, "text/plain", "OK");
}

void handleUpdateSettings() {
  if (server.hasArg("t")) tempThreshold = server.arg("t").toFloat();
  if (server.hasArg("h")) humThreshold  = server.arg("h").toFloat();
  if (server.hasArg("s")) {
    fanSpeedPct = server.arg("s").toInt();
    if (motorOn) {
      motorA.rotate(1, fanSpeedPct, direction);
      motorB.rotate(1, fanSpeedPct, direction);
    }
  }
  server.send(200, "text/plain", "OK");
}

// ===================== CONTROLLO AUTOMATICO =====================
void checkAutoControl() {
  if (!autoMode) return;
  
  // PRIORITÀ MASSIMA: Allarme Gas. Se rileva Metano (MQ4) o CO (MQ7) accende le ventole.
  // Tipicamente il pin digitale scende a LOW quando rileva gas.
  if (mq4_digital == LOW || mq7_digital == LOW) {
    setFanState(true, CW); 
    return;
  }

  if (isnan(currentHum) || isnan(currentTemp)) return;

  if (currentHum > humThreshold) {
    setFanState(true, CCW);
    return;
  }
  if (currentTemp > tempThreshold) {
    setFanState(true, CW);
    return;
  }
  setFanState(false);
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);

  // --- Inizializzazione Pin Gas ---
  pinMode(MQ4_AO_PIN, INPUT);
  pinMode(MQ4_DO_PIN, INPUT);
  pinMode(MQ7_AO_PIN, INPUT);
  pinMode(MQ7_DO_PIN, INPUT);

  // --- Timer hardware ---
  timer0 = timerBegin(0, 80, true);
  timerStart(timer0);

  // --- I2C ---
  Wire.begin(SDA_PIN, SCL_PIN);

  // --- OLED ---
  while (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    Serial.println("Display non trovato");
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  // --- Sensori ---
  while (!bmp.begin(0x77)) Serial.println("BMP280 non trovato");
  while (!aht.begin())     Serial.println("AHT20 non trovato");

  // --- GPIO ---
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  // --- Motori ---
  motorA.begin();
  motorB.begin();

  // --- LittleFS ---
  if (!LittleFS.begin(true)) {
    Serial.println("ERRORE: LittleFS mount fallito");
  } else {
    Serial.println("LittleFS montato correttamente");
  }

  // --- WiFi ---
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connessione WiFi...");
  display.display();

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    Serial.print(".");
    tries++;
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connesso: " + WiFi.localIP().toString());
    display.println("WiFi OK");
    display.println(WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi fallito");
    display.println("WiFi fallito");
  }
  display.display();
  delay(2000);

  // --- mDNS ---
  if (MDNS.begin("ambiente")) {
    Serial.println("mDNS avviato: http://ambiente.local");
  }

  // --- Route HTTP ---
  server.on("/",                HTTP_GET, handleRoot);
  server.on("/data",            HTTP_GET, handleData);
  server.on("/toggleMode",      HTTP_GET, handleToggleMode);
  server.on("/toggleFan",       HTTP_GET, handleToggleFan);
  server.on("/updateSettings",  HTTP_GET, handleUpdateSettings);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("Server HTTP avviato sulla porta 80");
}

// ===================== LOOP =====================
void loop() {
  server.handleClient();

  totSec  = timerRead(timer0) / 1000000ULL;
  minutes = totSec / 60;
  seconds = totSec % 60;

  // --- Debounce pulsante fisico ---
  int reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonState) lastDebounceTime = millis();
  if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        autoMode    = false;
        manualTimer = millis();
        setFanState(!motorOn, CW);
      }
    }
  }
  lastButtonState = reading;

  // Ritorno automatico alla modalità AUTO dopo 30 s di manuale
  if (!autoMode && millis() - manualTimer > 30000) autoMode = true;

  // --- Campionamento sensori (1 Hz) ---
  if ((millis() - lastMs) > period) {
    lastMs = millis();

    // Lettura Sensori Gas
    mq4_analog  = analogRead(MQ4_AO_PIN);
    mq4_digital = digitalRead(MQ4_DO_PIN);
    mq7_analog  = analogRead(MQ7_AO_PIN);
    mq7_digital = digitalRead(MQ7_DO_PIN);

    // Lettura Temperatura/Umidita
    float tBmp = bmp.readTemperature();
    sensors_event_t humEv, tempEv;
    aht.getEvent(&humEv, &tempEv);
    float tAht = tempEv.temperature;

    currentTemp = (tBmp + tAht) / 2.0f;
    currentHum  = humEv.relative_humidity;

    histPush(currentTemp, currentHum, motorOn);
    checkAutoControl();

    // --- Aggiornamento OLED ---
    display.clearDisplay();
    display.setCursor(0, 0);
    display.printf("%02d:%02d ", minutes, seconds);
    if (WiFi.status() == WL_CONNECTED)
      display.print(WiFi.localIP().toString());
    display.println();
    display.println("T:" + String(currentTemp, 1) + "C H:" + String(currentHum, 1) + "%");
    
    // Mostro stato Gas (ALRM se LOW)
    display.print("MQ4:"); display.print(mq4_digital == LOW ? "ALRM " : "OK   ");
    display.print("MQ7:"); display.println(mq7_digital == LOW ? "ALRM" : "OK");

    display.print("Fan:"); display.print(motorOn ? "ON " : "OFF ");
    display.print(autoMode ? "[AUTO] " : "[MAN]  ");
    display.println(String(fanSpeedPct) + "%");
    display.display();
  }

  delay(10);
}