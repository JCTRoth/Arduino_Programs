/*
 * Router-Kuehler mit Arduino Uno
 * DHT11 (HS-11) Temperatursensor
 * Arctic F12 PWM 4-Pin Luefter
 * 
 * Funktionen:
 * - 25 kHz PWM fuer PC-Luefter (Timer1, Pin 9)
 * - Ab 23 C langsam hochfahren, 50% bei 28 C, 95% bei 35 C
 * - Hysterese verhindert staendiges Hin-und-Her
 * - Mindestgeschwindigkeit 5% (Arctic F12 steht bei <5% PWM)
 * - Sanftes Hoch-/Herunterfahren (Ramp)
 * - Notabschaltung bei Sensorfehler oder >50 C
 * - Winter: unter 23 C bleibt der Luefter aus
 */

#include "DHT.h"

// ================= KONFIGURATION =================
#define DHTTYPE       DHT11

// Pin Definitions
#define DHTPIN        2       // Datapin of the DHT11
#define PWM_PIN       9       // Timer1-Pin 25kHz PWM
#define TACHO_PIN     3       // Optional: Tachometer

const float TEMP_START      = 22..0;   // Ab hier anfangen zu lueften (steigend)
const float TEMP_HALF       = 24.0;   // 50% Luefter bei steigender Temp
const float TEMP_MAX        = 35.0;   // 95% Luefter bei steigender Temp
const float TEMP_EMERGENCY  = 50.0;   // Notabschaltung / 100% Alarm

const int   FAN_MIN_SPEED   = 5;      // Arctic F12 steht bei <5% PWM
const int   RAMP_STEP       = 10;     // Schnelleres Hoch-/Herunterfahren
const int   LOOP_MS         = 2000;   // Messintervall (DHT11 braucht min. 2s!)

DHT dht(DHTPIN, DHTTYPE);

// Aktuelle Lueftergeschwindigkeit (0-100%)
int currentSpeed = 0;

// Hysterese-Speicher
static int lastTarget = 0;

// 25 kHz PWM fuer PC-Luefter initialisieren (Timer1, Pin 9)
void setupPWM25kHz() {
  pinMode(PWM_PIN, OUTPUT);
  
  // Fast PWM Mode 14, ICR1 = TOP, non-inverting
  TCCR1A = _BV(COM1A1) | _BV(WGM11);
  TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10); // Prescaler = 1
  
  ICR1 = 640 - 1;           // TOP: 16.000.000 / 25.000 = 640
  OCR1A = 0;                // Luefter aus (0% Duty)
}

// Geschwindigkeit setzen (0-100%)
void setFanSpeed(int percent) {
  if (percent < FAN_MIN_SPEED && percent > 0) percent = FAN_MIN_SPEED;
  if (percent > 100) percent = 100;
  
  // Map 0..100 auf 0..639 (Timer1 TOP)
  OCR1A = map(percent, 0, 100, 0, 639);
}

// Optional: RPM messen (2 Pulse pro Umdrehung)
unsigned long readRPM() {
  unsigned long count = 0;
  unsigned long start = millis();
  while (millis() - start < 1000) {
    if (digitalRead(TACHO_PIN) == LOW) {
      while (digitalRead(TACHO_PIN) == LOW); // warte auf HIGH
      count++;
    }
  }
  // 2 Pulse pro Umdrehung -> * 30 fuer U/min
  return count * 30;
}

void setup() {
  Serial.begin(9600);
  dht.begin();
  setupPWM25kHz();
  
  pinMode(TACHO_PIN, INPUT_PULLUP);
  
  Serial.println(F("Router-Kuehler gestartet"));
  Serial.println(F("Ziel: 25kHz PWM, Hysterese, Min 5%, Notabschaltung"));
  Serial.println(F("=========================================="));
}

void loop() {
  // Temperatur lesen
  float temp = dht.readTemperature();
  
  // --- NOTABSCHALTUNG / Sensorfehler ---
  if (isnan(temp)) {
    Serial.println(F("FEHLER: Sensor nicht erreichbar! Luefter -> 100%"));
    setFanSpeed(100);
    delay(LOOP_MS);
    return;
  }
  
  if (temp >= TEMP_EMERGENCY) {
    Serial.println(F("!!! NOTABSCHALTUNG: Kritische Temperatur !!!"));
    setFanSpeed(100);
    delay(LOOP_MS);
    return;
  }
  
  // --- Zielgeschwindigkeit berechnen ---
  int calcSpeed = 0;
  
  if (temp <= TEMP_START) {
    calcSpeed = 0;                 // Winter / Aus
  } else if (temp >= TEMP_MAX) {
    calcSpeed = 95;                // Fast volle Leistung bei 35C
  } else if (temp >= TEMP_HALF) {
    // Zwischen 28C und 35C: 50% bis 95%
    calcSpeed = map((int)(temp * 10), 
                    (int)(TEMP_HALF * 10), 
                    (int)(TEMP_MAX * 10), 
                    50, 95);
  } else {
    // Zwischen 23C und 28C: 5% bis 50%
    calcSpeed = map((int)(temp * 10), 
                    (int)(TEMP_START * 10), 
                    (int)(TEMP_HALF * 10), 
                    FAN_MIN_SPEED, 50);
  }
  
  // --- Hysterese anwenden ---
  int targetSpeed;
  
  if (calcSpeed > lastTarget) {
    // Temp steigt -> sofort hochregeln
    targetSpeed = calcSpeed;
  } else if (calcSpeed < lastTarget) {
    // Temp sinkt -> erst bei groesserer Differenz runterregeln
    if (lastTarget - calcSpeed >= 15) {  // Mindestens 15% Unterschied
      targetSpeed = calcSpeed;
    } else {
      targetSpeed = lastTarget;  // Behalte alten Wert bei
    }
  } else {
    targetSpeed = calcSpeed;
  }
  
  lastTarget = targetSpeed;
  
  // --- Sanftes Hoch-/Herunterfahren (Rampe) ---
  if (currentSpeed < targetSpeed) {
    currentSpeed += RAMP_STEP;
    if (currentSpeed > targetSpeed) currentSpeed = targetSpeed;
  } else if (currentSpeed > targetSpeed) {
    currentSpeed -= RAMP_STEP;
    if (currentSpeed < targetSpeed) currentSpeed = targetSpeed;
  }
  
  setFanSpeed(currentSpeed);
  
  // --- Debug-Ausgabe ---
  Serial.print(F("Temp: "));      Serial.print(temp, 1);   Serial.print(F(" C | "));
  Serial.print(F("Ziel: "));      Serial.print(targetSpeed); Serial.print(F("% | "));
  Serial.print(F("Luefter: "));   Serial.print(currentSpeed); Serial.print(F("%"));
  
  // Optional: RPM anzeigen
  // unsigned long rpm = readRPM();
  // Serial.print(F(" | RPM: ")); Serial.print(rpm);
  
  Serial.println();
  
  delay(LOOP_MS);
}
