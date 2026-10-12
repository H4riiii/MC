#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============================================================
// SMART PILL BOX - SIMPLIFIED (RTC SKIPPED FOR NOW)
// ============================================================

// ---------------- OLED ----------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---------------- PINS ----------------
#define BUTTON_1 PA0
#define BUTTON_2 PA1
#define BUTTON_3 PA2
#define LED_1    PB1
#define LED_2    PB10
#define BUZZER   PB0

// ---------------- TIMING ----------------
const unsigned long DEBOUNCE_TIME = 200;
const unsigned long NORMAL_BLINK_INTERVAL = 500;
const unsigned long FAST_BLINK_INTERVAL = 100;
const unsigned long FIELD_BLINK_INTERVAL = 400;

// ---------------- SYSTEM STATES ----------------
enum SystemState {
  NORMAL_IDLE,
  REMINDER_DOSE_1,
  REMINDER_DOSE_2,
  DOSE_CONFIRMED,
  SETUP_MENU,
  SETUP_EDIT_HOUR,
  SETUP_EDIT_MINUTE,
  SETUP_CONFIRM,
  SETUP_FEEDBACK
};

SystemState currentState;
bool updateScreen = true;

// ---------------- ALARM SCHEDULES ----------------
struct DoseAlarm {
  int hour;
  int minute;
  int lastTriggeredDay;
};

DoseAlarm dose1 = {7, 30, -1};
DoseAlarm dose2 = {19, 0, -1};

// ---------------- SETUP TEMPORARIES ----------------
int setupSelectedPill = 1;
int tempHour = 0;
int tempMinute = 0;
bool confirmSaveChoice = true;
unsigned long feedbackTimer = 0;
unsigned long doseConfirmedTimer = 0;

// ---------------- BUTTON STATES ----------------
bool lastButton1State = HIGH;
bool lastButton2State = HIGH;
bool lastButton3State = HIGH;

unsigned long lastButton1Press = 0;
unsigned long lastButton2Press = 0;
unsigned long lastButton3Press = 0;

// ---------------- ALARM STATES ----------------
unsigned long lastAlarmToggle = 0;
unsigned long currentBlinkInterval = NORMAL_BLINK_INTERVAL;
bool alarmActive = false;
bool wrongButtonPressed = false;

unsigned long lastClockUpdate = 0;
unsigned long lastBlinkToggle = 0;
unsigned long lastTimeUpdate = 0;
bool cursorVisible = true;

// SIMULATED TIME (no RTC needed)
int simHour = 7;
int simMinute = 28;
int simSecond = 0;
int simDay = 1;

// ============================================================
// OLED UI SCREENS
// ============================================================

void printHeader() {
  char buf[12];
  sprintf(buf, "%02d:%02d:%02d", simHour, simMinute, simSecond);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(40, 2);
  display.print(buf);
  display.drawLine(0, 12, 128, 12, SSD1306_WHITE);
}

void renderScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  switch (currentState) {
    case NORMAL_IDLE: {
      printHeader();
      display.setTextSize(1);
      display.setCursor(4, 18);
      display.print("P1: ");
      char buf[8];
      sprintf(buf, "%02d:%02d", dose1.hour, dose1.minute);
      display.print(buf);

      display.setCursor(68, 18);
      display.print("P2: ");
      sprintf(buf, "%02d:%02d", dose2.hour, dose2.minute);
      display.print(buf);

      display.drawLine(0, 30, 128, 30, SSD1306_WHITE);
      display.setCursor(15, 36);
      display.println("READY & MONITORING");
      display.setCursor(15, 52);
      display.println("Press B3 for Setup");
      break;
    }

    case REMINDER_DOSE_1:
    case REMINDER_DOSE_2: {
      printHeader();
      display.setTextSize(2);
      display.setCursor(10, 16);
      display.println("MEDICINE!");
      display.setTextSize(1);
      display.setCursor(10, 38);
      if (currentState == REMINDER_DOSE_1) {
        display.println("TAKE: PILL 1");
        display.setCursor(10, 50);
        display.println("Press Button 1");
      } else {
        display.println("TAKE: PILL 2");
        display.setCursor(10, 50);
        display.println("Press Button 2");
      }
      if (wrongButtonPressed) {
        display.fillRect(0, 36, 128, 28, SSD1306_BLACK);
        display.setTextSize(1);
        display.setCursor(4, 40);
        display.println("WRONG PILL!");
        display.setCursor(4, 52);
        display.print("Take PILL ");
        display.print(currentState == REMINDER_DOSE_1 ? "1" : "2");
      }
      break;
    }

    case DOSE_CONFIRMED: {
      display.setTextSize(2);
      display.setCursor(12, 10);
      display.println("DOSE TAKEN");
      display.setTextSize(1);
      display.setCursor(20, 40);
      display.println("Good job!");
      break;
    }

    case SETUP_MENU: {
      display.setTextSize(1);
      display.setCursor(30, 2);
      display.println("SETUP DOSE");
      display.drawLine(0, 12, 128, 12, SSD1306_WHITE);
      display.setCursor(10, 24);
      display.print(setupSelectedPill == 1 ? "> PILL 1" : "  PILL 1");
      display.setCursor(10, 40);
      display.print(setupSelectedPill == 2 ? "> PILL 2" : "  PILL 2");
      display.setCursor(0, 54);
      display.println("B1:Move  B2:Select");
      break;
    }

    case SETUP_EDIT_HOUR:
    case SETUP_EDIT_MINUTE: {
      display.setTextSize(1);
      display.setCursor(26, 2);
      display.print(setupSelectedPill == 1 ? "PILL 1 TIME" : "PILL 2 TIME");
      display.drawLine(0, 12, 128, 12, SSD1306_WHITE);

      char buf[4];
      display.setTextSize(2);
      bool isEditHour = (currentState == SETUP_EDIT_HOUR);

      display.setCursor(30, 24);
      if (isEditHour && !cursorVisible) display.print("  ");
      else { sprintf(buf, "%02d", tempHour); display.print(buf); }

      display.setCursor(56, 24);
      display.print(":");

      display.setCursor(70, 24);
      if (!isEditHour && !cursorVisible) display.print("  ");
      else { sprintf(buf, "%02d", tempMinute); display.print(buf); }

      display.setTextSize(1);
      display.setCursor(isEditHour ? 35 : 75, 42);
      display.print("^^");
      display.setCursor(0, 55);
      display.print(isEditHour ? "B1:+Hour B2:Next" : "B1:+Min  B2:Save");
      break;
    }

    case SETUP_CONFIRM: {
      display.setTextSize(1);
      display.setCursor(30, 4);
      display.println("SAVE TIME?");
      display.drawLine(0, 14, 128, 14, SSD1306_WHITE);
      display.setCursor(25, 26);
      display.print(confirmSaveChoice ? "> YES" : "  YES");
      display.setCursor(25, 40);
      display.print(!confirmSaveChoice ? "> NO" : "  NO");
      display.setCursor(0, 55);
      display.println("B1:Toggle  B2:Confirm");
      break;
    }

    case SETUP_FEEDBACK: {
      display.setTextSize(2);
      display.setCursor(15, 12);
      display.print(setupSelectedPill == 1 ? "PILL 1" : "PILL 2");
      display.setTextSize(1);
      display.setCursor(25, 38);
      if (confirmSaveChoice) display.println("TIME SAVED!");
      else display.println("CANCELLED");
      break;
    }
  }
  display.display();
}

// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n==============================");
  Serial.println(" SMART PILL BOX");
  Serial.println("==============================");

  // Initialize Pins
  pinMode(BUTTON_1, INPUT_PULLUP);
  pinMode(BUTTON_2, INPUT_PULLUP);
  pinMode(BUTTON_3, INPUT_PULLUP);
  pinMode(LED_1, OUTPUT);
  pinMode(LED_2, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  // Ensure outputs are off
  digitalWrite(LED_1, LOW);
  digitalWrite(LED_2, LOW);
  noTone(BUZZER);

  // I2C
  Wire.setSCL(PB6);
  Wire.setSDA(PB7);
  Wire.begin();
  delay(100);

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("OLED FAILED!");
    while (1) delay(100);
  }
  Serial.println("OLED OK");

  // Initial state
  currentState = NORMAL_IDLE;
  updateScreen = true;
  lastTimeUpdate = millis();
}

// ============================================================
// LOOP
// ============================================================

void loop() {
  unsigned long now = millis();

  // Increment simulated time every second
  if (now - lastTimeUpdate >= 1000) {
    lastTimeUpdate = now;
    simSecond++;
    if (simSecond >= 60) {
      simSecond = 0;
      simMinute++;
      if (simMinute >= 60) {
        simMinute = 0;
        simHour++;
        if (simHour >= 24) {
          simHour = 0;
          simDay++;
        }
      }
    }
    updateScreen = true;
  }

  // Read buttons
  bool button1 = digitalRead(BUTTON_1);
  bool button2 = digitalRead(BUTTON_2);
  bool button3 = digitalRead(BUTTON_3);

  bool btn1ShortPressed = false;
  bool btn2ShortPressed = false;
  bool btn3ShortPressed = false;

  // Process Button 1
  if (lastButton1State == HIGH && button1 == LOW) {
    if (now - lastButton1Press > DEBOUNCE_TIME) {
      lastButton1Press = now;
      btn1ShortPressed = true;
      updateScreen = true;
    }
  }
  // Process Button 2
  if (lastButton2State == HIGH && button2 == LOW) {
    if (now - lastButton2Press > DEBOUNCE_TIME) {
      lastButton2Press = now;
      btn2ShortPressed = true;
      updateScreen = true;
    }
  }
  // Process Button 3
  if (lastButton3State == HIGH && button3 == LOW) {
    if (now - lastButton3Press > DEBOUNCE_TIME) {
      lastButton3Press = now;
      btn3ShortPressed = true;
      updateScreen = true;
    }
  }

  lastButton1State = button1;
  lastButton2State = button2;
  lastButton3State = button3;

  // Blink cursor in setup
  if (currentState == SETUP_EDIT_HOUR || currentState == SETUP_EDIT_MINUTE) {
    if (now - lastBlinkToggle >= FIELD_BLINK_INTERVAL) {
      lastBlinkToggle = now;
      cursorVisible = !cursorVisible;
      updateScreen = true;
    }
  }

  // ==========================================================
  // STATE MACHINE
  // ==========================================================
  switch (currentState) {

    case NORMAL_IDLE:
      if (btn3ShortPressed) {
        currentState = SETUP_MENU;
        setupSelectedPill = 1;
      }

      // Check Pill 1 Alarm
      if (simHour == dose1.hour && simMinute == dose1.minute && dose1.lastTriggeredDay != simDay) {
        dose1.lastTriggeredDay = simDay;
        alarmActive = false;
        wrongButtonPressed = false;
        currentBlinkInterval = NORMAL_BLINK_INTERVAL;
        currentState = REMINDER_DOSE_1;
        updateScreen = true;
      }

      // Check Pill 2 Alarm
      if (simHour == dose2.hour && simMinute == dose2.minute && dose2.lastTriggeredDay != simDay) {
        dose2.lastTriggeredDay = simDay;
        alarmActive = false;
        wrongButtonPressed = false;
        currentBlinkInterval = NORMAL_BLINK_INTERVAL;
        currentState = REMINDER_DOSE_2;
        updateScreen = true;
      }
      break;

    case REMINDER_DOSE_1:
      if (now - lastAlarmToggle >= currentBlinkInterval) {
        lastAlarmToggle = now;
        alarmActive = !alarmActive;
        digitalWrite(LED_1, alarmActive ? HIGH : LOW);
        if (alarmActive) tone(BUZZER, wrongButtonPressed ? 1500 : 1000);
        else noTone(BUZZER);
      }

      if (btn1ShortPressed) {
        digitalWrite(LED_1, LOW);
        noTone(BUZZER);
        doseConfirmedTimer = now;
        currentState = DOSE_CONFIRMED;
        updateScreen = true;
      } else if (btn2ShortPressed && !wrongButtonPressed) {
        wrongButtonPressed = true;
        currentBlinkInterval = FAST_BLINK_INTERVAL;
        updateScreen = true;
      }
      break;

    case REMINDER_DOSE_2:
      if (now - lastAlarmToggle >= currentBlinkInterval) {
        lastAlarmToggle = now;
        alarmActive = !alarmActive;
        digitalWrite(LED_2, alarmActive ? HIGH : LOW);
        if (alarmActive) tone(BUZZER, wrongButtonPressed ? 1500 : 1000);
        else noTone(BUZZER);
      }

      if (btn2ShortPressed) {
        digitalWrite(LED_2, LOW);
        noTone(BUZZER);
        doseConfirmedTimer = now;
        currentState = DOSE_CONFIRMED;
        updateScreen = true;
      } else if (btn1ShortPressed && !wrongButtonPressed) {
        wrongButtonPressed = true;
        currentBlinkInterval = FAST_BLINK_INTERVAL;
        updateScreen = true;
      }
      break;

    case DOSE_CONFIRMED:
      if (now - doseConfirmedTimer >= 2000) {
        currentState = NORMAL_IDLE;
        updateScreen = true;
      }
      break;

    case SETUP_MENU:
      if (btn1ShortPressed) setupSelectedPill = (setupSelectedPill == 1) ? 2 : 1;
      if (btn2ShortPressed) {
        tempHour = (setupSelectedPill == 1) ? dose1.hour : dose2.hour;
        tempMinute = (setupSelectedPill == 1) ? dose1.minute : dose2.minute;
        currentState = SETUP_EDIT_HOUR;
      }
      break;

    case SETUP_EDIT_HOUR:
      if (btn1ShortPressed) tempHour = (tempHour + 1) % 24;
      if (btn2ShortPressed) currentState = SETUP_EDIT_MINUTE;
      break;

    case SETUP_EDIT_MINUTE:
      if (btn1ShortPressed) tempMinute = (tempMinute + 1) % 60;
      if (btn2ShortPressed) {
        confirmSaveChoice = true;
        currentState = SETUP_CONFIRM;
      }
      break;

    case SETUP_CONFIRM:
      if (btn1ShortPressed) confirmSaveChoice = !confirmSaveChoice;
      if (btn2ShortPressed) {
        if (confirmSaveChoice) {
          if (setupSelectedPill == 1) {
            dose1.hour = tempHour;
            dose1.minute = tempMinute;
            dose1.lastTriggeredDay = -1;
          } else {
            dose2.hour = tempHour;
            dose2.minute = tempMinute;
            dose2.lastTriggeredDay = -1;
          }
        }
        feedbackTimer = now;
        currentState = SETUP_FEEDBACK;
      }
      break;

    case SETUP_FEEDBACK:
      if (now - feedbackTimer >= 1500) {
        currentState = NORMAL_IDLE;
        updateScreen = true;
      }
      break;
  }

  // Render screen when needed
  if (updateScreen) {
    renderScreen();
    updateScreen = false;
  }

  delay(10);
}
