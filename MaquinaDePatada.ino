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
#define BTNConfig       4    
#define BTNUp           12   
#define BTNDown         13   

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
  
  // --- 1. DIBUJAR NÚMERO DE JUGADOR (Matriz 0) ---
  uint32_t colorJugador;
  if (numeroJugador == 1) colorJugador = activo ? tira.Color(0,0,255) : 0;
  else                    colorJugador = activo ? tira.Color(255,0,255) : 0;
  
  dibujarDigito(tira, numeroJugador, 0, colorJugador);

  // --- 2. DIBUJAR PUNTAJE (Matrices 1, 2, 3) ---
  if (puntaje > 999) puntaje = 999;
  
  // Descomponer dígitos
  int c = puntaje / 100;
  int d = (puntaje / 10) % 10;
  int u = puntaje % 10;

  uint32_t colorScore = 0;

  // CASO A: Hay puntaje real (Colores fuertes)
  if (puntaje > 0) {
      colorScore = (puntaje < 300) ? tira.Color(0,255,0) : 
                   (puntaje < 700) ? tira.Color(255,200,0) : tira.Color(255,0,0);
      
      // Dibujamos normal (ocultando ceros a la izquierda si quieres, o mostrándolos)
      // Aquí ocultamos ceros a la izquierda para puntajes reales (ej: " 85")
      if(c > 0) dibujarDigito(tira, c, 1, colorScore);
      if(c > 0 || d > 0) dibujarDigito(tira, d, 2, colorScore);
      dibujarDigito(tira, u, 3, colorScore);
  } 
  // CASO B: Es turno activo pero puntaje 0 (MODO ESPERA 000)
  else if (activo) {
      colorScore = tira.Color(20, 20, 20); // Gris tenue
      
      // Forzamos dibujar 0 en las 3 posiciones
      dibujarDigito(tira, 0, 1, colorScore);
      dibujarDigito(tira, 0, 2, colorScore);
      dibujarDigito(tira, 0, 3, colorScore);
  }
  // CASO C: Inactivo y puntaje 0 -> Todo apagado (no hace nada)

  tira.show();
}

// Animación de subida
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
// INTERRUPCIÓN SENSOR
// ==========================================
void IRAM_ATTR isr_sensor() {
  if (millis() < tiempo_bloqueo_hasta) return; // Anti-retorno

  int estado = digitalRead(PIN_S1);
  if (estado == LOW) { 
    if (!calculando) { t_inicio = micros(); calculando = true; }
  } 
  else { 
    if (calculando && t_inicio > 0) t_fin = micros();
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
  
  // Refrescar luces al salir
  actualizarPantalla(stripP1, 1, 0, true);
  actualizarPantalla(stripP2, 2, 0, false);
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  
  pref.begin("arcade", true);
  factorDificultad = pref.getFloat("dif", 1.0);
  pref.end();

  lcd.init(); lcd.noBacklight(); lcd.clear();
  
  stripP1.begin(); stripP1.setBrightness(BRIGHTNESS); stripP1.clear();
  stripP2.begin(); stripP2.setBrightness(BRIGHTNESS); stripP2.clear();
  
  pinMode(PIN_S1, INPUT_PULLUP);
  pinMode(BTNConfig, INPUT_PULLUP);
  pinMode(BTNUp, INPUT_PULLUP);
  pinMode(BTNDown, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(PIN_S1), isr_sensor, CHANGE);

  // Estado Inicial: P1 Activo (000 tenue), P2 Inactivo (Apagado)
  actualizarPantalla(stripP1, 1, 0, true);
  actualizarPantalla(stripP2, 2, 0, false);
}

// ==========================================
// LOOP
// ==========================================
void loop() {
  
  if (calculando && t_fin > 0) {
    unsigned long duracion = t_fin - t_inicio;
    
    if (duracion > 2000) { 
      float segundos = duracion / 1000000.0;
      float velocidad = ANCHO_ALETA_M / segundos;
      int puntaje = (velocidad * 100) / factorDificultad;
      if (puntaje > 999) puntaje = 999;

      // --- LÓGICA DE TURNOS ---
      if (turnoActual == 1) {
        // Turno P1: Animar y mostrar
        animarConteo(stripP1, 1, puntaje);
        
        // Activar P2 (Muestra 000 tenue) y Desactivar P1 (Muestra puntaje fijo)
        // Nota: Al final de animarConteo, P1 queda con el puntaje y 'activo=true'.
        // Podríamos dejarlo así o apagarle el indicador de jugador si prefieres.
        
        actualizarPantalla(stripP2, 2, 0, true); 
        turnoActual = 2;
        
      } else {
        // Turno P2: Animar y mostrar
        animarConteo(stripP2, 2, puntaje);
        
        // Pausa Final
        delay(3000); 
        
        // Reiniciar ronda
        turnoActual = 1;
        actualizarPantalla(stripP1, 1, 0, true);  
        actualizarPantalla(stripP2, 2, 0, false); 
      }

      // Anti-retorno (3 segundos)
      tiempo_bloqueo_hasta = millis() + 3000;
      delay(3000); 
    } 
    
    t_inicio = 0; t_fin = 0; calculando = false;
  }

  // Timeout
  if (calculando && t_fin == 0) {
    if ((micros() - t_inicio) > (TIMEOUT_MS * 1000)) {
       t_inicio = 0; calculando = false;
       // Si hay error, restauramos el estado "000 Tenue" del turno actual
       if(turnoActual == 1) actualizarPantalla(stripP1, 1, 0, true);
       else                 actualizarPantalla(stripP2, 2, 0, true);
       
       tiempo_bloqueo_hasta = millis() + 1000;
    }
  }

  if (digitalRead(BTNConfig) == LOW) MenuConfig();
}
