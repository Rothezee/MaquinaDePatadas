#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_NeoPixel.h>
#include "AnimPatea.h"
#include <EEPROM.h>

// ==========================================
// CONFIGURACIÓN DE PINES
// ==========================================
#define PIN_S_LISTO     2   // Sensor bajo: corte al bajar; LIBRE al patear (inicio cronómetro)
#define PIN_S_DESTINO   4   // Sensor alto reposo: CORTADO con pelota arriba (fin cronómetro)
#define PIN_MONEDERO    32   // Pulso de moneda (activo LOW)

#define PIN_BOBINA      22   // Relé/bobina: suelta la pelota (reposo pin4 → pin2)
#define PIN_NEO_PANEL   25   // 10×8×8 en cadena = 640 px (40×16), DATA único

#define BTNStart        13   // Confirma dificultad; tras confirmar, suelta la pelota
#define BTNDificultad   14   // Cicla FACIL/MEDIO/CRACK (juego); en menú técnico baja valor
#define BTNConfig       12   // Entra al menú y cambia de pestaña
#define BTNUp           15   // Sube el valor en el menú

// Nivel en el pin cuando el haz está CORTADO. Ajustá si no coincide con tu sensor.
#define NIVEL_CORTE_LISTO     LOW
#define NIVEL_CORTE_DESTINO   LOW    // mismo criterio que listo si los sensores son iguales

#define SENSOR_ACTIVO   LOW
#define SENSOR_LIBRE    HIGH

// --- PANEL USUARIO: 10 módulos 8×8 en cadena (40×16 = 640 px). Animación / show. ---
#define NUMPIXELS_PANEL   (PATEA_W * PATEA_H)
#define BRIGHTNESS        15

#define BOBINA_MS         500
#define BOBINA_ACTIVA_LOW 0    // 1 si el relé enciende con LOW

// ==========================================
// TIEMPOS Y LÍMITES
// ==========================================
const unsigned long TIMEOUT_VUELO_MS     = 2500; // Pelota pateada que no llega
const unsigned long TIMEOUT_DESTINO_MS   = 8000; // Destino sigue ocupado tras el gol
const unsigned long DEBOUNCE_LISTO_MS    = 40;   // Evita rebotes al colocar la pelota
const unsigned long COOLDOWN_MS          = 3000; // Pausa entre jugadas válidas
const unsigned long TIMEOUT_BAJADA_MS    = 15000; // Pelota soltada y pin 2 no corta
const unsigned long DEBOUNCE_ISR_US      = 2500;  // Anti-rebote en interrupciones
const unsigned long DEBOUNCE_COIN_US     = 20000; // Anti-rebote del pulso de moneda
const float         VEL_MAX_MS           = 50.0;  // ~180 km/h: por encima es ruido
const float         VEL_MIN_MS           = 0.4;  // Demasiado lenta para ser una patada

#define DEBUG_SENSORES 1   // 1 = mensajes Serial en cada corte/libre de sensores

// ==========================================
// OBJETOS
// ==========================================
// LCD 16×2: solo técnicos (créditos, dificultad, estado, menú). El jugador mira el panel LED.
LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_NeoPixel panel(NUMPIXELS_PANEL, PIN_NEO_PANEL, NEO_GRB + NEO_KHZ800);

#define CRIT_ENTER() noInterrupts()
#define CRIT_EXIT() interrupts()
#define ISR_MARK

// ==========================================
// ESTADOS DEL JUEGO
// ==========================================
enum EstadoJuego {
  ESPERANDO_PELOTA,     // Reposo: pelota arriba tapando pin 4; moneda + START
  LISTO_PARA_PATEAR,    // Pelota abajo (pin 2 cortado): listo para patear
  EN_VUELO,             // Cronómetro: desde LIBRE pin 2 hasta CORTE pin 4
  GOL_DETECTADO,        // Pin 4 cortado tras patada: calcular puntaje
  ESPERANDO_LIBERAR,    // Tras jugada: esperando que vuelva a tapar pin 4 (reposo)
  COOLDOWN              // Pausa tras jugada válida (show / LEDs)
};

const char* nombreEstado(EstadoJuego e) {
  switch (e) {
    case ESPERANDO_PELOTA:  return "ESPERANDO_PELOTA";
    case LISTO_PARA_PATEAR: return "LISTO_PARA_PATEAR";
    case EN_VUELO:          return "EN_VUELO";
    case GOL_DETECTADO:     return "GOL_DETECTADO";
    case ESPERANDO_LIBERAR: return "ESPERANDO_LIBERAR";
    case COOLDOWN:          return "COOLDOWN";
    default:                return "DESCONOCIDO";
  }
}

// ==========================================
// VARIABLES
// ==========================================
float factorDificultad = 0.5;
float distanciaSensoresM = 1.00; // Distancia entre sensor 2 y sensor 4 (trayecto medido)

// EEPROM (Mega y demás AVR): byte 0 = magic, 4 y 8 = floats
static const uint8_t EEPROM_MAGIC = 0xA7;
static const int EEPROM_ADDR_MAGIC = 0;
static const int EEPROM_ADDR_DIF = 4;
static const int EEPROM_ADDR_DIST = 8;

void configCargar() {
  if (EEPROM.read(EEPROM_ADDR_MAGIC) != EEPROM_MAGIC) {
    factorDificultad = 0.5f;
    distanciaSensoresM = 1.00f;
    return;
  }
  EEPROM.get(EEPROM_ADDR_DIF, factorDificultad);
  EEPROM.get(EEPROM_ADDR_DIST, distanciaSensoresM);
  if (factorDificultad < 0.1f || factorDificultad > 5.0f) factorDificultad = 0.5f;
  if (distanciaSensoresM < 0.10f || distanciaSensoresM > 5.0f) distanciaSensoresM = 1.00f;
}

void configGuardar() {
  EEPROM.update(EEPROM_ADDR_MAGIC, EEPROM_MAGIC);
  EEPROM.put(EEPROM_ADDR_DIF, factorDificultad);
  EEPROM.put(EEPROM_ADDR_DIST, distanciaSensoresM);
}

int ultimoPuntaje = 0;

volatile EstadoJuego estado = ESPERANDO_PELOTA;
volatile unsigned long t_inicio = 0;
volatile unsigned long t_fin = 0;
volatile bool evento_listo_ocupado = false;
volatile bool evento_listo_libre = false;
volatile bool evento_destino_fuera_secuencia = false;
volatile bool juegoArmado = false;          // Pelota abajo + crédito (o reintento)
volatile bool partidaEnCurso = false;       // Ya se descontó moneda; permite reintentar
volatile uint32_t pulsosMonedero = 0;
volatile unsigned long ultimo_isr_listo_us = 0;
volatile unsigned long ultimo_isr_destino_us = 0;
volatile unsigned long ultimo_isr_coin_us = 0;

#if DEBUG_SENSORES
volatile uint8_t dbg_listo_edge = 0;     // 1 = corte, 2 = libre
volatile uint8_t dbg_destino_edge = 0;   // 1 = corte
volatile unsigned long dbg_listo_us = 0;
volatile unsigned long dbg_destino_us = 0;
#endif

int creditos = 0;

EstadoJuego estadoMostrado = ESPERANDO_PELOTA;
unsigned long t_estado_ms = 0;
unsigned long t_debounce_listo_ms = 0;
unsigned long t_cooldown_hasta = 0;
unsigned long t_esperando_liberar_ms = 0;
bool pendiente_confirmar_listo = false;
bool pelotaSoltada = false;       // Tras START: la pelota está bajando
unsigned long t_pelota_soltada_ms = 0;
bool dificultadConfirmada = false;
uint8_t nivelDificultad = 0;      // 0=FACIL, 1=MEDIO, 2=CRACK
char ultimoError[17] = "";

static const float FACTOR_POR_NIVEL[3] = {0.45f, 0.65f, 1.0f};
static const char* NOMBRE_NIVEL[3] = {"FACIL", "MEDIO", "CRACK"};

void aplicarNivelDificultad() {
  factorDificultad = FACTOR_POR_NIVEL[nivelDificultad];
}

void resetSeleccionDificultad() {
  dificultadConfirmada = false;
  nivelDificultad = 0;
  aplicarNivelDificultad();
}

// Crédito en cola o jugada pagada en curso (reintento sin otra moneda).
inline bool sesionJuegoActiva() {
  return creditos > 0 || partidaEnCurso;
}

void normalizarModoAtraccion();
void actualizarArmadoJuego();
void atenderMonedero();  // Siempre activo (loop, menú, show)
void atenderStart();
void atenderCambiarDificultad();
void depurarSensores();
void pulsarBobina();
void mostrarLCD(const char* linea1, const char* linea2);
void mostrarPuntajeLCD(int puntaje, float velocidadMs);
void iniciarShowResultado(int puntaje);
void actualizarPanelUsuario();
bool showResultadoActivo();

// ==========================================
// PANEL USUARIO 40×16 — fases de AnimPatea.h
// ==========================================
int pixelShow(int x, int y) {
  if (x < 0 || x >= PATEA_W || y < 0 || y >= PATEA_H) return 0;
  if (y % 2 == 0) return y * PATEA_W + x;
  return y * PATEA_W + (PATEA_W - 1 - x);
}

static PateaEstado panelAnim = {PATEA_COIN, 0, 0, 1, 70, 0, 0};
static bool showResultado = false;
static unsigned long t_panel_ms = 0;

bool showResultadoActivo() {
  return showResultado;
}

static void siguienteFaseResultado() {
  if (panelAnim.fase == PATEA_RES) {
    panelAnim.fase = (panelAnim.total >= pateaMeta(panelAnim.dif)) ? PATEA_WIN1 : PATEA_LOSE1;
  } else if (panelAnim.fase == PATEA_WIN1) {
    panelAnim.fase = PATEA_WIN2;
  } else if (panelAnim.fase == PATEA_LOSE1) {
    panelAnim.fase = PATEA_LOSE2;
  } else if (panelAnim.fase == PATEA_LOSE2) {
    panelAnim.fase = PATEA_BURLA;
  } else {
    showResultado = false;
    panelAnim.fase = PATEA_COIN;
  }
  panelAnim.frame = 0;
}

void iniciarShowResultado(int puntaje) {
  uint16_t v = pateaEscalarPuntaje(puntaje);
  panelAnim.fase = PATEA_RES;
  panelAnim.frame = 0;
  panelAnim.dif = (nivelDificultad > 2) ? 2 : nivelDificultad;
  panelAnim.patada = 1;
  panelAnim.fuerza = (v > 100) ? 100 : (uint8_t)v;
  panelAnim.puntos = v;
  panelAnim.total = v;
  showResultado = true;
  Serial.print("Panel resultado (escala 0-300): ");
  Serial.print(v);
  Serial.print(" meta ");
  Serial.println(pateaMeta(panelAnim.dif));
}

void actualizarPanelUsuario() {
  if (millis() - t_panel_ms < 83) return;
  t_panel_ms = millis();

  bool faseNueva = false;
  if (!showResultado) {
    EstadoJuego e;
    CRIT_ENTER();
    e = estado;
    CRIT_EXIT();

    uint8_t want = PATEA_COIN;
    if (sesionJuegoActiva()) {
      if (e == EN_VUELO) want = PATEA_FLY;
      else if (e == LISTO_PARA_PATEAR || (e == ESPERANDO_PELOTA && pelotaSoltada)) want = PATEA_ASK;
      else if (e == ESPERANDO_PELOTA || e == ESPERANDO_LIBERAR) want = PATEA_DIFF;
    }
    if (panelAnim.fase != want) {
      panelAnim.fase = want;
      panelAnim.frame = 0;
      faseNueva = true;
    }
    panelAnim.dif = (nivelDificultad > 2) ? 2 : nivelDificultad;
    panelAnim.patada = 1;
    if (want == PATEA_FLY && faseNueva) panelAnim.fuerza = 70;
  }

  pateaRender(panel, pixelShow, panelAnim);
  panel.show();

  if (showResultado) {
    uint16_t n = pateaFrames(panelAnim.fase);
    if (n && (uint16_t)(panelAnim.frame + 1) >= n) siguienteFaseResultado();
    else panelAnim.frame++;
  } else {
    uint16_t n = pateaFrames(panelAnim.fase);
    if (n == 0) {
      panelAnim.frame++;
      if (panelAnim.frame > 4000) panelAnim.frame = 0;
    } else if ((uint16_t)(panelAnim.frame + 1) < n) {
      panelAnim.frame++;
    }
  }
}

void pulsarBobina() {
#if BOBINA_ACTIVA_LOW
  digitalWrite(PIN_BOBINA, LOW);
  delay(BOBINA_MS);
  digitalWrite(PIN_BOBINA, HIGH);
#else
  digitalWrite(PIN_BOBINA, HIGH);
  delay(BOBINA_MS);
  digitalWrite(PIN_BOBINA, LOW);
#endif
}

void mostrarPuntajeLCD(int puntaje, float velocidadMs) {
  char l1[17], l2[17];
  snprintf(l1, sizeof(l1), "DBG Pts:%d", puntaje);
  snprintf(l2, sizeof(l2), "V:%d.%02d", (int)velocidadMs, (int)(velocidadMs * 100) % 100);
  mostrarLCD(l1, l2);
}

// ==========================================
// LCD técnico (no es UI del jugador; ver panel 40×16)
// ==========================================
void mostrarLCD(const char* linea1, const char* linea2) {
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(linea1);
  lcd.setCursor(0, 1);
  lcd.print(linea2);
}

const char* codigoEstadoTec(EstadoJuego e) {
  switch (e) {
    case ESPERANDO_PELOTA:  return "ESP_PEL";
    case LISTO_PARA_PATEAR: return "LISTO";
    case EN_VUELO:          return "VUELO";
    case GOL_DETECTADO:     return "GOL";
    case ESPERANDO_LIBERAR: return "LIB_PIN4";
    case COOLDOWN:          return "COOLDOWN";
    default:                return "?";
  }
}

void actualizarLCDEstado() {
  char l1[17], l2[17];

  snprintf(l1, sizeof(l1), "C:%d%c %s%s", creditos, partidaEnCurso ? '+' : ' ',
           NOMBRE_NIVEL[nivelDificultad], dificultadConfirmada ? "*" : "");

  if (ultimoError[0] != '\0') {
    snprintf(l2, sizeof(l2), "%s", ultimoError);
  } else {
    snprintf(l2, sizeof(l2), "%s", codigoEstadoTec(estado));
    if (pelotaSoltada) {
      strncat(l2, " BAJ", sizeof(l2) - strlen(l2) - 1);
    }
    if (estado == ESPERANDO_PELOTA && !sensorDestinoActivo()) {
      strncat(l2, " P4!", sizeof(l2) - strlen(l2) - 1);
    }
    if (estado == COOLDOWN || (estado == ESPERANDO_PELOTA && ultimoPuntaje > 0 && !sesionJuegoActiva())) {
      char extra[8];
      snprintf(extra, sizeof(extra), " P:%d", ultimoPuntaje);
      strncat(l2, extra, sizeof(l2) - strlen(l2) - 1);
    }
  }

  mostrarLCD(l1, l2);
  estadoMostrado = estado;
}

void cambiarEstado(EstadoJuego nuevo) {
  CRIT_ENTER();
  estado = nuevo;
  CRIT_EXIT();
  t_estado_ms = millis();
  Serial.print(">> Estado: ");
  Serial.println(nombreEstado(nuevo));
  actualizarLCDEstado();
  actualizarArmadoJuego();
}

void registrarError(const char* mensajeLcd, const char* detalleSerial) {
  strncpy(ultimoError, mensajeLcd, 16);
  ultimoError[16] = '\0';
  Serial.print("EXCEPCION: ");
  Serial.println(detalleSerial);
}

void limpiarMedicion() {
  CRIT_ENTER();
  t_inicio = 0;
  t_fin = 0;
  evento_listo_ocupado = false;
  evento_listo_libre = false;
  CRIT_EXIT();
  pendiente_confirmar_listo = false;
}

void recuperarTrasError(const char* mensajeLcd, const char* detalleSerial) {
  registrarError(mensajeLcd, detalleSerial);
  limpiarMedicion();
  pelotaSoltada = false;
  CRIT_ENTER();
  juegoArmado = false;
  CRIT_EXIT();
  // partidaEnCurso se mantiene: ya pagó, puede reintentar sin otra moneda
  if (sensorDestinoActivo()) {
    cambiarEstado(ESPERANDO_PELOTA);
  } else {
    t_esperando_liberar_ms = millis();
    cambiarEstado(ESPERANDO_LIBERAR);
  }
}

bool pinEsCorte(int pin, int nivelCorte) {
  return digitalRead(pin) == nivelCorte;
}

bool sensorLeeCorteDebounced(int pin, int nivelCorte) {
  uint8_t cortes = 0;
  for (uint8_t i = 0; i < 5; i++) {
    if (pinEsCorte(pin, nivelCorte)) cortes++;
    delayMicroseconds(120);
  }
  return cortes >= 3;
}

bool sensorListoActivo() {
  return sensorLeeCorteDebounced(PIN_S_LISTO, NIVEL_CORTE_LISTO);
}

bool sensorDestinoActivo() {
  return sensorLeeCorteDebounced(PIN_S_DESTINO, NIVEL_CORTE_DESTINO);
}

const char* nivelSensorTexto(int pin) {
  int nivelCorte = (pin == PIN_S_DESTINO) ? NIVEL_CORTE_DESTINO : NIVEL_CORTE_LISTO;
  return pinEsCorte(pin, nivelCorte) ? "CORTADO" : "LIBRE";
}

#if DEBUG_SENSORES
void logEventoSensor(const char* nombre, int pin, const char* transicion,
                     const char* origen, unsigned long tUs, EstadoJuego e) {
  Serial.print("[");
  Serial.print(nombre);
  Serial.print("] ");
  Serial.print(transicion);
  Serial.print(" (");
  Serial.print(origen);
  Serial.print(") | ahora=");
  Serial.print(nivelSensorTexto(pin));
  Serial.print(" | estado=");
  Serial.print(nombreEstado(e));
  Serial.print(" | armado=");
  Serial.print(juegoArmado ? "SI" : "NO");
  Serial.print(" | t=");
  Serial.print(tUs);
  Serial.println(" us");
}

void depurarSensores() {
  uint8_t edgeListo = 0, edgeDest = 0;
  unsigned long tListo = 0, tDest = 0;

  CRIT_ENTER();
  edgeListo = dbg_listo_edge;
  dbg_listo_edge = 0;
  tListo = dbg_listo_us;
  edgeDest = dbg_destino_edge;
  dbg_destino_edge = 0;
  tDest = dbg_destino_us;
  EstadoJuego e = estado;
  CRIT_EXIT();

  static int ultNivelListo = -1;
  static int ultNivelDestino = -1;

  if (edgeListo == 1) {
    logEventoSensor("S_LISTO", PIN_S_LISTO, "CORTE", "ISR", tListo, e);
    ultNivelListo = SENSOR_ACTIVO;
  } else if (edgeListo == 2) {
    logEventoSensor("S_LISTO", PIN_S_LISTO, "LIBRE", "ISR", tListo, e);
    ultNivelListo = SENSOR_LIBRE;
  }

  if (edgeDest == 1) {
    logEventoSensor("S_DESTINO", PIN_S_DESTINO, "CORTE", "ISR", tDest, e);
    ultNivelDestino = SENSOR_ACTIVO;
  }
  int nListo = digitalRead(PIN_S_LISTO);
  int nDest = digitalRead(PIN_S_DESTINO);
  if (ultNivelListo < 0) {
    ultNivelListo = nListo;
    ultNivelDestino = nDest;
    return;
  }
  if (nListo != ultNivelListo) {
    logEventoSensor("S_LISTO", PIN_S_LISTO,
                    pinEsCorte(PIN_S_LISTO, NIVEL_CORTE_LISTO) ? "CORTE" : "LIBRE",
                    "POLL", micros(), estado);
    ultNivelListo = nListo;
  }
  if (nDest != ultNivelDestino) {
    logEventoSensor("S_DESTINO", PIN_S_DESTINO,
                    pinEsCorte(PIN_S_DESTINO, NIVEL_CORTE_DESTINO) ? "CORTE" : "LIBRE",
                    "POLL", micros(), estado);
    ultNivelDestino = nDest;
  }
}
#else
void depurarSensores() {}
#endif

void actualizarArmadoJuego() {
  bool armado = (estado == LISTO_PARA_PATEAR) && (creditos > 0 || partidaEnCurso);
  CRIT_ENTER();
  juegoArmado = armado;
  CRIT_EXIT();
}

// ==========================================
// INTERRUPCIONES
// ==========================================
void ISR_MARK isr_sensor_listo() {
  unsigned long ahora = micros();
  if (ahora - ultimo_isr_listo_us < DEBOUNCE_ISR_US) return;
  ultimo_isr_listo_us = ahora;

  int nivel = digitalRead(PIN_S_LISTO);
#if DEBUG_SENSORES
  dbg_listo_edge = pinEsCorte(PIN_S_LISTO, NIVEL_CORTE_LISTO) ? 1 : 2;
  dbg_listo_us = ahora;
#endif
  EstadoJuego e = estado;

  if (e == GOL_DETECTADO || e == COOLDOWN) return;

  if (pinEsCorte(PIN_S_LISTO, NIVEL_CORTE_LISTO)) {
    if (e == ESPERANDO_PELOTA && pelotaSoltada) {
      evento_listo_ocupado = true;
    }
  } else {
    if (e == LISTO_PARA_PATEAR) {
      if (juegoArmado) {
        t_inicio = ahora;
        t_fin = 0;
        estado = EN_VUELO;
        evento_listo_libre = true;
      } else {
        // Pelota se fue sin moneda: no es jugada, se espera a que vuelva
        evento_listo_libre = true;
      }
    } else if (e == ESPERANDO_PELOTA) {
      evento_listo_ocupado = false;
    }
  }
}

void ISR_MARK isr_sensor_destino() {
  unsigned long ahora = micros();
  if (ahora - ultimo_isr_destino_us < DEBOUNCE_ISR_US) return;
  ultimo_isr_destino_us = ahora;

  if (!pinEsCorte(PIN_S_DESTINO, NIVEL_CORTE_DESTINO)) return;

#if DEBUG_SENSORES
  dbg_destino_edge = 1;
  dbg_destino_us = ahora;
#endif

  EstadoJuego e = estado;
  if (e == EN_VUELO && t_inicio > 0 && t_fin == 0) {
    t_fin = ahora;
    estado = GOL_DETECTADO;
  } else if (e == EN_VUELO) {
    evento_destino_fuera_secuencia = true;
  }
}

void ISR_MARK isr_monedero() {
  unsigned long ahora = micros();
  if (ahora - ultimo_isr_coin_us < DEBOUNCE_COIN_US) return;
  ultimo_isr_coin_us = ahora;
  pulsosMonedero++;
}

void habilitarInterrupciones() {
  attachInterrupt(digitalPinToInterrupt(PIN_S_LISTO), isr_sensor_listo, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_S_DESTINO), isr_sensor_destino, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_MONEDERO), isr_monedero, FALLING);
}

void deshabilitarInterrupciones() {
  detachInterrupt(digitalPinToInterrupt(PIN_S_LISTO));
  detachInterrupt(digitalPinToInterrupt(PIN_S_DESTINO));
  detachInterrupt(digitalPinToInterrupt(PIN_MONEDERO));
}

void habilitarSensoresJuego() {
  attachInterrupt(digitalPinToInterrupt(PIN_S_LISTO), isr_sensor_listo, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_S_DESTINO), isr_sensor_destino, CHANGE);
}

void deshabilitarSensoresJuego() {
  detachInterrupt(digitalPinToInterrupt(PIN_S_LISTO));
  detachInterrupt(digitalPinToInterrupt(PIN_S_DESTINO));
}

// ==========================================
// MENÚ DE CONFIGURACIÓN
// ==========================================
bool leerBoton(int pin) {
  if (digitalRead(pin) == LOW) {
    delay(50);
    if (digitalRead(pin) == LOW) {
      while (digitalRead(pin) == LOW) delay(10);
      return true;
    }
  }
  return false;
}

void MenuConfig() {
  // Solo sensores de juego: el monedero sigue por ISR en todo momento.
  deshabilitarSensoresJuego();

  lcd.backlight();
  lcd.clear();
  lcd.print(" CONFIGURACION");
  for (unsigned long t = millis(); millis() - t < 1000;) {
    atenderMonedero();
    delay(10);
  }

  lcd.clear();
  lcd.print("Dificultad:");
  while (digitalRead(BTNConfig) == HIGH) {
    atenderMonedero();
    lcd.setCursor(0, 1);
    lcd.print(factorDificultad);
    lcd.print("   ");
    if (leerBoton(BTNUp)) factorDificultad += 0.1;
    if (leerBoton(BTNDificultad) && factorDificultad > 0.1) factorDificultad -= 0.1;
    delay(10);
  }
  while (digitalRead(BTNConfig) == LOW) {
    atenderMonedero();
    delay(10);
  }

  lcd.clear();
  lcd.print("Distancia (m):");
  while (digitalRead(BTNConfig) == HIGH) {
    atenderMonedero();
    lcd.setCursor(0, 1);
    lcd.print(distanciaSensoresM);
    lcd.print("   ");
    if (leerBoton(BTNUp)) distanciaSensoresM += 0.05;
    if (leerBoton(BTNDificultad) && distanciaSensoresM > 0.10) distanciaSensoresM -= 0.05;
    delay(10);
  }
  while (digitalRead(BTNConfig) == LOW) {
    atenderMonedero();
    delay(10);
  }

  configGuardar();

  lcd.clear();
  lcd.print("GUARDADO!");
  for (unsigned long t = millis(); millis() - t < 1000;) {
    atenderMonedero();
    delay(10);
  }

  limpiarMedicion();
  ultimoError[0] = '\0';
  pelotaSoltada = false;
  resetSeleccionDificultad();
  CRIT_ENTER();
  juegoArmado = false;
  CRIT_EXIT();
  cambiarEstado(ESPERANDO_PELOTA);
  actualizarArmadoJuego();
  habilitarInterrupciones();
}

// ==========================================
// JUGADA VÁLIDA / EXCEPCIONES
// ==========================================
void procesarPatadaValida() {
  unsigned long t0, t1;
  CRIT_ENTER();
  t0 = t_inicio;
  t1 = t_fin;
  CRIT_EXIT();

  if (t1 <= t0) {
    recuperarTrasError("ERROR TIEMPO", "t_fin <= t_inicio (desborde o ruido)");
    return;
  }

  unsigned long duracion = t1 - t0;
  float segundos = duracion / 1000000.0;
  if (segundos < 0.001) {
    recuperarTrasError("MEDICION RAPIDA", "Duracion < 1ms: rebote o falso disparo");
    return;
  }

  float velocidad = distanciaSensoresM / segundos;

  if (velocidad > VEL_MAX_MS) {
    Serial.print("Velocidad imposible: ");
    Serial.print(velocidad, 2);
    Serial.println(" m/s");
    recuperarTrasError("VEL. IMPOSIBLE", "Velocidad sobre el limite fisico");
    return;
  }
  if (velocidad < VEL_MIN_MS) {
    Serial.print("Velocidad demasiado baja: ");
    Serial.print(velocidad, 2);
    Serial.println(" m/s");
    recuperarTrasError("PATADA LENTA", "Velocidad bajo el minimo");
    return;
  }

  int puntaje = (velocidad * 100) / factorDificultad;
  if (puntaje > 999) puntaje = 999;

  Serial.println("========================================");
  Serial.println("PATADA VALIDA");
  Serial.print("Duracion: ");
  Serial.print(segundos, 5);
  Serial.println(" s");
  Serial.print("Distancia: ");
  Serial.print(distanciaSensoresM, 2);
  Serial.println(" m");
  Serial.print("Velocidad: ");
  Serial.print(velocidad, 2);
  Serial.println(" m/s");
  Serial.print("Puntos: ");
  Serial.println(puntaje);
  Serial.println("========================================");

  ultimoPuntaje = puntaje;
  mostrarPuntajeLCD(puntaje, velocidad);

  iniciarShowResultado(puntaje);

  limpiarMedicion();
  ultimoError[0] = '\0';
  partidaEnCurso = false;
  resetSeleccionDificultad();
  t_cooldown_hasta = millis();
  cambiarEstado(COOLDOWN);
}

void evaluarExcepciones() {
  if (evento_destino_fuera_secuencia) {
    evento_destino_fuera_secuencia = false;
    Serial.println("EXCEPCION: pin 4 cortado sin cronometro activo");
  }

  EstadoJuego e;
  unsigned long t0, t1;
  CRIT_ENTER();
  e = estado;
  t0 = t_inicio;
  t1 = t_fin;
  CRIT_EXIT();

  if (e == EN_VUELO && t0 > 0 && t1 == 0) {
    if ((micros() - t0) > (TIMEOUT_VUELO_MS * 1000UL)) {
      recuperarTrasError("TIMEOUT PIN 4", "La pelota no tapo el sensor alto a tiempo");
      pelotaSoltada = false;
      return;
    }
  }

  if (e == ESPERANDO_PELOTA && !pelotaSoltada && !sensorDestinoActivo()) {
    if (millis() - t_estado_ms > 1500) {
      registrarError("NO EN REPOSO", "Pin 4 libre: subir pelota");
      t_estado_ms = millis();
      actualizarLCDEstado();
    }
  }

  if (sesionJuegoActiva() && e == ESPERANDO_PELOTA && pelotaSoltada) {
    if (t_pelota_soltada_ms != 0 && millis() - t_pelota_soltada_ms > TIMEOUT_BAJADA_MS) {
      recuperarTrasError("TIMEOUT PIN 2", "Pelota no corto el sensor bajo a tiempo");
      t_pelota_soltada_ms = 0;
      return;
    }
  }

  if (e == ESPERANDO_LIBERAR) {
    if (sensorDestinoActivo() && !sensorListoActivo()) {
      ultimoError[0] = '\0';
      pelotaSoltada = false;
      t_pelota_soltada_ms = 0;
      cambiarEstado(ESPERANDO_PELOTA);
      Serial.println("Reposo OK: pin 4 tapado (pelota arriba)");
      return;
    }
    if (millis() - t_esperando_liberar_ms > TIMEOUT_DESTINO_MS) {
      Serial.println("TIMEOUT reposo pin 4: recuperacion automatica");
      recuperarTrasError("SUBE PELOTA", "Timeout esperando reposo en pin 4");
      t_pelota_soltada_ms = 0;
    }
  }
}

void normalizarModoAtraccion() {
  if (sesionJuegoActiva()) return;

  EstadoJuego e;
  CRIT_ENTER();
  e = estado;
  CRIT_EXIT();

  if (e == COOLDOWN) return;

  bool enFlujoJuego = (e == LISTO_PARA_PATEAR || e == EN_VUELO || e == GOL_DETECTADO ||
                       e == ESPERANDO_LIBERAR || (e == ESPERANDO_PELOTA && pelotaSoltada));
  if (!enFlujoJuego) return;

  Serial.println("Modo atraccion: se abandona flujo de juego sin credito");
  limpiarMedicion();
  pelotaSoltada = false;
  t_pelota_soltada_ms = 0;
  pendiente_confirmar_listo = false;
  resetSeleccionDificultad();
  ultimoError[0] = '\0';
  CRIT_ENTER();
  juegoArmado = false;
  estado = ESPERANDO_PELOTA;
  CRIT_EXIT();
  t_estado_ms = millis();
  actualizarLCDEstado();
  actualizarArmadoJuego();
}

void consumirCreditoSiCorresponde() {
  if (partidaEnCurso) return;
  if (creditos > 0) creditos--;
  partidaEnCurso = true;
  Serial.print("Credito usado. Restan: ");
  Serial.println(creditos);
}

void atenderCambiarDificultad() {
  if (!leerBoton(BTNDificultad)) return;

  EstadoJuego e;
  CRIT_ENTER();
  e = estado;
  CRIT_EXIT();

  if (e != ESPERANDO_PELOTA || pelotaSoltada) return;
  if (!sesionJuegoActiva()) return;
  if (dificultadConfirmada) return;

  nivelDificultad = (nivelDificultad + 1) % 3;
  aplicarNivelDificultad();
  ultimoError[0] = '\0';
  actualizarLCDEstado();
  Serial.print("Dificultad (pin 14): ");
  Serial.println(NOMBRE_NIVEL[nivelDificultad]);
}

void atenderStart() {
  if (!leerBoton(BTNStart)) return;

  EstadoJuego e;
  CRIT_ENTER();
  e = estado;
  CRIT_EXIT();

  if (e != ESPERANDO_PELOTA) return;
  if (!sesionJuegoActiva()) {
    Serial.println("START ignorado: sin credito");
    return;
  }
  if (!sensorDestinoActivo()) {
    Serial.println("START ignorado: pin 4 no tapado (pelota no arriba)");
    return;
  }

  if (!dificultadConfirmada) {
    dificultadConfirmada = true;
    aplicarNivelDificultad();
    ultimoError[0] = '\0';
    actualizarLCDEstado();
    Serial.print("Dificultad confirmada (START): ");
    Serial.println(NOMBRE_NIVEL[nivelDificultad]);
    return;
  }

  pulsarBobina();
  pelotaSoltada = true;
  t_pelota_soltada_ms = millis();
  consumirCreditoSiCorresponde();
  actualizarArmadoJuego();
  ultimoError[0] = '\0';
  actualizarLCDEstado();
  Serial.println("START: bobina + bajada (esperando corte pin 2)");
}

void atenderMonedero() {
  uint32_t n;
  CRIT_ENTER();
  n = pulsosMonedero;
  pulsosMonedero = 0;
  CRIT_EXIT();

  if (n == 0) return;

  creditos += (int)n;
  resetSeleccionDificultad();
  Serial.print("Moneda detectada. Creditos: ");
  Serial.println(creditos);
  actualizarArmadoJuego();
  ultimoError[0] = '\0';
  actualizarLCDEstado();
}

void atenderSensorListo() {
  EstadoJuego e;
  bool ocupadoFlag, libreFlag;
  CRIT_ENTER();
  e = estado;
  ocupadoFlag = evento_listo_ocupado;
  libreFlag = evento_listo_libre;
  evento_listo_ocupado = false;
  evento_listo_libre = false;
  CRIT_EXIT();

  if (e == ESPERANDO_PELOTA && pelotaSoltada && (ocupadoFlag || sensorListoActivo())) {
    if (!pendiente_confirmar_listo) {
      pendiente_confirmar_listo = true;
      t_debounce_listo_ms = millis();
    } else if (millis() - t_debounce_listo_ms >= DEBOUNCE_LISTO_MS) {
      if (sensorListoActivo()) {
        ultimoError[0] = '\0';
        pendiente_confirmar_listo = false;
        pelotaSoltada = false;
        cambiarEstado(LISTO_PARA_PATEAR);
        Serial.println("Pin 2 cortado: listo para patear");
      } else {
        pendiente_confirmar_listo = false;
      }
    }
  } else if (e != ESPERANDO_PELOTA || !pelotaSoltada) {
    pendiente_confirmar_listo = false;
  }

  if (e == LISTO_PARA_PATEAR && (libreFlag || !sensorListoActivo())) {
    CRIT_ENTER();
    e = estado;
    CRIT_EXIT();
    if (e == EN_VUELO) {
      ultimoError[0] = '\0';
      actualizarLCDEstado();
      Serial.println("Pin 2 libre: cronometro pin2->pin4");
    } else {
      Serial.println("Pin 2 libre sin armar: volver a reposo");
      limpiarMedicion();
      pelotaSoltada = false;
      cambiarEstado(ESPERANDO_PELOTA);
    }
  }
}

void atenderCooldown() {
  if (estado != COOLDOWN) return;
  if (showResultadoActivo()) return;
  if (millis() < t_cooldown_hasta) return;

  if (sensorDestinoActivo()) {
    pelotaSoltada = false;
    if (!partidaEnCurso && creditos <= 0) resetSeleccionDificultad();
    cambiarEstado(ESPERANDO_PELOTA);
    Serial.println("Reposo: pin 4 tapado, listo para moneda/START");
  } else {
    t_esperando_liberar_ms = millis();
    cambiarEstado(ESPERANDO_LIBERAR);
    Serial.println("Esperando que la pelota suba y tape pin 4");
  }
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);

  configCargar();

  lcd.init();
  lcd.noBacklight();
  lcd.clear();

  panel.begin();
  panel.setBrightness(BRIGHTNESS);
  pateaRender(panel, pixelShow, panelAnim);
  panel.show();
  t_panel_ms = millis();

  pinMode(PIN_S_LISTO, INPUT_PULLUP);
  pinMode(PIN_S_DESTINO, INPUT_PULLUP);
  pinMode(PIN_MONEDERO, INPUT_PULLUP);
  pinMode(BTNConfig, INPUT_PULLUP);
  pinMode(BTNStart, INPUT_PULLUP);
  pinMode(BTNDificultad, INPUT_PULLUP);
  pinMode(BTNUp, INPUT_PULLUP);
#if BOBINA_ACTIVA_LOW
  pinMode(PIN_BOBINA, OUTPUT);
  digitalWrite(PIN_BOBINA, HIGH);
#else
  pinMode(PIN_BOBINA, OUTPUT);
  digitalWrite(PIN_BOBINA, LOW);
#endif

  habilitarInterrupciones();

  Serial.println("========================================");
  Serial.println("SISTEMA INICIADO - pin2 bajo / pin4 reposo arriba");
  Serial.println("Pin 2 : corte al bajar; LIBRE al patear (t0)");
  Serial.println("Pin 4 : tapado en reposo; corte al volver (t1)");
  Serial.print("Panel NeoPixel DATA pin ");
  Serial.print(PIN_NEO_PANEL);
  Serial.println(" (640 px / 40x16)");
  Serial.println("START pin 13 | Bobina pin 22");
  Serial.print("Distancia: ");
  Serial.print(distanciaSensoresM, 2);
  Serial.print(" m | Dificultad: ");
  Serial.println(factorDificultad, 1);
#if DEBUG_SENSORES
  Serial.print("Sensores iniciales: LISTO=");
  Serial.print(sensorListoActivo() ? "CORTADO" : "LIBRE");
  Serial.print(" | DESTINO=");
  Serial.println(sensorDestinoActivo() ? "CORTADO" : "LIBRE");
  Serial.println("Depuracion sensores: activa (DEBUG_SENSORES=1)");
#endif
  Serial.println("========================================");

  pelotaSoltada = false;
  resetSeleccionDificultad();
  if (sensorDestinoActivo() && !sensorListoActivo()) {
    cambiarEstado(ESPERANDO_PELOTA);
    Serial.println("Reposo inicial OK (pin 4 tapado)");
  } else if (sensorListoActivo()) {
    cambiarEstado(LISTO_PARA_PATEAR);
    Serial.println("Aviso: pin 2 cortado al iniciar");
  } else {
    registrarError("NO EN REPOSO", "Pin 4 libre al iniciar");
    cambiarEstado(ESPERANDO_PELOTA);
  }
}

// ==========================================
// LOOP
// ==========================================
void loop() {
  atenderMonedero();
  normalizarModoAtraccion();

  if (digitalRead(BTNConfig) == LOW) {
    MenuConfig();
    actualizarPanelUsuario();
    return;
  }

  if (sesionJuegoActiva()) {
    atenderCambiarDificultad();
    atenderStart();
  }
  depurarSensores();

  EstadoJuego e;
  CRIT_ENTER();
  e = estado;
  CRIT_EXIT();

  if (e == GOL_DETECTADO) {
    if (sesionJuegoActiva()) {
      procesarPatadaValida();
    } else {
      recuperarTrasError("SIN CREDITO", "Gol ignorado en modo atraccion");
    }
  } else if (sesionJuegoActiva()) {
    atenderSensorListo();
    evaluarExcepciones();
  } else if (e == COOLDOWN) {
    evaluarExcepciones();
  } else {
    if (e == ESPERANDO_PELOTA && !sensorDestinoActivo()) {
      if (millis() - t_estado_ms > 2000) {
        registrarError("NO EN REPOSO", "Pin 4 libre: subir pelota");
        t_estado_ms = millis();
        actualizarLCDEstado();
      }
    }
  }
  atenderCooldown();
  actualizarPanelUsuario();
}
