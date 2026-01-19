#include <SPI.h>
#include <Pixy.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

// --- PIN DEFINITIONS ---
#define DHTPIN 4          
#define DHTTYPE DHT11   
#define TRIG_PIN 8
#define ECHO_PIN 9
#define OVERLOAD_DIST 5
#define IR_SENSOR_PIN A0 
#define BUZZER_PIN 10    
#define LIMIT_SWITCH_PIN 11 

// --- MOTOR PINS ---
const int ENA = 5;  
const int IN1 = 6;
const int IN2 = 7;

// --- BUTTON PINS ---
const int TOGGLE_BTN = 2; 
const int TIMER_BTN  = 3; 

// --- INSTANCES ---
Pixy pixy;
LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT dht(DHTPIN, DHTTYPE);

// --- STATE DEFINITIONS ---
enum SystemState { STANDBY, CHOOSE_TIME, WASHING, DRAINING, DRYING };
SystemState currentState = STANDBY;

// --- VARIABLES ---
#define TEMP_THRESHOLD 30
int timerSetting = 1;      
unsigned long startTime = 0;
unsigned long washDuration = 10000;
const unsigned long DRAIN_DURATION = 5000;  
const unsigned long DRY_DURATION = 10000;   

bool lastToggleState = HIGH;
bool lastTimerState = HIGH;

// --- FUNCTION PROTOTYPES ---
void updateLCD(String line1, String line2 = "");
float getDistance();
void startMotor(int speed);
void stopMotor();
void deactivateSystem();
void beepWarning();
void beepFinish(); 
void debugSerial(float t, float d, int ir, bool whiteFound, String phase);

void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN); 
  digitalWrite(BUZZER_PIN, LOW);

  Serial.begin(115200); 
  pixy.init();
  dht.begin();

  lcd.init();
  lcd.backlight();
  lcd.clear();

  pinMode(ENA, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);

  pinMode(TOGGLE_BTN, INPUT_PULLUP); 
  pinMode(TIMER_BTN,  INPUT_PULLUP);
  pinMode(LIMIT_SWITCH_PIN, INPUT_PULLUP); 

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(IR_SENSOR_PIN, INPUT);
  
  stopMotor();
  updateLCD("SYSTEM READY", "PRESS START");
}

void loop() {
  bool currentToggleState = digitalRead(TOGGLE_BTN);
  bool currentTimerState = digitalRead(TIMER_BTN);

  // --- 1. TOGGLE BUTTON LOGIC ---
  if (currentToggleState == LOW && lastToggleState == HIGH) {
    delay(50); 
    if (currentState == STANDBY) {
      currentState = CHOOSE_TIME;
      updateLCD("CHOOSE TIME:", String(timerSetting * 10) + " Seconds");
    } 
    else if (currentState == CHOOSE_TIME) {
      if (digitalRead(LIMIT_SWITCH_PIN) == LOW) { 
        currentState = WASHING;
        startTime = millis();
        lcd.clear();
      } else { 
        updateLCD("PLEASE CLOSE", "THE DOOR");
        beepWarning(); 
        updateLCD("CHOOSE TIME:", String(timerSetting * 10) + " Seconds");
      }
    } 
    else if (currentState == WASHING || currentState == DRAINING || currentState == DRYING) {
      currentState = STANDBY;
      deactivateSystem();
      updateLCD("SYSTEM STOPPED", "PRESS START");
    }
  }
  lastToggleState = currentToggleState;

  // --- 2. TIMER SELECTION LOGIC ---
  if (currentState == CHOOSE_TIME) {
    if (currentTimerState == LOW && lastTimerState == HIGH) {
      delay(50); 
      timerSetting++;
      if (timerSetting > 3) timerSetting = 1;
      washDuration = (unsigned long)timerSetting * 10000;
      updateLCD("CHOOSE TIME:", String(timerSetting * 10) + " Seconds");
    }
  }
  lastTimerState = currentTimerState;

  // --- 3. MAIN OPERATION LOGIC ---
  if (currentState != STANDBY && currentState != CHOOSE_TIME) {
    
    // SAFETY: Door check
    if (digitalRead(LIMIT_SWITCH_PIN) == HIGH) { 
      stopMotor();
      tone(BUZZER_PIN, 1000); 
      updateLCD("DOOR OPENED!", "STOPPING...");
      return; 
    }

    unsigned long elapsed = millis() - startTime;
    float temp = dht.readTemperature();
    float distance = getDistance();
    int irVal = digitalRead(IR_SENSOR_PIN); 
    
    // --- PIXY SIGNATURE LOGIC ---
    uint16_t blocks = pixy.getBlocks();
    bool whiteFound = false;
    for (int i = 0; i < blocks; i++) {
      if (pixy.blocks[i].signature == 1) { // Specifically checking for Signature 1
        whiteFound = true;
        break;
      }
    }

    // --- PHASE: WASHING ---
    if (currentState == WASHING) {
      if (elapsed >= washDuration) {
        currentState = DRAINING;
        startTime = millis(); 
        stopMotor();
      } else {
        // ERROR HANDLING
        if (distance < OVERLOAD_DIST) {
          stopMotor();
          tone(BUZZER_PIN, 1000);
          updateLCD("OVERLOAD", "REDUCE LOAD");
        } 
        else if (!isnan(temp) && temp >= TEMP_THRESHOLD) {
          stopMotor();
          tone(BUZZER_PIN, 1000);
          updateLCD("OVERHEAT", "TEMP: " + String(temp) + "C");
        } 
        else if (irVal == HIGH) { // HIGH usually means nothing is in front of the IR sensor
          stopMotor();
          tone(BUZZER_PIN, 1000);
          updateLCD("NO CLOTH", "INSERT LAUNDRY");
        } 
        else {
          // NORMAL WASHING
          startMotor(200); 
          noTone(BUZZER_PIN);
          String timeStr = "WASHING: " + String((washDuration - elapsed) / 1000) + "s";
          updateLCD(timeStr, whiteFound ? "WHITE CLOTH" : "NO WHITE CLOTH");
        }
      }
    }
    // --- PHASE: DRAINING ---
    else if (currentState == DRAINING) {
      if (elapsed >= DRAIN_DURATION) {
        currentState = DRYING;
        startTime = millis(); 
      } else {
        stopMotor(); 
        noTone(BUZZER_PIN);
        updateLCD("DRAINING...", String((DRAIN_DURATION - elapsed) / 1000) + "s");
      }
    }
    // --- PHASE: DRYING ---
    else if (currentState == DRYING) {
      if (elapsed >= DRY_DURATION) {
        stopMotor();
        updateLCD("FINISHED", "CLEANING UP");
        beepFinish(); 
        currentState = STANDBY;
        deactivateSystem();
        updateLCD("FINISHED!", "PRESS START");
      } else {
        startMotor(255); 
        noTone(BUZZER_PIN);
        updateLCD("DRYING: " + String((DRY_DURATION - elapsed) / 1000) + "s", "HIGH SPEED");
      }
    }

    debugSerial(temp, distance, irVal, whiteFound, getPhaseName());
  } 
  else if (currentState == STANDBY) {
    deactivateSystem();
  }

  delay(50); 
}

// --- HELPER FUNCTIONS ---

void beepFinish() {
  for(int i = 0; i < 5; i++) {
    tone(BUZZER_PIN, 800); 
    delay(500);            
    noTone(BUZZER_PIN);
    delay(500);            
  }
}

String getPhaseName() {
  if (currentState == WASHING) return "WASH";
  if (currentState == DRAINING) return "DRAIN";
  if (currentState == DRYING) return "DRY";
  return "IDLE";
}

void debugSerial(float t, float d, int ir, bool whiteFound, String phase) {
  Serial.print("[" + phase + "] ");
  Serial.print("Temp:"); Serial.print(t);
  Serial.print(" C | Dist:"); Serial.print(d);
  Serial.print(" cm | Cloth:"); Serial.print(ir == LOW ? " OK" : " EMPTY");
  Serial.print(" | White Cloth:"); Serial.println(whiteFound ? " YES" : " NO");
}

void beepWarning() {
  for(int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, 1000); delay(200);
    noTone(BUZZER_PIN); delay(200);
  }
}

float getDistance() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 20000); 
  float d = (duration * 0.0343) / 2;
  return (d <= 0) ? 999 : d;
}

void startMotor(int speed) {
  digitalWrite(IN1, HIGH); 
  digitalWrite(IN2, LOW);
  analogWrite(ENA, speed);
}

void stopMotor() {
  digitalWrite(IN1, LOW); 
  digitalWrite(IN2, LOW);
  analogWrite(ENA, 0);
}

void deactivateSystem() {
  noTone(BUZZER_PIN);
  digitalWrite(BUZZER_PIN, LOW); 
  stopMotor();
}

void updateLCD(String line1, String line2) {
  lcd.setCursor(0,0); lcd.print(line1); 
  for(int i = line1.length(); i < 16; i++) { lcd.print(" "); }
  lcd.setCursor(0,1); lcd.print(line2);
  for(int i = line2.length(); i < 16; i++) { lcd.print(" "); }
}