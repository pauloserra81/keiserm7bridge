#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

extern bool deviceConnected;
extern bool oldDeviceConnected;

byte flags = 0b00111110;
int pwr = 100;
byte power[9] = { 0b00100000, 0b00000000, 100, 0, 0, 0, 0, 0, 10};
byte feature[4] = { 0b00010000, 0b00000000, 0b00000000, 0b00000000};
byte pwrPos[1] = {5};

#define powerMeterService BLEUUID((uint16_t)0x1818)
BLECharacteristic powerMeterMeasurementCharacteristics(BLEUUID((uint16_t)0x2A63), BLECharacteristic::PROPERTY_NOTIFY);
BLECharacteristic cyclingPowerFeatureCharacteristics(BLEUUID((uint16_t)0x2A65), BLECharacteristic::PROPERTY_READ);
BLECharacteristic sensorLocationCharacteristic(BLEUUID((uint16_t)0x2A5D), BLECharacteristic::PROPERTY_READ);
BLEDescriptor powerMeterMeasuremenDescriptor(BLEUUID((uint16_t)0x2901));
BLEDescriptor cyclingPowerFeatureDescriptor(BLEUUID((uint16_t)0x2901));
BLEDescriptor sensorLocationDescriptor(BLEUUID((uint16_t)0x2901));


//Change this to scale power numbers i.e. set to 2 to double power
float POWERFACTOR =10;

class MyServerCallbacks : public BLEServerCallbacks
{
private:

    void onConnect(BLEServer *pServer) //todo add a reference to deviceconnected
    {
        deviceConnected=true;
        //digitalWrite(LED_PIN, HIGH); we can get LED to change color here
    };

    void onDisconnect(BLEServer *pServer)
    {
        deviceConnected=false;
        //digitalWrite(LED_PIN, LOW);
    }
};



uint32_t powerTxValue = 0; //the BLE-Compliant, flagged, ready to transmit power value
uint64_t CSCTxValue = 0;   //the BLE-Compliant, flagged, ready to transmit CSC value

typedef struct struct_message {
uint16_t power;
uint16_t cadence;
} struct_message;

//reading holder
struct_message powerReadings;

void setup()
{
  Serial.begin(115200);
  InitBLE();
  
  //initialize ESP-NOW to get the power data from other board
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
  Serial.println("Error initializing ESP-NOW");
  return;
  }
  
  esp_now_register_recv_cb(OnDataRecv);

  //initialize values
  powerReadings.power =0;
  powerReadings.cadence =0;
}

       
//reading holders
uint64_t cumulativeRevolutions = 0; //a represents the total number of times a crank rotates
uint64_t lastCET = 0;        
/*lastCrankEvent: The 'crank event time' is a free-running-count of 1/1024 second units and it 
represents the time when the crank revolution was detected by the crank rotation sensor. Since
 several crank events can occur between transmissions, only the Last Crank Event Time value is
  transmitted. This value is used in combination with the Cumulative Crank Revolutions value to
   enable the Client to calculate cadence. The Last Crank Event Time value rolls over every 64 seconds.
*/

/*Cadence = (Difference in two successive Cumulative Crank Revolution values) /
(Difference in two successive Last Crank Event Time values)
*/
// Callback when data is received
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&powerReadings, incomingData, sizeof(powerReadings));
  Serial.print("Bytes received: ");
  Serial.println(len);
  Serial.println(powerReadings.cadence);
  Serial.println(powerReadings.power);
}

bool deviceConnected = false;
bool oldDeviceConnected = false;
uint16_t loopDelay = 1024U; //to help in artificial rpm generation

void loop()
{
  // notify changed value
  if (deviceConnected)
  {

    if (powerReadings.cadence > 0 ) {
    cumulativeRevolutions+=1; 
    lastCET+=loopDelay*600.0/powerReadings.cadence;
    }
    //BLE Transmit
    uint16_t txPower = powerReadings.power * POWERFACTOR ;
  
  power[3] = (byte) (txPower / 256);
  power[2] = (byte) (txPower - (power[3] * 256));

  power[5] = (byte) (cumulativeRevolutions / 256);
  power[4] = (byte) (cumulativeRevolutions - (power[5] * 256));

  power[7] = (byte) (lastCET / 256);
  power[6] = (byte) (lastCET - (power[7] * 256));


    
  powerMeterMeasurementCharacteristics.setValue(power, 9);
  powerMeterMeasurementCharacteristics.notify();
  cyclingPowerFeatureCharacteristics.setValue(feature, 4);
  sensorLocationCharacteristic.setValue(pwrPos, 1);

    delay(loopDelay); // the minimum is 3ms according to official docs
  }
  // disconnecting
  if (!deviceConnected && oldDeviceConnected)
  {
  //  bluetooth->startBroadcast(); restart
    InitBLE(); //disconnected - init again
    oldDeviceConnected = deviceConnected;
  }
  // connecting
  if (deviceConnected && !oldDeviceConnected)
  {
    // do stuff here on connecting
    oldDeviceConnected = deviceConnected;
  }
}

void InitBLE() {
    BLEDevice::init("Keiser Soze");
    // Create the BLE Server
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
  
    // Create the BLE Service
    BLEService *pPower = pServer->createService(powerMeterService);
  
    pPower->addCharacteristic(&powerMeterMeasurementCharacteristics);
    powerMeterMeasuremenDescriptor.setValue("sint 16bit");
    powerMeterMeasurementCharacteristics.addDescriptor(&powerMeterMeasuremenDescriptor);
    powerMeterMeasurementCharacteristics.addDescriptor(new BLE2902());
  
    pPower->addCharacteristic(&cyclingPowerFeatureCharacteristics);
    cyclingPowerFeatureDescriptor.setValue("Bits 0 - 21");
    cyclingPowerFeatureCharacteristics.addDescriptor(&cyclingPowerFeatureDescriptor);

      
    pPower->addCharacteristic(&sensorLocationCharacteristic);
    sensorLocationDescriptor.setValue("Position 0 - 16");
    sensorLocationCharacteristic.addDescriptor(&sensorLocationDescriptor);
  
    pServer->getAdvertising()->addServiceUUID(powerMeterService);
  
    pPower->start();
    // Start advertising
    pServer->getAdvertising()->start();
  }
