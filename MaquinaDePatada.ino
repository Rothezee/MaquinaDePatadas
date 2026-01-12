#include <Arduino.h>

// --- Configuración de Hardware ---
const int PIN_S1 = 26; // Sensor de inicio (ranura 1)
const int PIN_S2 = 25; // Sensor de fin (ranura 2)

// --- Parámetros de la Máquina ---
const float DISTANCIA_M = 0.12;       // 12 cm convertidos a metros
float factorDificultad = 1.0;         // Ajustable (1.0 = normal)
const unsigned long TIMEOUT_MS = 2000; // Tiempo máximo entre sensores (2 seg)

// --- Variables de Tiempo (Volatile para Interrupciones) ---
volatile unsigned long t1 = 0;
volatile unsigned long t2 = 0;
volatile bool calculando = false;
volatile bool error_timeout = false;

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

void setup() {
  Serial.begin(115200);
  
  pinMode(PIN_S1, INPUT_PULLUP);
  pinMode(PIN_S2, INPUT_PULLUP);

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

    Serial.println("\n--- RESULTADO ---");
    Serial.printf("Tiempo: %.4f seg\n", segundos);
    Serial.printf("Velocidad: %.2f m/s\n", velocidad_ms);
    Serial.printf("PUNTAJE: %d\n", (int)puntaje);
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
}
