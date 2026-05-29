/*
 * Groovebox Polifonico v5 - Arduino Uno Q
 * 2 tracce + accordi + effetti + arpeggiatore + forme d'onda HW
 *
 * ATTENZIONE Uno Q: potenziometro a 3.3V, non 5V!
 *
 * Forma d'onda: usa Zephyr PWM API (hardware STM32 TIM3/TIM4)
 *   Richiede arduino_uno_q.overlay nella cartella sketch/
 *   D8=PB4=TIM3_CH1  D9=PB8=TIM4_CH3  D10=PB9=TIM4_CH4
 *
 * Se il build fallisce per errori DTS, commenta USE_HW_PWM
 * e verrà usato tone() come fallback.
 *
 * Pulsanti (azioni al RILASCIO — barra progresso per long press):
 *   PLAY   short → play/stop
 *   LEFT   short → step -1     |  long → forma d'onda (pot)
 *   RIGHT  short → step +1     |  long → pattern arp (pot)
 *   UP     short → nota +1     |  long → arp on/off per step
 *   DOWN   short → nota -1
 *   TOGGLE short → step on/off |  long → accordo (traccia A)
 *   TRACK  short → switch A/B  |  long → effetto (pot)
 */

#include <U8g2lib.h>
#include <Wire.h>

#define USE_HW_PWM   // Hardware PWM via STM32 TIM3/TIM4 (zephyr_user node)

#ifdef USE_HW_PWM
#include <zephyr/drivers/pwm.h>
#endif

U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

// ════════════════════════════ Pin ════════════════════════════
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

// ════════════════════════ Scala C1–G5 ════════════════════════
const uint16_t NOTES[] = {
  0,
  33, 37, 41, 49, 55,      // C1 D1 E1 G1 A1
  65, 73, 82, 98, 110,     // C2 D2 E2 G2 A2
  131,147,165,196,220,     // C3 D3 E3 G3 A3
  262,294,330,392,440,     // C4 D4 E4 G4 A4
  523,587,659,784          // C5 D5 E5 G5
};
const char* NOTE_NAMES[] = {
  "---",
  "C1","D1","E1","G1","A1",
  "C2","D2","E2","G2","A2",
  "C3","D3","E3","G3","A3",
  "C4","D4","E4","G4","A4",
  "C5","D5","E5","G5"
};
#define NUM_NOTES  25
#define STEPS       8
#define NUM_TRACKS  2

// ════════════════════════ Effetti ════════════════════════
enum FxType  : uint8_t { FX_NONE, FX_VIBRATO, FX_TREMOLO, FX_STACCATO, FX_PORTAMENTO };
const char*  FX_NAMES[] = { "NONE","VIBRATO","TREMOLO","STACC.","PORTAM." };
#define NUM_FX 5

// ════════════════════════ Forme d'onda ════════════════════════
enum WaveType : uint8_t { WAVE_SQUARE, WAVE_PULSE25, WAVE_PULSE12 };
const char*  WAVE_NAMES[] = { "SQUARE","PULSE 25","PULSE 12" };
const uint8_t WAVE_DUTY[] = { 127, 64, 32 };   // duty cycle /255
#define NUM_WAVES 3

// ════════════════════════ Arpeggiatore ════════════════════════
enum ArpMode : uint8_t { ARP_UP, ARP_DOWN, ARP_UPDOWN, ARP_RANDOM };
const char*  ARP_NAMES[] = { "UP","DOWN","UP-DOWN","RANDOM" };
#define NUM_ARP_MODES 4
#define ARP_DIVS      4   // suddivisioni per step

// ════════════════════════ Stato sequencer ════════════════════════
uint8_t seq[NUM_TRACKS][STEPS];
bool    active[NUM_TRACKS][STEPS];
bool    chordOn[STEPS];
uint8_t stepFx[NUM_TRACKS][STEPS];
bool    stepArp[NUM_TRACKS][STEPS];

uint8_t  waveform[NUM_TRACKS] = { WAVE_SQUARE, WAVE_SQUARE };
ArpMode  arpMode              = ARP_UP;

uint8_t editTrack = 0;
int8_t  editStep  = 0;
int8_t  playStep  = -1;
bool    playing   = false;

unsigned long lastTick    = 0;
unsigned long lastArpTick = 0;
uint8_t       arpPos      = 0;

// ════════════════════════ UI modalità ════════════════════════
enum UIMode : uint8_t { UI_NORMAL, UI_FX, UI_WAVE, UI_ARP_MODE };
UIMode uiMode = UI_NORMAL;

// ════════════════════════ Playback voce ════════════════════════
uint16_t      baseFreq[3]  = {};
uint16_t      prevFreq[3]  = {};
bool          noteActive[3]= {};
unsigned long noteStart[3] = {};
unsigned long noteDur[3]   = {};
uint8_t       noteFx[3]    = {};
uint8_t       noteWave[3]  = {};

// ════════════════════════ Debounce + press detection ════════════════════════
// Tutte le azioni scattano al RILASCIO del tasto:
//   held < LONG_PRESS_MS  → short press
//   held >= LONG_PRESS_MS → long press
// La barra di progresso sul display mostra quanto manca al long press.

#define NUM_BTNS      7
#define DEBOUNCE_MS   50
#define LONG_PRESS_MS 3000UL

const uint8_t BTN_PINS[NUM_BTNS] = {
  BTN_PLAY, BTN_LEFT, BTN_RIGHT, BTN_UP, BTN_DOWN, BTN_TOGGLE, BTN_TRACK
};

bool          btnCur[NUM_BTNS]   = {};  // stato debounced corrente
bool          btnPrev[NUM_BTNS]  = {};  // stato raw precedente
bool          btnShort[NUM_BTNS] = {};  // true per un frame al rilascio short
bool          btnLong[NUM_BTNS]  = {};  // true per un frame al rilascio long
unsigned long btnTime[NUM_BTNS]  = {};  // timestamp ultimo cambio raw
unsigned long btnHold[NUM_BTNS]  = {};  // timestamp pressione debounced

void readButtons() {
  for (uint8_t i = 0; i < NUM_BTNS; i++) {
    btnShort[i] = false;
    btnLong[i]  = false;

    bool raw = (digitalRead(BTN_PINS[i]) == LOW);
    if (raw != btnPrev[i]) btnTime[i] = millis();
    btnPrev[i] = raw;

    bool deb = (millis() - btnTime[i] >= DEBOUNCE_MS) ? raw : btnCur[i];

    bool rising  =  deb && !btnCur[i];   // fronte di salita  (pressione)
    bool falling = !deb &&  btnCur[i];   // fronte di discesa (rilascio)

    if (rising)  btnHold[i] = millis();  // registra quando è stato premuto

    if (falling) {
      unsigned long held = millis() - btnHold[i];
      if (held >= LONG_PRESS_MS) btnLong[i]  = true;
      else                       btnShort[i] = true;
    }

    btnCur[i] = deb;
  }
}

bool pressed(uint8_t i)     { return btnShort[i]; }
bool longPressed(uint8_t i) { return btnLong[i];  }

// Indice del tasto tenuto premuto più a lungo (-1 se nessuno)
// Usato per mostrare la barra di progresso sul display
int8_t heldButtonIdx() {
  unsigned long best = 0;
  int8_t        idx  = -1;
  for (uint8_t i = 0; i < NUM_BTNS; i++) {
    if (btnCur[i]) {
      unsigned long held = millis() - btnHold[i];
      if (held > best) { best = held; idx = i; }
    }
  }
  return idx;
}

// ════════════════════════ Audio helpers ════════════════════════

#ifdef USE_HW_PWM
// Indici nel nodo /zephyr,user (system overlay arduino_uno_q_stm32u585xx.overlay):
//   5 → D8  / PB4 / TIM3_CH1  (BUZZER_A)
//   6 → D9  / PB8 / TIM4_CH3  (BUZZER_CHR)
//   7 → D10 / PB9 / TIM4_CH4  (BUZZER_B)
// Usa DT_PATH (→ DT_N_S_zephyr_user), NON DT_NODELABEL (→ DT_N_NODELABEL_...)
// perché il nodo non ha un label registrato nell'LLEXT precompilato.
static const struct pwm_dt_spec pwm_specs[3] = {
  PWM_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 5),
  PWM_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 6),
  PWM_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 7),
};
#endif

// Percentuali duty cycle per ogni forma d'onda
// SQUARE=50%, PULSE25=25%, PULSE12=12%
const uint8_t WAVE_DUTY_PCT[] = { 50, 25, 12 };

void voiceTone(uint8_t voice, uint16_t freq, uint8_t wave) {
  if (freq == 0) { voiceOff(voice); return; }

#ifdef USE_HW_PWM
  // Hardware PWM: periodo e pulse in nanosecondi
  uint32_t period_ns = 1000000000UL / (uint32_t)freq;
  uint32_t pulse_ns  = period_ns * WAVE_DUTY_PCT[wave] / 100;
  pwm_set_dt(&pwm_specs[voice], period_ns, pulse_ns);
#else
  // Fallback: tone() — solo onda quadra 50%
  tone(BUZZER_PINS[voice], freq);
#endif
}

void voiceOff(uint8_t voice) {
#ifdef USE_HW_PWM
  // Duty cycle 0 = silenzio, mantiene il timer attivo
  pwm_set_dt(&pwm_specs[voice], PWM_MSEC(1), 0);
#else
  noTone(BUZZER_PINS[voice]);
#endif
}

void stopAllVoices() {
  for (uint8_t v = 0; v < 3; v++) {
    voiceOff(v);
    noteActive[v] = false;
  }
}

void startNote(uint8_t voice, uint16_t freq, unsigned long dur,
               uint8_t effect, uint8_t wave) {
  prevFreq[voice]   = baseFreq[voice];
  baseFreq[voice]   = freq;
  noteFx[voice]     = effect;
  noteWave[voice]   = wave;
  noteStart[voice]  = millis();
  noteDur[voice]    = (effect == FX_STACCATO) ? dur / 4 : dur * 8 / 10;
  noteActive[voice] = (freq > 0);

  if (freq > 0) {
    uint16_t startF = (effect == FX_PORTAMENTO && prevFreq[voice] > 0)
                      ? prevFreq[voice] : freq;
    voiceTone(voice, startF, wave);
  }
}

void updateVoices() {
  unsigned long now = millis();
  for (uint8_t v = 0; v < 3; v++) {
    if (!noteActive[v]) continue;
    unsigned long el = now - noteStart[v];

    if (el >= noteDur[v]) {
      voiceOff(v);
      noteActive[v] = false;
      continue;
    }

    switch (noteFx[v]) {
      case FX_VIBRATO: {
        uint16_t phase = el % 167;
        int16_t  mod   = (phase < 84) ? (int16_t)phase - 42 : 42 - (int16_t)(phase - 84);
        uint16_t f     = baseFreq[v] + (int16_t)(baseFreq[v] * mod / 1050);
        voiceTone(v, f, noteWave[v]);
        break;
      }
      case FX_TREMOLO:
        if ((el / 62) % 2 == 0) voiceTone(v, baseFreq[v], noteWave[v]);
        else                     voiceOff(v);
        break;
      case FX_PORTAMENTO:
        if (prevFreq[v] > 0 && prevFreq[v] != baseFreq[v]) {
          long diff = (long)baseFreq[v] - (long)prevFreq[v];
          uint16_t f = prevFreq[v] + (uint16_t)(diff * (long)el / (long)noteDur[v]);
          voiceTone(v, f, noteWave[v]);
        }
        break;
      default: break;
    }
  }
}

// ════════════════════════ Arpeggiatore ════════════════════════
uint8_t scaleUp(uint8_t root, uint8_t steps) {
  return (uint8_t)min((int)root + steps, NUM_NOTES - 1);
}

// Triade pentatonica: root, +2 (3a), +4 (5a)
uint8_t arpNoteForPos(uint8_t track, uint8_t step, uint8_t pos) {
  uint8_t root  = seq[track][step];
  const uint8_t triad[3] = { root, scaleUp(root, 2), scaleUp(root, 4) };

  switch (arpMode) {
    case ARP_UP:     { const uint8_t p[] = {0,1,2,1}; return triad[p[pos % 4]]; }
    case ARP_DOWN:   { const uint8_t p[] = {2,1,0,1}; return triad[p[pos % 4]]; }
    case ARP_UPDOWN: { const uint8_t p[] = {0,1,2,1}; return triad[p[pos % 4]]; }
    case ARP_RANDOM: return triad[random(3)];
    default:         return root;
  }
}

// ════════════════════════ Trigger step ════════════════════════
void triggerStep(uint8_t step, unsigned long stepMs) {
  bool hasArpA = stepArp[0][step] && active[0][step] && seq[0][step] > 0;
  bool hasArpB = stepArp[1][step] && active[1][step] && seq[1][step] > 0;
  unsigned long arpMs = stepMs / ARP_DIVS;

  // Traccia A
  if (active[0][step] && seq[0][step] > 0) {
    uint16_t freq = hasArpA ? NOTES[arpNoteForPos(0, step, 0)] : NOTES[seq[0][step]];
    unsigned long dur = hasArpA ? arpMs : stepMs;
    startNote(0, freq, dur, stepFx[0][step], waveform[0]);

    // Accordo (solo senza arp)
    if (!hasArpA && chordOn[step]) {
      uint8_t cn = scaleUp(seq[0][step], 2);
      if (cn > 0) startNote(1, NOTES[cn], stepMs, stepFx[0][step], waveform[0]);
    }
  }

  // Traccia B
  if (active[1][step] && seq[1][step] > 0) {
    uint16_t freq = hasArpB ? NOTES[arpNoteForPos(1, step, 0)] : NOTES[seq[1][step]];
    unsigned long dur = hasArpB ? arpMs : stepMs;
    startNote(2, freq, dur, stepFx[1][step], waveform[1]);
  }
}

void tickArp(uint8_t step, unsigned long arpMs) {
  if (stepArp[0][step] && active[0][step] && seq[0][step] > 0) {
    voiceOff(0);
    startNote(0, NOTES[arpNoteForPos(0, step, arpPos)], arpMs, stepFx[0][step], waveform[0]);
  }
  if (stepArp[1][step] && active[1][step] && seq[1][step] > 0) {
    voiceOff(2);
    startNote(2, NOTES[arpNoteForPos(1, step, arpPos)], arpMs, stepFx[1][step], waveform[1]);
  }
}

// ════════════════════════ Display ════════════════════════
void drawModal(const char* title, const char* value, const char* hint) {
  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);
  display.setCursor(0, 12); display.print(title);
  display.setCursor(0, 26);
  display.print(editTrack == 0 ? F("Traccia A") : F("Traccia B"));
  display.setCursor(0, 44); display.print(F("> ")); display.print(value);
  display.setFont(u8g2_font_4x6_tf);
  display.setCursor(0, 58); display.print(hint);
  display.sendBuffer();
}

void drawUI() {
  // ── Modalità secondarie ──
  if (uiMode == UI_FX) {
    drawModal("EFFETTO", FX_NAMES[stepFx[editTrack][editStep]], "Rilascia TRACK per salvare");
    return;
  }
  if (uiMode == UI_WAVE) {
    drawModal("FORMA D'ONDA", WAVE_NAMES[waveform[editTrack]], "Rilascia LEFT per salvare");
    return;
  }
  if (uiMode == UI_ARP_MODE) {
    drawModal("MODO ARPEGGIO", ARP_NAMES[(uint8_t)arpMode], "Rilascia RIGHT per salvare");
    return;
  }

  // ── Vista normale ──
  display.clearBuffer();
  display.setDrawColor(1);
  display.setFont(u8g2_font_4x6_tf);

  // Etichette tracce
  display.setCursor(0, 10); display.print(editTrack == 0 ? F(">A") : F(" A"));
  display.setCursor(0, 22); display.print(editTrack == 1 ? F(">B") : F(" B"));

  // Step boxes
  for (uint8_t t = 0; t < NUM_TRACKS; t++) {
    uint8_t yBase = (t == 0) ? 4 : 16;

    for (uint8_t i = 0; i < STEPS; i++) {
      uint8_t x = 14 + i * 14;

      // Cursore editing
      if (i == (uint8_t)editStep && t == editTrack) {
        display.drawRBox(x - 2, yBase - 1, 13, 13, 2);
        display.setDrawColor(0);
      }

      if (active[t][i]) display.drawBox  (x, yBase, 9, 9);
      else              display.drawFrame(x, yBase, 9, 9);

      display.setDrawColor(1);

      // ▪ Puntino accordo (angolo sup. sinistro, solo track A)
      if (t == 0 && chordOn[i] && !stepArp[t][i])
        display.drawBox(x, yBase, 2, 2);

      // ▸ Puntino arpeggio (angolo sup. destro)
      if (stepArp[t][i])
        display.drawBox(x + 7, yBase, 2, 2);

      // ─ Lineetta effetto (sotto box)
      if (stepFx[t][i] != FX_NONE)
        display.drawHLine(x + 1, yBase + 10, 7);

      // ▼ Indicatore playback
      if (i == (uint8_t)playStep)
        display.drawBox(x + 3, yBase + 10, 3, 2);
    }
  }

  // ── Info step selezionato ──
  display.setFont(u8g2_font_6x10_tf);

  // Riga 1: traccia + nota + accordo/arp
  display.setCursor(0, 38);
  display.print(editTrack == 0 ? F("A:") : F("B:"));
  display.print(NOTE_NAMES[seq[editTrack][editStep]]);

  if (stepArp[editTrack][editStep]) {
    display.print(F(" ARP:"));
    display.print(ARP_NAMES[(uint8_t)arpMode]);
  } else if (editTrack == 0 && chordOn[editStep]) {
    display.print(F(" +CHR"));
  }
  if (stepFx[editTrack][editStep] != FX_NONE) {
    display.print(F(" "));
    display.print(FX_NAMES[stepFx[editTrack][editStep]]);
  }

  // Riga 2: waveform + BPM + play
  int bpm = map(analogRead(POT_PIN), 0, 1023, 40, 200);
  display.setCursor(0, 52);
  display.print(WAVE_NAMES[waveform[editTrack]]);
  display.print(playing ? F(" >> ") : F(" [] "));
  display.print(bpm);

  // ── Barra progresso long press ──
  int8_t held = heldButtonIdx();
  if (held >= 0) {
    unsigned long elapsed = millis() - btnHold[held];
    if (elapsed > 300) {  // mostra solo dopo 300ms per non essere invadente
      uint8_t barW = (uint8_t)min((long)elapsed * 128 / (long)LONG_PRESS_MS, 128L);
      display.drawFrame(0, 62, 128, 2);
      display.drawBox(0, 62, barW, 2);
    }
  }

  display.sendBuffer();
}

// ════════════════════════ Setup ════════════════════════
void setup() {
  display.begin();

  for (uint8_t i = 0; i < NUM_BTNS; i++)
    pinMode(BTN_PINS[i], INPUT_PULLUP);

#ifdef USE_HW_PWM
  // TIM3 e TIM4 usano zephyr,deferred-init: vanno inizializzati prima dell'uso.
  // analogWrite() sul core Zephyr attiva il device la prima volta che viene chiamato.
  analogWrite(BUZZER_A,   0);
  analogWrite(BUZZER_CHR, 0);
  analogWrite(BUZZER_B,   0);
  delay(10);

  // Verifica che i device PWM siano pronti
  for (uint8_t v = 0; v < 3; v++) {
    if (!device_is_ready(pwm_specs[v].dev)) {
      display.clearBuffer();
      display.setFont(u8g2_font_6x10_tf);
      display.setCursor(0, 20);
      display.print(F("ERR: PWM"));
      display.print(v);
      display.print(F(" not ready"));
      display.setCursor(0, 35);
      display.print(F("Ricompila senza"));
      display.setCursor(0, 48);
      display.print(F("USE_HW_PWM"));
      display.sendBuffer();
      while (1) delay(1000);
    }
  }
  // Metti tutte le voci in silenzio
  for (uint8_t v = 0; v < 3; v++) voiceOff(v);
#else
  for (uint8_t b : BUZZER_PINS) pinMode(b, OUTPUT);
#endif

  randomSeed(analogRead(A1));

  for (uint8_t i = 0; i < STEPS; i++) {
    seq[0][i]     = 17;  // C4
    seq[1][i]     = 12;  // C3
    active[0][i]  = (i < 4);
    active[1][i]  = (i % 2 == 0);
    chordOn[i]    = false;
    stepFx[0][i]  = FX_NONE;
    stepFx[1][i]  = FX_NONE;
    stepArp[0][i] = false;
    stepArp[1][i] = false;
  }
  // Pattern esempio
  seq[0][1] = 19; seq[0][2] = 21; seq[0][3] = 20; // E4 A4 G4
  seq[1][2]  = 14; seq[1][4] = 16;                  // E3 A3
}

// ════════════════════════ Loop ════════════════════════
void loop() {
  readButtons();

  int bpm = map(analogRead(POT_PIN), 0, 1023, 40, 200);
  unsigned long stepMs  = 60000UL / (unsigned long)bpm / 2;
  unsigned long arpMs   = stepMs / ARP_DIVS;

  // ── Gestione modalità secondarie ──
  if (uiMode != UI_NORMAL) {
    int pot = analogRead(POT_PIN);

    if      (uiMode == UI_FX)       stepFx[editTrack][editStep] = map(pot, 0, 1023, 0, NUM_FX - 1);
    else if (uiMode == UI_WAVE)     waveform[editTrack]         = map(pot, 0, 1023, 0, NUM_WAVES - 1);
    else if (uiMode == UI_ARP_MODE) arpMode = (ArpMode)map(pot, 0, 1023, 0, NUM_ARP_MODES - 1);

    // Esci al rilascio del tasto che ha aperto la modalità
    if ((uiMode == UI_FX       && !btnCur[6]) ||
        (uiMode == UI_WAVE     && !btnCur[1]) ||
        (uiMode == UI_ARP_MODE && !btnCur[2]))
      uiMode = UI_NORMAL;

    updateVoices();
    drawUI();
    return;
  }

  // ── Pulsanti modalità normale ──
  if (pressed(0)) {                                   // PLAY
    playing = !playing;
    if (!playing) { stopAllVoices(); playStep = -1; arpPos = 0; }
  }
  if (pressed(1)) editStep = (editStep - 1 + STEPS) % STEPS;   // LEFT
  if (pressed(2)) editStep = (editStep + 1) % STEPS;            // RIGHT
  if (pressed(3)) seq[editTrack][editStep] = min((int)seq[editTrack][editStep] + 1, NUM_NOTES - 1); // UP
  if (pressed(4)) seq[editTrack][editStep] = max((int)seq[editTrack][editStep] - 1, 0);             // DOWN

  // TOGGLE short = step on/off  |  long = accordo (solo track A)
  if (pressed(5))                       active[editTrack][editStep] = !active[editTrack][editStep];
  if (longPressed(5) && editTrack == 0) chordOn[editStep] = !chordOn[editStep];

  // UP long = arp toggle per step
  if (longPressed(3)) stepArp[editTrack][editStep] = !stepArp[editTrack][editStep];

  // LEFT long = modalità waveform
  if (longPressed(1)) uiMode = UI_WAVE;

  // RIGHT long = modalità arp mode
  if (longPressed(2)) uiMode = UI_ARP_MODE;

  // TRACK short = switch traccia  |  long = modalità effetto
  if (pressed(6))     editTrack = 1 - editTrack;
  if (longPressed(6)) uiMode = UI_FX;

  // ── Sequencer ──
  if (playing) {
    unsigned long now = millis();

    if (now - lastTick >= stepMs) {
      lastTick    = now;
      lastArpTick = now;
      playStep    = (playStep + 1) % STEPS;
      arpPos      = 0;
      stopAllVoices();
      triggerStep(playStep, stepMs);

    } else if (arpPos < ARP_DIVS - 1 && now - lastArpTick >= arpMs) {
      // Verifica che almeno uno step abbia arp
      if (stepArp[0][playStep] || stepArp[1][playStep]) {
        lastArpTick = now;
        arpPos++;
        tickArp(playStep, arpMs);
      }
    }
  }

  updateVoices();
  drawUI();
}
