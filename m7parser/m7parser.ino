/*
   Based on Neil Kolban example for IDF: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleScan.cpp
   Ported to Arduino ESP32 by Evandro Copercini
   Changed to connect to a Keiser M7 structure by Paulo Serra
*/
#include <esp_now.h>
#include <WiFi.h>
#include <Arduino.h>

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEEddystoneURL.h>
#include <BLEEddystoneTLM.h>
#include <BLEBeacon.h>

#define ENDIAN_CHANGE_U16(x) ((((x)&0xFF00) >> 8) + (((x)&0xFF) << 8))

typedef struct struct_message {
uint16_t power;
uint16_t cadence;
} struct_message;

//paste HERE the address you got from running the wifi mac address finder on the RECEIVER board
uint8_t broadcastAddress[] = {0x50, 0x02, 0x91, 0x88, 0x4F, 0x40};

// Callback when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nLast Packet Send Status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

BLEScan *pBLEScan;

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
    void onResult(BLEAdvertisedDevice advertisedDevice)
    {
        if (advertisedDevice.haveManufacturerData() == true)
        {
          std::string strManufacturerData = advertisedDevice.getManufacturerData();

          // Create a struct_message to hold power readings
          struct_message powerReadings;

          uint8_t cManufacturerData[100];
          strManufacturerData.copy((char *)cManufacturerData, strManufacturerData.length(), 0);

          if ( advertisedDevice.getName() == "M7" && cManufacturerData[0] == 0x02 && cManufacturerData[1] == 0x01)
          {
            Serial.println("Found a Keiser M7!");
            powerReadings.power = cManufacturerData[10]+(256*cManufacturerData[11]);
            powerReadings.cadence = cManufacturerData[6]+(256*cManufacturerData[7]);

            Serial.printf("Power is [%d] W\n", powerReadings.power );
            Serial.printf("Cadence is [%d] rpm\n", powerReadings.cadence);

            // Send message via ESP-NOW
            esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &powerReadings, sizeof(powerReadings));
           if (result == ESP_OK) {
             Serial.println("Sent with success");
             }
             else {
             Serial.println("Error sending the data");
              }
            
          }
         }
        return;     
   }
};

void setup()
{
  Serial.begin(115200);
  //Set device in STA mode to begin with
  WiFi.mode(WIFI_STA);
  //init ESP-NOW
  if (esp_now_init() != ESP_OK) {
  Serial.println("Error initializing ESP-NOW");
  return;
  }

  // Register peer
  esp_now_peer_info_t peerInfo;
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  // Add peer
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
   Serial.println("Failed to add peer");
   return;
   }

  //debug only
  esp_now_register_send_cb(OnDataSent);

  Serial.println("Scanning...");
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan(); //create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
  pBLEScan->setInterval(200);
  pBLEScan->setWindow(99); // less or equal setInterval value
}

void loop()
{
  // put your main code here, to run repeatedly:
  BLEScanResults foundDevices = pBLEScan->start(0, false);
  delay(1000);
}
