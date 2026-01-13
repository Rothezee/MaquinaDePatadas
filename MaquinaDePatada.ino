#include <Arduino.h>
#include <LiquidCrystal_I2C.h>

// --- Configuración de Hardware ---
#define PIN_S1 26 // Sensor de inicio (ranura 1)
#define PIN_S2 25 // Sensor de fin (ranura 2)
#define BTNConfig 4 //boton para entrar en configuracion 
#define BTNUp 12 //boton para aumentar o subir
#define BTNDown 13 // boton para disminuir o bajar

// --- Parámetros de la Máquina ---
const float DISTANCIA_M = 0.12;       // 12 cm convertidos a metros
float factorDificultad = 1.0;         // Ajustable (1.0 = normal)
const unsigned long TIMEOUT_MS = 2000; // Tiempo máximo entre sensores (2 seg)

// --- Variables de Tiempo (Volatile para Interrupciones) ---
volatile unsigned long t1 = 0;
volatile unsigned long t2 = 0;
volatile bool calculando = false;
volatile bool error_timeout = false;

// --- Variables de LCD ---
int lcdColumns = 16;
int lcdRows = 2;
LiquidCrystal_I2C lcd(0x27, lcdColumns, lcdRows);

float Podio[3];

// --- ISR: Interrupción Sensor 1 ---
void IRAM_ATTR isr_inicio() {
  if (!calculando) {
    Serial.printf("S1 cortado");
    t1 = micros();
    t2 = 0;
    calculando = true;
  }
}

// --- ISR: Interrupción Sensor 2 ---
void IRAM_ATTR isr_fin() {
  if (calculando && t2 == 0) {
    Serial.printf("S2 cortado");
    t2 = micros();
  }
}

bool leerBotonConDebounce(int pin, int delayMs = 50) {
    if (digitalRead(pin) == LOW) {
        delay(delayMs);
        if (digitalRead(pin) == LOW) {
            while (digitalRead(pin) == LOW) delay(10);
            delay(50); return true;
        }
    }
    return false;
}

void Configurar(){

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("VERSION 1");
  lcd.setCursor(0,1);
  lcd.print("01/02/26");
  delay(500);

    // Esperar a que se suelte DATO3 si está presionado
  while (digitalRead(BTNConfig) == LOW) {
      delay(20);
  }

  delay(100);

      // Esperar a que se presione DATO3
  while (digitalRead(BTNConfig) == HIGH) {
      delay(20);
  }
  // Esperar a que se suelte
  while (digitalRead(BTNConfig) == LOW) {
      delay(20);
  }
  delay(100);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Ajustar Dificultad");
  delay(500);

  while (digitalRead(BTNConfig) == HIGH) {
      lcd.setCursor(0,1);
      lcd.print("   ");  // Limpiar
      lcd.setCursor(0,1);
      lcd.print(factorDificultad);
      
      if (leerBotonConDebounce(BTNUp)) {
          factorDificultad += 0.1;
      }
      if (leerBotonConDebounce(BTNDown)) {
          factorDificultad -= 0.1;
      }
      delay(10);
  }

  while(digitalRead(BTNConfig) == LOW) {
    delay(20);
  }

  delay(100);

  return;
}

void setup() {
  Serial.begin(115200);

  lcd.init(); lcd.backlight(); //inicializo lcd

  pinMode(PIN_S1, INPUT_PULLUP);
  pinMode(PIN_S2, INPUT_PULLUP);
  pinMode(BTNConfig, INPUT_PULLUP);
  pinMode(BTNUp, INPUT_PULLUP);
  pinMode(BTNDown, INPUT_PULLUP);

  // Interrupción en FALLING (cuando la aleta entra en la ranura y corta la luz)
  attachInterrupt(digitalPinToInterrupt(PIN_S1), isr_inicio, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_S2), isr_fin, FALLING);

  Serial.println(">>> SISTEMA DE PATADA LISTO <<<");
  Serial.printf("Configuración: Distancia %.2f m | Esperando impacto...\n", DISTANCIA_M);
}

void loop() {
  
  // 1. Verificar si se completó el recorrido
  if (calculando && t2 > 0) {
    unsigned long duracion_us = t2 - t1;
    float segundos = duracion_us / 1000000.0;
    
    // Calcular Velocidad (v = d/t)
    float velocidad_ms = DISTANCIA_M / segundos;
    
    // Algoritmo de Puntaje (Ejemplo: Velocidad x 100 / dificultad)
    // Una patada rápida puede ir a 10-15 m/s
    float puntaje = (velocidad_ms * 100) / factorDificultad;

    if(puntaje > 999) puntaje = 999;

    for (int i = 0; i < 3; i++) {
      if (puntaje > Podio[i]) {
        // 1. Desplazar los puntajes hacia abajo desde el final hasta la posición i
        for (int j = 2; j > i; j--) {
          Podio[j] = Podio[j - 1];
        }
        
        // 2. Insertar el nuevo puntaje en la posición i
        Podio[i] = (int)puntaje;
        
        break; 
      }
    }

    Serial.println("\n--- RESULTADO ---");
    Serial.printf("Tiempo: %.4f seg\n", segundos);
    Serial.printf("Velocidad: %.2f m/s\n", velocidad_ms);
    Serial.printf("PUNTAJE: %d\n", (int)puntaje);
    Serial.println("--- PODIO ---");
    for(int i = 0; i < 3; i++) {
        // IMPORTANTE: Usamos %.0f porque Podio es un array de floats.
        Serial.printf("Puesto %d: %.0f\n", i + 1, Podio[i]);
    }
    Serial.println("-----------------");

    delay(3000); // Pausa para mostrar el puntaje
    
    // Resetear variables
    t1 = 0; t2 = 0;
    calculando = false;
    Serial.println("\nEsperando siguiente patada...");
  }

  // 2. Lógica de Timeout (Si se activa S1 pero nunca llega a S2)
  if (calculando && t2 == 0) {
    if ((micros() - t1) > (TIMEOUT_MS * 1000)) {
      Serial.println("\n[ERROR] Golpe demasiado débil o sensor obstruido.");
      t1 = 0;
      calculando = false;
    }
  }

  if(digitalRead(BTNConfig) == LOW) Configurar();
}