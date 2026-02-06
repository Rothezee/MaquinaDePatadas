#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>

// ==========================================
// CONFIGURACIÓN DE PINES
// ==========================================
#define PIN_S1          26   // Sensor Único

#define PIN_NEO_P1      27   // Tira Jugador 1
#define PIN_NEO_P2      25   // Tira Jugador 2

// ¡CAMBIO DE PINES! (34 y 35 no tienen PullUp, por eso fallaba)
#define BTNConfig       12   // Botón Menú 35
#define BTNUp           13   // Botón Subir 4
#define BTNDown         14   // Botón Bajar 34

// --- LUCES ---
#define NUM_MATRICES      4
#define PIXELS_PER_MATRIX 64
#define NUMPIXELS         (NUM_MATRICES * PIXELS_PER_MATRIX) 
#define BRIGHTNESS        15 

// ==========================================
// OBJETOS
// ==========================================
LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_NeoPixel stripP1(NUMPIXELS, PIN_NEO_P1, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel stripP2(NUMPIXELS, PIN_NEO_P2, NEO_GRB + NEO_KHZ800);
Preferences pref; 

// ==========================================
// VARIABLES
// ==========================================
const float ANCHO_ALETA_M = 0.03;      
const unsigned long TIMEOUT_MS = 1000; 
float factorDificultad = 0.5; 

// Juego
int turnoActual = 1; 
volatile unsigned long t_inicio = 0;
volatile unsigned long t_fin = 0;
volatile bool calculando = false;
unsigned long tiempo_bloqueo_hasta = 0; 
volatile bool sensor_detectado_debug = false;

// ✅ NUEVO: Control de pasos del sensor
volatile bool primerCorteIgnorado = false;
volatile int contadorCortes = 0;

// Últimos puntajes
int ultimoPuntajeP1 = 0;
int ultimoPuntajeP2 = 0;

// ==========================================
// FUNCIONES DE PANTALLA LED
// ==========================================
int getPixelIndex(int x, int y) {
  if (y % 2 == 0) return (y * 8) + x;
  else            return (y * 8) + (7 - x);
}

void dibujarDigito(Adafruit_NeoPixel &tira, int num, int matrizPos, uint32_t color) {
  const byte digits[10][8] = {
    {0x3C,0x42,0x42,0x42,0x42,0x42,0x42,0x3C}, // 0
    {0x18,0x1C,0x18,0x18,0x18,0x18,0x18,0x3E}, // 1
    {0x3C,0x42,0x02,0x20,0x08,0x08,0x20,0x7E}, // 2
    {0x3C,0x42,0x02,0x3C,0x02,0x40,0x42,0x3C}, // 3
    {0x04,0x30,0x14,0x24,0x7E,0x20,0x04,0x20}, // 4
    {0x7E,0x02,0x7C,0x40,0x02,0x40,0x42,0x3C}, // 5
    {0x3C,0x02,0x40,0x3E,0x42,0x42,0x42,0x3C}, // 6
    {0x7E,0x40,0x02,0x20,0x08,0x08,0x20,0x04}, // 7
    {0x3C,0x42,0x42,0x3C,0x42,0x42,0x42,0x3C}, // 8
    {0x3C,0x42,0x42,0x7C,0x02,0x40,0x02,0x3C}  // 9
  };
  int offset = matrizPos * PIXELS_PER_MATRIX;
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      if (bitRead(digits[num][y], 7 - x)) {
        tira.setPixelColor(getPixelIndex(x, y) + offset, color);
      }
    }
  }
}

void actualizarPantalla(Adafruit_NeoPixel &tira, int numeroJugador, int puntaje, bool activo) {
  tira.clear();
  
  // 1. DIBUJAR NÚMERO DE JUGADOR
  uint32_t colorJugador;
  if (numeroJugador == 1) colorJugador = activo ? tira.Color(0,0,255) : tira.Color(0,0,50); // ✅ Azul tenue si inactivo
  else                    colorJugador = activo ? tira.Color(255,0,255) : tira.Color(50,0,50); // ✅ Magenta tenue si inactivo
  dibujarDigito(tira, numeroJugador, 0, colorJugador);

  // 2. DIBUJAR PUNTAJE
  if (puntaje > 999) puntaje = 999;
  
  int c = puntaje / 100;
  int d = (puntaje / 10) % 10;
  int u = puntaje % 10;

  uint32_t colorScore = 0;

  // Determinar color base según puntaje
  if (puntaje > 0) {
      colorScore = (puntaje < 300) ? tira.Color(0,255,0) : 
                   (puntaje < 700) ? tira.Color(255,200,0) : tira.Color(255,0,0);
      
      // ✅ Si está inactivo, reducir brillo al 25%
      if (!activo) {
        uint8_t r = (colorScore >> 16) & 0xFF;
        uint8_t g = (colorScore >> 8) & 0xFF;
        uint8_t b = colorScore & 0xFF;
        colorScore = tira.Color(r/4, g/4, b/4);
      }
  } else {
      // ✅ ESTADO DEFAULT: "000" siempre visible (activo o inactivo)
      colorScore = tira.Color(20, 20, 20); // Gris visible para ambos
  }
  
  // Dibujar dígitos (SIEMPRE mostrar al menos "000")
  if(c > 0) dibujarDigito(tira, c, 1, colorScore);
  else      dibujarDigito(tira, 0, 1, colorScore); // ✅ Mostrar 0 explícitamente
  
  if(c > 0 || d > 0) dibujarDigito(tira, d, 2, colorScore);
  else               dibujarDigito(tira, 0, 2, colorScore); // ✅ Mostrar 0 explícitamente
  
  dibujarDigito(tira, u, 3, colorScore); // Siempre mostrar unidades
  tira.show();
}

void animarConteo(Adafruit_NeoPixel &tira, int numeroJugador, int puntajeFinal) {
  if (puntajeFinal == 0) return;
  int incremento = 1;
  if (puntajeFinal > 300) incremento = 3;
  if (puntajeFinal > 700) incremento = 5;

  for (int i = 0; i <= puntajeFinal; i += incremento) {
    if (i > puntajeFinal) i = puntajeFinal;
    actualizarPantalla(tira, numeroJugador, i, true);
    if (puntajeFinal - i < 50) delay(15); else delay(5);
  }
  actualizarPantalla(tira, numeroJugador, puntajeFinal, true);
  delay(500); 
}

// ==========================================
// INTERRUPCIÓN SENSOR (LIMPIA)
// ==========================================
// ==========================================
// INTERRUPCIÓN SENSOR (CON BLOQUEO DE PRIMER CORTE)
// ==========================================
void IRAM_ATTR isr_sensor() {
  if (millis() < tiempo_bloqueo_hasta) return; 

  int estado = digitalRead(PIN_S1);
  
  if (estado == LOW) { // Objeto entra en ranura
    contadorCortes++;
    
    // ✅ IGNORAR EL PRIMER CORTE (pelota yendo hacia el usuario)
    if (contadorCortes == 1) {
      primerCorteIgnorado = true;
      sensor_detectado_debug = true; // Debug
      Serial.println("Primer corte ignorado (ida)");
      return; // ⛔ NO iniciar cronómetro
    }
    
    // ✅ SEGUNDO CORTE EN ADELANTE (pelota regresando)
    if (contadorCortes >= 2 && !calculando) { 
      t_inicio = micros(); 
      calculando = true; 
      sensor_detectado_debug = true;
    }
  } 
  else { // Objeto sale de ranura
    if (calculando && t_inicio > 0) {
      t_fin = micros();
    }
  }
}

// ==========================================
// MENÚ DE CONFIGURACIÓN
// ==========================================
bool leerBoton(int pin) {
  if (digitalRead(pin) == LOW) {
    delay(50);
    if (digitalRead(pin) == LOW) {
      while(digitalRead(pin) == LOW) delay(10);
      return true;
    }
  }
  return false;
}

void MenuConfig() {
  detachInterrupt(digitalPinToInterrupt(PIN_S1)); 
  
  lcd.backlight(); lcd.clear(); lcd.print(" CONFIGURACION"); delay(1000);
  
  lcd.clear(); lcd.print("Dificultad:");
  while(digitalRead(BTNConfig) == HIGH) {
    lcd.setCursor(0,1); lcd.print(factorDificultad); lcd.print("   ");
    if(leerBoton(BTNUp)) factorDificultad += 0.1;
    if(leerBoton(BTNDown) && factorDificultad > 0.1) factorDificultad -= 0.1;
  }
  while(digitalRead(BTNConfig) == LOW) delay(50); 
  
  pref.begin("arcade", false);
  pref.putFloat("dif", factorDificultad);
  pref.end();

  lcd.clear(); lcd.print("GUARDADO!"); delay(1000);
  lcd.noBacklight(); lcd.clear();

  t_inicio = 0; t_fin = 0; calculando = false;
  attachInterrupt(digitalPinToInterrupt(PIN_S1), isr_sensor, CHANGE);
  
  // ✅ Restaurar displays con últimos puntajes
  if(turnoActual == 1) {
     actualizarPantalla(stripP1, 1, ultimoPuntajeP1, true);
     actualizarPantalla(stripP2, 2, ultimoPuntajeP2, false);
  } else {
     actualizarPantalla(stripP1, 1, ultimoPuntajeP1, false);
     actualizarPantalla(stripP2, 2, ultimoPuntajeP2, true);
  }
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  
  pref.begin("arcade", true);
  factorDificultad = pref.getFloat("dif", 0.5); 
  pref.end();

  lcd.init(); lcd.noBacklight(); lcd.clear();
  
  stripP1.begin(); stripP1.setBrightness(BRIGHTNESS); stripP1.clear();
  stripP2.begin(); stripP2.setBrightness(BRIGHTNESS); stripP2.clear();
  
  pinMode(PIN_S1, INPUT_PULLUP);
  pinMode(BTNConfig, INPUT_PULLUP);
  pinMode(BTNUp, INPUT_PULLUP);
  pinMode(BTNDown, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(PIN_S1), isr_sensor, CHANGE);

  // ✅ Ambos displays encendidos desde el inicio
  actualizarPantalla(stripP1, 1, 0, true);
  actualizarPantalla(stripP2, 2, 0, false);
  
  Serial.println("SISTEMA INICIADO. Sensor en pin 26.");
}

// ==========================================
// LOOP
// ==========================================
void loop() {
  
  // Debug seguro
  if (sensor_detectado_debug) {
    Serial.print("Sensor activado! Corte #");
    Serial.println(contadorCortes);
    sensor_detectado_debug = false;
  }

  if (calculando && t_fin > 0) {
    unsigned long duracion = t_fin - t_inicio;
    
    if (duracion > 2000) { 
      float segundos = duracion / 1000000.0;
      float velocidad = ANCHO_ALETA_M / segundos;
      int puntaje = (velocidad * 100) / factorDificultad;
      if (puntaje > 999) puntaje = 999;
      
      Serial.printf("Golpe válido! Duracion: %.5f s | Puntos: %d\n", segundos, puntaje);

      if (turnoActual == 1) {
        ultimoPuntajeP1 = puntaje;
        animarConteo(stripP1, 1, puntaje);
        actualizarPantalla(stripP1, 1, puntaje, false);
        actualizarPantalla(stripP2, 2, ultimoPuntajeP2, true);
        turnoActual = 2;
      } else {
        ultimoPuntajeP2 = puntaje;
        animarConteo(stripP2, 2, puntaje);
        actualizarPantalla(stripP2, 2, puntaje, false);
        delay(3000); 
        turnoActual = 1;
        actualizarPantalla(stripP1, 1, ultimoPuntajeP1, true);
        actualizarPantalla(stripP2, 2, puntaje, false);
      }

      tiempo_bloqueo_hasta = millis() + 3000;
      delay(3000);
      
      // ✅ RESETEAR contador para siguiente jugada
      contadorCortes = 0;
      primerCorteIgnorado = false;
    } 
    
    t_inicio = 0; t_fin = 0; calculando = false;
  }

  if (calculando && t_fin == 0) {
    if ((micros() - t_inicio) > (TIMEOUT_MS * 1000)) {
       Serial.println("Error: Timeout de sensor.");
       t_inicio = 0; calculando = false;
       
       // ✅ RESETEAR en caso de timeout
       contadorCortes = 0;
       primerCorteIgnorado = false;
       
       if(turnoActual == 1) {
         actualizarPantalla(stripP1, 1, ultimoPuntajeP1, true);
         actualizarPantalla(stripP2, 2, ultimoPuntajeP2, false);
       } else {
         actualizarPantalla(stripP1, 1, ultimoPuntajeP1, false);
         actualizarPantalla(stripP2, 2, ultimoPuntajeP2, true);
       }
       tiempo_bloqueo_hasta = millis() + 1000;
    }
  }

  if (digitalRead(BTNConfig) == LOW) MenuConfig();
}