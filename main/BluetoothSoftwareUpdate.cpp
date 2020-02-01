#include "BluetoothUtil.h"
#include "BluetoothSoftwareUpdate.h"
#include <esp_gatt_defs.h>
#include <BLE2902.h>
#include <Arduino.h>
#include <Update.h>
#include <CRC32.h>

static BLECharacteristic swUpdateTotalSizeCharacteristic("e74dd9c0-a301-4a6f-95a1-f0e1dbea8e1e", BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ);
static BLECharacteristic swUpdateDataCharacteristic("e272ebac-d463-4b98-bc84-5cc1a39ee517", BLECharacteristic::PROPERTY_WRITE);
static BLECharacteristic swUpdateCRC32Characteristic("4826129c-c22a-43a3-b066-ce8f0d5bacc6", BLECharacteristic::PROPERTY_WRITE);
static BLECharacteristic swUpdateResultCharacteristic("5e134862-7411-4424-ac4a-210937432c77", BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

CRC32 crc;

class UpdateCallbacks : public BLECharacteristicCallbacks
{
    void onRead(BLECharacteristic *pCharacteristic) {
        BLECharacteristicCallbacks::onRead(pCharacteristic);
        Serial.println("Got on read");
    }

    void onWrite(BLECharacteristic *pCharacteristic)
    {
        // dumpCharacteristic(pCharacteristic);

        if (pCharacteristic == &swUpdateTotalSizeCharacteristic)
        {
            // Check if there is enough to OTA Update
            uint32_t len = getValue32(pCharacteristic, 0);
            crc.reset();
            bool canBegin = Update.begin(len);
            Serial.printf("Setting update size %u, result %d\n", len, canBegin);
            if(!canBegin)
                // Indicate failure by forcing the size to 0
                pCharacteristic->setValue(0UL);
        }
        else if (pCharacteristic == &swUpdateDataCharacteristic)
        {
            std::string value = pCharacteristic->getValue();
            uint32_t len = value.length();
            uint8_t *data = pCharacteristic->getData();
            // Serial.printf("Writing %u\n", len);
            crc.update(data, len);
            Update.write(data, len);
        }
        else if (pCharacteristic == &swUpdateCRC32Characteristic)
        {
            uint32_t expectedCRC = getValue32(pCharacteristic, 0);
            Serial.printf("expected CRC %u\n", expectedCRC);

            uint8_t result = 0xff;

            // Check the CRC before asking the update to happen.
            if(crc.finalize() != expectedCRC) {
                Serial.println("Invalid CRC!");
                result = 0xe0; // FIXME, use real error codes
            }
            else {
                if (Update.end())
                {
                    Serial.println("OTA done!");
                    // ESP.restart();
                }
                else
                {
                    Serial.println("Error Occurred. Error #: " + String(Update.getError()));
                }
                result = Update.getError();
            }
            swUpdateResultCharacteristic.setValue(&result, 1);
        }
        else {
            Serial.println("unexpected write");
        }
    }
};

UpdateCallbacks updateCb;

/*
SoftwareUpdateService UUID cb0b9a0b-a84c-4c0d-bdbb-442e3144ee30

Characteristics

UUID                                 properties          description
e74dd9c0-a301-4a6f-95a1-f0e1dbea8e1e write|read          total image size, 32 bit, write this first, then read read back to see if it was acceptable (0 mean not accepted)
e272ebac-d463-4b98-bc84-5cc1a39ee517 write               data, variable sized, recommended 512 bytes, write one for each block of file
4826129c-c22a-43a3-b066-ce8f0d5bacc6 write               crc32, write last - writing this will complete the OTA operation, now you can read result
5e134862-7411-4424-ac4a-210937432c77 read|notify         result code, readable but will notify when the OTA operation completes
 */
BLEService *createUpdateService(BLEServer* server) {
    // Create the BLE Service
    BLEService *service = server->createService("cb0b9a0b-a84c-4c0d-bdbb-442e3144ee30");

    addWithDesc(service, &swUpdateTotalSizeCharacteristic, "total image size");
    addWithDesc(service, &swUpdateDataCharacteristic, "data");
    addWithDesc(service, &swUpdateCRC32Characteristic, "crc32");
    addWithDesc(service, &swUpdateResultCharacteristic, "result code");

    swUpdateTotalSizeCharacteristic.setCallbacks(&updateCb);
    swUpdateDataCharacteristic.setCallbacks(&updateCb);
    swUpdateCRC32Characteristic.setCallbacks(&updateCb);
    
    swUpdateResultCharacteristic.addDescriptor(new BLE2902()); // Needed so clients can request notification

    return service;
}

