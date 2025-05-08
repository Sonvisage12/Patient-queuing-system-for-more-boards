#include <SPI.h>
#include <MFRC522.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <RTClib.h>
#include <esp_now.h>
#include <WiFi.h>
#include <vector>
#include <algorithm>

#define RST_PIN  5
#define SS_PIN   4
#define GREEN_LED_PIN 15
#define RED_LED_PIN   2

MFRC522 mfrc522(SS_PIN, RST_PIN);
Preferences prefs;
RTC_DS3231 rtc;

#include "SharedQueue.h"
SharedQueue sharedQueue("rfid-patients");

uint8_t arrivalMAC[] = {0x30, 0xC6, 0xF7, 0x44, 0x1D, 0x24};
uint8_t doctorMAC1[] = {0x78, 0x42, 0x1C, 0x6C, 0xA8, 0x3C};
uint8_t doctorMAC[]  = {0x78, 0x42, 0x1C, 0x6C, 0xE4, 0x9C};

void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  QueueItem item;
  memcpy(&item, incomingData, sizeof(item));

  if (item.removeFromQueue) {
    sharedQueue.removeByUID(String(item.uid));
  } else {
    sharedQueue.addIfNew(String(item.uid), String(item.timestamp), item.number);
  }

  Serial.print("📩 Received from: ");
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2],
           recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5]);
  Serial.println(macStr);

  sharedQueue.print();
}

void setup() {
  Serial.begin(115200);
  SPI.begin();
  WiFi.mode(WIFI_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  Serial.print("WiFi MAC Address: ");
  Serial.println(WiFi.macAddress());

  mfrc522.PCD_Init();
  Wire.begin();

  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC!");
    while (1);
  }
  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting time to compile time...");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  digitalWrite(GREEN_LED_PIN, HIGH);
  digitalWrite(RED_LED_PIN, HIGH);

  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ Error initializing ESP-NOW");
    return;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, doctorMAC, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  if (!esp_now_is_peer_exist(doctorMAC)) {
    esp_now_add_peer(&peerInfo);
  }

  esp_now_peer_info_t peerInfo2 = {};
  memcpy(peerInfo2.peer_addr, arrivalMAC, 6);
  peerInfo2.channel = 1;
  peerInfo2.encrypt = false;
  if (!esp_now_is_peer_exist(arrivalMAC)) {
    esp_now_add_peer(&peerInfo2);
  }

  esp_now_peer_info_t peerInfo1 = {};
  memcpy(peerInfo1.peer_addr, doctorMAC1, 6);
  peerInfo1.channel = 1;
  peerInfo1.encrypt = false;
  if (!esp_now_is_peer_exist(doctorMAC1)) {
    esp_now_add_peer(&peerInfo1);
  }

  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);

  sharedQueue.load();
  Serial.println("📍 RFID Arrival Node Initialized. Waiting for patient card...");
  sharedQueue.print();
}

void loop() {
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  String uid = getUIDString(mfrc522.uid.uidByte, mfrc522.uid.size);
  Serial.print("🆔 Card UID detected: ");
  Serial.println(uid);

  if (sharedQueue.exists(uid)) {
    Serial.println("⏳ Already in queue. Wait for your turn.");
    blinkLED(RED_LED_PIN);
  } else {
    int pid = sharedQueue.getOrAssignPermanentNumber(uid, rtc.now());
    String timeStr = formatDateTime(rtc.now());

    sharedQueue.add(uid, timeStr, pid);

    QueueItem item;
    strncpy(item.uid, uid.c_str(), sizeof(item.uid));
    strncpy(item.timestamp, timeStr.c_str(), sizeof(item.timestamp));
    item.number = pid;
    item.removeFromQueue = false;

    esp_now_send(doctorMAC, (uint8_t *)&item, sizeof(item));
    esp_now_send(arrivalMAC, (uint8_t *)&item, sizeof(item));
    esp_now_send(doctorMAC1, (uint8_t *)&item, sizeof(item));

    Serial.print("✅ Patient Registered. Assigned Number: ");
    Serial.print(pid);
    Serial.print(" | Time: ");
    Serial.println(timeStr);

    blinkLED(GREEN_LED_PIN);
    sharedQueue.print();
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  delay(1500);
}

String getUIDString(byte *buffer, byte bufferSize) {
  String uidString = "";
  for (byte i = 0; i < bufferSize; i++) {
    if (buffer[i] < 0x10) uidString += "0";
    uidString += String(buffer[i], HEX);
  }
  uidString.toUpperCase();
  return uidString;
}

String formatDateTime(const DateTime &dt) {
  char buffer[25];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
           dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute(), dt.second());
  return String(buffer);
}

void blinkLED(int pin) {
  digitalWrite(pin, LOW);
  delay(1000);
  digitalWrite(pin, HIGH);
}

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("📤 Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivered 🟢" : "Failed 🔴");
}