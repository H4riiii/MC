#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============================================================
// SMART PILL BOX - OLED & BUTTONS (MINIMAL HARDWARE CONFIG)
// Designed for STM32F103C8 (Blue Pill)
// ============================================================

// ============================================================
// HARDWARE FEATURE FLAGS
// Set to 1 when you connect the physical LED / Buzzer hardware!
// Set to 0 to run with only STM32 + OLED + Buttons.
// ============================================================
#define ENABLE_LEDS   0   // 1 = Enable Pill 1 (PB1) & Pill 2 (PB10) LEDs
#define ENABLE_BUZZER 0   // 1 = Enable Piezo Buzzer (PB0)

// ---------------- OLED DISPLAY ----------------
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_ADDRESS  0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---------------- PIN ASSIGNMENTS ----------------
#define BUTTON_1 PB12  // Pushbutton 1: Pill 1 Dose confirm / Menu +
#define BUTTON_2 PB14  // Pushbutton 2: Pill 2 Dose confirm / Menu select
#define BUTTON_3 PB15  // Pushbutton 3: Open Setup Menu

#if ENABLE_LEDS
#define LED_1    PB1   // Pill 1 LED (Red)
#define LED_2    PB10  // Pill 2 LED (Green)
#endif

#if ENABLE_BUZZER
#define BUZZER   PB0   // Piezo Buzzer
#endif

// ---------------- TIMING CONSTANTS (ms) ----------------
const unsigned long DEBOUNCE_TIME         = 200;
const unsigned long NORMAL_BLINK_INTERVAL = 500;
const unsigned long FAST_BLINK_INTERVAL   = 150;
const unsigned long FIELD_BLINK_INTERVAL  = 400;

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

SystemState currentState = NORMAL_IDLE;
bool updateScreen = true;

// ---------------- ALARM SCHEDULES ----------------
struct DoseAlarm {
  int hour;
  int minute;
  int lastTriggeredDay;
};

// Default dose schedules: Dose 1 at 07:30, Dose 2 at 19:00
DoseAlarm dose1 = {7, 30, -1};
DoseAlarm dose2 = {19, 0, -1};

// ---------------- SETUP TEMPORARIES ----------------
int setupSelectedPill = 1;
int tempHour = 0;
int tempMinute = 0;
bool confirmSaveChoice = true;
unsigned long feedbackTimer = 0;
unsigned long doseConfirmedTimer = 0;

// ---------------- BUTTON DEBOUNCE TRACKERS ----------------
bool lastButton1State = HIGH;
bool lastButton2State = HIGH;
bool lastButton3State = HIGH;

unsigned long lastButton1Press = 0;
unsigned long lastButton2Press = 0;
unsigned long lastButton3Press = 0;

// ---------------- ALARM & BLINK TRACKERS ----------------
unsigned long lastAlarmToggle = 0;
unsigned long currentBlinkInterval = NORMAL_BLINK_INTERVAL;
bool alarmVisualState = false;    // Toggles for visual OLED/LED flashing
bool wrongButtonPressed = false;

unsigned long lastBlinkToggle = 0;
unsigned long lastTimeUpdate = 0;
bool cursorVisible = true;

// ---------------- SIMULATED CLOCK (24H) ----------------
// Starts at 07:28:00 so Dose 1 (07:30) triggers ~2 minutes after boot for easy testing
int simHour   = 7;
int simMinute = 28;
int simSecond = 0;
int simDay    = 1;

// ============================================================
// OLED UI RENDERING
// ============================================================

void printHeader() {
  char buf[12];
  sprintf(buf, "%02d:%02d:%02d", simHour, simMinute, simSecond);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(38, 2);
  display.print(buf);
  display.drawLine(0, 12, 128, 12, SSD1306_WHITE);
}

void renderScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  switch (currentState) {
    case NORMAL_IDLE: {
      printHeader();

      // Show scheduled dose times
      char buf[10];
      display.setTextSize(1);
      display.setCursor(4, 18);
      display.print("P1: ");
      sprintf(buf, "%02d:%02d", dose1.hour, dose1.minute);
      display.print(buf);

      display.setCursor(68, 18);
      display.print("P2: ");
      sprintf(buf, "%02d:%02d", dose2.hour, dose2.minute);
      display.print(buf);

      display.drawLine(0, 30, 128, 30, SSD1306_WHITE);

      // Status indicator
      display.setCursor(12, 36);
      display.println("READY & MONITORING");
      display.setCursor(10, 52);
      display.println("Press [B3] for Setup");
      break;
    }

    case REMINDER_DOSE_1:
    case REMINDER_DOSE_2: {
      printHeader();

      bool isDose1 = (currentState == REMINDER_DOSE_1);

      if (wrongButtonPressed) {
        // High-contrast warning box for wrong button press
        display.fillRect(0, 16, 128, 48, alarmVisualState ? SSD1306_WHITE : SSD1306_BLACK);
        display.setTextColor(alarmVisualState ? SSD1306_BLACK : SSD1306_WHITE);

        display.setTextSize(2);
        display.setCursor(10, 20);
        display.println("WRONG PILL!");

        display.setTextSize(1);
        display.setCursor(10, 40);
        display.print("Press BUTTON ");
        display.print(isDose1 ? "1" : "2");
        display.setCursor(10, 52);
        display.print("for PILL ");
        display.print(isDose1 ? "1" : "2");
      } else {
        // Normal reminder display with flashing alert box for visual pop
        if (alarmVisualState) {
          display.fillRect(0, 15, 128, 18, SSD1306_WHITE);
          display.setTextColor(SSD1306_BLACK);
        } else {
          display.setTextColor(SSD1306_WHITE);
        }

        display.setTextSize(2);
        display.setCursor(12, 17);
        display.print("MEDICINE!");

        display.setTextColor(SSD1306_WHITE);
        display.setTextSize(1);
        display.setCursor(8, 38);
        display.print(isDose1 ? "TAKE: >> PILL 1 <<" : "TAKE: >> PILL 2 <<");

        display.setCursor(8, 52);
        display.print(isDose1 ? "Press Button 1 to OK" : "Press Button 2 to OK");
      }
      break;
    }

    case DOSE_CONFIRMED: {
      display.fillRect(2, 4, 124, 56, SSD1306_BLACK);
      display.drawRect(0, 2, 128, 60, SSD1306_WHITE);

      display.setTextSize(2);
      display.setCursor(6, 14);
      display.println("DOSE TAKEN");

      display.setTextSize(1);
      display.setCursor(14, 40);
      display.println("Recorded! Good job.");
      break;
    }

    case SETUP_MENU: {
      display.setTextSize(1);
      display.setCursor(34, 2);
      display.println("SETUP DOSE");
      display.drawLine(0, 12, 128, 12, SSD1306_WHITE);

      display.setCursor(10, 24);
      display.print(setupSelectedPill == 1 ? "> PILL 1" : "  PILL 1");

      display.setCursor(10, 40);
      display.print(setupSelectedPill == 2 ? "> PILL 2" : "  PILL 2");

      display.drawLine(0, 52, 128, 52, SSD1306_WHITE);
      display.setCursor(2, 55);
      display.println("B1:Move  B2:Select");
      break;
    }

    case SETUP_EDIT_HOUR:
    case SETUP_EDIT_MINUTE: {
      display.setTextSize(1);
      display.setCursor(24, 2);
      display.print(setupSelectedPill == 1 ? "PILL 1 TIME" : "PILL 2 TIME");
      display.drawLine(0, 12, 128, 12, SSD1306_WHITE);

      char buf[4];
      display.setTextSize(2);
      bool isEditHour = (currentState == SETUP_EDIT_HOUR);

      // Hour display (blinking if editing)
      display.setCursor(30, 24);
      if (isEditHour && !cursorVisible) {
        display.print("  ");
      } else {
        sprintf(buf, "%02d", tempHour);
        display.print(buf);
      }

      display.setCursor(56, 24);
      display.print(":");

      // Minute display (blinking if editing)
      display.setCursor(70, 24);
      if (!isEditHour && !cursorVisible) {
        display.print("  ");
      } else {
        sprintf(buf, "%02d", tempMinute);
        display.print(buf);
      }

      display.setTextSize(1);
      display.setCursor(isEditHour ? 35 : 75, 42);
      display.print("^^");

      display.drawLine(0, 52, 128, 52, SSD1306_WHITE);
      display.setCursor(2, 55);
      display.print(isEditHour ? "B1:+Hour B2:Next" : "B1:+Min  B2:Save");
      break;
    }

    case SETUP_CONFIRM: {
      display.setTextSize(1);
      display.setCursor(32, 2);
      display.println("SAVE TIME?");
      display.drawLine(0, 12, 128, 12, SSD1306_WHITE);

      display.setTextSize(1);
      display.setCursor(30, 24);
      display.print(confirmSaveChoice ? "> YES, SAVE" : "  YES, SAVE");

      display.setCursor(30, 38);
      display.print(!confirmSaveChoice ? "> CANCEL" : "  CANCEL");

      display.drawLine(0, 52, 128, 52, SSD1306_WHITE);
      display.setCursor(2, 55);
      display.println("B1:Toggle  B2:Confirm");
      break;
    }

    case SETUP_FEEDBACK: {
      display.setTextSize(2);
      display.setCursor(16, 12);
      display.print(setupSelectedPill == 1 ? "PILL 1" : "PILL 2");

      display.setTextSize(1);
      display.setCursor(24, 38);
      if (confirmSaveChoice) {
        display.println("TIME SAVED!");
      } else {
        display.println("CHANGES DISCARDED");
      }
      break;
    }
  }

  display.display();
}

// ============================================================
// HARDWARE CONTROL HELPERS
// ============================================================

void updateAlarmIndicators(bool state, bool isPill1, bool isFastBlink) {
  // If LED hardware is enabled, toggle corresponding LED
#if ENABLE_LEDS
  if (isPill1) {
    digitalWrite(LED_1, state ? HIGH : LOW);
  } else {
    digitalWrite(LED_2, state ? HIGH : LOW);
  }
#else
  (void)isPill1; // Suppress unused warning
#endif

  // If Buzzer hardware is enabled, play warning tone
#if ENABLE_BUZZER
  if (state) {
    tone(BUZZER, isFastBlink ? 1500 : 1000);
  } else {
    noTone(BUZZER);
  }
#else
  (void)isFastBlink; // Suppress unused warning
#endif
  (void)state;
}

void stopAlarmIndicators() {
#if ENABLE_LEDS
  digitalWrite(LED_1, LOW);
  digitalWrite(LED_2, LOW);
#endif

#if ENABLE_BUZZER
  noTone(BUZZER);
#endif
}

// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println("\n==================================");
  Serial.println(" SMART PILL BOX - FIRMWARE v1.0");
  Serial.println("==================================");

  // Initialize Pushbutton Pins with internal Pullups
  pinMode(BUTTON_1, INPUT_PULLUP);
  pinMode(BUTTON_2, INPUT_PULLUP);
  pinMode(BUTTON_3, INPUT_PULLUP);

#if ENABLE_LEDS
  pinMode(LED_1, OUTPUT);
  pinMode(LED_2, OUTPUT);
  digitalWrite(LED_1, LOW);
  digitalWrite(LED_2, LOW);
  Serial.println("Hardware: LEDs enabled");
#else
  Serial.println("Hardware: LEDs disabled (OLED visual alerts active)");
#endif

#if ENABLE_BUZZER
  pinMode(BUZZER, OUTPUT);
  noTone(BUZZER);
  Serial.println("Hardware: Buzzer enabled");
#else
  Serial.println("Hardware: Buzzer disabled");
#endif

  // Configure STM32 I2C1 (PB6 = SCL, PB7 = SDA)
  Wire.setSCL(PB6);
  Wire.setSDA(PB7);
  Wire.begin();
  delay(100);

  // Initialize SSD1306 OLED Display
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("ERROR: SSD1306 OLED not detected at 0x3C!");
    while (1) {
      delay(500);
    }
  }
  Serial.println("SUCCESS: SSD1306 OLED initialized");

  // Show welcome splash screen
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(14, 14);
  display.println("PILL BOX");
  display.setTextSize(1);
  display.setCursor(20, 38);
  display.println("INITIALIZING...");
  display.display();
  delay(1200);

  // Enter normal idle monitoring
  currentState = NORMAL_IDLE;
  updateScreen = true;
  lastTimeUpdate = millis();
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop() {
  unsigned long now = millis();

  // 1. Increment simulated 24-hour time clock
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

  // 2. Read buttons with edge-detection debounce
  bool b1 = digitalRead(BUTTON_1);
  bool b2 = digitalRead(BUTTON_2);
  bool b3 = digitalRead(BUTTON_3);

  bool btn1Pressed = false;
  bool btn2Pressed = false;
  bool btn3Pressed = false;

  if (lastButton1State == HIGH && b1 == LOW) {
    if (now - lastButton1Press > DEBOUNCE_TIME) {
      lastButton1Press = now;
      btn1Pressed = true;
      updateScreen = true;
    }
  }

  if (lastButton2State == HIGH && b2 == LOW) {
    if (now - lastButton2Press > DEBOUNCE_TIME) {
      lastButton2Press = now;
      btn2Pressed = true;
      updateScreen = true;
    }
  }

  if (lastButton3State == HIGH && b3 == LOW) {
    if (now - lastButton3Press > DEBOUNCE_TIME) {
      lastButton3Press = now;
      btn3Pressed = true;
      updateScreen = true;
    }
  }

  lastButton1State = b1;
  lastButton2State = b2;
  lastButton3State = b3;

  // 3. Blink cursor while editing time in setup
  if (currentState == SETUP_EDIT_HOUR || currentState == SETUP_EDIT_MINUTE) {
    if (now - lastBlinkToggle >= FIELD_BLINK_INTERVAL) {
      lastBlinkToggle = now;
      cursorVisible = !cursorVisible;
      updateScreen = true;
    }
  }

  // ==========================================================
  // 4. SYSTEM FINITE STATE MACHINE
  // ==========================================================
  switch (currentState) {

    case NORMAL_IDLE:
      // Button 3 opens setup menu
      if (btn3Pressed) {
        currentState = SETUP_MENU;
        setupSelectedPill = 1;
        updateScreen = true;
      }

      // Check Pill 1 Alarm Trigger
      if (simHour == dose1.hour && simMinute == dose1.minute && dose1.lastTriggeredDay != simDay) {
        dose1.lastTriggeredDay = simDay;
        alarmVisualState = false;
        wrongButtonPressed = false;
        currentBlinkInterval = NORMAL_BLINK_INTERVAL;
        currentState = REMINDER_DOSE_1;
        updateScreen = true;
      }

      // Check Pill 2 Alarm Trigger
      if (simHour == dose2.hour && simMinute == dose2.minute && dose2.lastTriggeredDay != simDay) {
        dose2.lastTriggeredDay = simDay;
        alarmVisualState = false;
        wrongButtonPressed = false;
        currentBlinkInterval = NORMAL_BLINK_INTERVAL;
        currentState = REMINDER_DOSE_2;
        updateScreen = true;
      }
      break;

    case REMINDER_DOSE_1:
      // Periodic blink timer for OLED visual flash (and LED/buzzer if enabled)
      if (now - lastAlarmToggle >= currentBlinkInterval) {
        lastAlarmToggle = now;
        alarmVisualState = !alarmVisualState;
        updateAlarmIndicators(alarmVisualState, true, wrongButtonPressed);
        updateScreen = true;
      }

      // Button 1 is the CORRECT button for Pill 1
      if (btn1Pressed) {
        stopAlarmIndicators();
        doseConfirmedTimer = now;
        currentState = DOSE_CONFIRMED;
        updateScreen = true;
      }
      // Button 2 is the WRONG button for Pill 1
      else if (btn2Pressed && !wrongButtonPressed) {
        wrongButtonPressed = true;
        currentBlinkInterval = FAST_BLINK_INTERVAL;
        updateScreen = true;
      }
      break;

    case REMINDER_DOSE_2:
      // Periodic blink timer for OLED visual flash (and LED/buzzer if enabled)
      if (now - lastAlarmToggle >= currentBlinkInterval) {
        lastAlarmToggle = now;
        alarmVisualState = !alarmVisualState;
        updateAlarmIndicators(alarmVisualState, false, wrongButtonPressed);
        updateScreen = true;
      }

      // Button 2 is the CORRECT button for Pill 2
      if (btn2Pressed) {
        stopAlarmIndicators();
        doseConfirmedTimer = now;
        currentState = DOSE_CONFIRMED;
        updateScreen = true;
      }
      // Button 1 is the WRONG button for Pill 2
      else if (btn1Pressed && !wrongButtonPressed) {
        wrongButtonPressed = true;
        currentBlinkInterval = FAST_BLINK_INTERVAL;
        updateScreen = true;
      }
      break;

    case DOSE_CONFIRMED:
      // Show confirmation message for 2 seconds then return to idle
      if (now - doseConfirmedTimer >= 2000) {
        currentState = NORMAL_IDLE;
        updateScreen = true;
      }
      break;

    case SETUP_MENU:
      if (btn1Pressed) {
        // Toggle selected pill
        setupSelectedPill = (setupSelectedPill == 1) ? 2 : 1;
        updateScreen = true;
      }
      if (btn2Pressed) {
        // Load existing time and advance to hour edit
        tempHour   = (setupSelectedPill == 1) ? dose1.hour : dose2.hour;
        tempMinute = (setupSelectedPill == 1) ? dose1.minute : dose2.minute;
        currentState = SETUP_EDIT_HOUR;
        updateScreen = true;
      }
      break;

    case SETUP_EDIT_HOUR:
      if (btn1Pressed) {
        tempHour = (tempHour + 1) % 24;
        updateScreen = true;
      }
      if (btn2Pressed) {
        currentState = SETUP_EDIT_MINUTE;
        updateScreen = true;
      }
      break;

    case SETUP_EDIT_MINUTE:
      if (btn1Pressed) {
        tempMinute = (tempMinute + 1) % 60;
        updateScreen = true;
      }
      if (btn2Pressed) {
        confirmSaveChoice = true;
        currentState = SETUP_CONFIRM;
        updateScreen = true;
      }
      break;

    case SETUP_CONFIRM:
      if (btn1Pressed) {
        confirmSaveChoice = !confirmSaveChoice;
        updateScreen = true;
      }
      if (btn2Pressed) {
        if (confirmSaveChoice) {
          if (setupSelectedPill == 1) {
            dose1.hour = tempHour;
            dose1.minute = tempMinute;
            dose1.lastTriggeredDay = -1; // Reset to allow trigger today if time in future
          } else {
            dose2.hour = tempHour;
            dose2.minute = tempMinute;
            dose2.lastTriggeredDay = -1;
          }
        }
        feedbackTimer = now;
        currentState = SETUP_FEEDBACK;
        updateScreen = true;
      }
      break;

    case SETUP_FEEDBACK:
      if (now - feedbackTimer >= 1500) {
        currentState = NORMAL_IDLE;
        updateScreen = true;
      }
      break;
  }

  // 5. Redraw display only when state changes or screen flag is set
  if (updateScreen) {
    renderScreen();
    updateScreen = false;
  }

  delay(10);
}
