/*
  ThreeBodyProblem

  Serial ANSI visualization of the classic equal-mass figure-eight three-body
  orbit. Open a terminal on UART2 at 115200 baud for the animated output:

  - PC11 -> TX
  - PC12 -> RX

  Recommended terminal: one that supports ANSI escape sequences.
*/

#include <math.h>

namespace {

constexpr uint8_t kBodyCount = 3u;
constexpr uint8_t kWidth = 52u;
constexpr uint8_t kHeight = 20u;
constexpr double kViewRadius = 1.35;
constexpr double kSoftening = 0.003;
constexpr double kDt = 0.008;
constexpr uint8_t kSubstepsPerFrame = 4u;
constexpr uint8_t kTrailMax = 9u;
constexpr unsigned long kFrameDelayMs = 70u;

struct Vec2 {
  double x;
  double y;
};

struct Body {
  Vec2 pos;
  Vec2 vel;
  Vec2 acc;
  char glyph;
  const char* color;
};

Body g_bodies[kBodyCount] = {
    {{-0.97000436, 0.24308753}, {0.4662036850, 0.4323657300}, {0.0, 0.0}, '1', "\x1B[38;5;203m"},
    {{0.97000436, -0.24308753}, {0.4662036850, 0.4323657300}, {0.0, 0.0}, '2', "\x1B[38;5;81m"},
    {{0.0, 0.0}, {-0.93240737, -0.86473146}, {0.0, 0.0}, '3', "\x1B[38;5;228m"},
};

uint8_t g_trail[kHeight][kWidth] = {};
unsigned long g_frame = 0u;

constexpr char kTrailRamp[] = " .,:;+*#%@";

void computeAccelerations() {
  for (uint8_t i = 0; i < kBodyCount; ++i) {
    g_bodies[i].acc = {0.0, 0.0};
  }

  for (uint8_t i = 0; i < kBodyCount; ++i) {
    for (uint8_t j = i + 1u; j < kBodyCount; ++j) {
      const double dx = g_bodies[j].pos.x - g_bodies[i].pos.x;
      const double dy = g_bodies[j].pos.y - g_bodies[i].pos.y;
      const double dist2 = dx * dx + dy * dy + kSoftening * kSoftening;
      const double inv_dist = 1.0 / sqrt(dist2);
      const double inv_dist3 = inv_dist * inv_dist * inv_dist;
      const double ax = dx * inv_dist3;
      const double ay = dy * inv_dist3;

      g_bodies[i].acc.x += ax;
      g_bodies[i].acc.y += ay;
      g_bodies[j].acc.x -= ax;
      g_bodies[j].acc.y -= ay;
    }
  }
}

void integrateStep() {
  for (uint8_t i = 0; i < kBodyCount; ++i) {
    g_bodies[i].vel.x += 0.5 * kDt * g_bodies[i].acc.x;
    g_bodies[i].vel.y += 0.5 * kDt * g_bodies[i].acc.y;
    g_bodies[i].pos.x += kDt * g_bodies[i].vel.x;
    g_bodies[i].pos.y += kDt * g_bodies[i].vel.y;
  }

  computeAccelerations();

  for (uint8_t i = 0; i < kBodyCount; ++i) {
    g_bodies[i].vel.x += 0.5 * kDt * g_bodies[i].acc.x;
    g_bodies[i].vel.y += 0.5 * kDt * g_bodies[i].acc.y;
  }
}

bool projectToScreen(const Vec2& pos, uint8_t* sx, uint8_t* sy) {
  const double nx = (pos.x / kViewRadius + 1.0) * 0.5;
  const double ny = (1.0 - ((pos.y / kViewRadius + 1.0) * 0.5));

  if (nx < 0.0 || nx > 1.0 || ny < 0.0 || ny > 1.0) return false;

  const int x = (int)(nx * (double)(kWidth - 1u));
  const int y = (int)(ny * (double)(kHeight - 1u));
  if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) return false;

  *sx = (uint8_t)x;
  *sy = (uint8_t)y;
  return true;
}

void fadeTrail() {
  for (uint8_t y = 0; y < kHeight; ++y) {
    for (uint8_t x = 0; x < kWidth; ++x) {
      if (g_trail[y][x] > 0u) --g_trail[y][x];
    }
  }
}

void stampBodiesIntoTrail() {
  for (uint8_t i = 0; i < kBodyCount; ++i) {
    uint8_t sx = 0u;
    uint8_t sy = 0u;
    if (projectToScreen(g_bodies[i].pos, &sx, &sy)) {
      g_trail[sy][sx] = kTrailMax;
    }
  }
}

void renderFrame() {
  uint8_t body_x[kBodyCount] = {};
  uint8_t body_y[kBodyCount] = {};
  bool visible[kBodyCount] = {};

  for (uint8_t i = 0; i < kBodyCount; ++i) {
    visible[i] = projectToScreen(g_bodies[i].pos, &body_x[i], &body_y[i]);
  }

  Serial.print("\x1B[H");
  Serial.println("HT5023 Figure-Eight Three-Body Problem");
  Serial.println("UART2 115200 baud  |  ANSI terminal recommended");

  for (uint8_t y = 0; y < kHeight; ++y) {
    for (uint8_t x = 0; x < kWidth; ++x) {
      bool drew_body = false;
      for (uint8_t i = 0; i < kBodyCount; ++i) {
        if (visible[i] && body_x[i] == x && body_y[i] == y) {
          Serial.print(g_bodies[i].color);
          Serial.write(g_bodies[i].glyph);
          Serial.print("\x1B[0m");
          drew_body = true;
          break;
        }
      }

      if (!drew_body) {
        Serial.write(kTrailRamp[g_trail[y][x]]);
      }
    }
    Serial.println();
  }

  Serial.print("frame=");
  Serial.print((unsigned long)g_frame);
  Serial.print("  dt=");
  Serial.print((unsigned long)(kDt * 1000.0));
  Serial.println(" ms x substep");
}

}  // namespace

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  delay(100);

  computeAccelerations();
  Serial.print("\x1B[2J\x1B[H\x1B[?25l");
  Serial.println("Booting three-body simulator...");
}

void loop() {
  for (uint8_t i = 0; i < kSubstepsPerFrame; ++i) {
    integrateStep();
  }

  fadeTrail();
  stampBodiesIntoTrail();
  renderFrame();
  digitalToggle(LED_BUILTIN);
  ++g_frame;
  delay(kFrameDelayMs);
}
