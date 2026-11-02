/*
  E54-24LD12B (E54 B) presence radar  ->  ESP32 (Arduino core)
  ------------------------------------------------------------
  Wiring (ESP32 DevKit V1):
    GPIO25 (RX1) <- module pin 3 "O_T1" (UART TX)
    GPIO26 (TX1) -> module pin 2 "RX"
    GPIO27       <- module pin 1 "O_T2" (HIGH = occupied)
    3V3          -> module pin 5 "3V3"
    GND          -> module pin 4 "GND"

  USB serial monitor: 115200 baud.  Radar UART: 256000 8N1.

  Output (CSV, one line per radar frame):
    ms,state,moving_cm,moving_energy,static_cm,static_energy,gpio[,eng: mov gates | static gates]
  Lines starting with '#' are events.

  Keys typed in the serial monitor:
    e  engineering mode ON  (adds per-gate energy values)
    n  engineering mode OFF
    v  read firmware version        p  read parameters   (reply shown as raw hex)
    3  max gate 3  (~2.25 m)        6  max gate 6 (~4.5 m, default)
    h  high sensitivity (50/50)     d  default sensitivity (20/25)
    u  unoccupied delay 1 s         t  unoccupied delay 5 s (default)

  NOTE: written from the E54-24LD12B manual V1.2; check against your unit.
*/
#include <Arduino.h>

#define B_RX_PIN  25
#define B_TX_PIN  26
#define B_OUT_PIN 27
#define B_BAUD    256000

HardwareSerial RadarB(1);

static const uint8_t DATA_HDR[4] = {0xF4, 0xF3, 0xF2, 0xF1};
static const uint8_t DATA_FTR[4] = {0xF8, 0xF7, 0xF6, 0xF5};
static const uint8_t CMD_HDR[4]  = {0xFD, 0xFC, 0xFB, 0xFA};
static const uint8_t CMD_FTR[4]  = {0x04, 0x03, 0x02, 0x01};

uint32_t rawUntil = 0;   // print raw bytes (command ACKs) until this time
int lastPin = -1;

// ---------- command helpers ----------
void sendCmd(const uint8_t *payload, uint16_t len) {
  RadarB.write(CMD_HDR, 4);
  RadarB.write((uint8_t)(len & 0xFF));
  RadarB.write((uint8_t)(len >> 8));
  RadarB.write(payload, len);
  RadarB.write(CMD_FTR, 4);
  RadarB.flush();
  rawUntil = millis() + 400;
}

// enable config -> command -> end config (must all finish within 3 s)
void configSession(const uint8_t *cmd, uint16_t len) {
  const uint8_t en[]   = {0xFF, 0x00, 0x01, 0x00};
  const uint8_t endc[] = {0xFE, 0x00};
  sendCmd(en, sizeof(en));   delay(120);
  sendCmd(cmd, len);         delay(120);
  sendCmd(endc, sizeof(endc)); delay(120);
}

void setGateDelay(uint8_t gate, uint16_t delaySec) {
  uint8_t p[20] = {0x60, 0x00,
                   0x00, 0x00, gate, 0, 0, 0,                                   // max moving gate
                   0x01, 0x00, gate, 0, 0, 0,                                   // max static gate
                   0x02, 0x00, (uint8_t)(delaySec & 0xFF), (uint8_t)(delaySec >> 8), 0, 0}; // unoccupied s
  configSession(p, sizeof(p));
}

void setSensitivity(uint8_t mov, uint8_t stat) {
  uint8_t p[20] = {0x64, 0x00,
                   0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,   // all gates
                   0x01, 0x00, mov, 0, 0, 0,
                   0x02, 0x00, stat, 0, 0, 0};
  configSession(p, sizeof(p));
}

// remember current gate/delay so gate and delay keys don't undo each other
uint8_t curGate = 6;
uint16_t curDelay = 5;

// ---------- data frame handling ----------
const char *stateName(uint8_t s) {
  switch (s) {
    case 0: return "NONE";
    case 1: return "MOVING";
    case 2: return "STATIONARY";
    case 3: return "MOVING+STATIONARY";
    default: return "?";
  }
}

// p = in-frame data, len = its length
void handleFrame(const uint8_t *p, uint16_t len) {
  if (len < 11 || p[1] != 0xAA) return;
  uint8_t type = p[0];                       // 0x02 basic, 0x01 engineering
  uint8_t st = p[2];
  uint16_t movD = p[3] | (p[4] << 8);
  uint8_t movE = p[5];
  uint16_t stD = p[6] | (p[7] << 8);
  uint8_t stE = p[8];
  Serial.printf("%lu,%s,%u,%u,%u,%u,%d", (unsigned long)millis(), stateName(st),
                movD, movE, stD, stE, digitalRead(B_OUT_PIN));
  if (type == 0x01) {                        // engineering mode: per-gate energies
    uint8_t n = p[9];                        // max gate N -> N+1 values each
    if (n <= 8 && len >= 11 + 2 * (n + 1)) {
      Serial.print(",mov:");
      for (uint8_t i = 0; i <= n; i++) Serial.printf("%u ", p[11 + i]);
      Serial.print("|stat:");
      for (uint8_t i = 0; i <= n; i++) Serial.printf("%u ", p[11 + n + 1 + i]);
    }
  }
  Serial.println();
}

// ---------- byte-wise frame parser ----------
uint8_t buf[96];
uint8_t bn = 0;

void feed(uint8_t c) {
  if (bn < 4) {                              // looking for F4 F3 F2 F1
    if (c == DATA_HDR[bn]) buf[bn++] = c;
    else { bn = 0; if (c == DATA_HDR[0]) buf[bn++] = c; }
    return;
  }
  buf[bn++] = c;
  if (bn >= 6) {
    uint16_t len = buf[4] | (buf[5] << 8);
    if (len < 11 || len > 80) { bn = 0; return; }
    if (bn == 6 + len + 4) {
      if (!memcmp(&buf[6 + len], DATA_FTR, 4)) handleFrame(&buf[6], len);
      bn = 0;
    }
  }
}

// ---------- keys ----------
void handleKey(char k) {
  const uint8_t ver[]    = {0x00, 0x00};
  const uint8_t par[]    = {0x61, 0x00};
  const uint8_t engOn[]  = {0x62, 0x00};
  const uint8_t engOff[] = {0x63, 0x00};
  switch (k) {
    case 'e': configSession(engOn, 2);  Serial.println("# engineering ON");  break;
    case 'n': configSession(engOff, 2); Serial.println("# engineering OFF"); break;
    case 'v': configSession(ver, 2); break;
    case 'p': configSession(par, 2); break;
    case '3': curGate = 3; setGateDelay(curGate, curDelay); Serial.println("# max gate 3 (~2.25 m)"); break;
    case '6': curGate = 6; setGateDelay(curGate, curDelay); Serial.println("# max gate 6 (~4.5 m)"); break;
    case 'u': curDelay = 1; setGateDelay(curGate, curDelay); Serial.println("# unoccupied delay 1 s"); break;
    case 't': curDelay = 5; setGateDelay(curGate, curDelay); Serial.println("# unoccupied delay 5 s"); break;
    case 'h': setSensitivity(50, 50); Serial.println("# sensitivity 50/50"); break;
    case 'd': setSensitivity(20, 25); Serial.println("# sensitivity 20/25"); break;
    default: break;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(B_OUT_PIN, INPUT);
  RadarB.setRxBufferSize(1024);
  RadarB.begin(B_BAUD, SERIAL_8N1, B_RX_PIN, B_TX_PIN);
  Serial.println("# E54-B presence demo. Columns: ms,state,mov_cm,mov_E,stat_cm,stat_E,gpio");
}

void loop() {
  while (RadarB.available()) {
    uint8_t c = RadarB.read();
    if (millis() < rawUntil) Serial.printf("%02X ", c);   // show ACK bytes after a command
    feed(c);
  }
  int pin = digitalRead(B_OUT_PIN);
  if (pin != lastPin) {
    lastPin = pin;
    Serial.printf("\n# %lu GPIO -> %s\n", (unsigned long)millis(), pin ? "OCCUPIED" : "EMPTY");
  }
  while (Serial.available()) handleKey(Serial.read());
}
