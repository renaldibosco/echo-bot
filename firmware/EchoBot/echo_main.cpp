// ============================================================
//  ECHO BOT  -  pocket desk companion
//  ESP32 + 0.96" OLED + HC-SR04 + buzzer + touch pad
//  Works fully offline. Phone control: join Wi-Fi "Echo-Bot",
//  open http://192.168.4.1
//  by Reno (renaldibosco) & Claude
// ============================================================
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "config.h"
#include "webpage.h"

// ------------------------------------------------------------
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
WebServer server(80);
DNSServer dns;
Preferences prefs;

enum Mood { NEUTRAL, HAPPY, SAD, ANGRY, SLEEPY, SURPRISED, LOVE, CURIOUS, MOOD_COUNT };
const char* MOOD_NAMES[] = {"neutral","happy","sad","angry","sleepy","surprised","love","curious"};

enum Mode { M_FACE, M_POMO, M_DICE, M_DIST, M_PIANO, M_GUARD, M_SCAN, M_FLAPPY, M_BATT, MODE_COUNT };
const char* MODE_KEYS[]  = {"face","pomo","dice","dist","piano","guard","scan","flappy","batt"};
const char* MODE_TITLE[] = {"Face","Study timer","Decide","Distance","Air piano","Guard","Wi-Fi scan","Flappy","Battery"};

// ---------------- state ----------------
Mood mood = NEUTRAL;
unsigned long moodUntil = 0;           // mood returns to neutral after this
Mode mode = M_FACE;
bool menuOpen = false; int menuIdx = 0; unsigned long menuTouched = 0;

int distCm = -1; unsigned long lastSonar = 0;
bool present = false; unsigned long lastSeen = 0, lastAbsentStart = 0;
unsigned long lastActivity = 0; bool sleeping = false;

float battV = 0; int battPct = 0; unsigned long lastBatt = 0; bool lowWarned = false;

// message overlay
String msgText = ""; unsigned long msgUntil = 0; bool msgSticky = false;

// eyes animation
float lookX = 0, lookY = 0, tgtX = 0, tgtY = 0;
unsigned long nextLook = 0, nextBlink = 0; int blinkStep = 0;
int petCount = 0; unsigned long petWindow = 0;

// time (synced from phone)
bool hasTime = false; uint32_t epochAtSync = 0; unsigned long msAtSync = 0; int tzMin = 330;
int lastMealDay = -1, lastMealSlot = -1;

// reminders
int waterMin = 60, stretchMin = 0; bool mealsOn = true;
unsigned long lastWater = 0, lastStretch = 0;

// pomodoro
enum PState { P_IDLE, P_FOCUS, P_BREAK, P_PAUSED };
PState pState = P_IDLE, pResume = P_FOCUS;
int focusMin = 25, breakMin = 5; long pLeft = 25 * 60; unsigned long pTick = 0; int pDone = 0;

// dice
int diceVal = 1; bool coinHeads = true; bool yes = true; unsigned long rollUntil = 0;

// guard
bool gArmed = false, gAlarm = false; int gBase = -1; unsigned long gArmAt = 0; int gCount = 0;

// scan
struct Net { String ssid; int rssi; int ch; bool lock; };
Net nets[8]; int netCount = 0; bool scanning = false;

// flappy
float bY, bV; float pipeX[2]; int pipeGap[2]; int score = 0, best = 0; bool fDead = false, fStarted = false;

// touch
#if USE_TOUCH_PIN
int touchBase = 0;
#endif
bool tDown = false, longFired = false; unsigned long tStart = 0;

// sound sequencer
struct Note { uint16_t f; uint16_t ms; };
Note seq[24]; int seqLen = 0, seqPos = 0; unsigned long seqNext = 0; bool pianoTone = false;

// wifi station (hotspot link)
String staSsid = "", staPass = "";
// hotspot link: try for 20 s, then stop searching (searching makes the phone drop off Echo-Bot)
unsigned long staStart = 0, staRetryAt = 0; bool staTrying = false, staLinked = false;
void staBegin() {
  if (!staSsid.length()) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(staSsid.c_str(), staPass.c_str());
  staStart = millis(); staTrying = true;
}
void staStop() {
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_AP);
  staTrying = false;
  staRetryAt = millis() + 10UL * 60UL * 1000UL;   // try again in 10 minutes
}

void drawFrame();
bool passed[2] = {false, false};

// ============================================================
//  SOUND
// ============================================================
void play(const Note* notes, int n) {
  if (n > 24) n = 24;
  for (int i = 0; i < n; i++) seq[i] = notes[i];
  seqLen = n; seqPos = 0; seqNext = 0;
}
void soundLoop() {
  if (seqLen == 0 || millis() < seqNext) return;
  if (seqPos >= seqLen) { noTone(PIN_BUZZER); seqLen = 0; seqPos = 0; return; }
  Note n = seq[seqPos++];
  if (n.f) tone(PIN_BUZZER, BUZZER_PASSIVE ? n.f : 2000); else noTone(PIN_BUZZER);
  seqNext = millis() + n.ms;
}
const Note SND_HI[]    = {{1568,70},{0,30},{2093,110}};
const Note SND_PET[]   = {{1760,50},{2349,70}};
const Note SND_LOVE[]  = {{1319,90},{1568,90},{2093,90},{2637,160}};
const Note SND_SAD[]   = {{880,160},{698,220}};
const Note SND_CLICK[] = {{2400,25}};
const Note SND_OK[]    = {{1568,60},{2093,80}};
const Note SND_ALERT[] = {{2093,120},{0,60},{2093,120},{0,60},{2093,120}};
const Note SND_DONE[]  = {{1047,120},{1319,120},{1568,120},{2093,280}};
const Note SND_ALARM[] = {{2600,150},{1800,150},{2600,150},{1800,150},{2600,150},{1800,150}};
const Note SND_FIND[]  = {{2000,90},{0,60},{2500,90},{0,60},{3000,90},{0,60},{2500,90},{0,60},{2000,90}};
const Note SND_SLEEP[] = {{1047,150},{784,150},{523,260}};
const Note SND_R2[]    = {{2800,40},{2200,40},{3100,40},{1900,50},{2600,60},{3300,80}};
#define PLAY(x) play(x, sizeof(x)/sizeof(Note))

// ============================================================
//  HELPERS
// ============================================================
void setMood(Mood m, unsigned long holdMs = 15000) { mood = m; moodUntil = millis() + holdMs; }
void showMsg(const String& t, unsigned long ms = 6000, bool sticky = false) {
  msgText = t; msgUntil = millis() + ms; msgSticky = sticky;
}
void wake() {
  lastActivity = millis();
  if (sleeping) { sleeping = false; oled.dim(false); setMood(HAPPY, 4000); PLAY(SND_HI); }
}
uint32_t nowLocal() { return epochAtSync + (millis() - msAtSync) / 1000 + (int32_t)tzMin * 60; }
String clockStr() {
  if (!hasTime) return "";
  uint32_t t = nowLocal(); int h = (t / 3600) % 24, m = (t / 60) % 60;
  char b[8]; snprintf(b, sizeof b, "%02d:%02d", h, m); return String(b);
}
String jsonEsc(const String& s) {
  String o; for (char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += c; } else if ((uint8_t)c >= 32) o += c; }
  return o;
}

// word-wrapped text, returns lines used
int drawWrapped(const String& text, int x, int y, int size, int maxLines) {
  int cols = 128 / (6 * size); int line = 0; String cur = ""; String word = "";
  oled.setTextSize(size); oled.setTextColor(SSD1306_WHITE);
  auto flush = [&](String& l) { if (line < maxLines) { oled.setCursor(x, y + line * 8 * size); oled.print(l); } line++; l = ""; };
  for (unsigned i = 0; i <= text.length(); i++) {
    char c = i < text.length() ? text[i] : ' ';
    if (c == ' ' || c == '\n') {
      if ((int)(cur.length() + word.length() + (cur.length() ? 1 : 0)) > cols) flush(cur);
      while ((int)word.length() > cols) { String part = word.substring(0, cols); flush(part); word = word.substring(cols); }
      if (word.length()) { if (cur.length()) cur += ' '; cur += word; }
      word = ""; if (c == '\n') flush(cur);
    } else word += c;
  }
  if (cur.length()) flush(cur);
  return line;
}
void centerText(const String& t, int y, int size) {
  oled.setTextSize(size); int w = t.length() * 6 * size;
  oled.setCursor((128 - w) / 2, y); oled.print(t);
}

// ============================================================
//  SENSORS
// ============================================================
int readSonar() {
  digitalWrite(PIN_TRIG, LOW); delayMicroseconds(3);
  digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10); digitalWrite(PIN_TRIG, LOW);
  unsigned long us = pulseIn(PIN_ECHO, HIGH, 25000UL);
  if (us == 0) return -1;
  int cm = us / 58; return (cm > 400) ? -1 : cm;
}
void readBattery() {
  uint32_t mv = 0; for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(PIN_BATT);
  battV = (mv / 8.0f) * BATT_DIVIDER / 1000.0f;
  // Li-ion curve (rough)
  const float v[] = {3.30, 3.50, 3.60, 3.70, 3.75, 3.80, 3.87, 3.95, 4.05, 4.20};
  const int   p[] = {0,    5,    10,   20,   35,   50,   65,   80,   92,   100};
  if (battV <= v[0]) battPct = 0; else if (battV >= v[9]) battPct = 100;
  else for (int i = 0; i < 9; i++) if (battV < v[i + 1]) { battPct = p[i] + (p[i + 1] - p[i]) * (battV - v[i]) / (v[i + 1] - v[i]); break; }
  if (battV < 1.0f) battPct = 0;              // no divider connected
}

// ---------------- touch / button ----------------
bool rawTouched() {
#if USE_TOUCH_PIN
  int v = touchRead(PIN_TOUCH);
  int diff = abs(v - touchBase);
  bool t = diff > max(8, touchBase / 4);      // works for ESP32 (drops) and S3 (rises)
  if (!t) touchBase = (touchBase * 31 + v) / 32;
  return t;
#else
  return digitalRead(PIN_TOUCH) == LOW;
#endif
}
void onTap();
void onLong();
void touchLoop() {
  static unsigned long lastSample = 0; static int stable = 0; static bool state = false;
  if (millis() - lastSample < 15) return; lastSample = millis();
  bool r = rawTouched();
  if (r != state) { if (++stable >= 2) { state = r; stable = 0; } } else stable = 0;
  if (state && !tDown) { tDown = true; tStart = millis(); longFired = false; }
  if (state && tDown && !longFired && millis() - tStart > LONG_PRESS_MS) { longFired = true; onLong(); }
  if (!state && tDown) { tDown = false; if (!longFired) onTap(); }
}

// ============================================================
//  MODES
// ============================================================
void enterMode(Mode m) {
  mode = m; menuOpen = false; noTone(PIN_BUZZER); pianoTone = false;
  if (m == M_FLAPPY) { fStarted = false; fDead = false; bY = 28; bV = 0; score = 0; pipeX[0] = 128; pipeX[1] = 196; pipeGap[0] = random(14, 38); pipeGap[1] = random(14, 38); passed[0] = passed[1] = false; }
  if (m == M_DICE) rollUntil = 0;
  PLAY(SND_OK);
}
void pomoStart() { if (pState == P_IDLE) { pState = P_FOCUS; pLeft = focusMin * 60L; } else if (pState == P_PAUSED) pState = pResume; pTick = millis(); }
void pomoPause() { if (pState == P_FOCUS || pState == P_BREAK) { pResume = pState; pState = P_PAUSED; } }
void pomoReset() { pState = P_IDLE; pLeft = focusMin * 60L; }
void guardArm()  { gArmed = true; gAlarm = false; gBase = -1; gArmAt = millis() + 5000; PLAY(SND_OK); }
void guardOff()  { gArmed = false; gAlarm = false; PLAY(SND_CLICK); }
void doScan() {
  scanning = true; drawFrame();
  int n = WiFi.scanNetworks(false, true);
  netCount = 0;
  for (int i = 0; i < n && netCount < 8; i++) {
    int best = -1;
    for (int j = 0; j < n; j++) {        // pick strongest not yet taken
      bool used = false; for (int k = 0; k < netCount; k++) if (nets[k].ssid == WiFi.SSID(j) && nets[k].ch == WiFi.channel(j)) used = true;
      if (!used && (best < 0 || WiFi.RSSI(j) > WiFi.RSSI(best))) best = j;
    }
    if (best < 0) break;
    nets[netCount++] = {WiFi.SSID(best), (int)WiFi.RSSI(best), (int)WiFi.channel(best), WiFi.encryptionType(best) != WIFI_AUTH_OPEN};
  }
  WiFi.scanDelete(); scanning = false;
}

void onTap() {
  wake();
  if (msgSticky && millis() < msgUntil) { msgUntil = 0; msgSticky = false; setMood(HAPPY, 4000); PLAY(SND_OK); return; }
  if (menuOpen) { menuIdx = (menuIdx + 1) % MODE_COUNT; menuTouched = millis(); PLAY(SND_CLICK); return; }
  switch (mode) {
    case M_FACE: {
      if (millis() > petWindow) petCount = 0;
      petCount++; petWindow = millis() + 2500;
      if (petCount >= 5) { setMood(LOVE, 6000); PLAY(SND_LOVE); petCount = 0; }
      else { setMood(HAPPY, 5000); random(4) == 0 ? PLAY(SND_R2) : PLAY(SND_PET); }
      break;
    }
    case M_POMO: if (pState == P_FOCUS || pState == P_BREAK) pomoPause(); else pomoStart(); PLAY(SND_CLICK); break;
    case M_DICE: rollUntil = millis() + 900; diceVal = random(1, 7); coinHeads = random(2); yes = random(2); break;
    case M_GUARD: if (gArmed) guardOff(); else guardArm(); break;
    case M_SCAN: doScan(); PLAY(SND_OK); break;
    case M_FLAPPY:
      if (fDead) { enterMode(M_FLAPPY); return; }
      fStarted = true; bV = -2.4f; tone(PIN_BUZZER, BUZZER_PASSIVE ? 1800 : 2000, 15); break;
    default: PLAY(SND_CLICK); break;
  }
}
void onLong() {
  wake();
  if (menuOpen) { enterMode((Mode)menuIdx); return; }
  menuOpen = true; menuIdx = mode; menuTouched = millis(); PLAY(SND_CLICK);
}

// ============================================================
//  DRAWING
// ============================================================
void drawEye(int cx, int cy, int w, int h, bool left) {
  int x = cx - w / 2, y = cy - h / 2;
  if (h < 4) { oled.fillRect(x, cy - 1, w, 3, SSD1306_WHITE); return; }
  int r = min(8, min(w, h) / 2);
  switch (mood) {
    case SURPRISED: oled.fillCircle(cx, cy, 15, SSD1306_WHITE); oled.fillCircle(cx, cy, 5, SSD1306_BLACK); return;
    case LOVE: {
      int s = 9; oled.fillCircle(cx - s / 2 - 1, cy - 3, s / 2 + 2, SSD1306_WHITE); oled.fillCircle(cx + s / 2 + 1, cy - 3, s / 2 + 2, SSD1306_WHITE);
      oled.fillTriangle(cx - s - 3, cy - 1, cx + s + 3, cy - 1, cx, cy + s + 5, SSD1306_WHITE); return;
    }
    case SLEEPY: oled.fillRoundRect(x, cy, w, 5, 2, SSD1306_WHITE); return;
    case CURIOUS: if (!left) { h += 8; y -= 4; } else { h -= 6; y += 3; } break;
    default: break;
  }
  oled.fillRoundRect(x, y, w, h, r, SSD1306_WHITE);
  switch (mood) {
    case HAPPY: oled.fillCircle(cx, y + h + h / 3, w * 3 / 4, SSD1306_BLACK); break;          // ^ ^
    case SAD:   if (left) oled.fillTriangle(x - 1, y - 1, x + w, y - 1, x - 1, y + h / 2, SSD1306_BLACK);
                else      oled.fillTriangle(x - 1, y - 1, x + w, y - 1, x + w, y + h / 2, SSD1306_BLACK); break;
    case ANGRY: if (left) oled.fillTriangle(x - 1, y - 1, x + w, y - 1, x + w, y + h / 2, SSD1306_BLACK);
                else      oled.fillTriangle(x - 1, y - 1, x + w, y - 1, x - 1, y + h / 2, SSD1306_BLACK); break;
    default: break;
  }
}
void drawFace() {
  unsigned long now = millis();
  if (now > nextLook) {
    tgtX = random(-10, 11); tgtY = random(-5, 6);
    if (random(3) == 0) { tgtX = 0; tgtY = 0; }
    nextLook = now + random(1200, 4000);
  }
  lookX += (tgtX - lookX) * 0.25f; lookY += (tgtY - lookY) * 0.25f;
  if (blinkStep == 0 && now > nextBlink) blinkStep = 1;
  int h = 34;
  if (blinkStep) {
    const int seqH[] = {34, 22, 10, 3, 3, 10, 22, 34};
    h = seqH[blinkStep]; blinkStep++;
    if (blinkStep >= 8) { blinkStep = 0; nextBlink = now + random(2000, 6000); if (random(5) == 0) nextBlink = now + 250; }
  }
  if (sleeping || mood == SLEEPY) h = 3;
  int cy = 32 + (int)lookY, ox = (int)lookX;
  drawEye(40 + ox, cy, 30, h, true);
  drawEye(88 + ox, cy, 30, h, false);
  if (sleeping || mood == SLEEPY) {                   // floating zZ
    int t = (now / 120) % 30;
    oled.setTextSize(1); oled.setCursor(104, 18 - t / 3); oled.print("z");
    oled.setTextSize(2); oled.setCursor(110, 4 - t / 6 + 6); oled.print("Z");
  }
  if (!sleeping) {                                    // tiny status line
    oled.setTextSize(1);
    String c = clockStr(); if (c.length()) { oled.setCursor(0, 0); oled.print(c); }
    if (battPct > 0 && battPct <= 15 && (now / 600) % 2) { oled.setCursor(98, 0); oled.print("LOW!"); }
    if (gArmed) { oled.setCursor(0, 56); oled.print(gAlarm ? "ALARM" : "guard"); }
    if (pState == P_FOCUS || pState == P_BREAK) { oled.setCursor(92, 56); oled.printf("%02ld:%02ld", pLeft / 60, pLeft % 60); }
  }
}
void drawDie(int x, int y, int s, int v) {
  oled.drawRoundRect(x, y, s, s, 6, SSD1306_WHITE);
  int c = s / 2, q = s / 4, r = 3;
  auto dot = [&](int dx, int dy) { oled.fillCircle(x + dx, y + dy, r, SSD1306_WHITE); };
  if (v % 2) dot(c, c);
  if (v > 1) { dot(q, q); dot(s - q, s - q); }
  if (v > 3) { dot(s - q, q); dot(q, s - q); }
  if (v == 6) { dot(q, c); dot(s - q, c); }
}
void drawBattIcon(int x, int y, int w, int h, int pct) {
  oled.drawRect(x, y, w, h, SSD1306_WHITE); oled.fillRect(x + w, y + h / 3, 3, h / 3, SSD1306_WHITE);
  oled.fillRect(x + 2, y + 2, (w - 4) * pct / 100, h - 4, SSD1306_WHITE);
}

void drawFrame() {
  oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE); oled.setTextSize(1);
  unsigned long now = millis();

  if (scanning) { centerText("Scanning", 20, 2); centerText("Wi-Fi around me...", 44, 1); oled.display(); return; }

  if (menuOpen) {
    oled.fillRect(0, 0, 128, 12, SSD1306_WHITE); oled.setTextColor(SSD1306_BLACK);
    oled.setCursor(4, 2); oled.print("MENU  tap=next hold=go"); oled.setTextColor(SSD1306_WHITE);
    for (int i = -1; i <= 1; i++) {
      int idx = (menuIdx + i + MODE_COUNT) % MODE_COUNT;
      int y = 28 + i * 16;
      if (i == 0) { oled.drawRoundRect(2, y - 4, 124, 16, 4, SSD1306_WHITE); oled.setTextSize(1); oled.setCursor(10, y); oled.print("> "); }
      else oled.setCursor(22, y);
      oled.print(MODE_TITLE[idx]);
    }
    oled.display(); return;
  }

  if (msgText.length() && now < msgUntil) {
    oled.drawRoundRect(0, 0, 128, 64, 6, SSD1306_WHITE);
    int lines = (msgText.length() <= 30) ? 2 : 6;
    int size = (msgText.length() <= 20) ? 2 : 1;
    drawWrapped(msgText, 5, size == 2 ? 14 : 6, size, lines);
    if (msgSticky) { oled.setTextSize(1); oled.setCursor(40, 55); oled.print("tap = ok"); }
    oled.display(); return;
  }

  switch (mode) {
    case M_FACE: drawFace(); break;

    case M_POMO: {
      const char* lbl = pState == P_FOCUS ? "FOCUS" : pState == P_BREAK ? "BREAK" : pState == P_PAUSED ? "PAUSED" : "READY";
      centerText(lbl, 2, 1);
      char b[8]; snprintf(b, sizeof b, "%02ld:%02ld", pLeft / 60, pLeft % 60); centerText(b, 18, 3);
      long total = (pState == P_BREAK || (pState == P_PAUSED && pResume == P_BREAK)) ? breakMin * 60L : focusMin * 60L;
      oled.drawRect(4, 48, 120, 6, SSD1306_WHITE);
      if (total > 0) oled.fillRect(6, 50, 116 * (total - pLeft) / total, 2, SSD1306_WHITE);
      oled.setCursor(4, 56); oled.printf("done:%d", pDone); oled.setCursor(76, 56); oled.print(pState == P_IDLE || pState == P_PAUSED ? "tap=go" : "tap=||");
      break;
    }

    case M_DICE: {
      bool rolling = now < rollUntil;
      int v = rolling ? random(1, 7) : diceVal;
      drawDie(6, 8, 48, v);
      oled.setTextSize(1); oled.setCursor(64, 10); oled.print("Coin:");
      oled.setCursor(64, 20); oled.print(rolling ? (random(2) ? "Heads" : "Tails") : (coinHeads ? "Heads" : "Tails"));
      oled.setCursor(64, 34); oled.print("Answer:");
      oled.setTextSize(2); oled.setCursor(64, 44); oled.print(rolling ? (random(2) ? "YES" : "NO") : (yes ? "YES" : "NO"));
      if (rolling && random(3) == 0) tone(PIN_BUZZER, BUZZER_PASSIVE ? random(1500, 2500) : 2000, 8);
      break;
    }

    case M_DIST: {
      centerText("Distance", 0, 1);
      if (distCm > 0) {
        String s = String(distCm); centerText(s, 16, 3); oled.setCursor(100, 30); oled.print("cm");
        oled.drawRect(4, 50, 120, 10, SSD1306_WHITE);
        oled.fillRect(6, 52, constrain(116 * distCm / 200, 0, 116), 6, SSD1306_WHITE);
      } else centerText("---", 20, 3);
      break;
    }

    case M_PIANO: {
      centerText("Air piano", 0, 1);
      const char* names[] = {"C","D","E","F","G","A","B","C'"};
      if (distCm >= 5 && distCm < 45) {
        int k = (distCm - 5) / 5; centerText(names[k], 14, 4);
        for (int i = 0; i < 8; i++) { int x = 4 + i * 15; if (i == k) oled.fillRect(x, 50, 13, 12, SSD1306_WHITE); else oled.drawRect(x, 50, 13, 12, SSD1306_WHITE); }
      } else { centerText("move your hand", 22, 1); centerText("5 - 45 cm away", 36, 1); }
      break;
    }

    case M_GUARD: {
      if (gAlarm && (now / 250) % 2) { oled.fillRect(0, 0, 128, 64, SSD1306_WHITE); oled.setTextColor(SSD1306_BLACK); }
      centerText(gAlarm ? "INTRUDER!" : gArmed ? (now < gArmAt ? "arming..." : "GUARDING") : "Guard off", 6, 2);
      oled.setTextSize(1);
      if (gArmed && now < gArmAt) { centerText(String((gArmAt - now) / 1000 + 1) + " s - step away", 32, 1); }
      else if (gArmed) { oled.setCursor(10, 32); oled.printf("base %d cm  now %d", gBase, distCm); }
      oled.setCursor(10, 46); oled.printf("alerts: %d", gCount);
      oled.setCursor(10, 56); oled.print(gArmed ? "tap = disarm" : "tap = arm");
      oled.setTextColor(SSD1306_WHITE);
      break;
    }

    case M_SCAN: {
      oled.setCursor(0, 0); oled.printf("Wi-Fi nearby: %d", netCount);
      if (!netCount) { centerText("tap to scan", 30, 1); break; }
      for (int i = 0; i < min(netCount, 6); i++) {
        oled.setCursor(0, 10 + i * 9);
        String s = nets[i].ssid.length() ? nets[i].ssid : "(hidden)"; if (s.length() > 13) s = s.substring(0, 13);
        oled.print(nets[i].lock ? "*" : " "); oled.print(s);
        int bars = nets[i].rssi > -55 ? 4 : nets[i].rssi > -67 ? 3 : nets[i].rssi > -78 ? 2 : 1;
        for (int b = 0; b < 4; b++) { int bh = 2 + b * 2; int bx = 108 + b * 5; if (b < bars) oled.fillRect(bx, 10 + i * 9 + 8 - bh, 3, bh, SSD1306_WHITE); else oled.drawPixel(bx + 1, 10 + i * 9 + 7, SSD1306_WHITE); }
      }
      break;
    }

    case M_FLAPPY: {
      for (int i = 0; i < 2; i++) {
        int x = (int)pipeX[i];
        oled.fillRect(x, 0, 12, pipeGap[i], SSD1306_WHITE);
        oled.fillRect(x, pipeGap[i] + 24, 12, 64 - pipeGap[i] - 24, SSD1306_WHITE);
      }
      oled.fillCircle(24, (int)bY, 3, SSD1306_WHITE); oled.drawPixel(25, (int)bY - 1, SSD1306_BLACK);
      oled.setCursor(56, 0); oled.print(score);
      if (!fStarted) centerText("tap to fly", 50, 1);
      if (fDead) { oled.fillRect(14, 16, 100, 32, SSD1306_BLACK); oled.drawRect(14, 16, 100, 32, SSD1306_WHITE);
        centerText("GAME OVER", 20, 1); centerText("score " + String(score) + " best " + String(best), 32, 1); }
      break;
    }

    case M_BATT: {
      centerText("Battery", 0, 1);
      drawBattIcon(20, 14, 80, 26, battPct);
      oled.setCursor(10, 46); oled.printf("%.2f V   %d%%", battV, battPct);
      oled.setCursor(10, 56); oled.printf("awake %lu min", millis() / 60000);
      break;
    }
    default: break;
  }
  oled.display();
}

// ============================================================
//  LOGIC (runs every loop)
// ============================================================
void updateLogic() {
  unsigned long now = millis();
  if (moodUntil && now > moodUntil) { mood = NEUTRAL; moodUntil = 0; }
  if (menuOpen && now - menuTouched > 8000) menuOpen = false;

  // sonar
  unsigned long sonarEvery = (mode == M_PIANO || mode == M_FLAPPY) ? 60 : (mode == M_GUARD || mode == M_DIST) ? 100 : 200;
  if (mode != M_FLAPPY && now - lastSonar > sonarEvery) {
    lastSonar = now; distCm = readSonar();
    bool near = distCm > 0 && distCm < PRESENCE_CM;
    if (near) {
      if (!present && now - lastSeen > 30000 && mode == M_FACE) {       // greet after being away
        wake(); setMood(HAPPY, 5000); PLAY(SND_HI); showMsg(String("Hi ") + OWNER_NAME + "!", 2000);
      }
      present = true; lastSeen = now; lastActivity = now;
      if (sleeping && mode == M_FACE) wake();
      if (distCm < 8 && mode == M_FACE && mood != SURPRISED) setMood(SURPRISED, 1500);
    } else if (present && now - lastSeen > 5000) present = false;
  }

  // battery
  if (now - lastBatt > 5000 || lastBatt == 0) {
    lastBatt = now; readBattery();
    if (battPct > 0 && battPct <= 15 && !lowWarned) { lowWarned = true; showMsg(String(OWNER_NAME) + ", I'm hungry. Charge me!", 8000, true); setMood(SAD, 8000); PLAY(SND_SAD); }
    if (battPct > 25) lowWarned = false;
  }

  // sleep
  if (!sleeping && mode == M_FACE && !gArmed && pState != P_FOCUS && pState != P_BREAK && now - lastActivity > SLEEP_AFTER_MS) {
    sleeping = true; oled.dim(true); PLAY(SND_SLEEP);
  }

  // pomodoro
  if ((pState == P_FOCUS || pState == P_BREAK) && now - pTick >= 1000) {
    pTick += 1000; pLeft--;
    if (pLeft <= 0) {
      if (pState == P_FOCUS) { pDone++; pState = P_BREAK; pLeft = breakMin * 60L; showMsg("Great focus! Take a break", 15000, true); setMood(HAPPY); }
      else { pState = P_FOCUS; pLeft = focusMin * 60L; showMsg("Break over. Back to study!", 15000, true); setMood(CURIOUS); }
      wake(); PLAY(SND_DONE);
    }
  }

  // reminders (paused while sleeping / nobody around for long)
  if (waterMin > 0 && now - lastWater > waterMin * 60000UL) { lastWater = now; wake(); showMsg("Drink some water " + String(OWNER_NAME) + "!", 60000, true); setMood(CURIOUS); PLAY(SND_ALERT); }
  if (stretchMin > 0 && now - lastStretch > stretchMin * 60000UL) { lastStretch = now; wake(); showMsg("Stand up & stretch!", 60000, true); setMood(HAPPY); PLAY(SND_ALERT); }
  if (mealsOn && hasTime) {
    uint32_t t = nowLocal(); int day = t / 86400, hh = (t / 3600) % 24, mi = (t / 60) % 60;
    int slot = (hh == 8 && mi == 30) ? 0 : (hh == 13 && mi == 30) ? 1 : (hh == 20 && mi == 30) ? 2 : -1;
    if (slot >= 0 && !(day == lastMealDay && slot == lastMealSlot)) {
      lastMealDay = day; lastMealSlot = slot; wake();
      const char* m[] = {"Breakfast time!", "Lunch time! Go eat", "Dinner time! Don't say 'soon'"};
      showMsg(String(m[slot]) + " " + OWNER_NAME, 60000, true); setMood(ANGRY, 4000); PLAY(SND_ALERT);
    }
  }

  // guard
  if (gArmed && now > gArmAt) {
    if (gBase < 0 && distCm > 0) gBase = distCm;
    if (gBase > 0 && !gAlarm && distCm > 0 && abs(distCm - gBase) > 20) { gAlarm = true; gCount++; setMood(ANGRY, 60000); }
    if (gAlarm && seqLen == 0) PLAY(SND_ALARM);
  }

  // air piano
  if (mode == M_PIANO) {
    const uint16_t freq[] = {523, 587, 659, 698, 784, 880, 988, 1047};
    if (distCm >= 5 && distCm < 45) { tone(PIN_BUZZER, BUZZER_PASSIVE ? freq[(distCm - 5) / 5] : 2000); pianoTone = true; lastActivity = now; }
    else if (pianoTone) { noTone(PIN_BUZZER); pianoTone = false; }
  }

  // flappy physics (30 fps)
  static unsigned long lastF = 0;
  if (mode == M_FLAPPY && fStarted && !fDead && now - lastF >= 33) {
    lastF = now; bV += 0.32f; bY += bV;
    for (int i = 0; i < 2; i++) {
      pipeX[i] -= 1.6f;
      if (pipeX[i] < -12) { pipeX[i] += 136; pipeGap[i] = random(6, 34); }
    }
    for (int i = 0; i < 2; i++) {
      if (pipeX[i] + 12 < 21 && !passed[i]) { passed[i] = true; score++; }
      if (pipeX[i] > 100) passed[i] = false;
      bool inX = 24 + 3 > pipeX[i] && 24 - 3 < pipeX[i] + 12;
      bool inGap = bY - 3 > pipeGap[i] && bY + 3 < pipeGap[i] + 24;
      if (inX && !inGap) fDead = true;
    }
    if (bY > 63 || bY < 0) fDead = true;
    if (fDead) { if (score > best) { best = score; prefs.putInt("best", best); } PLAY(SND_SAD); }
  }
}

// ============================================================
//  WEB API  (phone control page)
// ============================================================
void sendJson(const String& s) { server.sendHeader("Cache-Control", "no-store"); server.send(200, "application/json", s); }
String statusJson() {
  const char* ps[] = {"idle", "focus", "break", "paused"};
  String j = "{";
  j += "\"mood\":\"" + String(MOOD_NAMES[mood]) + "\",";
  j += "\"mode\":\"" + String(MODE_KEYS[mode]) + "\",";
  j += "\"pct\":" + String(battPct) + ",\"volt\":" + String(battV, 2) + ",";
  j += "\"dist\":" + String(distCm) + ",\"present\":" + String(present ? "true" : "false") + ",";
  j += "\"sleeping\":" + String(sleeping ? "true" : "false") + ",";
  j += "\"time\":\"" + clockStr() + "\",";
  j += "\"pstate\":\"" + String(ps[pState]) + "\",\"pleft\":" + String(pLeft) + ",\"pdone\":" + String(pDone) + ",";
  j += "\"garmed\":" + String(gArmed ? "true" : "false") + ",\"galarm\":" + String(gAlarm ? "true" : "false") + ",\"gcount\":" + String(gCount) + ",";
  j += "\"water\":" + String(waterMin) + ",\"stretch\":" + String(stretchMin) + ",\"meals\":" + String(mealsOn ? "true" : "false") + ",";
  j += "\"best\":" + String(best) + ",";
  j += "\"stassid\":\"" + jsonEsc(staSsid) + "\",\"staip\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("")) + "\"";
  j += "}";
  return j;
}
void setupWeb() {
  server.on("/", []() { server.send_P(200, "text/html", PAGE_HTML); });
  server.on("/api/status", []() { sendJson(statusJson()); });
  server.on("/api/say", []() {                 // also used by the laptop brain later
    String t = server.arg("text"); t.trim();
    if (t.length()) { wake(); showMsg(t.substring(0, 160), 4000 + t.length() * 180UL); PLAY(SND_R2); }
    if (server.hasArg("mood")) { String m = server.arg("mood"); for (int i = 0; i < MOOD_COUNT; i++) if (m == MOOD_NAMES[i]) setMood((Mood)i, 20000); }
    sendJson(statusJson());
  });
  server.on("/api/mood", []() {
    String m = server.arg("m");
    for (int i = 0; i < MOOD_COUNT; i++) if (m == MOOD_NAMES[i]) { wake(); setMood((Mood)i, 30000); if (mode != M_FACE) enterMode(M_FACE); }
    sendJson(statusJson());
  });
  server.on("/api/mode", []() {
    String m = server.arg("m");
    for (int i = 0; i < MODE_COUNT; i++) if (m == MODE_KEYS[i]) { wake(); enterMode((Mode)i); }
    sendJson(statusJson());
  });
  server.on("/api/beep", []() { wake(); PLAY(SND_FIND); showMsg("I'm here!", 3000); setMood(SURPRISED, 3000); sendJson(statusJson()); });
  server.on("/api/time", []() {
    if (server.hasArg("epoch")) {
      epochAtSync = strtoul(server.arg("epoch").c_str(), nullptr, 10); msAtSync = millis(); hasTime = epochAtSync > 1600000000UL;
      if (server.hasArg("tz")) tzMin = server.arg("tz").toInt();
    }
    sendJson(statusJson());
  });
  server.on("/api/pomo", []() {
    if (server.hasArg("focus")) focusMin = constrain(server.arg("focus").toInt(), 1, 120);
    if (server.hasArg("brk"))   breakMin = constrain(server.arg("brk").toInt(), 1, 60);
    String a = server.arg("act");
    if (a == "start") { pomoStart(); if (mode != M_POMO) enterMode(M_POMO); }
    else if (a == "pause") pomoPause();
    else if (a == "reset") pomoReset();
    wake(); sendJson(statusJson());
  });
  server.on("/api/remind", []() {
    if (server.hasArg("water"))   waterMin = constrain(server.arg("water").toInt(), 0, 240);
    if (server.hasArg("stretch")) stretchMin = constrain(server.arg("stretch").toInt(), 0, 240);
    if (server.hasArg("meals"))   mealsOn = server.arg("meals") == "1";
    prefs.putInt("water", waterMin); prefs.putInt("stretch", stretchMin); prefs.putBool("meals", mealsOn);
    lastWater = lastStretch = millis(); PLAY(SND_OK); sendJson(statusJson());
  });
  server.on("/api/guard", []() {
    if (server.arg("act") == "arm") { guardArm(); if (mode != M_GUARD) enterMode(M_GUARD); } else guardOff();
    sendJson(statusJson());
  });
  server.on("/api/scan", []() {
    doScan();
    String j = "[";
    for (int i = 0; i < netCount; i++) {
      if (i) j += ",";
      j += "{\"ssid\":\"" + jsonEsc(nets[i].ssid) + "\",\"rssi\":" + nets[i].rssi + ",\"ch\":" + nets[i].ch + ",\"lock\":" + (nets[i].lock ? "true" : "false") + "}";
    }
    sendJson(j + "]");
  });
  server.on("/api/wifi", []() {
    staSsid = server.arg("ssid"); staPass = server.arg("pass");
    prefs.putString("ssid", staSsid); prefs.putString("pass", staPass);
    if (staSsid.length()) staBegin(); else { staStop(); staRetryAt = 0; }
    sendJson(statusJson());
  });
  server.on("/api/sleep", []() { lastActivity = 0; sleeping = true; oled.dim(true); PLAY(SND_SLEEP); sendJson(statusJson()); });
  server.on("/api/wake",  []() { wake(); sendJson(statusJson()); });
  // captive portal: any other address opens the control page
  server.onNotFound([]() { server.sendHeader("Location", "http://192.168.4.1/", true); server.send(302, "text/plain", ""); });
  server.begin();
}

// ============================================================
//  SETUP / LOOP
// ============================================================
void bootAnimation() {
  for (int h = 2; h <= 34; h += 4) { oled.clearDisplay(); oled.fillRoundRect(25, 32 - h / 2, 30, h, min(8, h / 2), SSD1306_WHITE); oled.fillRoundRect(73, 32 - h / 2, 30, h, min(8, h / 2), SSD1306_WHITE); oled.display(); delay(30); }
  oled.clearDisplay(); centerText("ECHO", 14, 3); centerText("waking up...", 46, 1); oled.display();
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_TRIG, OUTPUT); pinMode(PIN_ECHO, INPUT);
#if !USE_TOUCH_PIN
  pinMode(PIN_TOUCH, INPUT_PULLUP);
#endif
  analogReadResolution(12);
  randomSeed(esp_random());

  Wire.begin(PIN_SDA, PIN_SCL); Wire.setClock(400000);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C) && !oled.begin(SSD1306_SWITCHCAPVCC, 0x3D))
    Serial.println("OLED not found - check SDA/SCL wires");
  oled.setTextWrap(false);
  bootAnimation();

  prefs.begin("echo", false);
  waterMin = prefs.getInt("water", 60); stretchMin = prefs.getInt("stretch", 0); mealsOn = prefs.getBool("meals", true);
  best = prefs.getInt("best", 0);
  staSsid = prefs.getString("ssid", ""); staPass = prefs.getString("pass", "");

#if USE_TOUCH_PIN
  long sum = 0; for (int i = 0; i < 16; i++) { sum += touchRead(PIN_TOUCH); delay(5); } touchBase = sum / 16;
#endif

  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);                         // steadier access point
  WiFi.softAP(ECHO_WIFI_NAME, ECHO_WIFI_PASS);
  WiFi.setTxPower(WIFI_POWER_15dBm);            // less current spike on battery
  if (staSsid.length()) staBegin(); else WiFi.mode(WIFI_AP);
  dns.start(53, "*", WiFi.softAPIP());
  setupWeb();

  Serial.printf("Echo ready. Join Wi-Fi '%s' (pass %s) and open http://%s\n", ECHO_WIFI_NAME, ECHO_WIFI_PASS, WiFi.softAPIP().toString().c_str());
  delay(600);
  lastActivity = lastWater = lastStretch = millis(); lastSeen = 0;
  nextBlink = millis() + 1500;
  setMood(HAPPY, 4000); PLAY(SND_HI);
}

void wifiLoop() {
  if (!staSsid.length()) return;
  bool up = WiFi.status() == WL_CONNECTED;
  if (up && !staLinked) {
    staLinked = true; staTrying = false;
    Serial.printf("Linked to %s, my address: http://%s\n", staSsid.c_str(), WiFi.localIP().toString().c_str());
    showMsg("Linked! " + WiFi.localIP().toString(), 8000);
  }
  if (!up && staLinked) { staLinked = false; staBegin(); }
  if (staTrying && !up && millis() - staStart > 20000) {
    Serial.println("Hotspot not found - stopped searching (retry in 10 min)");
    staStop();
  }
  if (!staTrying && !up && staRetryAt && millis() > staRetryAt) staBegin();
}

void loop() {
  wifiLoop();
  dns.processNextRequest();
  server.handleClient();
  touchLoop();
  updateLogic();
  soundLoop();
  static unsigned long lastDraw = 0;
  if (millis() - lastDraw >= 33) { lastDraw = millis(); drawFrame(); }
}
