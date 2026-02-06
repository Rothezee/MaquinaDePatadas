# 🥊 Arcade Kick Machine - Score Keeper System

Este proyecto consiste en un sistema de medición de velocidad y gestión de puntajes para una máquina de patadas (tipo Boxer) de arcade. El sistema intercepta la señal del sensor original de la máquina para calcular la velocidad de impacto y mostrar los resultados en tiempo real a través de un diplay modular.



## 🚀 Características

* **Detección Inteligente de Giro:** Lógica diseñada para ignorar el primer corte del sensor (cuando la bolsa baja) y medir con precisión el segundo corte (impacto de regreso).
* **Interfaz Visual Dinámica:** Dos pantallas compuestas por 4 matrices LED RGB (NeoPixel) cada una, con cambio de color según el puntaje (Verde 🟢, Amarillo 🟡, Rojo 🔴).
* **Menú de Configuración Integrado:** Interfaz mediante LCD 16x2 y 3 botones para ajustar el factor de dificultad sin necesidad de reprogramar.
* **Memoria No Volátil:** Los ajustes de dificultad se guardan permanentemente en el ESP32 mediante la librería `Preferences`.

## 🛠️ Hardware Requerido

* **Microcontrolador:** ESP32.
* **Pantallas de Puntaje:** 2 Tiras de 4 matrices NeoPixel 8x8 (64 píxeles por matriz).
* **Pantalla de Ajuste:** LCD 16x2 con adaptador I2C.
* **Sensor:** Sensor óptico de herradura (original de la máquina).
* **Entradas:** 3 Pulsadores (Menú, Arriba, Abajo). 

## 📋 Conexiones (Pinout)

| Componente | Pin ESP32 | Función |
| :--- | :--- | :--- |
| **Sensor de Herradura** | GPIO 26 | Entrada de señal (con interrupción) |
| **Matrices LED P1** | GPIO 27 | Datos NeoPixel Patada 1 |
| **Matrices LED P2** | GPIO 25 | Datos NeoPixel Patada 2 |
| **Botón Menú** | GPIO 12 | Navegación y Guardado |
| **Botón Subir** | GPIO 13 | Aumentar/Subir |
| **Botón Bajar** | GPIO 14 | Disminuir/Bajar |
| **LCD 16x2 SDA** | GPIO 21 | Comunicación I2C |
| **LCD 16x2 SCL** | GPIO 22 | Comunicación I2C |



## 🕹️ Lógica de Funcionamiento

El cálculo se basa en el tiempo que la aleta física de la máquina obstruye el sensor óptico:

1.  **Estado de Espera:** Ambos displays muestran `000`. El sistema espera el primer corte del sensor.
2.  **Filtrado:** El código detecta el primer paso (ida) y activa una bandera(bloqueo).
3.  **Captura de Microsegundos:** En el segundo paso (regreso), se activa el cronómetro mediante una interrupción de hardware (`isr_sensor`) para obtener la duración exacta del corte.
4.  **Cálculo de Física:**
    $$Velocidad = \frac{Ancho\ de\ la\ Aleta}{Tiempo\ de\ paso}$$
    $$Puntaje = \frac{Velocidad \times 100}{Factor\ de\ Dificultad}$$
5.  **Animación:** El puntaje sube de forma progresiva en los LEDs para dar un efecto arcade clásico.

## 🔧 Configuración y Calibración

Para entrar al modo de configuración, presiona el botón **BTNConfig**.
* Utiliza los botones **Up** y **Down** para modificar el `factorDificultad`.
* Un factor más bajo hará que sea más fácil llegar a 999.
* Presiona **Config** nuevamente para guardar el valor en la memoria interna.

## 📚 Librerías Utilizadas

* `Adafruit_NeoPixel`: Control de las matrices RGB.
* `LiquidCrystal_I2C`: Gestión de la pantalla LCD.
* `Preferences`: Almacenamiento de datos en la memoria Flash del ESP32.

---
