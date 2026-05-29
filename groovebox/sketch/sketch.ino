/*
 * Groovebox 8-step Polifonico - Arduino Uno Q
 * 2 tracce indipendenti + accordi sulla traccia A
 * Hardware: SH1106 OLED (I2C), 3 buzzer, 1 pot, 7 pulsanti
 *
 * ATTENZIONE Uno Q: alimentare il potenziometro a 3.3V (non 5V)
 */

#include <U8g2lib.h>
#include <Wire.h>

U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

// --- Pin ---
#define BTN_PLAY    2
#define BTN_LEFT    3
#define BTN_RIGHT   4
#define BTN_UP      5
#define BTN_DOWN    6
#define BTN_TOGGLE  7
#define BTN_TRACK   11   // <-- nuovo: switch traccia A/B

#define BUZZER_A    8    // Traccia A voce principale
#define BUZZER_CHR  9    // Traccia A accordo
#define BUZZER_B    10   // Traccia B basso

#define POT_PIN     A0

// --- Scala pentatonica Do maggiore (2 ottave) ---
const uint16_t NOTES[]     = {0, 262, 294, 330, 392, 440, 523, 587, 659, 784, 880};
const char*    NOTE_NAMES[] = {"---","C4","D4","E4","G4","A4","C5","D5","E5","G5","A5"};
#define NUM_NOTES  11
#define STEPS      8
#define NUM_TRACKS 2

// --- Stato sequencer ---
uint8_t seq[NUM_TRACKS][STEPS];
bool    active[NUM_TRACKS][STEPS];
bool    chordOn[STEPS];       // accordo abilitato per ogni step (solo traccia A)
uint8_t editTrack = 0;        // traccia in editing: 0=A, 1=B
int8_t  editStep  = 0;
int8_t  playStep  = -1;
bool    playing   = false;
unsigned long lastTick = 0;

// --- Debounce + long press ---
#define NUM_BTNS     7
#define DEBOUNCE_MS  50
#define LONGPRESS_MS 500

const uint8_t BTN_PINS[NUM_BTNS] = {BTN_PLAY, BTN_LEFT, BTN_RIGHT, BTN_UP, BTN_DOWN, BTN_TOGGLE, BTN_TRACK};
bool    btnCur[NUM_BTNS], btnPrev[NUM_BTNS];
unsigned long btnTime[NUM_BTNS];
unsigned long btnHold[NUM_BTNS];

bool pressed(uint8_t idx) {
  bool raw = (digitalRead(BTN_PINS[idx]) == LOW);
  if (raw != btnPrev[idx]) btnTime[idx] = millis();
  btnPrev[idx] = raw;
  if (millis() - btnTime[idx] < DEBOUNCE_MS) return false;
  bool edge = (raw && !btnCur[idx]);
  if (edge) btnHold[idx] = millis();
  btnCur[idx] = raw;
  return edge;
}

bool longPressed(uint8_t idx) {
  if (!btnCur[idx]) return false;
  if (millis() - btnHold[idx] > LONGPRESS_MS) {
    btnHold[idx] = millis() + 9999999UL; // evita ripetizioni
    return true;
  }
  return false;
}

// Nota accordo: +2 posizioni nella scala (3a pentatonica)
uint8_t chordNote(uint8_t root) {
  if (root == 0) return 0;
  return min((int)root + 2, NUM_NOTES - 1);
}

// --- Display ---
void drawUI() {
  display.clearBuffer();
  display.setDrawColor(1);

  // Etichette tracce
  display.setFont(u8g2_font_4x6_tf);
  display.setCursor(0, 9);
  display.print(editTrack == 0 ? F(">A") : F(" A"));
  display.setCursor(0, 23);
  display.print(editTrack == 1 ? F(">B") : F(" B"));

  // Step box per entrambe le tracce
  for (uint8_t t = 0; t < NUM_TRACKS; t++) {
    uint8_t yBase = (t == 0) ? 2 : 16;

    for (uint8_t i = 0; i < STEPS; i++) {
      uint8_t x = 14 + i * 14;

      // Cursore editing
      if (i == (uint8_t)editStep && t == editTrack) {
        display.drawRBox(x - 2, yBase - 2, 13, 14, 2);
        display.setDrawColor(0);
      }

      if (active[t][i]) {
        display.drawBox(x, yBase, 9, 10);
      } else {
        display.drawFrame(x, yBase, 9, 10);
      }

      display.setDrawColor(1);

      // Puntino accordo (sopra lo step, solo traccia A)
      if (t == 0 && chordOn[i]) {
        display.drawBox(x + 3, yBase - 3, 3, 2);
      }

      // Indicatore playback (lineetta sotto la riga corrente)
      if (i == (uint8_t)playStep) {
        display.drawHLine(x + 1, (t == 0) ? 13 : 27, 7);
      }
    }
  }

  // Info step selezionato
  display.setFont(u8g2_font_6x10_tf);
  display.setCursor(0, 42);
  display.print(editTrack == 0 ? F("A:") : F("B:"));
  display.print(NOTE_NAMES[seq[editTrack][editStep]]);
  if (editTrack == 0 && chordOn[editStep]) {
    display.print(F("+"));
    display.print(NOTE_NAMES[chordNote(seq[0][editStep])]);
  }
  display.print(F(" S:"));
  display.print(editStep + 1);

  // Stato e BPM
  int bpm = map(analogRead(POT_PIN), 0, 1023, 40, 200);
  display.setCursor(0, 56);
  display.print(playing ? F(">> ") : F("[] "));
  display.print(F("BPM:"));
  display.print(bpm);

  display.sendBuffer();
}

// --- Setup ---
void setup() {
  display.begin();

  for (uint8_t i = 0; i < NUM_BTNS; i++) {
    pinMode(BTN_PINS[i], INPUT_PULLUP);
    btnCur[i] = btnPrev[i] = false;
    btnTime[i] = btnHold[i] = 0;
  }

  pinMode(BUZZER_A,   OUTPUT);
  pinMode(BUZZER_CHR, OUTPUT);
  pinMode(BUZZER_B,   OUTPUT);

  // Pattern di default
  for (uint8_t i = 0; i < STEPS; i++) {
    seq[0][i]    = 1;            // C4 melodia
    seq[1][i]    = 1;            // C4 basso
    active[0][i] = (i < 4);
    active[1][i] = (i % 2 == 0);
    chordOn[i]   = false;
  }
  seq[0][1] = 3; seq[0][2] = 5; seq[0][3] = 4; // melodia: C E A G
  seq[1][2] = 3; seq[1][4] = 5;                  // basso: C E A
}

// --- Loop ---
void loop() {
  int bpm = map(analogRead(POT_PIN), 0, 1023, 40, 200);
  unsigned long stepMs = 60000UL / (unsigned long)bpm / 2;

  // Pulsanti
  if (pressed(0)) {
    playing = !playing;
    if (!playing) {
      noTone(BUZZER_A);
      noTone(BUZZER_CHR);
      noTone(BUZZER_B);
      playStep = -1;
    }
  }
  if (pressed(1)) editStep = (editStep - 1 + STEPS) % STEPS;
  if (pressed(2)) editStep = (editStep + 1) % STEPS;
  if (pressed(3)) seq[editTrack][editStep] = min((int)seq[editTrack][editStep] + 1, NUM_NOTES - 1);
  if (pressed(4)) seq[editTrack][editStep] = max((int)seq[editTrack][editStep] - 1, 0);
  if (pressed(5))     active[editTrack][editStep] = !active[editTrack][editStep];
  if (longPressed(5) && editTrack == 0) chordOn[editStep] = !chordOn[editStep];
  if (pressed(6)) editTrack = 1 - editTrack;

  // Sequencer tick
  if (playing && millis() - lastTick >= stepMs) {
    lastTick = millis();
    playStep = (playStep + 1) % STEPS;

    noTone(BUZZER_A);
    noTone(BUZZER_CHR);
    noTone(BUZZER_B);

    // Traccia A (melodia + accordo)
    if (active[0][playStep] && seq[0][playStep] > 0) {
      tone(BUZZER_A, NOTES[seq[0][playStep]], stepMs * 8 / 10);
      if (chordOn[playStep]) {
        uint8_t cn = chordNote(seq[0][playStep]);
        if (cn > 0) tone(BUZZER_CHR, NOTES[cn], stepMs * 8 / 10);
      }
    }

    // Traccia B (basso)
    if (active[1][playStep] && seq[1][playStep] > 0) {
      tone(BUZZER_B, NOTES[seq[1][playStep]], stepMs * 8 / 10);
    }
  }

  drawUI();
}
