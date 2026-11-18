#include <Arduino.h>
#include <M5Cardputer.h>
#include <Network.h>
#include <WiFi.h>
#include "demo_models.h"
#include "network_test.h"
#include "launcher_return.h"
#include "speech_service.h"

enum class Screen { Menu, Calculator, Timer };
static Screen screen = Screen::Menu;
static String expression;
static DemoTimer timer;
// Adapted from foopod's MicroHydra timer: numeric duration and s/m/h units.
// The original source is in ../MicroHydra-Apps/app-source/timer/timer.py.
static String timerValue = "0";
static uint32_t timerUnitSeconds = 60;
static bool timerAlarmActive = false;
static uint32_t nextAlarm = 0;
static const char *timerUnitName() {
  return timerUnitSeconds == 1 ? "seconds" : timerUnitSeconds == 60 ? "minutes" : "hours";
}
static wl_status_t previousWifi = WL_IDLE_STATUS;
static bool watchWifi = false;

static void render(const String &line) {
  M5Cardputer.Display.fillScreen(BLACK);
  M5Cardputer.Display.setCursor(0, 0);
  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.println("Speech test");
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.println(line);
  if (screen == Screen::Menu) {
    M5Cardputer.Display.println("1: calculator  2: timer");
    M5Cardputer.Display.println("w: WiFi  s: scan  o: off  l: launcher");
  } else if (screen == Screen::Calculator) {
    M5Cardputer.Display.println("Type two numbers and an operator");
    M5Cardputer.Display.println("Enter: result  Esc: menu");
  } else {
    M5Cardputer.Display.println("s/m/h: units  Enter: start");
    M5Cardputer.Display.println("r: remaining  Esc: menu");
  }
}

static void showAndSpeak(const String &line) {
  render(line);
  speechSay(line.c_str());
}

static void calculate() {
  char value[48];
  const char *error = evaluateExpression(expression.c_str(), value, sizeof(value));
  if (error) { showAndSpeak(error); return; }
  expression = value;
  showAndSpeak(String("Result ") + value);
}

void setup() {
  Serial.begin(115200);
  auto cfg = M5.config();
  cfg.fallback_board = m5::board_t::board_M5CardputerADV;
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Speaker.setVolume(96);
  if (!speechServiceBegin()) {
    Serial.println("ERROR: speech worker creation failed");
    return;
  }
#if defined(DEMO_START_CALCULATOR)
  screen = Screen::Calculator;
  showAndSpeak("Calculator. Enter an expression. P repeats speech. X stops speech. Escape for menu.");
#elif defined(DEMO_START_TIMER)
  screen = Screen::Timer;
  showAndSpeak("Timer. Enter a duration. S for seconds, M for minutes, H for hours. P repeats, X stops speech. Escape for menu.");
#else
  render("Choose a demo.");
  speechSay("Speech test. 1 calculator, 2 timer, L launcher. P repeats speech, X stops speech.");
#endif
}

void loop() {
  M5Cardputer.update();
  if (networkTestPoll()) {
    watchWifi = true;
    previousWifi = WL_IDLE_STATUS;
    speechSay("Connecting to Wi-Fi");
  }
  static uint32_t lastLoadAnnouncement = 0;
  if (networkTestRunning() && millis() - lastLoadAnnouncement >= 5000) {
    lastLoadAnnouncement = millis();
    speechSay("Network load test in progress");
  }
  if (watchWifi && WiFi.status() != previousWifi) {
    previousWifi = WiFi.status();
    Serial.printf("WIFI status=%d heap=%u min_heap=%u\n", (int)previousWifi, ESP.getFreeHeap(), ESP.getMinFreeHeap());
    if (previousWifi == WL_CONNECTED) speechSay("Wi-Fi connected");
    else if (previousWifi == WL_CONNECT_FAILED || previousWifi == WL_NO_SSID_AVAIL) speechSay("Wi-Fi connection failed");
  }
  if (timer.elapsed(millis())) {
    timerAlarmActive = true;
    nextAlarm = millis() + 10000;
    showAndSpeak("Timer finished");
  }
  if (timerAlarmActive && int32_t(millis() - nextAlarm) >= 0) {
    nextAlarm = millis() + 10000;
    speechSay("Timer finished. Press any key to dismiss.");
  }
  if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
    delay(5);
    return;
  }
  auto keys = M5Cardputer.Keyboard.keysState();
  if (timerAlarmActive) {
    timerAlarmActive = false;
    showAndSpeak("Timer dismissed");
    return;
  }
  if (keys.word.size() == 1 && keys.word[0] == 'p') { speechRepeat(); return; }
  if (keys.word.size() == 1 && keys.word[0] == 'x') { speechStop(); return; }
  if (keys.esc || (screen != Screen::Menu && keys.word.size() && keys.word[0] == '`')) {
    timer.stop();
    screen = Screen::Menu;
    showAndSpeak("Menu. 1 calculator, 2 timer, L launcher. P repeats speech, X stops speech.");
    return;
  }
  if (screen == Screen::Calculator) {
    if (keys.enter) { calculate(); return; }
    if (keys.backspace && expression.length()) {
      expression.remove(expression.length() - 1);
      showAndSpeak(expression.length() ? expression : "Cleared");
      return;
    }
    for (char c : keys.word) {
      if (strchr("0123456789.+-*/", c) && expression.length() < 32) {
        expression += c;
        char label[2] = {c, '\0'};
        render(expression);
        speechSay(label);
      }
    }
    return;
  }
  if (screen == Screen::Timer) {
    if (keys.enter) {
      if (!timer.start(millis(), timerValue.c_str(), timerUnitSeconds)) {
        showAndSpeak("Enter a duration between one millisecond and 24 hours");
        return;
      }
      showAndSpeak(String("Timer started. ") + timerValue + " " + timerUnitName());
      return;
    }
    if (keys.backspace && !timer.running) {
      if (timerValue.length() > 1) timerValue.remove(timerValue.length() - 1);
      else timerValue = "0";
      showAndSpeak(timerValue + " " + timerUnitName());
      return;
    }
    for (char c : keys.word) {
      if (c == 'r') {
        if (!timer.running) showAndSpeak(timerValue + " " + timerUnitName());
        else showAndSpeak(String(timer.remaining(millis())) + " seconds remaining");
      } else if (!timer.running && (c == 's' || c == 'm' || c == 'h')) {
        timerUnitSeconds = c == 's' ? 1 : c == 'm' ? 60 : 3600;
        showAndSpeak(timerValue + " " + timerUnitName());
      } else if (!timer.running && ((c >= '0' && c <= '9') || c == '.') && timerValue.length() < 8) {
        if (c == '.' && timerValue.indexOf('.') >= 0) continue;
        if (timerValue == "0" && c != '.') timerValue = "";
        timerValue += c;
        char label[2] = {c, '\0'};
        render(timerValue + " " + timerUnitName());
        speechSay(label);
      }
    }
    return;
  }
  for (char c : keys.word) {
    if (c == '1') {
      screen = Screen::Calculator;
      expression = "";
      showAndSpeak("Calculator. Enter an expression. P repeats speech. X stops speech. Escape for menu.");
    } else if (c == '2') {
      screen = Screen::Timer;
      timerValue = "0";
      timerUnitSeconds = 60;
      showAndSpeak("Timer. Enter a duration. S for seconds, M for minutes, H for hours. P repeats, X stops speech. Escape for menu.");
    } else if (c == 'w') {
      WiFi.mode(WIFI_STA);
      previousWifi = WL_IDLE_STATUS;
      watchWifi = true;
      WiFi.begin(); // Reuse credentials already saved in ESP32 NVS.
      showAndSpeak("Connecting to saved Wi-Fi");
    } else if (c == 's') {
      WiFi.mode(WIFI_STA);
      WiFi.scanNetworks(true);
      render("WiFi scan started");
      speechSay("Wi-Fi scan started.");
    } else if (c == 'o') {
      watchWifi = false;
      WiFi.mode(WIFI_OFF);
      render("WiFi off");
      speechSay("Wi-Fi off.");
    } else if (c == 'l') {
      LauncherReturn result = launcherReturnCheck();
      if (result == LauncherReturn::NoLauncher) showAndSpeak("Launcher partition not found");
      else if (result == LauncherReturn::SettingsUnavailable) showAndSpeak("Launcher settings unavailable");
      else if (result == LauncherReturn::Disabled) showAndSpeak("Launcher return disabled by boot settings");
      else {
        render("Returning to launcher");
        speechSayAndWait("Returning to launcher", 2000);
        if (!launcherReturnWake()) showAndSpeak("Could not return to launcher");
      }
    }
  }
}
