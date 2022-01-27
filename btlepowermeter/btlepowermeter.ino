#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

extern bool deviceConnected;
extern bool oldDeviceConnected;

#define CYCLING_POWER_SERVICE_UUID "00001818-0000-1000-8000-00805F9B34FB"
#define POWER_CHARACTERISTIC_UUID "00002A63-0000-1000-8000-00805F9B34FB"
#define SENSORPOS_CHARACTERISTIC_UUID "00002A5D-0000-1000-8000-00805F9B34FB"
#define POWERFEATURE_CHARACTERISTIC_UUID "00002A65-0000-1000-8000-00805F9B34FB"

#define CSC_SERVICE_UUID "00001816-0000-1000-8000-00805F9B34FB"
#define CSC_MEASUREMENT_CHARACTERISTIC_UUID "00002A5B-0000-1000-8000-00805F9B34FB"
#define CSC_FEATURE_CHARACTERISTIC_UUID "00002A5C-0000-1000-8000-00805F9B34FB"
#define powerFlags 0b0000000000000000;
#define CSCFlags 0b10; //todo is it good practice to define flags up here?

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

class BLEPowerCSC
{ //a class for holding bluetooth-related code
private:
    BLEServer *pServer = NULL;
    //Power Characteristics
    BLECharacteristic *pCharacteristicPower = NULL; //the power reading itself
    BLECharacteristic *pCharacteristicSensorPos = NULL;
    BLECharacteristic *pCharacteristicPowerFeature = NULL;
    //CSC ONES
    BLECharacteristic *pCharacteristicCSC = NULL; //the cadence reading
    BLECharacteristic *pCharacteristicCSCFeature = NULL;

    uint32_t powerTxValue = 0; //the BLE-Compliant, flagged, ready to transmit power value
    uint64_t CSCTxValue = 0;   //the BLE-Compliant, flagged, ready to transmit CSC value

    //connection indicators
    bool deviceConnected = false;
    bool oldDeviceConnected = false;

public:
    BLEPowerCSC()
    { // a constructor
    }

    void initialize()
    {
        //pinMode(LED_PIN, OUTPUT);
        // Create the BLE Device
        BLEDevice::init("KeiserSoze"); // weirdly enough names with spaces do not seem to work

        // Create the BLE Server
        pServer = BLEDevice::createServer();
        pServer->setCallbacks(new MyServerCallbacks());

        // Create the BLE Service
        BLEService *pService = pServer->createService(CYCLING_POWER_SERVICE_UUID);

        //CSC SERVICE
        BLEService *CSCService = pServer->createService(CSC_SERVICE_UUID);

        // Create the needed BLE Characteristics
        pCharacteristicPower = pService->createCharacteristic(
            POWER_CHARACTERISTIC_UUID,
            BLECharacteristic::PROPERTY_NOTIFY);

        pCharacteristicSensorPos = pService->createCharacteristic(
            SENSORPOS_CHARACTERISTIC_UUID,
            BLECharacteristic::PROPERTY_READ);

        pCharacteristicPowerFeature = pService->createCharacteristic(
            POWERFEATURE_CHARACTERISTIC_UUID,
            BLECharacteristic::PROPERTY_READ);

        //CSC CHARACTERISTICS
        pCharacteristicCSC = CSCService->createCharacteristic(
            CSC_MEASUREMENT_CHARACTERISTIC_UUID,
            BLECharacteristic::PROPERTY_NOTIFY);

        pCharacteristicCSCFeature = CSCService->createCharacteristic(
            CSC_FEATURE_CHARACTERISTIC_UUID,
            BLECharacteristic::PROPERTY_READ);

        // https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.client_characteristic_configuration.xml
        // Create a BLE Descriptor
        pCharacteristicPower->addDescriptor(new BLE2902());

        //CSC
        pCharacteristicCSC->addDescriptor(new BLE2902());

        // Start the service
        pService->start();

        //CSC
        CSCService->start();

        byte posvalue = 6; // right crank
        pCharacteristicSensorPos->setValue((uint8_t *)&posvalue, 1);

        //ALL FEATURE SETTINGfor now keep it simple i can add the vectoring later
        uint32_t powerFeature = 0b0; //just 32 old zeroes
        pCharacteristicPowerFeature->setValue((uint8_t *)&powerFeature, 4);

        uint16_t CSCFeature = 0b010; //only crank rpms supported
        pCharacteristicCSCFeature->setValue((uint8_t *)&CSCFeature, 2);

        // Start advertising
        BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
        pAdvertising->addServiceUUID(CYCLING_POWER_SERVICE_UUID); //todo does it report cadence even if it is not advertised
        pAdvertising->setScanResponse(false);
        pAdvertising->setMinPreferred(0x0); // set value to 0x00 to not advertise this parameter
        BLEDevice::startAdvertising();
    }

    void sendPower(int16_t powerReading)
    {
        powerTxValue = (powerReading << 16) | powerFlags; //very inefficient but just for readability
        pCharacteristicPower->setValue((uint8_t *)&powerTxValue, 4);
        pCharacteristicPower->notify();
    }

    void sendCSC(uint64_t lastCET, uint64_t cumulativeRevolutions)
    {
        CSCTxValue = (lastCET << 24) | (cumulativeRevolutions << 8) | CSCFlags;
        pCharacteristicCSC->setValue((uint8_t *)&CSCTxValue, 5);
        pCharacteristicCSC->notify();
    }

    void startBroadcast()
    {
        delay(500);                  // give the bluetooth stack the chance to get things ready
        pServer->startAdvertising(); // restart advertising
    }
};

BLEPowerCSC *bluetooth = new BLEPowerCSC();

typedef struct struct_message {
uint16_t power;
uint16_t cadence;
} struct_message;

//reading holder
struct_message powerReadings;

void setup()
{
  Serial.begin(115200);
  bluetooth->initialize();

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
    bluetooth->sendPower(txPower);
    bluetooth->sendCSC(lastCET, cumulativeRevolutions);

    delay(loopDelay); // the minimum is 3ms according to official docs
  }
  // disconnecting
  if (!deviceConnected && oldDeviceConnected)
  {
    bluetooth->startBroadcast();
    oldDeviceConnected = deviceConnected;
  }
  // connecting
  if (deviceConnected && !oldDeviceConnected)
  {
    // do stuff here on connecting
    oldDeviceConnected = deviceConnected;
  }
}
