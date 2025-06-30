#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino.h>

// 硬件引脚定义
#define LED_BUILTIN_0 12
#define LED_BUILTIN_1 13
#define IO_D0 3 // Connected to comparer. Low for leaking

// WiFi配置
const char* ssid = "<Your WiFi SSID>";
const char* password = "<Your WiFi Password>";
const char* webhookUrl = "WeChat Enterprise WebHook";

// 状态枚举
enum LeakageState {
  STATE_NORMAL,
  STATE_LEAKAGE_DETECTED,
  STATE_RECOVERED
};

// 全局变量
LeakageState leakageState = STATE_NORMAL;
LeakageState lastReportedState = STATE_NORMAL;
unsigned long leakStartTime = 0;
unsigned long lastAlertTime = 0;
unsigned long lastStateChangeTime = 0;
const unsigned long ALERT_INTERVAL = 30000; // 提醒间隔(ms)
const unsigned long STATE_DEBOUNCE_TIME = 100; // 状态防抖时间(ms)
bool isFirstAlert = true; // 首次报警标志
bool leakageDetected = false; // 当前泄漏状态

// LED控制函数封装
void setLEDs(uint8_t brightness) {
  analogWrite(LED_BUILTIN_0, brightness);
  analogWrite(LED_BUILTIN_1, brightness);
}

void breathingLED() {
  for (int i = 0; i < 128; i++) {
    setLEDs(i);
    delay(4);
  }
  for (int i = 128; i >= 0; i--) {
    setLEDs(i);
    delay(4);
  }
}

// 企业微信消息发送
void sendToWechatWebhook(String message) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, cannot send message");
    return;
  }

  HTTPClient https;
  if (https.begin(webhookUrl)) {
    String payload = "{\"msgtype\":\"text\",\"text\":{\"content\":\"" + message + "\"}}";
    int httpResponseCode = https.POST(payload);

    if (httpResponseCode == HTTP_CODE_OK) {
      Serial.println("Message sent successfully");
    } else {
      Serial.printf("Send failed, error: %s\n", https.errorToString(httpResponseCode).c_str());
    }
    https.end();
    } else {
      Serial.println("Failed to connect to webhook");
    }
    }

    // 格式化时间函数
    String formatDuration(unsigned long milliseconds) {
      unsigned long seconds = milliseconds / 1000;
      unsigned long minutes = seconds / 60;
      unsigned long hours = minutes / 60;

      minutes %= 60;
      seconds %= 60;

      char buffer[20];
      if (hours > 0) {
        snprintf(buffer, sizeof(buffer), "%lu小时%lu分%lu秒", hours, minutes, seconds);
      } else if (minutes > 0) {
        snprintf(buffer, sizeof(buffer), "%lu分%lu秒", minutes, seconds);
      } else {
        snprintf(buffer, sizeof(buffer), "%lu秒", seconds);
      }
      return String(buffer);
    }

    // 发送泄漏警报
    void sendLeakageAlert(bool isFirst) {
      unsigned long leakDuration = millis() - leakStartTime;
      String message;

      if (isFirst) {
        // 首次报警特殊消息
        message = "🚨 检测到水泄漏！\n";
        message += "发现泄露！请立即处理！";
      } else {
        // 后续报警显示持续时间
        String durationStr = formatDuration(leakDuration);
        message = "🚨 水泄漏持续中！\n";
        message += "泄漏时间: " + durationStr + "\n";
        message += "请尽快处理！";
      }

      Serial.println(message);
      sendToWechatWebhook(message);
    }

    // 发送恢复通知
    void sendRecoveryNotification() {
      if (leakStartTime > 0) {
        unsigned long leakDuration = millis() - leakStartTime;
        String durationStr = formatDuration(leakDuration);

        String message = "✅ 水泄漏已恢复！\n";
        message += "总泄漏时间: " + durationStr;

        Serial.println(message);
        sendToWechatWebhook(message);
      }
    }

    // 检测泄漏状态（带状态锁定）
    bool checkLeakageState() {
      static bool lastState = digitalRead(IO_D0);
      static unsigned long lastChangeTime = 0;

      bool currentState = digitalRead(IO_D0);
      unsigned long currentTime = millis();

      // 状态变化检测
      if (currentState != lastState) {
        lastChangeTime = currentTime;
        lastState = currentState;
      }

      // 状态稳定时间检查
      if (currentTime - lastChangeTime > STATE_DEBOUNCE_TIME) {
        // 泄漏状态：当IO_D0为LOW时表示泄漏
        return (currentState == LOW);
      }

      // 状态未稳定，返回上一次的有效状态
      return leakageDetected;
    }

    // WiFi连接管理
    void connectWiFi() {
      if (WiFi.status() == WL_CONNECTED) return;

      Serial.print("Connecting to WiFi...");
      WiFi.disconnect();
      WiFi.begin(ssid, password);

      unsigned long startTime = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
        breathingLED();
        Serial.print(".");
      }

      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nConnected! IP: " + WiFi.localIP().toString());
        setLEDs(10); // 连接成功低亮度
        sendToWechatWebhook("🔌 ESP32C3已连接至WiFi: " + String(ssid) + "\nIP: " + WiFi.localIP().toString());
      } else {
        Serial.println("\nConnection failed!");
        setLEDs(0); // 连接失败关闭LED
      }
    }

    void setup() {
      Serial.begin(115200);

      // 初始化引脚
      pinMode(LED_BUILTIN_0, OUTPUT);
      pinMode(LED_BUILTIN_1, OUTPUT);
      pinMode(IO_D0, INPUT_PULLUP); // 启用内部上拉电阻

      // 初始LED状态
      setLEDs(0);

      // 连接WiFi
      connectWiFi();

      // 初始化泄漏状态
      leakageDetected = checkLeakageState();
    }

    void loop() {
      // 网络连接管理
      if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
      }

      // 检测当前泄漏状态（带防抖）
      bool currentLeakage = checkLeakageState();

      // 状态变化处理
      if (currentLeakage != leakageDetected) {
        leakageDetected = currentLeakage;
        lastStateChangeTime = millis();

        if (leakageDetected) {
          // 检测到泄漏
          leakageState = STATE_LEAKAGE_DETECTED;
          leakStartTime = millis();
          lastAlertTime = millis();
          isFirstAlert = true;
          sendLeakageAlert(true);  // 发送首次报警
          setLEDs(255); // 高亮度报警
        } else {
          // 泄漏恢复
          leakageState = STATE_RECOVERED;
          sendRecoveryNotification();
          setLEDs(10); // 恢复正常亮度
          leakStartTime = 0; // 重置泄漏计时
          isFirstAlert = true; // 重置首次报警标志
        }
      }

      // 泄漏持续状态处理
      if (leakageDetected && leakageState == STATE_LEAKAGE_DETECTED) {
        unsigned long currentTime = millis();

        // 检查是否需要发送定时提醒
        if (currentTime - lastAlertTime >= ALERT_INTERVAL) {
          lastAlertTime = currentTime;
          sendLeakageAlert(false); // 发送定时提醒
          isFirstAlert = false;
        }
      }

      delay(50); // 主循环延迟适当缩短可提高响应速度
    }
