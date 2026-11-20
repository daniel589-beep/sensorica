# Automatización eléctrica de aulas y monitoreo de la calidad del aire con nodos IoT basados en ESP32

Proyecto de la asignatura **Sensórica para Ingeniería** – Ingeniería Electrónica, Universidad Surcolombiana (Neiva, Huila, Colombia).

**Autores:** Emerson Felipe Barrera Ramos, Dominy Melandri Cadena Soto, Daniel Stiven Olarte Fernández, Jerónimo Pérez Pérez y Diego Alejandro Rodríguez Lemos.

Dos nodos ESP32, uno por proyecto, montados en protoboard:

| Proyecto | Qué resuelve | Carpeta |
|---|---|---|
| 1. Automatización eléctrica | Enciende luces y A/C según el horario de clases (RTC DS3231) y los apaga por ausencia de movimiento (PIR HC-SR501). Página web en la propia ESP32 para programar horarios. | [`proyecto1_aulas/`](proyecto1_aulas) |
| 2. Calidad del aire | Mide eCO₂/TVOC (SGP30) y temperatura/humedad (AHT20), clasifica el aire con un semáforo, activa un ventilador con histéresis y registra cada lectura en Supabase. | [`proyecto2_aire/`](proyecto2_aire) |

El informe completo en formato IEEE está en [`docs/Informe_IEEE.pdf`](docs/Informe_IEEE.pdf) (editable: [`.docx`](docs/Informe_IEEE.docx)).

## Estructura del repositorio

```
.
├── proyecto1_aulas/
│   └── proyecto1_aulas.ino      # Código del Proyecto 1
├── proyecto2_aire/
│   ├── proyecto2_aire.ino       # Código del Proyecto 2
│   └── supabase_schema.sql      # Tabla y políticas para Supabase
├── docs/
│   ├── Informe_IEEE.pdf / .docx # Informe
│   └── figuras/                 # Arquitectura y curvas características
└── README.md
```

## Requisitos

- Placa **ESP32 DevKit** (30 pines) y **Arduino IDE 2** con el soporte de placas ESP32 de Espressif instalado (Gestor de tarjetas → *esp32*). Placa seleccionada: **ESP32 Dev Module**, monitor serie a **115200 baud**.
- Red Wi-Fi de **2.4 GHz**.
- Para el Proyecto 2: cuenta gratuita en [Supabase](https://supabase.com).
- Multímetro y, para validar el SGP30, un medidor de CO₂ de referencia (recomendado).

> **Seguridad eléctrica:** las pruebas se hacen con una carga de baja tensión (bombillo LED de prueba). La conmutación de la red de 120 V debe hacerla personal calificado, con un relé de capacidad adecuada y protección.

## Proyecto 1 – Luces y A/C por horario y presencia

**Hardware:** ESP32, PIR HC-SR501, RTC DS3231 con batería CR2032, módulo relé de 5 V con optoacoplador, LED infrarrojo 940 nm + transistor 2N2222 + resistencias de 1 kΩ y 47 Ω, bombillo LED de prueba, fuente de 5 V / 2 A.

**Conexiones**

| Señal | Dispositivo | ESP32 |
|---|---|---|
| SDA, SCL | RTC DS3231 (0x68) | GPIO21, GPIO22 (3.3 V) |
| OUT | PIR HC-SR501 | GPIO27 (alimentado a 5 V desde VIN) |
| IN | Módulo relé (luces), activo en bajo | GPIO26 |
| Base (vía 1 kΩ) | 2N2222 que maneja el LED IR (ánodo a 5 V con 47 Ω) | GPIO4 |

**Librerías (Gestor de librerías):** `RTClib` (Adafruit) e `IRremoteESP8266`.

**Pasos**

1. Monta la ESP32 en el centro de la protoboard y distribuye los rieles de 5 V y 3.3 V.
2. Conecta el RTC por I²C y confirma la dirección `0x68` con un escáner I²C.
3. Conecta el PIR: sensibilidad media, retardo al mínimo, jumper en modo repetible; espera 1 min de calentamiento.
4. Conecta el relé y, como carga de práctica, un bombillo LED de baja tensión en los bornes COM y NO.
5. Arma el transmisor infrarrojo apuntando al receptor del A/C.
6. Abre `proyecto1_aulas/proyecto1_aulas.ino` y cambia `WIFI_SSID`, `WIFI_PASS`, `WEB_USER` y `WEB_PASS`. El código usa el protocolo **COOLIX** como ejemplo: para otra marca de A/C cambia `ac.next.protocol` (si no la conoces, captura el control remoto con un receptor VS1838B y el ejemplo `IRrecvDumpV2`).
7. Carga el programa, abre el monitor serie y verifica la conexión Wi-Fi.
8. Entra a `http://aula1.local` (o a la IP que muestra el monitor serie) desde un celular o PC de la misma red e inicia sesión con el usuario y clave definidos.
9. Programa una clase desde la página y verifica arranque, presencia, apagado por inactividad y forzado manual. En banco puedes reducir `T_SIN_MOV_MS` a 1 min para acortar la prueba.

**Comportamiento:** el horario manda para encender (5 min antes de la clase) y la presencia manda para apagar (15 min sin movimiento). Los horarios se guardan en memoria no volátil y sobreviven a cortes de energía.

**Extremos HTTP (puerto 80, autenticación Basic, solo red local)**

| Método y ruta | Función |
|---|---|
| `GET /` | Estado, hora, lista de horarios y formulario |
| `POST /add` | Agrega una clase (días, inicio, fin) |
| `GET /del?i=N` | Elimina la clase N |
| `GET /modo?m=auto\|on\|off` | Modo automático o forzado |

## Proyecto 2 – Monitor de calidad del aire

**Hardware:** ESP32, SGP30, AHT20, OLED SSD1306 128×64 (I²C), PIR HC-SR501, 3 LEDs (verde, amarillo, rojo) con 330 Ω, buzzer activo de 5 V, módulo relé de 5 V + ventilador de 5 V.

**Conexiones**

| Señal | Dispositivo | ESP32 |
|---|---|---|
| SDA, SCL | SGP30 (0x58), AHT20 (0x38), OLED (0x3C) | GPIO21, GPIO22 (3.3 V) |
| OUT | PIR HC-SR501 | GPIO27 |
| Verde, amarillo, rojo (con 330 Ω) | LEDs del semáforo | GPIO25, GPIO33, GPIO32 |
| Señal | Buzzer activo | GPIO14 |
| IN | Módulo relé del ventilador, activo en bajo | GPIO26 |

**Librerías:** `Adafruit SGP30`, `Adafruit AHTX0`, `Adafruit SSD1306` y `Adafruit GFX`.

**Pasos**

1. Monta los tres dispositivos I²C en el mismo bus y comprueba con un escáner que aparecen `0x38`, `0x3C` y `0x58`.
2. Conecta LEDs, buzzer, PIR y relé del ventilador según la tabla.
3. En Supabase crea un proyecto y, en el editor SQL, ejecuta [`proyecto2_aire/supabase_schema.sql`](proyecto2_aire/supabase_schema.sql) (crea la tabla `lecturas` y las políticas de acceso).
4. En `proyecto2_aire.ino` cambia `WIFI_SSID`, `WIFI_PASS`, `SUPABASE_URL` (`https://TU_PROYECTO.supabase.co/rest/v1/lecturas`), `SUPABASE_KEY` (clave *anon*) y, si quieres, `AULA`.
5. Carga el programa. Deja el SGP30 encendido unas **12 h (burn-in)** antes de las mediciones definitivas; durante los primeros 15 s reporta 400 ppm.
6. Prueba acercando el aliento al sensor: el semáforo pasa de verde a amarillo y rojo, el ventilador enciende y, con presencia detectada, suena la alerta.
7. Contrasta el eCO₂ con un medidor de CO₂ de referencia y ajusta los umbrales si hace falta.
8. Verifica en Supabase que llega una fila nueva cada 30 s con aula, valores, estado y hora.

**Umbrales:** verde < 800 ppm, amarillo 800–1200 ppm, rojo > 1200 ppm. El ventilador enciende sobre 1200 ppm y apaga bajo 800 ppm (histéresis).

## Pruebas funcionales (criterios de aceptación)

| Prueba | Criterio |
|---|---|
| P1: arranque programado | Luces y A/C encienden 5 min antes de la clase |
| P1: apagado por inactividad | Salida apagada a los 15 min del último movimiento |
| P1: reingreso | Vuelve a encender si hay movimiento dentro del horario |
| P1: forzado web | Encendido o apagado inmediato sin importar el horario |
| P1: persistencia | Los horarios se conservan tras cortar la alimentación |
| P2: semáforo | Verde < 800, amarillo 800–1200, rojo > 1200 ppm |
| P2: ventilación | Relé encendido sobre 1200 ppm y apagado bajo 800 ppm |
| P2: alerta | Buzzer solo con estado rojo y presencia detectada |
| P2: registro | Una fila nueva cada 30 s en Supabase |

## Limitaciones y seguridad

- El PIR no detecta personas quietas; mejora prevista: radar mmWave LD2410.
- El SGP30 **estima** el eCO₂ (no lo mide directamente): calíbralo contra un medidor real.
- HTTP Basic no cifra las credenciales (usar solo en red local). `setInsecure()` no valida el certificado de Supabase: en producción usa `setCACert()`.
- **No subas a GitHub tus credenciales reales** (Wi-Fi, claves de Supabase). Los archivos de este repositorio traen valores de ejemplo (`TU_RED`, `TU_CLAVE`, …).
- Las políticas SQL de ejemplo permiten leer e insertar con la clave *anon*; restríngelas si el proyecto sale del laboratorio.

## Referencias principales

ESP32 (Espressif), SGP30 (Sensirion), AHT20 (Aosong), DS3231 (Maxim), HC-SR501, especificación I²C (NXP UM10204), librería IRremoteESP8266, API REST de Supabase y ASHRAE 62.1. La lista completa está en el informe.
