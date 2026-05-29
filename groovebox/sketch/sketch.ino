/*
 * Groovebox Polifonico v2 - Arduino Uno Q
 * 2 tracce + accordi + effetti per step (C1-G5)
 *
 * ATTENZIONE Uno Q: potenziometro a 3.3V, non 5V!
 *
 * Pin:
 *   BTN_PLAY=2  BTN_LEFT=3  BTN_RIGHT=4  BTN_UP=5  BTN_DOWN=6
 *   BTN_TOGGLE=7  BTN_TRACK=11
 *   BUZZER_A=8 (melodia)  BUZZER_CHR=9 (accordo)  BUZZER_B=10 (basso)
 *   OLED SDA/SCL  POT=A0
 *
 * TOGGLE short press  = abilita/disabilita step
 * TOGGLE long press   = attiva/disattiva accordo (solo traccia A)
 * TRACK short press   = switch traccia A/B
 * TRACK long press    = entra in modalità effetto (pot = scelta effetto)
 */

#include <U8g2lib.h>
#include <Wire.h>

U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

// ──────────────────────────── Pin ────────────────────────────
#define BTN_PLAY    2
#define BTN_LEFT    3
#define BTN_RIGHT   4
#define BTN_UP      5
#define BTN_DOWN    6
#define BTN_TOGGLE  7
#define BTN_TRACK   11

#define BUZZER_A    8
#define BUZZER_CHR  9
#define BUZZER_B    10
#define POT_PIN     A0

const uint8_t BUZZER_PINS[3] = {BUZZER_A, BUZZER_CHR, BUZZER_B};

// ──────────────────────── Scala pentatonica C1–G5 ────────────────────────
// 0=silenzio, 1–5=ottava1, 6–10=ottava2, ..., 21–24=ottava5 (C5–G5)
const uint16_t NOTES[] = {
  0,
  33,  37,  41,  49,  55,    // C1 D1 E1 G1 A1
  65,  73,  82,  98,  110,   // C2 D2 E2 G2 A2
  131, 147, 165, 196, 220,   // C3 D3 E3 G3 A3
  262, 294, 330, 392, 440,   // C4 D4 E4 G4 A4
  523, 587, 659, 784         // C5 D5 E5 G5
};
const char* NOTE_NAMES[] = {
  "---",
  "C1","D1","E1","G1","A1",
  "C2","D2","E2","G2","A2",
  "C3","D3","E3","G3","A3",
  "C4","D4","E4","G4","A4",
  "C5","D5","E5","G5"
};
#define NUM_NOTES  25   // indice 0–24
#define STEPS       8
#define NUM_TRACKS  2

// ──────────────────────────── Effetti ────────────────────────────
enum FxType : uint8_t { FX_NONE, FX_VIBRATO, FX_TREMOLO, FX_STACCATO, FX_PORTAMENTO };
const char* FX_NAMES[] = { "NONE", "VIBRATO", "TREMOLO", "STACC.", "PORTAM." };
#define NUM_FX 5

// ──────────────────────── Stato sequencer ────────────────────────
uint8_t seq[NUM_TRACKS][STEPS];
bool    active[NUM_TRACKS][STEPS];
bool    chordOn[STEPS];
uint8_t stepFx[NUM_TRACKS][STEPS];

uint8_t editTrack = 0;
int8_t  editStep  = 0;
int8_t  playStep  = -1;
bool    playing   = false;
bool    fxMode    = false;    // modalità selezione effetto
unsigned long lastTick = 0;

// ──────────────────────── Playback per voce ────────────────────────
uint16_t      baseFreq[3]  = {0, 0, 0};
uint16_t      prevFreq[3]  = {0, 0, 0};
bool          noteActive[3]= {false, false, false};
unsigned long noteStart[3] = {0, 0, 0};
unsigned long noteDur[3]   = {0, 0, 0};
uint8_t       noteFx[3]    = {FX_NONE, FX_NONE, FX_NONE};

// ──────────────────────── Debounce + long press ────────────────────────
#define NUM_BTNS     7
#define DEBOUNCE_MS  50
#define LONGPRESS_MS 500

const uint8_t BTN_PINS[NUM_BTNS] = {
  BTN_PLAY, BTN_LEFT, BTN_RIGHT, BTN_UP, BTN_DOWN, BTN_TOGGLE, BTN_TRACK
};
bool          btnCur[NUM_BTNS]      = {};
bool          btnPrev[NUM_BTNS]     = {};
bool          btnEdge[NUM_BTNS]     = {};   // true solo nel frame di pressione
bool          btnLongFired[NUM_BTNS]= {};   // long press già scattato per questa hold
unsigned long btnTime[NUM_BTNS]     = {};
unsigned long btnHold[NUM_BTNS]     = {};

void readButtons() {
  for (uint8_t i = 0; i < NUM_BTNS; i++) {
    bool raw = (digitalRead(BTN_PINS[i]) == LOW);

    if (raw != btnPrev[i]) {
      btnTime[i] = millis();
      if (!raw) btnLongFired[i] = false;  // reset long press al rilascio
    }
    btnPrev[i] = raw;

    // Applica debounce
    bool debounced = (millis() - btnTime[i] >= DEBOUNCE_MS) ? raw : btnCur[i];

    // Fronte di salita (pressed)
    btnEdge[i] = (debounced && !btnCur[i]);
    if (btnEdge[i]) btnHold[i] = millis();

    btnCur[i] = debounced;
  }
}

bool pressed(uint8_t idx)    { return btnEdge[idx]; }

bool longPressed(uint8_t idx) {
  // Scatta una sola volta per ogni hold, solo dopo LONGPRESS_MS
  if (btnCur[idx] && !btnLongFired[idx] && millis() - btnHold[idx] > LONGPRESS_MS) {
    btnLongFired[idx] = true;
    return true;
  }
  return false;
}

// ──────────────────────── Nota accordo ────────────────────────
uint8_t chordNote(uint8_t root) {
  if (root == 0) return 0;
  return min((int)root + 2, NUM_NOTES - 1);
}

// ──────────────────────── Playback ────────────────────────
void startNote(uint8_t voice, uint16_t freq, unsigned long dur, uint8_t effect) {
  prevFreq[voice]   = baseFreq[voice];
  baseFreq[voice]   = freq;
  noteFx[voice]     = effect;
  noteStart[voice]  = millis();
  noteDur[voice]    = (effect == FX_STACCATO) ? dur / 4 : dur * 8 / 10;
  noteActive[voice] = (freq > 0);

  if (freq > 0) {
    uint16_t startF = (effect == FX_PORTAMENTO && prevFreq[voice] > 0)
                      ? prevFreq[voice] : freq;
    tone(BUZZER_PINS[voice], startF);
  }
}

void stopAllVoices() {
  for (uint8_t v = 0; v < 3; v++) {
    noTone(BUZZER_PINS[v]);
    noteActive[v] = false;
  }
}

void updateVoices() {
  unsigned long now = millis();
  for (uint8_t v = 0; v < 3; v++) {
    if (!noteActive[v]) continue;
    unsigned long el = now - noteStart[v];

    if (el >= noteDur[v]) {
      noTone(BUZZER_PINS[v]);
      noteActive[v] = false;
      continue;
    }

    switch (noteFx[v]) {

      case FX_VIBRATO: {
        // LFO triangolare ~6 Hz (periodo 167 ms), deviazione ±4%
        uint16_t phase = el % 167;
        int16_t  mod   = (phase < 84) ? (int16_t)phase - 42
                                      : 42 - (int16_t)(phase - 84);
        uint16_t f = baseFreq[v] + (int16_t)(baseFreq[v] * mod / 1050);
        tone(BUZZER_PINS[v], f);
        break;
      }

      case FX_TREMOLO: {
        // On/off ~8 Hz (62 ms half-period)
        if ((el / 62) % 2 == 0) tone(BUZZER_PINS[v], baseFreq[v]);
        else                     noTone(BUZZER_PINS[v]);
        break;
      }

      case FX_PORTAMENTO: {
        // Sweep lineare da prevFreq a baseFreq
        if (prevFreq[v] > 0 && prevFreq[v] != baseFreq[v]) {
          long  diff = (long)baseFreq[v] - (long)prevFreq[v];
          uint16_t f = prevFreq[v] + (uint16_t)(diff * (long)el / (long)noteDur[v]);
          tone(BUZZER_PINS[v], f);
        }
        break;
      }

      default: break;  // NONE, STACCATO: il tone è già impostato
    }
  }
}

// ──────────────────────── Display ────────────────────────
void drawUI() {
  display.clearBuffer();
  display.setDrawColor(1);

  // ── Modalità effetto ──
  if (fxMode) {
    uint8_t  curFx  = stepFx[editTrack][editStep];
    int      potVal = map(analogRead(POT_PIN), 0, 1023, 0, NUM_FX - 1);

    display.setFont(u8g2_font_6x10_tf);
    display.setCursor(0, 12);
    display.print(F("EFFETTO STEP "));
    display.print(editStep + 1);

    display.setCursor(0, 28);
    display.print(editTrack == 0 ? F("Traccia A") : F("Traccia B"));

    display.setCursor(0, 46);
    display.print(F("> "));
    display.print(FX_NAMES[potVal]);

    display.setFont(u8g2_font_4x6_tf);
    display.setCursor(0, 58);
    display.print(F("Rilascia TRACK per salvare"));

    display.sendBuffer();
    return;
  }

  // ── Vista normale ──
  display.setFont(u8g2_font_4x6_tf);

  // Etichette tracce
  display.setCursor(0, 9);  display.print(editTrack == 0 ? F(">A") : F(" A"));
  display.setCursor(0, 21); display.print(editTrack == 1 ? F(">B") : F(" B"));

  // Step boxes
  for (uint8_t t = 0; t < NUM_TRACKS; t++) {
    uint8_t yBase = (t == 0) ? 2 : 14;

    for (uint8_t i = 0; i < STEPS; i++) {
      uint8_t x = 14 + i * 14;

      if (i == (uint8_t)editStep && t == editTrack) {
        display.drawRBox(x - 2, yBase - 1, 13, 13, 2);
        display.setDrawColor(0);
      }

      if (active[t][i]) display.drawBox(x, yBase, 9, 9);
      else              display.drawFrame(x, yBase, 9, 9);

      display.setDrawColor(1);

      // Puntino accordo (sopra, solo track A)
      if (t == 0 && chordOn[i])
        display.drawBox(x + 3, yBase - 3, 3, 2);

      // Indicatore effetto (sotto la box, solo se non NONE)
      if (stepFx[t][i] != FX_NONE)
        display.drawHLine(x + 1, yBase + 10, 7);

      // Cursore playback
      if (i == (uint8_t)playStep)
        display.drawHLine(x, (t == 0) ? 12 : 24, 9);
    }
  }

  // Info step
  display.setFont(u8g2_font_6x10_tf);
  display.setCursor(0, 38);
  display.print(editTrack == 0 ? F("A:") : F("B:"));
  display.print(NOTE_NAMES[seq[editTrack][editStep]]);

  if (editTrack == 0 && chordOn[editStep]) {
    display.print(F("+"));
    display.print(NOTE_NAMES[chordNote(seq[0][editStep])]);
  }

  uint8_t ef = stepFx[editTrack][editStep];
  if (ef != FX_NONE) {
    display.print(F(" "));
    display.print(FX_NAMES[ef]);
  }

  // BPM + stato
  int bpm = map(analogRead(POT_PIN), 0, 1023, 40, 200);
  display.setCursor(0, 52);
  display.print(playing ? F(">> ") : F("[] "));
  display.print(F("BPM:"));
  display.print(bpm);

  display.sendBuffer();
}

// ──────────────────────── Setup ────────────────────────
void setup() {
  display.begin();

  for (uint8_t i = 0; i < NUM_BTNS; i++)
    pinMode(BTN_PINS[i], INPUT_PULLUP);

  for (uint8_t b : BUZZER_PINS) pinMode(b, OUTPUT);

  // Pattern di default
  for (uint8_t i = 0; i < STEPS; i++) {
    seq[0][i]    = 17;           // C4
    seq[1][i]    = 12;           // C3
    active[0][i] = (i < 4);
    active[1][i] = (i % 2 == 0);
    chordOn[i]   = false;
    stepFx[0][i] = FX_NONE;
    stepFx[1][i] = FX_NONE;
  }
  seq[0][1] = 19; seq[0][2] = 21; seq[0][3] = 20; // D4 A4... wait let me recalculate
  // C4=17 D4=18 E4=19 G4=20 A4=21
  seq[0][1] = 19; // E4
  seq[0][2] = 21; // A4
  seq[0][3] = 20; // G4
  seq[1][2] = 14; // E3
  seq[1][4] = 16; // A3... wait C3=12 D3=13 E3=14 G3=15 A3=16
  seq[1][4] = 16; // A3
}

// ──────────────────────── Loop ────────────────────────
void loop() {
  readButtons();

  int bpm = map(analogRead(POT_PIN), 0, 1023, 40, 200);
  unsigned long stepMs = 60000UL / (unsigned long)bpm / 2;

  // ── Gestione modalità effetto ──
  if (fxMode) {
    // Il pot sceglie l'effetto in tempo reale
    uint8_t selFx = (uint8_t)map(analogRead(POT_PIN), 0, 1023, 0, NUM_FX - 1);
    stepFx[editTrack][editStep] = selFx;

    // Esci da fxMode al rilascio di BTN_TRACK
    if (!btnCur[6]) fxMode = false;

    drawUI();
    updateVoices();
    return;
  }

  // ── Pulsanti in modalità normale ──

  // PLAY
  if (pressed(0)) {
    playing = !playing;
    if (!playing) { stopAllVoices(); playStep = -1; }
  }

  // Navigazione step
  if (pressed(1)) editStep = (editStep - 1 + STEPS) % STEPS;
  if (pressed(2)) editStep = (editStep + 1) % STEPS;

  // Nota su/giù
  if (pressed(3)) seq[editTrack][editStep] = min((int)seq[editTrack][editStep] + 1, NUM_NOTES - 1);
  if (pressed(4)) seq[editTrack][editStep] = max((int)seq[editTrack][editStep] - 1, 0);

  // TOGGLE: short = abilita/disabilita step, long = accordo (solo track A)
  if (pressed(5))                              active[editTrack][editStep] = !active[editTrack][editStep];
  if (longPressed(5) && editTrack == 0)        chordOn[editStep] = !chordOn[editStep];

  // TRACK: short = switch traccia, long = entra in fx mode
  if (pressed(6))      editTrack = 1 - editTrack;
  if (longPressed(6))  fxMode = true;

  // ── Tick sequencer ──
  if (playing && millis() - lastTick >= stepMs) {
    lastTick = millis();
    playStep = (playStep + 1) % STEPS;

    stopAllVoices();

    // Traccia A (melodia)
    if (active[0][playStep] && seq[0][playStep] > 0) {
      startNote(0, NOTES[seq[0][playStep]], stepMs, stepFx[0][playStep]);

      // Accordo
      if (chordOn[playStep]) {
        uint8_t cn = chordNote(seq[0][playStep]);
        if (cn > 0) startNote(1, NOTES[cn], stepMs, stepFx[0][playStep]);
      }
    }

    // Traccia B (basso)
    if (active[1][playStep] && seq[1][playStep] > 0)
      startNote(2, NOTES[seq[1][playStep]], stepMs, stepFx[1][playStep]);
  }

  updateVoices();
  drawUI();
}
