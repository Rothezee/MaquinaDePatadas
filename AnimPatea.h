#pragma once
// Animación 40×16 del jugador, port de animaciones_40x16/patea.html.
// Cada fase se dibuja en el momento (no hay fotogramas en PROGMEM).

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>

static const uint8_t PATEA_W = 40;
static const uint8_t PATEA_H = 16;

enum PateaFase : uint8_t {
  PATEA_COIN = 0,   // Inserte ficha
  PATEA_DIFF,       // Elegir dificultad
  PATEA_ASK,        // Patea
  PATEA_FLY,        // La patada (28 cuadros)
  PATEA_RES,        // Suma del puntaje (40)
  PATEA_WIN1,       // GANASTE (47)
  PATEA_WIN2,       // VOS 1 – MAQUINA 0 (48)
  PATEA_LOSE1,      // MAQUINA 1 – VOS 0 (28)
  PATEA_LOSE2,      // Parpadeo derrota (28)
  PATEA_BURLA       // GAME OVER (42)
};

struct PateaEstado {
  uint8_t fase;
  uint16_t frame;
  uint8_t dif;      // 0 FACIL, 1 MEDIO, 2 CRACK
  uint8_t patada;   // 1..3 (la máquina usa 1 por crédito)
  uint8_t fuerza;   // 0..100, arco de la pelota
  uint16_t puntos;  // valor de esta patada en escala 0..300
  uint16_t total;   // total mostrado
};

inline uint16_t pateaMeta(uint8_t dif) {
  static const uint16_t META[3] = {120, 180, 240};
  if (dif > 2) dif = 2;
  return META[dif];
}

inline uint16_t pateaHueDif(uint8_t dif) {
  static const uint16_t HUE[3] = {120, 55, 0};
  if (dif > 2) dif = 2;
  return HUE[dif];
}

inline const char* pateaNombreDif(uint8_t dif) {
  static const char* N[3] = {"FACIL", "MEDIO", "CRACK"};
  if (dif > 2) dif = 2;
  return N[dif];
}

// 0 = la fase se repite (atracción / menú / espera).
inline uint16_t pateaFrames(uint8_t fase) {
  switch (fase) {
    case PATEA_FLY:   return 28;
    case PATEA_RES:   return 40;
    case PATEA_WIN1:  return 47;
    case PATEA_WIN2:  return 48;
    case PATEA_LOSE1: return 28;
    case PATEA_LOSE2: return 28;
    case PATEA_BURLA: return 42;
    default:          return 0;
  }
}

// Puntaje interno 0..999 → escala visual del HTML (máx. 300, metas 120/180/240).
inline uint16_t pateaEscalarPuntaje(int puntaje) {
  if (puntaje < 0) puntaje = 0;
  if (puntaje > 999) puntaje = 999;
  return (uint16_t)((uint32_t)puntaje * 300UL / 999UL);
}

namespace patea_draw {

static const uint8_t FONT[][5] PROGMEM = {
  {2,5,7,5,5}, {0,0,0,0,0}, {7,4,4,4,7}, {6,5,5,5,6}, {7,4,6,4,7},
  {7,4,6,4,4}, {7,4,5,5,7}, {5,5,7,5,5}, {7,2,2,2,7}, {0,0,0,0,0},
  {5,5,6,5,5}, {4,4,4,4,7}, {5,7,7,5,5}, {6,5,5,5,5}, {7,5,5,5,7},
  {7,5,7,4,4}, {7,5,5,7,3}, {6,5,6,5,5}, {7,4,7,1,7}, {7,2,2,2,2},
  {5,5,5,5,7}, {5,5,5,5,2}, {0,0,0,0,0}, {0,0,0,0,0}, {0,0,0,0,0},
  {0,0,0,0,0},
  {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1},
  {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1}, {7,5,7,5,7}, {7,5,7,1,7},
  {0,2,7,2,0}, {0,7,0,7,0}
};

static int fontIndex(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= '0' && c <= '9') return 26 + (c - '0');
  if (c == '+') return 36;
  if (c == '=') return 37;
  return -1;
}

enum Paint : uint8_t {
  P_SOLID = 0,
  P_INSERTE,
  P_FICHA,
  P_RAINBOW,
  P_DIFFNAME,
  P_SCORE,
  P_WINSCROLL
};

struct Ctx {
  Adafruit_NeoPixel* panel;
  int (*indexOf)(int x, int y);
  uint16_t frame;
  uint8_t dif;
  uint16_t meta;
  uint16_t shown;
  uint32_t solid;
  uint16_t hue;
};

static Ctx* g = nullptr;

static uint32_t packHsl(int h, int l) {
  h %= 360;
  if (h < 0) h += 360;
  float lf = l / 100.0f;
  float c = 1.0f - fabsf(2.0f * lf - 1.0f);
  float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
  float m = lf - c * 0.5f;
  float r = 0, gv = 0, b = 0;
  int hi = h / 60;
  switch (hi) {
    case 0: r = c; gv = x; break;
    case 1: r = x; gv = c; break;
    case 2: gv = c; b = x; break;
    case 3: gv = x; b = c; break;
    case 4: r = x; b = c; break;
    default: r = c; b = x; break;
  }
  return g->panel->Color((uint8_t)((r + m) * 255), (uint8_t)((gv + m) * 255), (uint8_t)((b + m) * 255));
}

static void px(int x, int y, uint32_t col) {
  if (!col || x < 0 || x >= PATEA_W || y < 0 || y >= PATEA_H) return;
  g->panel->setPixelColor(g->indexOf(x, y), col);
}

static uint32_t paintPix(Paint mode, int X, int Y) {
  switch (mode) {
    case P_INSERTE: {
      int sweep = ((int)g->frame * 2) % 80 - 20;
      if (abs(X - sweep) < 3) return g->panel->Color(255, 255, 255);
      return packHsl(45, 50);
    }
    case P_FICHA: {
      float onda = sinf(X * 0.62f + g->frame * 0.38f) * 1.7f;
      float rel = Y - 6.0f + onda;
      if (rel < 3.1f) return g->panel->Color(0x74, 0xAC, 0xDF);
      if (rel < 6.9f) return g->panel->Color(0xF4, 0xF7, 0xFB);
      return g->panel->Color(0x74, 0xAC, 0xDF);
    }
    case P_RAINBOW:
      return packHsl(X * 9 + (int)g->frame * 18, 50);
    case P_DIFFNAME: {
      int l = ((g->frame % 16) < 12) ? 50 : 70;
      return packHsl(g->hue, l);
    }
    case P_SCORE: {
      int h = (int)((g->shown * 120UL) / (g->meta ? g->meta : 1));
      if (h > 120) h = 120;
      return packHsl(h, 50);
    }
    case P_WINSCROLL:
      return packHsl(X * 12 + (int)g->frame * 25, 50);
    default:
      return g->solid;
  }
}

static int textWidth(const char* s, uint8_t k) {
  int n = 0;
  while (s[n]) n++;
  if (n == 0) return 0;
  return n * 4 * k - k;
}

static int textCenter(const char* s, uint8_t k) {
  return (PATEA_W - textWidth(s, k)) >> 1;
}

static void txt(const char* s, int x, int y, uint8_t k, Paint mode, uint32_t solid = 0) {
  g->solid = solid;
  const int adv = 4 * k;
  for (const char* p = s; *p; ++p) {
    int fi = fontIndex(*p);
    for (uint8_t j = 0; j < 5; j++) {
      uint8_t row = (fi < 0) ? 0 : pgm_read_byte(&FONT[fi][j]);
      for (uint8_t i = 0; i < 3; i++) {
        if (row & (1 << (2 - i))) {
          for (uint8_t a = 0; a < k; a++) {
            for (uint8_t b = 0; b < k; b++) {
              int X = x + i * k + a;
              int Y = y + j * k + b;
              px(X, Y, paintPix(mode, X, Y));
            }
          }
        }
      }
    }
    x += adv;
  }
}

static const uint8_t V4[][3] PROGMEM = {
  {2,7,5}, {0,0,0}, {0,0,0}, {0,0,0}, {7,6,7}, {0,0,0}, {7,4,3},
  {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {5,7,5}, {0,0,0},
  {7,5,7}, {0,0,0}, {0,0,0}, {6,7,5}, {0,0,0}, {0,0,0}, {0,0,0},
  {5,5,2}
};

static void txtVertical(const char* s, int x, uint32_t col) {
  int y = 0;
  for (const char* p = s; *p; ++p) {
    int fi = (*p >= 'A' && *p <= 'V') ? (*p - 'A') : -1;
    for (uint8_t j = 0; j < 3; j++) {
      uint8_t row = (fi < 0) ? 0 : pgm_read_byte(&V4[fi][j]);
      for (uint8_t i = 0; i < 3; i++) {
        if (row & (1 << (2 - i))) px(x + i, y, col);
      }
      y++;
    }
    y++;
  }
}

static void solDeMayo() {
  const int cx = 19, cy = 10;
  uint32_t oro = g->panel->Color(0xF6, 0xB4, 0x0E);
  uint32_t cara = g->panel->Color(0xA8, 0x5A, 0x00);
  int d = (g->frame % 2) ? 2 : 3;
  for (int y = -1; y <= 1; y++)
    for (int x = -1; x <= 1; x++) px(cx + x, cy + y, oro);
  px(cx, cy - 3, oro); px(cx, cy + 3, oro); px(cx - 3, cy, oro); px(cx + 3, cy, oro);
  px(cx - d, cy - d, oro); px(cx + d, cy - d, oro);
  px(cx - d, cy + d, oro); px(cx + d, cy + d, oro);
  px(cx - 1, cy, cara); px(cx + 1, cy, cara); px(cx, cy + 1, cara);
}

static void cancha() {
  uint32_t linea = g->panel->Color(0x07, 0x40, 0x16);
  for (int y = 0; y < PATEA_H; y++) {
    for (int x = 0; x < PATEA_W; x++) {
      float d = hypotf(x - 19.5f, y - 7.5f);
      if (x == 0 || x == 39 || y == 0 || y == 15 || x == 19 || x == 20 || fabsf(d - 5.0f) < 0.6f)
        px(x, y, linea);
    }
  }
}

static void spr(const char* rows, uint8_t nRows, uint8_t w, int x, int y, uint32_t (*pal)(char)) {
  for (uint8_t j = 0; j < nRows; j++) {
    for (uint8_t i = 0; i < w; i++) {
      char ch = (char)pgm_read_byte(rows + j * w + i);
      if (ch != '.') px(x + i, y + j, pal(ch));
    }
  }
}

static uint32_t palJugador(char ch) {
  switch (ch) {
    case 'h': return g->panel->Color(0x8A, 0x4B, 0x12);
    case 'f': return g->panel->Color(0xFF, 0xB2, 0x7A);
    case 's': return g->panel->Color(0x74, 0xAC, 0xDF);
    case 'p': return g->panel->Color(0x1D, 0x46, 0xFF);
    case 'b': return g->panel->Color(0xFF, 0xE6, 0x00);
    default:  return g->panel->Color(255, 255, 255);
  }
}

static uint32_t palEstrella(char) {
  return (g->frame % 2) ? g->panel->Color(0xFF, 0xE6, 0x00) : g->panel->Color(255, 255, 255);
}

static uint32_t palPelota(char) {
  return g->panel->Color(255, 255, 255);
}

static const char SPR_P[] PROGMEM =
  "....hhh....."
  "....fff....."
  "....fff....."
  "...wswsw...."
  "..swswsws..."
  "..fwswswf..."
  "..fwswswf..."
  "...wswsw...."
  "....ppp.....";
static const char SPR_S[] PROGMEM =
  "....p.p....."
  "....f.f....."
  "....w.w....."
  "....w.w....."
  "....w.w....."
  "...bb.bb....";
static const char SPR_W[] PROGMEM =
  "...pp.p....."
  "..ff..f....."
  "..w...w....."
  ".bb...w....."
  "......w....."
  "......bb....";
static const char SPR_K[] PROGMEM =
  "....p.p........"
  "....f.fffwwbb.."
  "....w.........."
  "....w.........."
  "....w.........."
  "...bb..........";
static const char SPR_STAR[] PROGMEM =
  "#..#..#"
  ".#.#.#."
  "..###.."
  "#######"
  "..###.."
  ".#.#.#."
  "#..#..#";
static const char SPR_BALL[] PROGMEM =
  ".www."
  "wwwww"
  "ww.ww"
  "wwwww"
  ".www.";

static void caraGrande() {
  uint32_t Y = g->panel->Color(0xF6, 0xB4, 0x0E);
  uint32_t K = g->panel->Color(0x1A, 0x12, 0x08);
  uint32_t W = g->panel->Color(255, 255, 255);
  uint32_t O = g->panel->Color(0xE3, 0x9B, 0x00);
  uint32_t T = g->panel->Color(0x3E, 0xC0, 0xFF);
  uint32_t boca = g->panel->Color(0xE0, 0x70, 0x70);
  const int x0 = 10;
  for (int dy = 0; dy < 16; dy++) {
    for (int dx = 0; dx < 20; dx++) {
      float d = hypotf(dx - 9.5f, dy - 7.5f);
      if (d < 7.7f) px(x0 + dx, dy, d > 6.8f ? O : Y);
    }
  }
  const int8_t ojos[][2] = {
    {3,6},{4,5},{5,4},{6,4},{7,5},{8,6},{11,6},{12,5},{13,4},{14,4},{15,5},{16,6}
  };
  for (uint8_t i = 0; i < 12; i++) px(x0 + ojos[i][0], ojos[i][1], K);
  int a = g->frame % 5, b = (g->frame + 2) % 5;
  const int8_t lag[][2] = {{3, (int8_t)(7 + a)}, {4, (int8_t)(9 + a)}, {16, (int8_t)(7 + b)}, {15, (int8_t)(9 + b)}};
  for (uint8_t i = 0; i < 4; i++) if (lag[i][1] < 15) px(x0 + lag[i][0], lag[i][1], T);
  for (int dx = 5; dx <= 14; dx++) {
    px(x0 + dx, 9, W); px(x0 + dx, 10, K); px(x0 + dx, 11, K);
  }
  px(x0 + 4, 10, K); px(x0 + 15, 10, K);
  for (int dx = 6; dx <= 13; dx++) px(x0 + dx, 12, boca);
}

static int vueloX(int t) { return 15 + 3 * t; }
static int vueloY(int t, uint8_t fuerza) {
  float y = 11.0f - (1.0f + fuerza * 0.02f) * (float)t + 0.15f * (float)t * (float)t;
  return (int)(y + (y >= 0.0f ? 0.5f : -0.5f));
}

static void faseCoin() {
  txt("INSERTE", textCenter("INSERTE", 1), 0, 1, P_INSERTE);
  txt("FICHA", 1, 6, 2, P_FICHA);
  solDeMayo();
}

static void faseDiff() {
  char meta[12];
  snprintf(meta, sizeof(meta), "META %u", g->meta);
  txt(meta, 4, 0, 1, P_SOLID, g->panel->Color(255, 255, 255));
  for (uint8_t i = 0; i < 3; i++) {
    uint32_t col = (i == g->dif) ? packHsl(pateaHueDif(i), 50) : g->panel->Color(0x3A, 0x3A, 0x3A);
    for (uint8_t x = 0; x < 12; x++) px(i * 14 + x, 5, col);
  }
  g->hue = pateaHueDif(g->dif);
  txt(pateaNombreDif(g->dif), 1, 6, 2, P_DIFFNAME);
}

static void faseAsk(uint8_t patada) {
  char linea[12];
  if (patada < 1) patada = 1;
  if (patada > 3) patada = 3;
  snprintf(linea, sizeof(linea), "PATADA %u", patada);
  txt(linea, 4, 0, 1, P_SOLID, g->panel->Color(255, 255, 255));
  txt("PATEA", 1, 6, 2, P_RAINBOW);
}

static void faseFly(uint8_t fuerza) {
  uint32_t cesped = g->panel->Color(0x0C, 0x7A, 0x22);
  uint32_t blanco = g->panel->Color(255, 255, 255);
  uint32_t red = g->panel->Color(0x2A, 0x5A, 0x3A);
  for (int x = 0; x < PATEA_W; x++) px(x, 15, cesped);
  for (int y = 5; y < 15; y++) {
    px(35, y, blanco);
    for (int x = 36; x < 40; x++) if ((x + y) % 2) px(x, y, red);
  }
  for (int x = 35; x < 40; x++) px(x, 5, blanco);

  spr(SPR_P, 9, 12, 2, 1, palJugador);
  uint16_t f = g->frame;
  if (f < 8) spr(SPR_S, 6, 12, 2, 10, palJugador);
  else if (f < 14) spr(SPR_W, 6, 12, 2, 10, palJugador);
  else if (f < 20) spr(SPR_K, 6, 15, 2, 10, palJugador);
  else spr(SPR_S, 6, 12, 2, 10, palJugador);

  if (f >= 14 && f < 18) spr(SPR_STAR, 7, 7, 11, 8, palEstrella);
  if (f < 14) {
    spr(SPR_BALL, 5, 5, 15, 11, palPelota);
  } else {
    int t = (int)f - 14;
    for (int k = 1; k <= 4; k++) {
      if (t - k >= 0) {
        px(vueloX(t - k) + 2, vueloY(t - k, fuerza) + 2, packHsl(t * 30 + k * 40, 50));
      }
    }
    spr(SPR_BALL, 5, 5, vueloX(t), vueloY(t, fuerza), palPelota);
  }
}

static void faseRes(const PateaEstado& st) {
  uint16_t k = st.puntos;
  uint16_t v = k;
  uint16_t paso = (uint16_t)st.frame * 8;
  if (paso < v) v = paso;
  uint16_t t = (st.total >= k) ? (st.total - k + v) : v;
  g->shown = t;
  g->meta = pateaMeta(st.dif);
  char mas[8], tot[4];
  snprintf(mas, sizeof(mas), "+%u", v);
  snprintf(tot, sizeof(tot), "%03u", t > 999 ? 999 : t);
  txt(mas, 1, 0, 1, P_SOLID, packHsl((int)(v * 1.2f), 50));
  txt("TOTAL", 20, 0, 1, P_SOLID, g->panel->Color(0xBB, 0xBB, 0xBB));
  txt(tot, 9, 6, 2, P_SCORE);
}

static void faseWin1() {
  txt("GANASTE", (int)PATEA_W - (int)g->frame * 2, 3, 2, P_WINSCROLL);
}

static void faseWin2() {
  uint32_t cel = g->panel->Color(0x74, 0xAC, 0xDF);
  uint32_t sol = g->panel->Color(0xF6, 0xB4, 0x0E);
  uint32_t blanco = g->panel->Color(0xF4, 0xF7, 0xFB);
  for (int i = 0; i < 8; i++) px((i * 5 + (int)g->frame) % 40, (i % 2) ? 7 : 8, (i % 2) ? cel : sol);
  txt("VOS=1", textCenter("VOS=1", 1), 1, 1, P_SOLID, sol);
  txt("MAQUINA=0", textCenter("MAQUINA=0", 1), 9, 1, P_SOLID, blanco);
}

static void faseLose(bool blink) {
  uint32_t rojo = g->panel->Color(0xFF, 0x40, 0x40);
  if (blink && (g->frame % 8) >= 4) rojo = g->panel->Color(0xFF, 0xB0, 0xB0);
  uint32_t blanco = blink ? g->panel->Color(0xC8, 0xC8, 0xC8) : g->panel->Color(0xF4, 0xF7, 0xFB);
  txt("MAQUINA=1", textCenter("MAQUINA=1", 1), 1, 1, P_SOLID, rojo);
  txt("VOS=0", textCenter("VOS=0", 1), 9, 1, P_SOLID, blanco);
}

static void faseBurla() {
  caraGrande();
  uint32_t rojo = g->panel->Color(0xFF, 0x30, 0x30);
  txtVertical("GAME", 0, rojo);
  txtVertical("OVER", 37, rojo);
}

}  // namespace patea_draw

inline void pateaRender(Adafruit_NeoPixel& panel, int (*indexOf)(int x, int y), const PateaEstado& st) {
  patea_draw::Ctx ctx;
  ctx.panel = &panel;
  ctx.indexOf = indexOf;
  ctx.frame = st.frame;
  ctx.dif = (st.dif > 2) ? 2 : st.dif;
  ctx.meta = pateaMeta(ctx.dif);
  ctx.shown = 0;
  ctx.solid = 0;
  ctx.hue = pateaHueDif(ctx.dif);
  patea_draw::g = &ctx;

  panel.clear();
  bool fondo = !(st.fase == PATEA_WIN2 || st.fase == PATEA_LOSE1 ||
                 st.fase == PATEA_LOSE2 || st.fase == PATEA_BURLA);
  if (fondo) patea_draw::cancha();

  switch (st.fase) {
    case PATEA_DIFF: patea_draw::faseDiff(); break;
    case PATEA_ASK:  patea_draw::faseAsk(st.patada); break;
    case PATEA_FLY:  patea_draw::faseFly(st.fuerza); break;
    case PATEA_RES:  patea_draw::faseRes(st); break;
    case PATEA_WIN1: patea_draw::faseWin1(); break;
    case PATEA_WIN2: patea_draw::faseWin2(); break;
    case PATEA_LOSE1: patea_draw::faseLose(false); break;
    case PATEA_LOSE2: patea_draw::faseLose(true); break;
    case PATEA_BURLA: patea_draw::faseBurla(); break;
    default: patea_draw::faseCoin(); break;
  }
  patea_draw::g = nullptr;
}
