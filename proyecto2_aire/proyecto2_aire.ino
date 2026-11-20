/*
  Proyecto 2: Monitor de calidad del aire
  ESP32 + SGP30 + AHT20 + OLED SSD1306 + LEDs + buzzer + relé (ventilador) + PIR
*/
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_SGP30.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------- Configuración ----------
const char* WIFI_SSID    = "TU_RED";
const char* WIFI_PASS    = "TU_CLAVE";
const char* SUPABASE_URL = "https://TU_PROYECTO.supabase.co/rest/v1/lecturas";
const char* SUPABASE_KEY = "TU_CLAVE_ANON";
const char* AULA         = "Lab 2";

const uint8_t PIN_PIR = 27, PIN_LED_V = 25, PIN_LED_A = 33, PIN_LED_R = 32;
const uint8_t PIN_BUZZER = 14, PIN_RELE = 26;
const uint8_t RELE_ON = LOW, RELE_OFF = HIGH;           // módulo de relé activo en BAJO

const uint16_t ECO2_BUENO = 800;                        // ppm: por debajo, aire bueno
const uint16_t ECO2_MALO  = 1200;                       // ppm: por encima, aire malo
const uint32_t PERIODO_ENVIO_MS = 30000;                // una fila cada 30 s
const uint32_t T_OCUPADO_MS = 5UL * 60UL * 1000UL;      // presencia tras el último movimiento

// ---------- Objetos y estado ----------
Adafruit_SGP30 sgp;
Adafruit_AHTX0 aht;
Adafruit_SSD1306 oled(128, 64, &Wire, -1);

enum Estado { VERDE, AMARILLO, ROJO };
const char* NOMBRE[] = {"verde", "amarillo", "rojo"};
Estado estadoActual = VERDE;
bool ventiladorOn = false;
uint32_t ultimoMov = 0, ultimoEnvio = 0, ultimaLectura = 0;

// Humedad absoluta en mg/m^3 para compensar el SGP30
uint32_t humedadAbsoluta(float tC, float rh) {
  float ah = 216.7f * ((rh / 100.0f) * 6.112f * expf(17.62f * tC / (243.12f + tC)) / (273.15f + tC));
  return (uint32_t)(1000.0f * ah);
}

Estado clasificar(uint16_t eco2) {
  if (eco2 < ECO2_BUENO) return VERDE;
  if (eco2 <= ECO2_MALO) return AMARILLO;
  return ROJO;
}

void mostrar(uint16_t eco2, uint16_t tvoc, float t, float h, Estado e) {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(2);
  oled.setCursor(0, 0);
  oled.print(eco2);
  oled.print(" ppm");
  oled.setTextSize(1);
  oled.setCursor(0, 24);
  oled.printf("TVOC: %u ppb", tvoc);
  oled.setCursor(0, 36);
  oled.printf("%.1f C  %.0f %%", t, h);
  oled.setCursor(0, 50);
  oled.print("Aire: ");
  oled.print(NOMBRE[e]);
  if (ventiladorOn) oled.print("  VENT");
  oled.display();
}

// POST HTTPS a la API REST de Supabase
void enviarSupabase(uint16_t eco2, uint16_t tvoc, float t, float h, const char* estado, bool ocupado) {
  if (WiFi.status() != WL_CONNECTED) { WiFi.reconnect(); return; }
  WiFiClientSecure cliente;
  cliente.setInsecure();                                // laboratorio; en producción usar setCACert()
  HTTPClient http;
  if (!http.begin(cliente, SUPABASE_URL)) return;
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Prefer", "return=minimal");

  char cuerpo[240];
  snprintf(cuerpo, sizeof(cuerpo),
           R"({"aula":"%s","eco2_ppm":%u,"tvoc_ppb":%u,"temp_c":%.1f,"humedad_pct":%.1f,"estado":"%s","ocupado":%s})",
           AULA, eco2, tvoc, t, h, estado, ocupado ? "true" : "false");
  int codigo = http.POST(String(cuerpo));
  Serial.print("Supabase HTTP ");
  Serial.println(codigo);
  http.end();
}

void detener(const char* msg) {
  Serial.println(msg);
  while (true) delay(1000);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_LED_V, OUTPUT);
  pinMode(PIN_LED_A, OUTPUT);
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_RELE, OUTPUT);
  digitalWrite(PIN_RELE, RELE_OFF);

  Wire.begin(21, 22);
  if (!aht.begin()) detener("AHT20 no encontrado");
  if (!sgp.begin()) detener("SGP30 no encontrado");
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) detener("OLED no encontrada");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(300);
}

void loop() {
  uint32_t ahora = millis();
  if (digitalRead(PIN_PIR)) ultimoMov = ahora;
  bool ocupado = ultimoMov != 0 && (ahora - ultimoMov) < T_OCUPADO_MS;

  if (ahora - ultimaLectura >= 1000) {                  // el SGP30 exige una medición por segundo
    ultimaLectura = ahora;

    sensors_event_t hum, tmp;
    aht.getEvent(&hum, &tmp);
    sgp.setHumidity(humedadAbsoluta(tmp.temperature, hum.relative_humidity));
    if (!sgp.IAQmeasure()) { Serial.println("Error de lectura SGP30"); return; }

    estadoActual = clasificar(sgp.eCO2);

    // Histéresis: el ventilador enciende sobre 1200 ppm y apaga bajo 800 ppm
    if (sgp.eCO2 > ECO2_MALO) ventiladorOn = true;
    else if (sgp.eCO2 < ECO2_BUENO) ventiladorOn = false;

    digitalWrite(PIN_LED_V, estadoActual == VERDE);
    digitalWrite(PIN_LED_A, estadoActual == AMARILLO);
    digitalWrite(PIN_LED_R, estadoActual == ROJO);
    digitalWrite(PIN_RELE, ventiladorOn ? RELE_ON : RELE_OFF);
    mostrar(sgp.eCO2, sgp.TVOC, tmp.temperature, hum.relative_humidity, estadoActual);

    if (ahora - ultimoEnvio >= PERIODO_ENVIO_MS) {
      ultimoEnvio = ahora;
      enviarSupabase(sgp.eCO2, sgp.TVOC, tmp.temperature, hum.relative_humidity, NOMBRE[estadoActual], ocupado);
    }
  }

  // Alerta sonora intermitente solo con aire malo y presencia
  bool alerta = (estadoActual == ROJO) && ocupado;
  digitalWrite(PIN_BUZZER, alerta && ((ahora / 500) % 2));
}
