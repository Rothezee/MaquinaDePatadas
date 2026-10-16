# Máquina de patadas (arcade)

Firmware para una máquina de patadas con **monedero**, **bobina**, dos sensores ópticos y medición de velocidad entre sensores. La placa objetivo es **Arduino Mega 2560**.

Repositorio: [github.com/Rothezee/MaquinaDePatadas](https://github.com/Rothezee/MaquinaDePatadas)

## Qué hace

- Acepta **fichas** en cualquier momento (incluso en menú técnico o durante animaciones).
- Con crédito: el jugador elige **dificultad**, confirma y suelta la pelota; al patear se mide el tiempo **pin 2 → pin 4** y se calcula velocidad y puntaje.
- Si ya pagó una moneda y la jugada falla, puede **reintentar** sin insertar otra ficha (`partidaEnCurso`).
- Tras un gol válido, el **panel LED** muestra el show de resultado (victoria o derrota según meta de dificultad).
- La máquina **no debe quedarse colgada**: timeouts y recuperación automática; sin crédito solo hay atracción y menú técnico.

Documentación interna detallada: [`REGLAS_MAQUINA.md`](REGLAS_MAQUINA.md).

## Pantallas (dos roles)

| Salida | Público | Uso |
|--------|---------|-----|
| **Panel NeoPixel** — 10 módulos **8×8** en cadena (**40×16**, 640 px, pin **25**) | Jugador | Animación en `AnimPatea.h` (port de [`animaciones_40x16/patea.html`](animaciones_40x16/patea.html)): ficha, dificultad, “PATEA”, patada, puntaje, ganaste / perdés |
| **LCD I2C 16×2** (dirección `0x27`) | Técnico | Créditos, nivel, estado interno (`LISTO`, `VUELO`…), errores, menú de calibración |

El LCD **no** es la interfaz del jugador. Cualquier mensaje “de público” va al panel.

### Fases del panel (resumen)

| Fase | Cuándo |
|------|--------|
| Inserte ficha | Sin crédito (atracción) |
| Dificultad (meta 120 / 180 / 240) | Con crédito, en reposo o eligiendo nivel |
| PATEA | Pelota bajando o lista para patear |
| La patada | Pelota en vuelo (animación de 28 cuadros) |
| Resultado + fin de partida | Tras gol válido; puntaje 0–999 se escala a **0–300** y se compara con la meta |

Hoy es **una patada por crédito** (en pantalla: “PATADA 1”).

## Hardware

| Pieza | Notas |
|-------|--------|
| **Arduino Mega 2560** | Interrupciones en sensores y monedero |
| **10× matriz 8×8** NeoPixel (WS2812), cable en serie | Un solo pin DATA; mapeo serpiente en `pixelShow()` |
| **LCD 16×2 + I2C** | SDA/SCL según tu shield (típico Mega: 20/21) |
| **2 sensores** ópticos en herradura | Pin **2** (bajo), pin **4** (alto / reposo) |
| **Monedero** | Pulso activo LOW, pin **32** |
| **Relé / bobina** | Suelta pelota, pin **22** (`BOBINA_MS`, polaridad en `#define`) |
| **4 botones** | Pull-up interno, activos en LOW |

## Pinout (firmware actual)

| Pin Mega | Función |
|----------|---------|
| **2** | Sensor bajo: corte al bajar; **libre** al patear (inicio cronómetro) |
| **4** | Sensor alto: en **reposo** la pelota lo tapa (corte); fin de cronómetro al volver |
| **12** | `BTNConfig` — menú técnico (mantener / soltar para cambiar pantalla) |
| **13** | `BTNStart` — confirma dificultad; luego suelta pelota |
| **14** | `BTNDificultad` — cicla FACIL / MEDIO / CRACK (en menú: baja valor) |
| **15** | `BTNUp` — sube valor en menú técnico |
| **22** | Bobina / relé |
| **25** | DATA NeoPixel (panel 40×16) |
| **32** | Monedero |

Ajustá `NIVEL_CORTE_LISTO` y `NIVEL_CORTE_DESTINO` si tu sensor usa otra lógica al cortar el haz.

## Flujo de juego

1. **Reposo:** pelota arriba tapando pin **4**.
2. Insertar moneda → créditos en LCD; panel pasa a fase de dificultad.
3. **Pin 14:** FACIL → MEDIO → CRACK. **Pin 13:** confirma nivel (`*` en LCD).
4. **Pin 13** otra vez: pulso de bobina, baja la pelota (se consume **un** crédito la primera vez de esa partida).
5. Pin **2** cortado → listo para patear (panel: “PATEA”).
6. Patada → pin **2** libre → cronómetro hasta que pin **4** vuelve a cortarse.
7. Puntaje, show en panel, cooldown; vuelta a reposo cuando pin **4** queda tapado.

### Dificultad en juego

| Nivel | Factor (puntaje) | Meta visual (escala 0–300) |
|-------|------------------|----------------------------|
| FACIL | 0.45 | 120 |
| MEDIO | 0.65 | 180 |
| CRACK | 1.00 | 240 |

Fórmula de puntaje medido:

`Puntaje = (velocidad_m/s × 100) / factorDificultad` (tope 999 en firmware).

Los factores de juego vienen de `FACTOR_POR_NIVEL[]` al confirmar con START. El menú técnico guarda otro **factor float** y la **distancia entre sensores** en EEPROM (calibración).

### Reglas de operación

1. **Sin crédito** = solo atracción en panel + menú config; no hay lógica de jugada (START / dificultad ignorados).
2. **Moneda siempre activa** (ISR no se corta en menú técnico).
3. **Sin bloqueos**: timeouts de vuelo, bajada y reposo; recuperación con reintento si ya pagó.

## Menú técnico (`BTNConfig`)

- Pantalla 1: **factor de dificultad** (float) — `BTNUp` / `BTNDificultad`.
- Pantalla 2: **distancia entre sensores** (metros) — mismos botones.
- Soltar `BTNConfig` en cada pantalla para avanzar; al salir se guarda en **EEPROM** del Mega.

## Archivos del proyecto

| Archivo | Descripción |
|---------|-------------|
| `MaquinaDePatada.ino` | Lógica de juego, sensores, monedero, panel, LCD |
| `AnimPatea.h` | Dibujo procedural del panel (fases PATEA) |
| `animaciones_40x16/patea.html` | Simulador / referencia visual en el navegador |
| `REGLAS_MAQUINA.md` | Handoff y reglas acordadas |

## Librerías (Arduino IDE)

- [Adafruit NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel)
- [LiquidCrystal I2C](https://github.com/johnrickman/LiquidCrystal_I2C) (o equivalente `0x27`)
- `EEPROM` (incluida en el core AVR)

## Depuración

- Serial **115200** baud.
- `DEBUG_SENSORES` en `1`: trazas de bordes en sensores.
- Cambios de estado impresos con `cambiarEstado()`.

## Calibración del panel

Si la imagen sale invertida o con filas cruzadas, corregí solo **`pixelShow()`** en el `.ino` (serpiente: filas pares izquierda→derecha, impares derecha→izquierda).

## Historial

Versión anterior del repo usaba **ESP32**, dos tiras NeoPixel y lógica de “doble paso” en un solo sensor. Esta rama es **Mega**, dos sensores (2 y 4), un panel 40×16 y monedero.
