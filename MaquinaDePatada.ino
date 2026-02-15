#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_NeoPixel.h>
#include <EEPROM.h> 

// ==========================================
// CONFIGURACIÓN DE PINES (ARDUINO MEGA)
// ==========================================
#define PIN_S1          2    // SENSOR (INT0)

#define PIN_NEO_P1      9    // Tira Arriba
#define PIN_NEO_P2      8    // Tira Abajo

#define PIN_MONEDERO    7    // Entrada Monedero (Contador de créditos)
#define PIN_BOBINA      3    // Salida Rele ()

#define PIN_BTN_START   6    // Botón físico de Start
#define PIN_LED_START   5    // Luz del botón Start

// --- BOTONERA ---
#define BTNConfig       A0  
#define BTNUp           A1   
#define BTNDown         A2   

// --- VARIABLES DE DIPLAY ---
#define NUM_MATRICES      4
#define PIXELS_PER_MATRIX 64
#define NUMPIXELS         (NUM_MATRICES * PIXELS_PER_MATRIX) 
#define BRIGHTNESS        15 

// --- EEPROM ---
#define ADDR_DIF        0  
#define ADDR_COINS      4  
#define ADDR_SHOTS      6  

// ==========================================
// OBJETOS
// ==========================================
LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_NeoPixel stripP1(NUMPIXELS, PIN_NEO_P1, NEO_GRB + NEO_KHZ800); //Display 1
Adafruit_NeoPixel stripP2(NUMPIXELS, PIN_NEO_P2, NEO_GRB + NEO_KHZ800); //Display 2

// ==========================================
// CONFIGURACIÓN
// ==========================================
float factorDificultad = 0.5; 
int configMonedas = 2;
int configTiros = 2;         

// ==========================================
// VARIABLES JUEGO
// ==========================================
const float ANCHO_ALETA_M = 0.03;      
const unsigned long TIMEOUT_MS = 1000; 

int tiroActual = 1; 
int creditosContados = 0; 
bool juegoHabilitado = false;
bool esperandoStart = false; // Nueva variable de estado

// Cronómetro
volatile unsigned long t_inicio = 0;
volatile unsigned long t_fin = 0;
volatile bool calculando = false;
unsigned long tiempo_bloqueo_hasta = 0; 

// Sensor Anti-Rebote
volatile int contadorCortes = 0;
volatile unsigned long ultimo_tiempo_corte = 0; 
const int DEBOUNCE_TIME = 150; 

// Depuración
volatile bool flag_debug_corte_1 = false; 
volatile bool flag_debug_corte_2 = false; 

// Bobina
unsigned long tiempo_inicio_bobina = 0;
bool bobinaActiva = false;
const int DURACION_BOBINA = 1000; 

// LED Start Parpadeo
unsigned long ultimoParpadeo = 0;
bool estadoLedStart = false;

// Puntajes 
int mejorPuntaje = 0; 

// ==========================================
// PANTALLA LED
// ==========================================
int getPixelIndex(int x, int y) {
  if (y % 2 == 0) return (y * 8) + x;
  else            return (y * 8) + (7 - x);
}

void dibujarDigito(Adafruit_NeoPixel &tira, int num, int matrizPos, uint32_t color) {
  const byte digits[10][8] = {
    {0x3C,0x42,0x42,0x42,0x42,0x42,0x42,0x3C}, 
    {0x18,0x1C,0x18,0x18,0x18,0x18,0x18,0x3E}, 
    {0x3C,0x42,0x02,0x20,0x08,0x08,0x20,0x7E}, 
    {0x3C,0x42,0x02,0x3C,0x02,0x40,0x42,0x3C}, 
    {0x04,0x30,0x14,0x24,0x7E,0x20,0x04,0x20}, 
    {0x7E,0x02,0x7C,0x40,0x02,0x40,0x42,0x3C}, 
    {0x3C,0x02,0x40,0x3E,0x42,0x42,0x42,0x3C}, 
    {0x7E,0x40,0x02,0x20,0x08,0x08,0x20,0x04}, 
    {0x3C,0x42,0x42,0x3C,0x42,0x42,0x42,0x3C}, 
    {0x3C,0x42,0x42,0x7C,0x02,0x40,0x02,0x3C}  
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

void actualizarPantalla(Adafruit_NeoPixel &tira, int numeroTiro, int puntaje, bool activo) {
  tira.clear();
  uint32_t colorId;
  if (numeroTiro % 2 != 0) colorId = activo ? tira.Color(0,0,255) : tira.Color(0,0,50); 
  else                     colorId = activo ? tira.Color(255,0,255) : tira.Color(50,0,50); 
  dibujarDigito(tira, numeroTiro, 0, colorId);

  if (puntaje > 999) puntaje = 999;
  
  int c = puntaje / 100;
  int d = (puntaje / 10) % 10;
  int u = puntaje % 10;

  uint32_t colorScore = 0;
  if (puntaje > 0) {
      colorScore = (puntaje < 300) ? tira.Color(0,255,0) : 
                   (puntaje < 700) ? tira.Color(255,200,0) : tira.Color(255,0,0);
      if (!activo) {
        uint8_t r = (uint8_t)(colorScore >> 16);
        uint8_t g = (uint8_t)(colorScore >> 8);
        uint8_t b = (uint8_t)colorScore;
        colorScore = tira.Color(r/4, g/4, b/4);
      }
      if(c > 0) dibujarDigito(tira, c, 1, colorScore);
      else      dibujarDigito(tira, 0, 1, colorScore);
      if(c > 0 || d > 0) dibujarDigito(tira, d, 2, colorScore);
      else               dibujarDigito(tira, 0, 2, colorScore);
      dibujarDigito(tira, u, 3, colorScore);
  } else {
      colorScore = tira.Color(20, 20, 20); 
      dibujarDigito(tira, 0, 1, colorScore);
      dibujarDigito(tira, 0, 2, colorScore);
      dibujarDigito(tira, 0, 3, colorScore); 
  }
  tira.show();
}

void animarConteo(Adafruit_NeoPixel &tira, int numeroTiro, int puntajeFinal) {
  if (puntajeFinal == 0) return;
  int incremento = (puntajeFinal > 700) ? 10 : (puntajeFinal > 300) ? 5 : 2;
  for (int i = 0; i <= puntajeFinal; i += incremento) {
    if (i > puntajeFinal) i = puntajeFinal;
    actualizarPantalla(tira, numeroTiro, i, true);
    if (puntajeFinal - i < 50) delay(2); else delay(10);
  }
  actualizarPantalla(tira, numeroTiro, puntajeFinal, true);
  delay(300); 
}

// ==========================================
// INTERRUPCIÓN SENSOR
// ==========================================
void isr_sensor() {
  if (!juegoHabilitado) return; 
  
  int estado = digitalRead(PIN_S1);
  unsigned long tiempo_actual = millis();

  if (estado == LOW) { 
    if (tiempo_actual - ultimo_tiempo_corte < DEBOUNCE_TIME) return;
    
    contadorCortes++;
    ultimo_tiempo_corte = tiempo_actual;

    if (contadorCortes == 1) {
      flag_debug_corte_1 = true; 
    }
    else if (contadorCortes == 2) {
      if (!calculando) { 
        t_inicio = micros(); 
        calculando = true; 
        flag_debug_corte_2 = true; 
      }
    }
  } 
  else { 
    if (calculando && t_inicio > 0) t_fin = micros();
  }
}

// ==========================================
// MENÚ
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

  // 1. DIFICULTAD
  lcd.clear(); lcd.print("DIFICULTAD");
  while(digitalRead(BTNConfig) == HIGH) {
    lcd.setCursor(0,1); lcd.print(factorDificultad); lcd.print("   ");
    if(leerBoton(BTNUp)) factorDificultad += 0.1;
    if(leerBoton(BTNDown) && factorDificultad > 0.1) factorDificultad -= 0.1;
  }
  while(digitalRead(BTNConfig) == LOW) delay(50);

  // 2. MONEDAS
  lcd.clear(); lcd.print("MONEDAS REQ.");
  while(digitalRead(BTNConfig) == HIGH) {
    lcd.setCursor(0,1); 
    if(configMonedas == 0) lcd.print("GRATIS / TEST ");
    else { lcd.print(configMonedas); lcd.print(" Monedas     "); }
    if(leerBoton(BTNUp)) configMonedas++;
    if(leerBoton(BTNDown) && configMonedas > 0) configMonedas--; 
  }
  while(digitalRead(BTNConfig) == LOW) delay(50);

  // 3. TIROS
  lcd.clear(); lcd.print("TIROS X JUEGO");
  while(digitalRead(BTNConfig) == HIGH) {
    lcd.setCursor(0,1); lcd.print(configTiros); lcd.print(" Tiros  ");
    if(leerBoton(BTNUp)) configTiros++;
    if(leerBoton(BTNDown) && configTiros > 1) configTiros--;
  }
  while(digitalRead(BTNConfig) == LOW) delay(50);

  EEPROM.put(ADDR_DIF, factorDificultad);
  EEPROM.put(ADDR_COINS, configMonedas);
  EEPROM.put(ADDR_SHOTS, configTiros);

  lcd.clear(); lcd.print(" DATOS GUARDADOS"); delay(1500);
  lcd.noBacklight(); lcd.clear();

  t_inicio = 0; t_fin = 0; calculando = false;
  creditosContados = 0; 
  contadorCortes = 0;
  attachInterrupt(digitalPinToInterrupt(PIN_S1), isr_sensor, CHANGE);
  
  actualizarPantalla(stripP1, 1, 0, false);
  actualizarPantalla(stripP2, 2, 0, false);
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(9600);
  
  EEPROM.get(ADDR_DIF, factorDificultad);
  EEPROM.get(ADDR_COINS, configMonedas);
  EEPROM.get(ADDR_SHOTS, configTiros);

  if (isnan(factorDificultad) || factorDificultad < 0.1 || factorDificultad > 5.0) factorDificultad = 0.5;
  if (configMonedas < 0 || configMonedas > 20) configMonedas = 2; 
  if (configTiros < 1 || configTiros > 20) configTiros = 2;

  lcd.init(); lcd.noBacklight(); lcd.clear();
  
  stripP1.begin(); stripP1.setBrightness(BRIGHTNESS); stripP1.clear(); stripP1.show();
  stripP2.begin(); stripP2.setBrightness(BRIGHTNESS); stripP2.clear(); stripP2.show();
  
  pinMode(PIN_MONEDERO, INPUT_PULLUP);
  pinMode(PIN_BTN_START, INPUT_PULLUP); // Nuevo Botón
  pinMode(PIN_LED_START, OUTPUT);       // Nuevo LED
  pinMode(PIN_BOBINA, OUTPUT);
  
  digitalWrite(PIN_BOBINA, LOW); 
  digitalWrite(PIN_LED_START, LOW);

  pinMode(PIN_S1, INPUT_PULLUP);
  pinMode(BTNConfig, INPUT_PULLUP);
  pinMode(BTNUp, INPUT_PULLUP);
  pinMode(BTNDown, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(PIN_S1), isr_sensor, CHANGE);

  actualizarPantalla(stripP1, 1, 0, false);
  actualizarPantalla(stripP2, 2, 0, false);
  
  Serial.println("SISTEMA MEGA: INICIADO");
}

// ==========================================
// LOOP
// ==========================================
void loop() {

  // ======================================================
  // 1. GESTIÓN DEL MONEDERO (Insertar créditos)
  // ======================================================
  if (!juegoHabilitado && !esperandoStart) {
    if (digitalRead(PIN_MONEDERO) == LOW) {
       delay(50); 
       if (digitalRead(PIN_MONEDERO) == LOW) {
           
           bool creditosSuficientes = false;

           if(configMonedas == 0) {
               Serial.println("MODO GRATIS - Pasando a Start...");
               creditosSuficientes = true;
           } 
           else {
               creditosContados++;
               Serial.print("Credito: "); Serial.print(creditosContados); Serial.print("/"); Serial.println(configMonedas);
               if(creditosContados >= configMonedas) creditosSuficientes = true;
           }

           while(digitalRead(PIN_MONEDERO) == LOW) delay(10); 
           if(configMonedas > 0) { delay(1000); lcd.noBacklight(); lcd.clear(); }

           // SI TENEMOS CRÉDITO -> PASAMOS A ESPERAR EL BOTÓN START
           if (creditosSuficientes) {
               creditosContados = 0; 
               Serial.println("CREDITOS LISTOS. ESPERANDO BOTON START...");
               esperandoStart = true;
           }
       }
    }
  }

  // ======================================================
  // 2. MODO ESPERA (Boton Start Parpadeando)
  // ======================================================
  if (esperandoStart) {
      
      // PARPADEO DEL LED (LENTO: 500ms)
      if (millis() - ultimoParpadeo > 500) {
          ultimoParpadeo = millis();
          estadoLedStart = !estadoLedStart;
          digitalWrite(PIN_LED_START, estadoLedStart);
      }

      // LEER BOTÓN START
      if (digitalRead(PIN_BTN_START) == LOW) {
          delay(50); // Debounce
          if (digitalRead(PIN_BTN_START) == LOW) {
              Serial.println(">>> START PRESIONADO <<<");
              
              // INICIAR JUEGO REALMENTE
              esperandoStart = false;
              juegoHabilitado = true;
              digitalWrite(PIN_LED_START, LOW); // Apagar o dejar fijo el LED
              
              // Resetear variables de juego
              tiroActual = 1;
              mejorPuntaje = 0;
              contadorCortes = 0;
              calculando = false;

              // SOLTAR PRIMERA PELOTA (Acción manual del botón)
              Serial.println("Liberando Pelota 1...");
              digitalWrite(PIN_BOBINA, HIGH);
              bobinaActiva = true;
              tiempo_inicio_bobina = millis();

              actualizarPantalla(stripP1, 1, 0, true);  
              actualizarPantalla(stripP2, 2, 0, false); 
              
              while(digitalRead(PIN_BTN_START) == LOW) delay(10);
          }
      }
  }

  // ======================================================
  // 3. APAGADO AUTOMÁTICO BOBINA
  // ======================================================
  if (bobinaActiva) {
    if (millis() - tiempo_inicio_bobina >= DURACION_BOBINA) {
      digitalWrite(PIN_BOBINA, LOW);
      bobinaActiva = false;
      Serial.println("Bobina OFF");
    }
  }

  // ======================================================
  // 4. LÓGICA DE JUEGO (SENSOR Y SECUENCIA AUTOMÁTICA)
  // ======================================================
  if (juegoHabilitado) {
      
      if (calculando && t_fin > 0) {
        unsigned long duracion = t_fin - t_inicio;
        
        if (duracion > 1000) { 
          float segundos = duracion / 1000000.0;
          float velocidad = ANCHO_ALETA_M / segundos;
          int puntaje = (velocidad * 100) / factorDificultad;
          if (puntaje > 999) puntaje = 999;
          
          Serial.print("Tiro #"); Serial.print(tiroActual);
          Serial.print(" | Puntos: "); Serial.println(puntaje);
          
          if (puntaje > mejorPuntaje) mejorPuntaje = puntaje;

          Adafruit_NeoPixel &targetStrip = (tiroActual % 2 != 0) ? stripP1 : stripP2;
          
          animarConteo(targetStrip, tiroActual, puntaje);
          actualizarPantalla(targetStrip, tiroActual, puntaje, true); 

          if (tiroActual < configTiros) {
              
              // === SECUENCIA AUTOMÁTICA SIGUIENTE TIRO ===
              Serial.println("Esperando 2 seg...");
              delay(2000); 
              actualizarPantalla(targetStrip, tiroActual, puntaje, false); 

              tiroActual++;
              
              contadorCortes = 0;
              calculando = false;
              t_inicio = 0; t_fin = 0;
              ultimo_tiempo_corte = 0;

              // SOLTAR SIGUIENTE PELOTA (AUTOMÁTICO)
              Serial.println("Liberando Pelota Automatica...");
              digitalWrite(PIN_BOBINA, HIGH);
              bobinaActiva = true;
              tiempo_inicio_bobina = millis();

              Adafruit_NeoPixel &nextStrip = (tiroActual % 2 != 0) ? stripP1 : stripP2;
              actualizarPantalla(nextStrip, tiroActual, 0, true);

          } else {
              // --- FIN DEL JUEGO ---
              Serial.println("JUEGO TERMINADO");
              delay(1000);
              
              actualizarPantalla(targetStrip, tiroActual, puntaje, (puntaje == mejorPuntaje));
              
              lcd.backlight(); lcd.setCursor(0,0); lcd.print("  MEJOR PUNT.   ");
              lcd.setCursor(0,1); lcd.print("      "); lcd.print(mejorPuntaje);
              delay(4000);
              lcd.noBacklight(); lcd.clear();

              juegoHabilitado = false; // Vuelve al inicio (esperar moneda)
          }
          
          tiempo_bloqueo_hasta = millis() + 1000; 
          delay(1000); 
          
          contadorCortes = 0;
          calculando = false;
          t_inicio = 0; t_fin = 0;
        } 
        else {
           Serial.println("Ruido detectado - Ignorando");
           calculando = false;
           contadorCortes = 0;
        }
      }

      // Timeout Sensor
      if (calculando && t_fin == 0) {
        if ((micros() - t_inicio) > (TIMEOUT_MS * 1000)) {
           Serial.println("Timeout sensor");
           calculando = false;
           contadorCortes = 0;
           actualizarPantalla(stripP1, (tiroActual%2!=0)?tiroActual:tiroActual-1, 0, false);
           actualizarPantalla(stripP2, (tiroActual%2==0)?tiroActual:tiroActual-1, 0, false);
           tiempo_bloqueo_hasta = millis() + 500;
        }
      }
  }

  // --- Condicionales de debuging ---
  if (flag_debug_corte_1) { Serial.println(">>> Sensor: IDA (Ignorado)"); flag_debug_corte_1 = false; }
  if (flag_debug_corte_2) { Serial.println(">>> Sensor: VUELTA (Midiendo)"); flag_debug_corte_2 = false; }


  // --- Condicional de Menu
  if (digitalRead(BTNConfig) == LOW) MenuConfig();
}
