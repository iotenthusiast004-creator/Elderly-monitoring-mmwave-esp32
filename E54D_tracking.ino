/*
  E54-24LD12D (E54 D) multi-target tracking radar  ->  ESP32 (Arduino core)
  -------------------------------------------------------------------------
  Wiring (ESP32 DevKit V1):
    VIN / 5V     -> module "5V"   (3.6-5.5 V, ~66 mA; do NOT use 3V3)
    GND          -> module "GND"
    GPIO16 (RX2) <- module "TX"   (use a 1k/2k divider if you measure 5 V on it)
    GPIO17 (TX2) -> module "RX"
  (J1 connector, or the J2 pads: 5V, GND, TXD, RXD)

  USB serial monitor: 115200 baud.  Radar UART: 256000 8N1.

  Output (CSV, one line per active target per frame, ~10 Hz):
    ms,id,x_mm,y_mm,speed_cm_s,dist_m,angle_deg,in_bed_zone
  Lines starting with '#' are events (appear / lost / fast move).

  Keys:  1 = single-target mode    3 = three-target mode

  NOTE: written from the E54-24LD12D spec v1.0; check against your unit.
*/
#include <Arduino.h>
#include <math.h>

#define D_RX_PIN 16
#define D_TX_PIN 17
#define D_BAUD   256000

// Some firmware needs enable/end-config around mode commands (the manual does
// not show it for this module). Set to 0 if you see odd ACKs.
#define D_WRAP_CONFIG 1

// Demo thresholds (tune on your setup)
#define MOVE_CMS 2        // |speed| >= this counts as "moving"
#define FAST_CMS 150      // |speed| >= this is logged as a fast move

// "Bed zone" rectangle in mm, radar at origin, y = straight ahead
#define BED_X_MIN (-1000)
#define BED_X_MAX ( 1000)
#define BED_Y_MIN ( 1500)
#define BED_Y_MAX ( 3500)

HardwareSerial RadarD(2);

struct TgtState { bool on; uint32_t lastMove; };
TgtState ts[3];

static int16_t dec(uint8_t lo, uint8_t hi) {      // sign-magnitude, bit15 = 1 -> positive
  uint16_t r = lo | (hi << 8);
  int16_t m = r & 0x7FFF;
  return (r & 0x8000) ? m : -m;
}

bool inBed(int x, int y) {
  return x >= BED_X_MIN && x <= BED_X_MAX && y >= BED_Y_MIN && y <= BED_Y_MAX;
}

void handleFrame(const uint8_t *f) {              // f = 30-byte frame
  uint32_t now = millis();
  for (int i = 0; i < 3; i++) {
    const uint8_t *t = f + 4 + 8 * i;
    bool on = false;
    for (int k = 0; k < 8; k++) if (t[k]) on = true;
    if (on) {
      int16_t x = dec(t[0], t[1]);                // mm
      int16_t y = dec(t[2], t[3]);                // mm
      int16_t v = dec(t[4], t[5]);                // cm/s (negative = approaching)
      float d = sqrtf((float)x * x + (float)y * y) / 1000.0f;
      float a = atan2f((float)x, (float)y) * 180.0f / PI;   // flip sign if mirrored vs. Fig 5-1
      if (!ts[i].on) { Serial.printf("# T%d APPEARED\n", i + 1); ts[i].lastMove = now; }
      if (abs(v) >= MOVE_CMS) ts[i].lastMove = now;
      if (abs(v) >= FAST_CMS) Serial.printf("# T%d FAST MOVE %d cm/s\n", i + 1, v);
      Serial.printf("%lu,%d,%d,%d,%d,%.2f,%.1f,%d\n", (unsigned long)now, i + 1,
                    x, y, v, d, a, inBed(x, y) ? 1 : 0);
      ts[i].on = true;
    } else if (ts[i].on) {
      Serial.printf("# T%d LOST after %.1f s without motion\n", i + 1,
                    (now - ts[i].lastMove) / 1000.0f);
      ts[i].on = false;
    }
  }
}

// ---------- frame parser: AA FF 03 00 + 24 bytes + 55 CC ----------
uint8_t fb[30];
uint8_t fi = 0;

void feed(uint8_t c) {
  if (fi == 0) { if (c == 0xAA) fb[fi++] = c; return; }
  fb[fi++] = c;
  if (fi == 4 && !(fb[1] == 0xFF && fb[2] == 0x03 && fb[3] == 0x00)) {
    fi = 0;
    if (c == 0xAA) fb[fi++] = c;
    return;
  }
  if (fi == 30) {
    if (fb[28] == 0x55 && fb[29] == 0xCC) handleFrame(fb);
    fi = 0;
  }
}

// ---------- commands ----------
uint32_t rawUntil = 0;

void sendFrame(const uint8_t *payload, uint16_t len) {
  const uint8_t H[4] = {0xFD, 0xFC, 0xFB, 0xFA}, F[4] = {0x04, 0x03, 0x02, 0x01};
  RadarD.write(H, 4);
  RadarD.write((uint8_t)(len & 0xFF));
  RadarD.write((uint8_t)(len >> 8));
  RadarD.write(payload, len);
  RadarD.write(F, 4);
  RadarD.flush();
  rawUntil = millis() + 400;
}

void setMode(bool multi) {
  const uint8_t en[]   = {0xFF, 0x00, 0x01, 0x00};
  const uint8_t endc[] = {0xFE, 0x00};
  const uint8_t cmd[]  = {multi ? (uint8_t)0x90 : (uint8_t)0x80, 0x00};
#if D_WRAP_CONFIG
  sendFrame(en, sizeof(en)); delay(120);
#endif
  sendFrame(cmd, sizeof(cmd)); delay(120);
#if D_WRAP_CONFIG
  sendFrame(endc, sizeof(endc)); delay(120);
#endif
  Serial.println(multi ? "# multi-target mode" : "# single-target mode");
}

void setup() {
  Serial.begin(115200);
  RadarD.setRxBufferSize(1024);
  RadarD.begin(D_BAUD, SERIAL_8N1, D_RX_PIN, D_TX_PIN);
  Serial.println("# E54-D tracking demo. Columns: ms,id,x_mm,y_mm,speed_cm_s,dist_m,angle_deg,in_bed");
}

void loop() {
  while (RadarD.available()) {
    uint8_t c = RadarD.read();
    if (millis() < rawUntil) Serial.printf("%02X ", c);   // ACK bytes after a command
    feed(c);
  }
  while (Serial.available()) {
    char k = Serial.read();
    if (k == '1') setMode(false);
    if (k == '3') setMode(true);
  }
}
