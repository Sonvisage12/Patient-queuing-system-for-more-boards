#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <map>
#include <queue>
#include <SPI.h>
#include <MFRC522.h>
#include <Preferences.h>

#define GREEN_LED_PIN 15
#define RED_LED_PIN   2
#define RST_PIN       5
#define SS_PIN        4

MFRC522 mfrc522(SS_PIN, RST_PIN);
Preferences preferences;
  int patientNum;
struct QueueItem {
  char uid[20];
  char timestamp[25];
  int number;
  bool removeFromQueue;
};

struct SyncRequest {
  char type[10]; // should be "SYNC_REQ"
};

std::map<String, QueueItem> queueMap;
std::queue<String> patientOrder;

uint8_t patientMAC[] = {0x08, 0xD1, 0xF9, 0xD7, 0x50, 0x98};
uint8_t patientMAC1[] = {0x30, 0xC6, 0xF7, 0x44, 0x1D, 0x24};
uint8_t displayMAC[] = {0x68, 0xC6, 0x3A, 0xFC, 0x61, 0x3E};
uint8_t StaffMAC2[] = {0x78, 0x42, 0x1C, 0x6C, 0xA8, 0x3C};

void persistQueue() {
  preferences.begin("queue", false);
  preferences.clear();
  int i = 0;
  for (auto& entry : queueMap) {
    String key = "q_" + String(i);
    preferences.putBytes(key.c_str(), &entry.second, sizeof(QueueItem));
    i++;
  }
  preferences.putUInt("queueSize", i);
  preferences.end();
}

void loadQueueFromFlash() {
  preferences.begin("queue", true);
  int size = preferences.getUInt("queueSize", 0);

  for (int i = 0; i < size; i++) {
    String key = "q_" + String(i);
    QueueItem item;
    if (preferences.getBytes(key.c_str(), &item, sizeof(QueueItem))) {
      String uid = String(item.uid);
      queueMap[uid] = item;
      patientOrder.push(uid);
    }
  }

  preferences.end();
  Serial.println("🔁 Restored queue from flash.");
}

void sendQueueToArrival() {
  for (auto& entry : queueMap) {
    QueueItem item = entry.second;
    esp_now_send(patientMAC, (uint8_t*)&item, sizeof(item));
    delay(10);
  }
  Serial.println("📤 Sent full queue to Arrival Node.");
}

void sendQueueToArrival2() {
  for (auto& entry : queueMap) {
    QueueItem item = entry.second;
    esp_now_send(patientMAC1, (uint8_t*)&item, sizeof(item));
    delay(10);
  }
  Serial.println("📤 Sent full queue to Arrival Node.");
}
void sendQueueToArrival1() {
  for (auto& entry : queueMap) {
    QueueItem item = entry.second;
    esp_now_send(StaffMAC2, (uint8_t*)&item, sizeof(item));
    delay(10);
  }
  Serial.println("📤 Sent full queue to Arrival Node.");
}

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  if (len == sizeof(SyncRequest)) {
    SyncRequest req;
    memcpy(&req, incomingData, sizeof(req));
    if (strcmp(req.type, "SYNC_REQ") == 0) {
      Serial.println("🔄 Received sync request from Arrival Node.");
      sendQueueToArrival(); sendQueueToArrival2();
      sendQueueToArrival1();
      return;
    }
  }

  if (len == sizeof(QueueItem)) {
    QueueItem item;
    memcpy(&item, incomingData, sizeof(item));
    String uid = String(item.uid);

    if (item.removeFromQueue) {
      // Handle removal broadcast from peer
      if (queueMap.count(uid)) {
        queueMap.erase(uid);

        // Remove UID from patientOrder queue
        std::queue<String> tempQueue;
        while (!patientOrder.empty()) {
          if (patientOrder.front() != uid)
            tempQueue.push(patientOrder.front());
          patientOrder.pop();
        }
        patientOrder = tempQueue;

        Serial.print("🗑️ Removed patient via peer sync: ");
        Serial.println(uid);
        persistQueue();
        displayNextPatient();
      }
    } else {
      // Handle new patient addition
      if (!queueMap.count(uid)) {
        queueMap[uid] = item;
        patientOrder.push(uid);
        Serial.print("\n📥 New Patient Added: ");
        Serial.print(item.number);
        Serial.print(" | UID: ");
        Serial.print(uid);
        Serial.print(" | Time: ");
        Serial.println(item.timestamp);
        displayNextPatient();
        persistQueue();
      }
    }
  }
}


void setup() {
  Serial.begin(115200);
  SPI.begin();

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  mfrc522.PCD_Init();

  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  digitalWrite(GREEN_LED_PIN, HIGH);
  digitalWrite(RED_LED_PIN, HIGH);

  WiFi.mode(WIFI_STA);
  Serial.print("WiFi MAC Address: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ Error initializing ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);

  // Register patient node
  esp_now_peer_info_t patientInfo = {};
  memcpy(patientInfo.peer_addr, patientMAC, 6);
  patientInfo.channel = 1;
  patientInfo.encrypt = false;

  if (!esp_now_is_peer_exist(patientMAC)) {
    esp_now_add_peer(&patientInfo);
  }
 esp_now_peer_info_t patientInfo1 = {};
  memcpy(patientInfo1.peer_addr, patientMAC1, 6);
  patientInfo1.channel = 1;
  patientInfo1.encrypt = false;

  if (!esp_now_is_peer_exist(patientMAC1)) {
    esp_now_add_peer(&patientInfo1);
  }
  // Register display node
  esp_now_peer_info_t displayInfo = {};
  memcpy(displayInfo.peer_addr, displayMAC, 6);
  displayInfo.channel = 1;
  displayInfo.encrypt = false;

  if (!esp_now_is_peer_exist(displayMAC)) {
    esp_now_add_peer(&displayInfo);
  }

  // Staff Node1
  esp_now_peer_info_t staffInfo = {};
  memcpy(staffInfo.peer_addr, StaffMAC2, 6);
  staffInfo.channel = 1;
 staffInfo.encrypt = false;

  if (!esp_now_is_peer_exist(StaffMAC2)) {
    esp_now_add_peer(&staffInfo);
  }

  loadQueueFromFlash();
  Serial.println("👨‍⚕️ Doctor Node Initialized");
  displayNextPatient();
}

void loop() {
  if (queueMap.empty() || patientOrder.empty()) return;

  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) return;

  String uid = getUIDString(mfrc522.uid.uidByte, mfrc522.uid.size);
  uid.toUpperCase();

  String currentUID = patientOrder.front();

  if (uid == currentUID) {
    QueueItem item = queueMap[currentUID];
    Serial.print("✅ Patient No ");
    Serial.print(item.number);
    Serial.println(" attended. Removing from queue.");

    item.removeFromQueue = true;

    esp_now_send(patientMAC, (uint8_t*)&item, sizeof(item));
    esp_now_send(patientMAC1, (uint8_t*)&item, sizeof(item));
    esp_now_send(StaffMAC2, (uint8_t*)&item, sizeof(item));
    patientNum = item.number;
    esp_now_send(displayMAC, (uint8_t*)&patientNum, sizeof(patientNum));
    //esp_now_send(displayMAC, (uint8_t*)&item, sizeof(item));

    queueMap.erase(currentUID);
    patientOrder.pop();
    persistQueue();

    blinkLED(GREEN_LED_PIN);
    displayNextPatient();
  } else {
    Serial.println("❌ Not the current patient in queue. Access Denied.");
    blinkLED(RED_LED_PIN);
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  delay(1500);
}

void displayNextPatient() {
  while (!patientOrder.empty()) {
    String uid = patientOrder.front();
    if (queueMap.count(uid)) {
      QueueItem item = queueMap[uid];

      esp_now_send(patientMAC, (uint8_t*)&item, sizeof(item));
      esp_now_send(patientMAC1, (uint8_t*)&item, sizeof(item));
      esp_now_send(StaffMAC2, (uint8_t*)&item, sizeof(item));
      patientNum = item.number;
      esp_now_send(displayMAC, (uint8_t*)&patientNum, sizeof(patientNum));

      Serial.print("🔔 Next Patient Number: ");
      Serial.println(patientNum);
      return;
    } else {
      
      patientOrder.pop();
    }
  }

  Serial.println("📭 Queue is now empty.");
      patientNum = 0;
  esp_now_send(displayMAC, (uint8_t*)&patientNum, sizeof(patientNum));
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

void blinkLED(int pin) {
  digitalWrite(pin, LOW);
  delay(1000);
  digitalWrite(pin, HIGH);
}
