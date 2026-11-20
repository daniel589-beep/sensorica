/*
  Proyecto 1: Automatización eléctrica de aulas
  ESP32 DevKit + PIR HC-SR501 + RTC DS3231 + relé (luces) + LED IR (A/C)
*/
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Wire.h>
#include <RTClib.h>
#include <IRremoteESP8266.h>
#include <IRac.h>

// ---------- Configuración ----------
const char* WIFI_SSID = "TU_RED";
const char* WIFI_PASS = "TU_CLAVE";
const char* WEB_USER  = "admin";
const char* WEB_PASS  = "aula1234";

const uint8_t PIN_PIR  = 27;
const uint8_t PIN_RELE = 26;                 // módulo de relé activo en BAJO
const uint8_t PIN_IR   = 4;
const uint8_t RELE_ON = LOW, RELE_OFF = HIGH;

const uint16_t PRE_MIN      = 5;                      // anticipación al horario (min)
const uint32_t T_SIN_MOV_MS = 15UL * 60UL * 1000UL;   // inactividad para apagar (ms)
const uint8_t  TEMP_AC      = 24;                     // consigna del A/C (°C)
const uint8_t  MAX_CLASES   = 8;

// ---------- Objetos y estado ----------
RTC_DS3231 rtc;
WebServer server(80);
Preferences prefs;
IRac ac(PIN_IR);

// dias: bit0 = domingo ... bit6 = sábado; ini y fin en minutos desde las 00:00
struct Clase { uint8_t dias; uint16_t ini; uint16_t fin; };
Clase clases[MAX_CLASES];
uint8_t nClases = 0;

enum Modo { AUTO, FORZ_ON, FORZ_OFF };
Modo modo = AUTO;
bool salidaOn = false, ventanaPrev = false;
uint32_t ultimoMov = 0;

// ---------- Memoria no volátil ----------
void cargarClases() {
  prefs.begin("aula", false);
  nClases = prefs.getUChar("n", 0);
  if (nClases > MAX_CLASES) nClases = 0;
  prefs.getBytes("clases", clases, sizeof(clases));
  prefs.end();
}

void guardarClases() {
  prefs.begin("aula", false);
  prefs.putUChar("n", nClases);
  prefs.putBytes("clases", clases, sizeof(clases));
  prefs.end();
}

// ---------- Salidas: relé de luces e infrarrojo del A/C ----------
void aplicarSalida(bool on) {
  if (on == salidaOn) return;
  salidaOn = on;
  digitalWrite(PIN_RELE, on ? RELE_ON : RELE_OFF);
  ac.next.protocol = decode_type_t::COOLIX;      // cambiar según la marca del A/C
  ac.next.mode     = stdAc::opmode_t::kCool;
  ac.next.celsius  = true;
  ac.next.degrees  = TEMP_AC;
  ac.next.fanspeed = stdAc::fanspeed_t::kAuto;
  ac.next.power    = on;
  ac.sendAc();
  Serial.println(on ? "Salida: ENCENDIDA" : "Salida: APAGADA");
}

// ---------- Lógica: el horario manda para encender, la presencia para apagar ----------
bool dentroDeHorario(const DateTime& t) {
  int m = t.hour() * 60 + t.minute();
  uint8_t dow = t.dayOfTheWeek();                // 0 = domingo
  for (uint8_t i = 0; i < nClases; i++) {
    if (!(clases[i].dias & (1 << dow))) continue;
    if (m >= (int)clases[i].ini - PRE_MIN && m < (int)clases[i].fin) return true;
  }
  return false;
}

void actualizar() {
  uint32_t ahora = millis();
  bool mov = digitalRead(PIN_PIR);
  if (mov) ultimoMov = ahora;

  if (modo == FORZ_ON)  { aplicarSalida(true);  return; }
  if (modo == FORZ_OFF) { aplicarSalida(false); return; }

  bool ventana = dentroDeHorario(rtc.now());
  if (ventana && !ventanaPrev) { ultimoMov = ahora; aplicarSalida(true); }        // arranque programado
  if (!ventana) aplicarSalida(false);                                             // fin del horario
  else if (salidaOn && ahora - ultimoMov > T_SIN_MOV_MS) aplicarSalida(false);    // inactividad
  else if (!salidaOn && mov) aplicarSalida(true);                                 // reingreso en horario
  ventanaPrev = ventana;
}

// ---------- Página web ----------
String hhmm(uint16_t m) {
  char b[6];
  snprintf(b, sizeof(b), "%02u:%02u", m / 60, m % 60);
  return String(b);
}

bool autorizado() {
  if (server.authenticate(WEB_USER, WEB_PASS)) return true;
  server.requestAuthentication();
  return false;
}

void volver() {
  server.sendHeader("Location", "/");
  server.send(303);
}

void paginaPrincipal() {
  if (!autorizado()) return;
  DateTime t = rtc.now();
  const char* dia[7] = {"D", "L", "M", "X", "J", "V", "S"};
  String h = F("<!doctype html><html><head><meta charset='utf-8'>"
               "<meta name='viewport' content='width=device-width,initial-scale=1'><title>Aula 1</title>"
               "<style>body{font-family:sans-serif;max-width:520px;margin:auto;padding:12px}"
               "button,input{font-size:1rem;margin:2px}table{width:100%;border-collapse:collapse}"
               "td{border-bottom:1px solid #ccc;padding:4px}</style></head><body><h2>Control del aula</h2>");
  h += "<p>Hora: " + hhmm(t.hour() * 60 + t.minute()) + " | Modo: ";
  h += (modo == AUTO ? "AUTO" : modo == FORZ_ON ? "FORZADO ON" : "FORZADO OFF");
  h += " | Salida: ";
  h += (salidaOn ? "ENCENDIDA" : "APAGADA");
  h += "</p>";
  h += F("<p><a href='/modo?m=auto'><button>Auto</button></a>"
         "<a href='/modo?m=on'><button>Forzar ON</button></a>"
         "<a href='/modo?m=off'><button>Forzar OFF</button></a></p><h3>Horarios</h3><table>");
  for (uint8_t i = 0; i < nClases; i++) {
    h += "<tr><td>";
    for (uint8_t d = 0; d < 7; d++) if (clases[i].dias & (1 << d)) { h += dia[d]; h += ' '; }
    h += "</td><td>" + hhmm(clases[i].ini) + " - " + hhmm(clases[i].fin) +
         "</td><td><a href='/del?i=" + String(i) + "'>Borrar</a></td></tr>";
  }
  h += F("</table><h3>Nueva clase</h3><form method='post' action='/add'>");
  for (uint8_t d = 0; d < 7; d++) h += "<label><input type='checkbox' name='d" + String(d) + "'>" + dia[d] + "</label> ";
  h += F("<br>Inicio <input type='time' name='ini' required> Fin <input type='time' name='fin' required>"
         "<br><button>Agregar</button></form></body></html>");
  server.send(200, "text/html", h);
}

void agregarClase() {
  if (!autorizado()) return;
  if (nClases < MAX_CLASES && server.hasArg("ini") && server.hasArg("fin")) {
    uint8_t d = 0;
    for (uint8_t i = 0; i < 7; i++) if (server.hasArg("d" + String(i))) d |= (1 << i);
    String a = server.arg("ini"), b = server.arg("fin");
    uint16_t ini = a.substring(0, 2).toInt() * 60 + a.substring(3, 5).toInt();
    uint16_t fin = b.substring(0, 2).toInt() * 60 + b.substring(3, 5).toInt();
    if (d && fin > ini) { clases[nClases++] = {d, ini, fin}; guardarClases(); }
  }
  volver();
}

void borrarClase() {
  if (!autorizado()) return;
  int i = server.arg("i").toInt();
  if (i >= 0 && i < nClases) {
    for (int k = i; k < nClases - 1; k++) clases[k] = clases[k + 1];
    nClases--;
    guardarClases();
  }
  volver();
}

void cambiarModo() {
  if (!autorizado()) return;
  String m = server.arg("m");
  if (m == "on") modo = FORZ_ON;
  else if (m == "off") modo = FORZ_OFF;
  else modo = AUTO;
  actualizar();
  volver();
}

// ---------- Arranque y ciclo principal ----------
void setup() {
  Serial.begin(115200);
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_RELE, OUTPUT);
  digitalWrite(PIN_RELE, RELE_OFF);

  Wire.begin(21, 22);
  if (!rtc.begin()) { Serial.println("RTC no encontrado"); while (true) delay(1000); }
  if (rtc.lostPower()) rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));   // pone la hora la primera vez

  cargarClases();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print('.'); }
  Serial.println();
  Serial.println(WiFi.localIP());
  if (MDNS.begin("aula1")) Serial.println("http://aula1.local");

  server.on("/", paginaPrincipal);
  server.on("/add", HTTP_POST, agregarClase);
  server.on("/del", borrarClase);
  server.on("/modo", cambiarModo);
  server.begin();
}

void loop() {
  server.handleClient();
  static uint32_t t0 = 0;
  if (millis() - t0 >= 500) { t0 = millis(); actualizar(); }
}
