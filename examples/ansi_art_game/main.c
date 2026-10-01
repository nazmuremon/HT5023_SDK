// examples/ansi_art_game/main.c — ANSI platformer mini-game over UART2
#include <stdbool.h>
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"
#include "ht5023/uart.h"
#include "ht5023/core/system.h"

extern void led_gpio_init(void);
extern void led_toggle(void);

#ifndef ANSI_GAME_SYSCLK_HZ
#define ANSI_GAME_SYSCLK_HZ HT5023_FSYS_HZ
#endif

#ifndef ANSI_GAME_BAUD
#define ANSI_GAME_BAUD 9600u
#endif

#define WORLD_W 72u
#define WORLD_H 10u
#define VIEW_W  24u
#define VIEW_H  WORLD_H

#define TILE_EMPTY    0u
#define TILE_GROUND   1u
#define TILE_PLATFORM 2u
#define TILE_SPIKE    3u
#define TILE_COIN     4u
#define TILE_FLAG     5u

typedef struct {
  bool left;
  bool right;
  bool jump;
  bool restart;
} input_t;

static uint8_t g_map[WORLD_H][WORLD_W];
static uint8_t g_player_x;
static uint8_t g_player_y;
static int8_t g_vy;
static uint8_t g_start_x;
static uint8_t g_start_y;
static uint8_t g_flag_x;
static uint8_t g_flag_y;
static uint8_t g_coins_total;
static uint8_t g_coins_got;
static uint32_t g_score;
static uint32_t g_best_score;
static uint32_t g_runs;
static bool g_dead;
static bool g_won;

static void delay_cycles(uint32_t cycles) {
  for (volatile uint32_t i = 0; i < cycles; ++i) {
    __asm__ volatile ("nop");
  }
}

static void uputs(const char* s) {
  ht5023_uart_write_str(HT5023_UART_ID_2, s);
}

static void uputc(char c) {
  ht5023_uart_write_byte(HT5023_UART_ID_2, (uint8_t)c);
}

static void uprint_u32(uint32_t v) {
  char buf[10];
  uint8_t n = 0u;
  if (v == 0u) {
    uputc('0');
    return;
  }
  while (v != 0u && n < sizeof(buf)) {
    buf[n++] = (char)('0' + (v % 10u));
    v /= 10u;
  }
  while (n != 0u) {
    uputc(buf[--n]);
  }
}

static uint32_t current_score(void) {
  return g_score + g_player_x;
}

static void map_clear(void) {
  for (uint8_t y = 0u; y < WORLD_H; ++y) {
    for (uint8_t x = 0u; x < WORLD_W; ++x) {
      g_map[y][x] = TILE_EMPTY;
    }
  }
}

static void set_tile(uint8_t x, uint8_t y, uint8_t t) {
  if (x < WORLD_W && y < WORLD_H) {
    g_map[y][x] = t;
  }
}

static uint8_t get_tile(uint8_t x, uint8_t y) {
  if (x >= WORLD_W || y >= WORLD_H) return TILE_EMPTY;
  return g_map[y][x];
}

static bool tile_is_solid(uint8_t t) {
  return (t == TILE_GROUND || t == TILE_PLATFORM || t == TILE_SPIKE);
}

static bool tile_is_hazard(uint8_t t) {
  return (t == TILE_SPIKE);
}

static bool is_solid_xy(int16_t x, int16_t y) {
  if (x < 0 || x >= (int16_t)WORLD_W) return true;
  if (y < 0) return true;
  if (y >= (int16_t)WORLD_H) return false;
  return tile_is_solid(g_map[(uint8_t)y][(uint8_t)x]);
}

static void carve_pit(uint8_t x0, uint8_t x1) {
  for (uint8_t x = x0; x <= x1 && x < WORLD_W; ++x) {
    set_tile(x, (uint8_t)(WORLD_H - 1u), TILE_EMPTY);
  }
}

static void add_platform(uint8_t x0, uint8_t y, uint8_t len) {
  for (uint8_t i = 0u; i < len; ++i) {
    set_tile((uint8_t)(x0 + i), y, TILE_PLATFORM);
  }
}

static void add_coins_row(uint8_t x0, uint8_t y, uint8_t len, uint8_t step) {
  for (uint8_t i = 0u; i < len; ++i) {
    uint8_t x = (uint8_t)(x0 + i);
    if ((step == 0u) || ((i % step) == 0u)) {
      if (get_tile(x, y) == TILE_EMPTY) set_tile(x, y, TILE_COIN);
    }
  }
}

static void build_level(void) {
  map_clear();

  for (uint8_t x = 0u; x < WORLD_W; ++x) {
    set_tile(x, (uint8_t)(WORLD_H - 1u), TILE_GROUND);
  }

  // Pits and bridges to force jumping/landing timing.
  carve_pit(15u, 17u);
  carve_pit(31u, 34u);
  carve_pit(46u, 48u);
  carve_pit(58u, 61u);

  add_platform(13u, 7u, 7u);   // above pit #1
  add_platform(28u, 6u, 9u);   // above pit #2
  add_platform(44u, 7u, 7u);   // above pit #3
  add_platform(56u, 6u, 10u);  // above pit #4 + lead to finish

  // Mid-level route and optional coin path.
  add_platform(6u, 8u, 4u);
  add_platform(22u, 7u, 5u);
  add_platform(38u, 8u, 4u);
  add_platform(52u, 8u, 3u);
  add_platform(66u, 5u, 5u);

  // Small obstacles and spikes on safe ground.
  set_tile(11u, (uint8_t)(WORLD_H - 1u), TILE_SPIKE);
  set_tile(24u, (uint8_t)(WORLD_H - 1u), TILE_SPIKE);
  set_tile(41u, (uint8_t)(WORLD_H - 1u), TILE_SPIKE);
  set_tile(54u, (uint8_t)(WORLD_H - 1u), TILE_SPIKE);

  add_platform(19u, 5u, 3u);
  add_platform(35u, 4u, 3u);
  add_platform(49u, 5u, 3u);

  add_coins_row(7u, 7u, 3u, 1u);
  add_coins_row(14u, 6u, 5u, 2u);
  add_coins_row(23u, 6u, 4u, 1u);
  add_coins_row(29u, 5u, 7u, 2u);
  add_coins_row(39u, 7u, 3u, 1u);
  add_coins_row(45u, 6u, 5u, 2u);
  add_coins_row(57u, 5u, 8u, 2u);
  add_coins_row(66u, 4u, 5u, 1u);

  g_start_x = 2u;
  g_start_y = (uint8_t)(WORLD_H - 2u);
  g_flag_x = 69u;
  g_flag_y = 4u;
  set_tile(g_flag_x, g_flag_y, TILE_FLAG);

  g_coins_total = 0u;
  for (uint8_t y = 0u; y < WORLD_H; ++y) {
    for (uint8_t x = 0u; x < WORLD_W; ++x) {
      if (g_map[y][x] == TILE_COIN) g_coins_total++;
    }
  }
}

static void reset_run(void) {
  if (current_score() > g_best_score) g_best_score = current_score();
  build_level();
  g_player_x = g_start_x;
  g_player_y = g_start_y;
  g_vy = 0;
  g_coins_got = 0u;
  g_score = 0u;
  g_dead = false;
  g_won = false;
  g_runs++;
}

static bool player_grounded(void) {
  return is_solid_xy(g_player_x, (int16_t)g_player_y + 1);
}

static void try_move_horizontal(int8_t dx) {
  int16_t nx;
  if (dx == 0 || g_dead || g_won) return;

  nx = (int16_t)g_player_x + dx;
  if (nx < 0 || nx >= (int16_t)WORLD_W) return;
  if (!is_solid_xy(nx, g_player_y)) {
    g_player_x = (uint8_t)nx;
  }
}

static void collect_or_trigger(void) {
  uint8_t t;
  if (g_player_y >= WORLD_H || g_player_x >= WORLD_W) {
    g_dead = true;
    return;
  }

  t = g_map[g_player_y][g_player_x];
  if (t == TILE_COIN) {
    g_map[g_player_y][g_player_x] = TILE_EMPTY;
    if (g_coins_got < 255u) g_coins_got++;
    g_score += 10u;
    led_toggle();
  } else if (t == TILE_FLAG) {
    g_won = true;
    g_score += 100u;
    led_toggle();
  } else if (tile_is_hazard(t)) {
    g_dead = true;
    led_toggle();
  }
}

static void apply_vertical_physics(input_t in) {
  bool grounded = player_grounded();

  if (in.jump && grounded) {
    g_vy = -3;
    led_toggle();
  } else if (grounded && g_vy > 0) {
    g_vy = 0;
  }

  if (!grounded && g_vy < 3) {
    g_vy++;
  }

  if (g_vy < 0) {
    int8_t steps = (int8_t)(-g_vy);
    while (steps-- > 0) {
      if (is_solid_xy(g_player_x, (int16_t)g_player_y - 1)) {
        g_vy = 0;
        break;
      }
      g_player_y--;
    }
  } else if (g_vy > 0) {
    int8_t steps = g_vy;
    while (steps-- > 0) {
      if (is_solid_xy(g_player_x, (int16_t)g_player_y + 1)) {
        g_vy = 0;
        break;
      }
      g_player_y++;
      if (g_player_y >= WORLD_H) {
        g_dead = true;
        break;
      }
    }
  } else if (!grounded) {
    // Begin falling on the first frame after walking off a ledge.
    g_vy = 1;
  }
}

static input_t poll_input(void) {
  input_t in = {0};
  uint8_t c;
  while (ht5023_uart_try_read_byte(HT5023_UART_ID_2, &c)) {
    if (c == 'a' || c == 'A' || c == 'h' || c == 'H') in.left = true;
    else if (c == 'd' || c == 'D' || c == 'l' || c == 'L') in.right = true;
    else if (c == 'w' || c == 'W' || c == 'k' || c == 'K' || c == ' ') in.jump = true;
    else if (c == 'r' || c == 'R') in.restart = true;
  }
  return in;
}

static uint8_t clamp_cam_x(void) {
  int16_t cam = (int16_t)g_player_x - 7;
  int16_t max_cam = (int16_t)WORLD_W - (int16_t)VIEW_W;
  if (cam < 0) cam = 0;
  if (cam > max_cam) cam = max_cam;
  return (uint8_t)cam;
}

static uint8_t sky_spark(uint8_t wx, uint8_t y, uint32_t frame) {
  uint8_t h = (uint8_t)(wx * 17u + y * 29u + (uint8_t)frame * 3u);
  if ((h & 0x1Fu) == 0u) return '.';
  if ((h & 0x3Fu) == 9u) return '\'';
  return ' ';
}

static void draw_cell(uint8_t wx, uint8_t y, uint32_t frame) {
  uint8_t t = get_tile(wx, y);
  bool is_player = (wx == g_player_x && y == g_player_y && !g_dead);
  bool player_dead_here = (wx == g_player_x && y == g_player_y && g_dead);

  if (is_player) {
    if (g_won) uputs("\x1b[1;32m@\x1b[0m");
    else       uputs("\x1b[1;36m@\x1b[0m");
    return;
  }
  if (player_dead_here) {
    uputs("\x1b[1;37;41mX\x1b[0m");
    return;
  }

  switch (t) {
    case TILE_GROUND:
      if (y == (uint8_t)(WORLD_H - 1u)) uputs("\x1b[1;32m#\x1b[0m");
      else                              uputs("\x1b[32m#\x1b[0m");
      break;
    case TILE_PLATFORM:
      if (((frame + wx) & 1u) == 0u) uputs("\x1b[1;34m=\x1b[0m");
      else                           uputs("\x1b[36m=\x1b[0m");
      break;
    case TILE_SPIKE:
      uputs("\x1b[1;31m^\x1b[0m");
      break;
    case TILE_COIN:
      if (((frame + y) & 1u) == 0u) uputs("\x1b[1;33m*\x1b[0m");
      else                          uputs("\x1b[93m+\x1b[0m");
      break;
    case TILE_FLAG:
      if (((frame >> 1u) & 1u) == 0u) uputs("\x1b[1;35mF\x1b[0m");
      else                            uputs("\x1b[95mF\x1b[0m");
      break;
    default:
      if (y == (uint8_t)(WORLD_H - 2u)) {
        uputs("\x1b[90m_\x1b[0m");
      } else {
        uputc((char)sky_spark(wx, y, frame));
      }
      break;
  }
}

static void render(uint32_t frame) {
  uint8_t cam_x = clamp_cam_x();

  uputs("\x1b[H");
  uputs("\x1b[2K\x1b[1;35mHT5023 ANSI PLATFORMER\x1b[0m  ");
  uputs("\x1b[1;36mQuest Run\x1b[0m\r\n");

  uputs("\x1b[2K\x1b[90mControls:\x1b[0m ");
  uputs("a/d move, w jump, r restart");
  uputs("  \x1b[90mUART2 PC11/PC12 @ 9600 8N1\x1b[0m\r\n");

  uputs("\x1b[2K\x1b[1;37mScore:\x1b[0m ");
  uprint_u32(current_score());
  uputs("  \x1b[1;37mBest:\x1b[0m ");
  uprint_u32(g_best_score);
  uputs("  \x1b[1;37mCoins:\x1b[0m ");
  uprint_u32(g_coins_got);
  uputc('/');
  uprint_u32(g_coins_total);
  uputs("  \x1b[1;37mRun:\x1b[0m ");
  uprint_u32(g_runs);
  uputs("\r\n");

  uputs("\x1b[2K\x1b[36m+------------------------+\x1b[0m\r\n");
  for (uint8_t y = 0u; y < VIEW_H; ++y) {
    uputs("\x1b[36m|\x1b[0m");
    for (uint8_t x = 0u; x < VIEW_W; ++x) {
      draw_cell((uint8_t)(cam_x + x), y, frame);
    }
    uputs("\x1b[36m|\x1b[0m\r\n");
  }
  uputs("\x1b[2K\x1b[36m+------------------------+\x1b[0m\r\n");

  uputs("\x1b[2K");
  if (g_won) {
    uputs("\x1b[1;32mLevel clear!\x1b[0m ");
    uputs("Press \x1b[1;37mr\x1b[0m to play again.");
  } else if (g_dead) {
    uputs("\x1b[1;31mYou fell or hit spikes.\x1b[0m ");
    uputs("Press \x1b[1;37mr\x1b[0m to retry.");
  } else {
    uputs("\x1b[90mGoal:\x1b[0m reach the flashing \x1b[1;35mF\x1b[0m and collect coins.");
  }
  uputs("\r\n");
}

static void tick_game(input_t in, uint32_t frame) {
  (void)frame;
  if (in.restart) {
    reset_run();
    return;
  }

  if (g_dead || g_won) return;

  if (in.left && !in.right)      try_move_horizontal(-1);
  else if (in.right && !in.left) try_move_horizontal(1);

  apply_vertical_physics(in);

  if (g_player_y >= WORLD_H) {
    g_dead = true;
    led_toggle();
    return;
  }

  collect_or_trigger();
  if (current_score() > g_best_score) g_best_score = current_score();
}

int main(void) {
  const ht5023_uart_config_t uart_cfg = {
    .sysclk_hz = ANSI_GAME_SYSCLK_HZ,
    .baud = ANSI_GAME_BAUD,
    .enable_tx = true,
    .enable_rx = true,
  };

  led_gpio_init();
  ht5023_uart2_pins_init_pc11_pc12();
  (void)ht5023_uart_init(HT5023_UART_ID_2, &uart_cfg);

  uputs("\x1b[2J\x1b[H\x1b[?25l");

  g_best_score = 0u;
  g_runs = 0u;
  reset_run();

  for (uint32_t frame = 0u;; ++frame) {
    input_t in = poll_input();
    tick_game(in, frame);
    render(frame);

    if (g_dead || g_won) delay_cycles(90000u);
    else                 delay_cycles(65000u);
  }
}
