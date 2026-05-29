/*
 * Groovebox 8-step - Arduino Uno Q (compatibile anche R3/R4)
 * Hardware: SH1106 OLED (I2C), buzzer passivo, 1 pot, 6 pulsanti
 * Libreria necessaria: U8g2 (installa da Library Manager)
 *
 * ATTENZIONE Uno Q: alimentare il potenziometro a 3.3V (non 5V)
 * I pin analogici non sono 5V-tolerant!
 */

#include <U8g2lib.h>
#include <Wire.h>

// --- Display SH1106 128x64 I2C ---
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

// --- Pin ---
#define BTN_PLAY    2
#define BTN_LEFT    3
#define BTN_RIGHT   4
#define BTN_UP      5
#define BTN_DOWN    6
#define BTN_TOGGLE  7
#define BUZZER_PIN  8
#define POT_PIN     A0

// --- Scale pentatonica (Do maggiore, 2 ottave) ---
// Indice 0 = silenzio
const uint16_t NOTES[]     = {0, 262, 294, 330, 392, 440, 523, 587, 659, 784, 880};
const char*    NOTE_NAMES[] = {"---","C4","D4","E4","G4","A4","C5","D5","E5","G5","A5"};
#define NUM_NOTES 11
#define STEPS     8

// --- Stato sequencer ---
uint8_t  seq[STEPS];        // indice nota per ogni step
bool     active[STEPS];     // step abilitato?
int8_t   editStep  = 0;     // step selezionato per editing
int8_t   playStep  = -1;    // step in riproduzione (-1 = fermo)
bool     playing   = false;
unsigned long lastTick = 0;

// --- Debounce pulsanti ---
#define NUM_BTNS 6
const uint8_t BTN_PINS[NUM_BTNS] = {BTN_PLAY, BTN_LEFT, BTN_RIGHT, BTN_UP, BTN_DOWN, BTN_TOGGLE};
bool    btnCur[NUM_BTNS], btnPrev[NUM_BTNS];
unsigned long btnTime[NUM_BTNS];
#define DEBOUNCE_MS 50

// Restituisce true nel frame in cui il pulsante viene premuto
bool pressed(uint8_t idx) {
  bool raw = (digitalRead(BTN_PINS[idx]) == LOW);
  if (raw != btnPrev[idx]) btnTime[idx] = millis();
  btnPrev[idx] = raw;
  if (millis() - btnTime[idx] < DEBOUNCE_MS) return false;
  bool edge = (raw && !btnCur[idx]);
  btnCur[idx] = raw;
  return edge;
}

// --- Display ---
void drawUI() {
  display.clearBuffer();
  display.setDrawColor(1);

  // Step boxes
  for (uint8_t i = 0; i < STEPS; i++) {
    uint8_t x = 4 + i * 15;
    uint8_t y = 2;

    // Cursore editing
    if (i == editStep) {
      display.drawRBox(x - 2, y - 2, 14, 18, 2);
      display.setDrawColor(0);
    }

    if (active[i]) {
      display.drawBox(x, y, 10, 14);
    } else {
      display.drawFrame(x, y, 10, 14);
    }

    display.setDrawColor(1);

    // Indicatore playback (triangolino sotto)
    if (i == playStep) {
      display.drawPixel(x + 4, y + 16);
      display.drawHLine(x + 3, y + 17, 3);
      display.drawHLine(x + 2, y + 18, 5);
    }
  }

  // Nota step selezionato
  display.setFont(u8g2_font_6x10_tf);
  display.setCursor(0, 42);
  display.print(F("Nota: "));
  display.print(NOTE_NAMES[seq[editStep]]);
  display.print(F("  Step:"));
  display.print(editStep + 1);

  // BPM
  int bpm = map(analogRead(POT_PIN), 0, 1023, 40, 200);
  display.setCursor(0, 56);
  display.print(playing ? F(">> PLAY") : F("[] STOP"));
  display.print(F("  BPM:"));
  display.print(bpm);

  display.sendBuffer();
}

// --- Setup ---
void setup() {
  display.begin();
  display.setFont(u8g2_font_6x10_tf);

  for (uint8_t i = 0; i < NUM_BTNS; i++) {
    pinMode(BTN_PINS[i], INPUT_PULLUP);
    btnCur[i] = btnPrev[i] = false;
    btnTime[i] = 0;
  }

  pinMode(BUZZER_PIN, OUTPUT);

  // Sequenza di default: prime 4 note abilitate
  for (uint8_t i = 0; i < STEPS; i++) {
    seq[i]    = 1;         // C4
    active[i] = (i < 4);
  }
  // Pattern di esempio
  seq[1] = 3; // E4
  seq[2] = 5; // A4
  seq[3] = 4; // G4
}

// --- Loop ---
void loop() {
  int bpm          = map(analogRead(POT_PIN), 0, 1023, 40, 200);
  unsigned long stepMs = 60000UL / (unsigned long)bpm / 2; // ottavi

  // --- Gestione pulsanti ---
  if (pressed(0)) {                          // PLAY/STOP
    playing = !playing;
    if (!playing) { noTone(BUZZER_PIN); playStep = -1; }
  }
  if (pressed(1)) editStep = (editStep - 1 + STEPS) % STEPS;  // LEFT
  if (pressed(2)) editStep = (editStep + 1) % STEPS;           // RIGHT
  if (pressed(3)) seq[editStep] = min(seq[editStep] + 1, NUM_NOTES - 1); // UP
  if (pressed(4)) seq[editStep] = max((int)seq[editStep] - 1, 0);        // DOWN
  if (pressed(5)) active[editStep] = !active[editStep];        // TOGGLE

  // --- Sequencer tick ---
  if (playing && millis() - lastTick >= stepMs) {
    lastTick = millis();
    playStep = (playStep + 1) % STEPS;
    noTone(BUZZER_PIN);
    if (active[playStep] && seq[playStep] > 0) {
      tone(BUZZER_PIN, NOTES[seq[playStep]], stepMs * 8 / 10);
    }
  }

  drawUI();
}
