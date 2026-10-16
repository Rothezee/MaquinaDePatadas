# Handoff — Máquina de patada (Arduino Mega)

Firmware principal: `MaquinaDePatada.ino` · Panel usuario 40×16 (`AnimPatea.h`) · LCD I2C 16×2 (solo técnicos).

## Reglas de producto (acordadas)

### 0. Dos pantallas, dos roles

| Salida | Quién la mira | Contenido |
|--------|----------------|-----------|
| **Panel LED** — 10 módulos 8×8 en cadena (640 px, **40×16**, pin 25) | **Jugador** | Fases de `AnimPatea.h` (port de `patea.html`): ficha, dificultad, PATEA, patada, resultado, victoria o derrota |
| **LCD I2C 16×2** | **Técnico / programación** | Créditos (`C:`), nivel (`FACIL`/`MEDIO`/`CRACK`, `*` = confirmado), `P+` = partida pagada, código de estado (`LISTO`, `VUELO`…), errores, menú config |

- No diseñar mensajes “para el público” en el LCD; lo que ve el usuario va al **panel**.
- Referencia visual: `animaciones_40x16/patea.html`. En placa: `AnimPatea.h` (dibujo en vivo, ~12 fps).
- Sin crédito: fase **ficha** (`INSERTE` / `FICHA`).
- Con crédito, eligiendo o esperando pelota: fase **dificultad** (meta 120 / 180 / 240).
- Pelota bajando o lista: fase **PATEA**.
- En vuelo: fase **patada** (28 cuadros, se queda en el último).
- Gol válido: el puntaje 0–999 se escala a **0–300** y se compara con la meta. Luego resultado → GANASTE / VOS=1 o MAQUINA=1 → GAME OVER. El cooldown no termina hasta que esa secuencia acaba.
- La máquina sigue siendo **una patada por crédito** (en pantalla figura PATADA 1).

### 1. Sin crédito = solo atracción y configuración

- **Sesión de juego** = `creditos > 0` **o** `partidaEnCurso` (ya se descontó moneda y puede reintentar sin pagar otra vez).
- Sin sesión:
  - Panel: fase ficha (inserir moneda).
  - LCD (técnico): `C:0`, estado `ESP_PEL`, `P4!` si pin 4 no está tapado.
  - **START** y **BTNDificultad (14)** no hacen nada.
  - No se procesan sensores de jugada ni timeouts de partida (`normalizarModoAtraccion` limpia estados colgados).
- **Menú técnico (BTNConfig 12)** siempre disponible, con o sin crédito.

### 2. Moneda en cualquier momento

- ISR del monedero (pin 32) **no se desactiva** en el menú de configuración (solo se desactivan ISR de sensores 2 y 4).
- Durante el **show LED** y las **pausas del menú**, el loop llama `atenderMonedero()` para no perder pulsos.
- Al detectar moneda: suma créditos, resetea selección de dificultad (FACIL) y actualiza LCD.

### 3. La máquina no debe quedarse colgada

- Cada jugada pagada debe **terminar** en reposo o en error recuperable con reintento (`partidaEnCurso`).
- **Timeouts y recuperación** (con sesión activa):
  - Vuelo sin gol en pin 4 → error + recuperación.
  - Pelota soltada y pin 2 no corta en 15 s → error + recuperación.
  - Pin 4 sin reposo tras jugada → error automático tras 8 s (antes solo log).
- Tras jugada válida: show → **COOLDOWN** → reposo o **ESPERANDO_LIBERAR** hasta que pin 4 tape de nuevo.
- Sin crédito: cualquier estado de juego a medias se **fuerza a atracción** (excepto **COOLDOWN** post-jugada hasta que termine el timer).

## Flujo de juego (con sesión)

1. Reposo: pelota arriba, **pin 4 cortado**.
2. **14** cicla FACIL / MEDIO / CRACK · **13** confirma · **13** otra vez suelta pelota (bobina pin 22, consume crédito la primera vez).
3. Pin 2 cortado → **LISTO PARA PATEAR**.
4. Patada (pin 2 libre) → cronómetro hasta pin 4 cortado → puntaje → show → cooldown → reposo.

## Pines (referencia rápida)

| Pin | Función |
|-----|---------|
| 2 | Sensor bajo (listo / patada) |
| 4 | Sensor alto (reposo / gol) |
| 22 | Bobina |
| 25 | Panel usuario NeoPixel (10×8×8, 40×16) |
| 32 | Monedero |
| 12 | Config (menú) |
| 13 | Start |
| 14 | Dificultad (juego) / bajar valor (menú) |
| 15 | Up (menú) |

## EEPROM (menú técnico)

- Factor float y distancia entre sensores (metros). Los niveles FACIL/MEDIO/CRACK en juego usan `FACTOR_POR_NIVEL[]` en RAM al confirmar con START.

## Pendiente / próximos pasos

- Probar en hardware: moneda durante menú y durante el show del panel.
- Afinar tiempos (`TIMEOUT_*`). El show de resultado ya no usa `COOLDOWN_MS`: espera a que terminen las fases.
- Cableado serpiente del panel: `pixelShow()` (filas pares L→R, impares R→L). Si la imagen sale espejada, ajustar solo esa función.

## Depuración

- `DEBUG_SENSORES 1`: trazas Serial de bordes en sensores.
- Estados nombrados en Serial al cambiar con `cambiarEstado()`.
