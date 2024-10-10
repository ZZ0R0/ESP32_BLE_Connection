#include <SPI.h>
#include <RadioLib.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

// ----------------------
// BLE UUID Definitions
// ----------------------

#define SERVICE_UUID "12345678-1234-5678-1234-56789abcdef0"
#define CHARACTERISTIC_CONFIG_UUID "12345678-1234-5678-1234-56789abcdef1"
#define CHARACTERISTIC_LOG_UUID "12345678-1234-5678-1234-56789abcdef2"
#define CHARACTERISTIC_MESSAGE_UUID "12345678-1234-5678-1234-56789abcdef3"
#define CHARACTERISTIC_HANDSHAKE_RX_UUID "12345678-1234-5678-1234-56789abcdef4"
#define CHARACTERISTIC_HANDSHAKE_TX_UUID "12345678-1234-5678-1234-56789abcdef5"

// ----------------------
// CC1101 Pin Definitions
// ----------------------

#define CC1101_CS 21   // Chip Select pin
#define CC1101_GDO0 17 // GDO0 pin
#define CC1101_SCK 5   // SPI Clock
#define CC1101_MOSI 18 // SPI MOSI
#define CC1101_MISO 19 // SPI MISO

// ----------------------
// Global Objects
// ----------------------

// Create SPI settings for CC1101
SPIClass spi = SPI; // Use the default SPI instance for the ESP32-S3
SPISettings spiSettings = SPISettings(4000000, MSBFIRST, SPI_MODE0);

// Initialize Module and CC1101 radio
Module module(CC1101_CS, CC1101_GDO0, CC1101_SCK, CC1101_MOSI, spi, spiSettings);
CC1101 radio = CC1101(&module);

// BLE Objects
BLEServer *pServer = nullptr;
BLEService *pService = nullptr;
BLECharacteristic *pConfigCharacteristic = nullptr;
BLECharacteristic *pLogCharacteristic = nullptr;
BLECharacteristic *pMessageCharacteristic = nullptr;
BLECharacteristic *pHandshakeTxCharacteristic = nullptr; // For handshake response

// Connection Flag
bool deviceConnected = false;

// ----------------------
// BLE Callback Classes
// ----------------------

// Callback Class for BLE Server Events
class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer) override
  {
    deviceConnected = true;
    Serial.println("BLE Device Connected.");
  }

  void onDisconnect(BLEServer *pServer) override
  {
    deviceConnected = false;
    Serial.println("BLE Device Disconnected.");
    // Restart advertising to allow new connections
    pServer->startAdvertising();
    Serial.println("BLE Advertising Restarted.");
  }
};

// Callback Class for Configuration Characteristic Writes
class ConfigCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pCharacteristic) override
  {
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0)
    {
      Serial.println("Received Configuration Command:");
      Serial.println(value.c_str());

      // Expected format: "FREQ:433.0;MOD:OOK;PWR:10;ROLE:Transmitter;"
      String cmd = String(value.c_str());

      // Parse Frequency
      int freqIndex = cmd.indexOf("FREQ:");
      if (freqIndex != -1)
      {
        int freqEnd = cmd.indexOf(';', freqIndex);
        String freqStr = cmd.substring(freqIndex + 5, freqEnd);
        float frequency = freqStr.toFloat();
        int state = radio.setFrequency(frequency);
        if (state == RADIOLIB_ERR_NONE)
        {
          Serial.printf("Frequency set to %.1f MHz\n", frequency);
        }
        else
        {
          Serial.printf("Failed to set frequency, code %d\n", state);
        }
      }

      // Parse Modulation
      int modIndex = cmd.indexOf("MOD:");
      if (modIndex != -1)
      {
        int modEnd = cmd.indexOf(';', modIndex);
        String modStr = cmd.substring(modIndex + 4, modEnd);
        if (modStr.equalsIgnoreCase("OOK"))
        {
          radio.setOOK(true);
          Serial.println("Modulation set to OOK.");
        }
        else if (modStr.equalsIgnoreCase("FSK"))
        {
          radio.setOOK(false);
          Serial.println("Modulation set to FSK.");
        }
        else
        {
          Serial.println("Invalid Modulation Type.");
        }
      }

      // Parse Power (Optional)
      int pwrIndex = cmd.indexOf("PWR:");
      if (pwrIndex != -1)
      {
        int pwrEnd = cmd.indexOf(';', pwrIndex);
        String pwrStr = cmd.substring(pwrIndex + 4, pwrEnd);
        int power = pwrStr.toInt();
        // Implement power setting if supported by RadioLib for CC1101
        // RadioLib may not support power setting for CC1101 directly
        Serial.printf("Power setting received: %d (Not implemented)\n", power);
      }

      // Parse Role (Transmitter or Receiver)
      int roleIndex = cmd.indexOf("ROLE:");
      if (roleIndex != -1)
      {
        int roleEnd = cmd.indexOf(';', roleIndex);
        String roleStr = cmd.substring(roleIndex + 5, roleEnd);
        if (roleStr.equalsIgnoreCase("Transmitter"))
        {
          // Set up as transmitter
          Serial.println("Role set to Transmitter.");
        }
        else if (roleStr.equalsIgnoreCase("Receiver"))
        {
          // Set up as receiver
          Serial.println("Role set to Receiver.");
        }
        else
        {
          Serial.println("Invalid Role Type.");
        }
      }
    }
  }
};

// Callback Class for Handshake Characteristic Writes (Receiving Handshake from C++)
class HandshakeCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pCharacteristic) override
  {
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0)
    {
      // Convert std::string to Arduino String for Serial.println()
      Serial.println("Received Handshake Message: " + String(value.c_str()));

      // Send back a handshake response
      std::string response = "Handshake_OK";
      pHandshakeTxCharacteristic->setValue(response);
      pHandshakeTxCharacteristic->notify();

      // Convert std::string to Arduino String for Serial.println()
      Serial.println("Sent Handshake Response: " + String(response.c_str()));
    }
  }
};

// Callback Class for Message Characteristic Writes (Receiving Messages from Python)
class MessageCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pCharacteristic) override
  {
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0)
    {
      Serial.println("Received Message:");
      Serial.println(value.c_str());
      // Handle the received message as needed
      // For example, process or store the message
    }
  }
};

// ----------------------
// Setup Function
// ----------------------

void setup()
{
  // Initialize Serial for debugging
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting BLE and CC1101 setup...");

  // Initialize BLE with proper device name
  uint64_t mac = ESP.getEfuseMac();
  char macStr[17];
  snprintf(macStr, sizeof(macStr), "%04llX%04llX", mac >> 32, mac & 0xFFFFFFFF);
  String bleName = String("ESP32-S3-") + String(macStr);
  BLEDevice::init(bleName.c_str());
  Serial.printf("BLE Device Name: %s\n", bleName.c_str());

  // Create BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Set security parameters (no security)
  BLESecurity *pSecurity = new BLESecurity();
  pSecurity->setAuthenticationMode(ESP_LE_AUTH_NO_BOND);
  pServer->getAdvertising()->setScanResponse(false);

  // Create BLE Service
  pService = pServer->createService(SERVICE_UUID);

  // Create Configuration Characteristic (Write)
  pConfigCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_CONFIG_UUID,
      BLECharacteristic::PROPERTY_WRITE);
  pConfigCharacteristic->setCallbacks(new ConfigCallbacks());

  // Create Log Characteristic (Notify)
  pLogCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_LOG_UUID,
      BLECharacteristic::PROPERTY_NOTIFY);
  pLogCharacteristic->addDescriptor(new BLE2902());

  // Create Message Characteristic (Write)
  pMessageCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_MESSAGE_UUID,
      BLECharacteristic::PROPERTY_WRITE);
  pMessageCharacteristic->setCallbacks(new MessageCallbacks());

  // Create Handshake RX Characteristic (Write)
  BLECharacteristic *pHandshakeRxCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_HANDSHAKE_RX_UUID,
      BLECharacteristic::PROPERTY_WRITE);
  pHandshakeRxCharacteristic->setCallbacks(new HandshakeCallbacks());

  // Create Handshake TX Characteristic (Notify)
  pHandshakeTxCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_HANDSHAKE_TX_UUID,
      BLECharacteristic::PROPERTY_NOTIFY);
  pHandshakeTxCharacteristic->addDescriptor(new BLE2902());

  // Start the BLE Service
  pService->start();

  // Start BLE Advertising
  pServer->getAdvertising()->start();
  Serial.println("BLE Initialized and Advertising.");

  // Initialize SPI
  spi.begin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
  Serial.println("SPI Initialized.");

  // Initialize CC1101 Radio
  int state = radio.begin(433.0); // Set frequency to 433 MHz
  if (state == RADIOLIB_ERR_NONE)
  {
    Serial.println("CC1101 initialized successfully at 433 MHz.");
  }
  else
  {
    Serial.printf("CC1101 initialization failed, code %d\n", state);
  }

  // Set default modulation to OOK
  state = radio.setOOK(true);
  if (state == RADIOLIB_ERR_NONE)
  {
    Serial.println("OOK modulation enabled.");
  }
  else
  {
    Serial.printf("Failed to set OOK modulation, code %d\n", state);
  }
}

// ----------------------
// Loop Function
// ----------------------

void loop()
{
  if (deviceConnected)
  {
    // Prepare log data
    String logData = "Log data from ESP32: " + String(millis());

    // Send log data via BLE Notify
    pLogCharacteristic->setValue(logData.c_str());
    pLogCharacteristic->notify();

    Serial.println("Sent Log Data: " + logData);

    // Check for incoming BLE message (from the computer)
    std::string receivedMessage = pMessageCharacteristic->getValue();
    if (!receivedMessage.empty())
    {
      Serial.println("Received Message from Computer:");
      Serial.println(receivedMessage.c_str());

      // Send back an acknowledgment or another message
      String ackMessage = "ESP32 ACK: " + String(millis());
      pMessageCharacteristic->setValue(ackMessage.c_str());
      pMessageCharacteristic->notify();

      Serial.println("Sent ACK Message: " + ackMessage);
    }

    delay(1000); // Send logs every second
  }

  // Optional: Handle radio communication here if needed
}