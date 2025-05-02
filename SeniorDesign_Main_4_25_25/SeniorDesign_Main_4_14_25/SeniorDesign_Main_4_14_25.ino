#include <Wire.h>
#include <SPI.h>
#include "DW1000Ranging.h"
#include <SD.h>
#include <Adafruit_PN532.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <WiFi.h>
#include "link.h"


// --- BLE UUIDs (Nordic UART Service) ---
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // Notify
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // Write

BLECharacteristic *pTxCharacteristic;
BLECharacteristic *pRxCharacteristic;

double range = 10;
String diagnosis = "";

// --- NFC over I2C ---
#define SDA_PIN 21
#define SCL_PIN 22
Adafruit_PN532 nfc(SDA_PIN, SCL_PIN);
#define DW_CS 4

// --- DW1000 over SPI ---
const uint8_t PIN_RST = 27;
const uint8_t PIN_IRQ = 34;
const uint8_t PIN_SS  = 4;

// --- SD card over SPI ---
#define SD_CS 5
File myFile;
String fileName = "/patientData.txt";

// -- Wifi for python -- //
const char *ssid = "Dilophosaurus";
const char *password = "spicy//raccoon";
const char *host = "192.168.1.10";
WiFiClient client;

// Positioning set up //
struct MyLink *uwb_data;
int index_num = 0;
long runtime = 0;
String all_json = "";

// --- BLE Connection Callback ---
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {

    Serial.println("Bluetooth connected");
     delay(500);  // <--- give it a moment
    pTxCharacteristic->setValue("test");
    pTxCharacteristic->notify();
  }

  void onDisconnect(BLEServer* pServer) {
    Serial.println("Bluetooth disconnected");
    delay(100);
    BLEDevice::startAdvertising();
  }
};

// --- BLE RX Write Callback ---
class RxCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pChar) override {
    String value = pChar->getValue().c_str();
    if (value.length() == 0) return;

    diagnosis = value;
    Serial.print("Received diagnosis: ");
    Serial.println(diagnosis);

    // --- Append diagnosis to the file ---
    myFile = SD.open(fileName, FILE_APPEND);
    if (myFile) {
      myFile.println("Diagnosis: " + diagnosis);
      myFile.close();
      Serial.println("Diagnosis appended to SD file.");
    } else {
      Serial.println("Error opening file for append.");
      return;
    }

    // --- Reopen file and print all contents ---
    myFile = SD.open(fileName);
    if (myFile) {
      Serial.println("Full file contents:");
      while (myFile.available()) {
        String line = myFile.readStringUntil('\n');
        Serial.println(line);
      }
      myFile.close();
    } else {
      Serial.println("Error reopening file for reading.");
    }
  }
};

// --- Send SD card contents over BLE ---
void sendFileOverBLE() {
  myFile = SD.open(fileName);
  if (myFile) {
    Serial.print("Sending contents of "); Serial.println(fileName);

    int lineNum = 0;
    String firstName, lastName, weight, height, diagnosisStr;

    while (myFile.available()) {
      String line = myFile.readStringUntil('\n');
      line.trim(); // clean it

      switch (lineNum) {
        case 0: firstName = line; break;
        case 1: lastName = line; break;
        case 2: weight = line; break;
        case 3: height = line; break;
        case 4: diagnosisStr = line; break;
      }

      // Force BLE monitor to show each on a new line
      String chunk = line + "\n";
      pTxCharacteristic->setValue(chunk.c_str());
      pTxCharacteristic->notify();
      Serial.print("Sent BLE chunk: "); Serial.print(chunk);
      delay(100);  // optional, helps some apps

      lineNum++;
    }

    myFile.close();

    //Serial.println("Parsed Record:");
    //Serial.println(firstName);
    //Serial.println(lastName);
    //Serial.println(weight);
    //Serial.println(height);
    //Serial.println(diagnosisStr);
    //Serial.println("Done sending over BLE.");
  } else {
    Serial.print("Error opening "); Serial.println(fileName);
  }
}


void setup() {
  Serial.begin(115200);
  
  delay(2000);

  // --- BLE Setup ---
  BLEDevice::init("Smart MedInfo Patch");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  pTxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_TX,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pTxCharacteristic->addDescriptor(new BLE2902());

  pRxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_RX,
    BLECharacteristic::PROPERTY_WRITE
  );
  pRxCharacteristic->setCallbacks(new RxCallback());

  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("BLE advertising started");

  // --- SD Setup ---
  Serial.println("Initializing SD card...");
  if (!SD.begin(SD_CS)) Serial.println("SD card init failed!");
  else Serial.println("SD card ready.");  
  myFile = SD.open(fileName, FILE_WRITE);
    if (myFile) {
      Serial.print("Writing to SD card: "); Serial.println(fileName);
      myFile.println("First Name: Jane");
      myFile.println("Last Name: Doe");
      myFile.println("Weight: 140");
      myFile.println("Height: 5'6");
      myFile.close();
      Serial.println("File safely closed.");
    } else {
      Serial.print("Error opening "); Serial.println(fileName);
    }


  delay(300);

  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  uint8_t test = SPI.transfer(0x00);
  SPI.endTransaction();
  Serial.print("SPI test byte: 0x"); Serial.println(test, HEX);
  delay(100);

  // --- DW1000 Setup ---
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, LOW); delay(10);
  digitalWrite(PIN_RST, HIGH); delay(10);


    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("Connected");
    Serial.print("IP Address:");
    Serial.println(WiFi.localIP());

    if (client.connect(host, 80))
    {
        Serial.println("Success");
        client.print(String("GET /") + " HTTP/1.1\r\n" +
                     "Host: " + host + "\r\n" +
                     "Connection: close\r\n" +
                     "\r\n");
    }

    delay(1000);



  DW1000Ranging.initCommunication(PIN_RST, PIN_SS, PIN_IRQ);
  //DW1000Ranging.initCommunication(PIN_RST, DW_CS, PIN_IRQ);
  
  delay(200);
  DW1000Ranging.attachNewRange(newRange);
  DW1000Ranging.attachNewDevice(newDevice);
  DW1000Ranging.attachInactiveDevice(inactiveDevice);
  DW1000Ranging.startAsTag("7D:00:22:EA:82:60:3B:9C", DW1000.MODE_LONGDATA_RANGE_LOWPOWER, false);
  //DW1000Ranging.startAsTag("7D:00:22:EA:82:60:3B:9C", DW1000.MODE_LONGDATA_RANGE_LOWPOWER);
    uwb_data = init_link();
  delay(300);

  // --- PN532 Setup ---
  Serial.println("Initializing I2C and PN532...");
  Wire.begin(SDA_PIN, SCL_PIN);
  delay(100);
  nfc.begin();
  delay(100);

  Wire.beginTransmission(0x24);
  byte err = Wire.endTransmission();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) Serial.println("Didn't find PN532 board");
  else {
    Serial.print("Found PN5"); Serial.println((versiondata >> 24) & 0xFF, HEX);
    nfc.SAMConfig();
    Serial.println("Waiting for an NFC card...");
  }
  delay(500);
}
void loop() {

  while (range > 0.5) {
    DW1000Ranging.loop();
    if ((millis() - runtime) > 1000)
    {
        make_link_json(uwb_data, &all_json);
        send_udp(&all_json);
        runtime = millis();
    }
    }

  Serial.print("Within range of receptionist (0.5M) \n");

delay(500);  // <-- NEW: let BLE finish connect stuff
  uint8_t uid[7] = { 0 };
  uint8_t uidLength = 0;

  Serial.print("Waiting for NFC: ");
  bool success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);

  if (success) {
    Serial.println("NFC tag detected.");
    sendFileOverBLE();
    delay(2000);
  } else {
    Serial.print("NO SUCCESS: ");
  }

  delay(500);
}

void newRange() {
   DW1000Device* device = DW1000Ranging.getDistantDevice();
  if (!device) return;

  float r = device->getRange();

  // Filter out garbage values
  if (isnan(r) || r < 0 || r > 50.0) return;
  
  Serial.print("from: "); Serial.print(DW1000Ranging.getDistantDevice()->getShortAddress(), HEX);
  Serial.print("\t Range: "); Serial.print(DW1000Ranging.getDistantDevice()->getRange()); Serial.print(" m");
  Serial.print("\t RX power: "); Serial.print(DW1000Ranging.getDistantDevice()->getRXPower()); Serial.println(" dBm");
  range = DW1000Ranging.getDistantDevice()->getRange();
  Serial.print("Range = "); Serial.println(range);
  fresh_link(uwb_data, DW1000Ranging.getDistantDevice()->getShortAddress(), DW1000Ranging.getDistantDevice()->getRange(), DW1000Ranging.getDistantDevice()->getRXPower());
}

void newDevice(DW1000Device* device) {
  Serial.print("ranging init; 1 device added ! -> short:");
  Serial.println(device->getShortAddress(), HEX);
  
    add_link(uwb_data, device->getShortAddress());
}

void inactiveDevice(DW1000Device* device) {
  Serial.print("delete inactive device: ");
  Serial.println(device->getShortAddress(), HEX);
  delete_link(uwb_data, device->getShortAddress());
}


void send_udp(String *msg_json)
{
    if (client.connected())
    {
        client.print(*msg_json);
        Serial.println("UDP send");
    }
}

