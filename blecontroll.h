#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLEHIDDevice.h>

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331916c"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b2679"
#define pin1 33
#define pin2 34
#define pinL 25
#define pinH 26

class BleHidJoystick;
static BleHidJoystick* instance = 0;

// HID input report data.
typedef struct {
  uint8_t sdta[2];
  int8_t sdtb[4];
} key_report_t;

// HID report desc (keyboard).
static const uint8_t reportMap[] = {
  0x05, 0x01, // USAGE_PAGE (Generic Desktop)
  0x09, 0x04, // USAGE (joys)
  0xa1, 0x01, // COLLECTION (Application)
  0x09, 0x01, // USAGE (pointesr)
  0xa1, 0x00, // COLLECTION (physical)
  0x85, 0x01, //  REPORT_ID (1)
  0x05, 0x09, //   USAGE_PAGE (usage = button)
  0x19, 0x01, //     USAGE_MINIMUM (1)
  0x29, 0x0c, //     USAGE_MAXIMUM (12)
  0x15, 0x00, //   LOGICAL_MINIMUM (0)
  0x25, 0x01, //   LOGICAL_MAXIMUM (1)
  0x95, 0x0c, //   REPORT_COUNT (12)
  0x75, 0x01, //   REPORT_SIZE (1) 各キーにつき1ビット
  0x81, 0x02, //   INPUT (Data,Var,Abs) 8ビット長のInputフィールド(変数)が1つ。

  0x05, 0x01, //   USAGE_PAGE (gene des)
  0x09, 0x39, //   usage hatswitch
  0x15, 0x00, //   LOGICAL_MINIMUM (0)
  0x25, 0x07, //   LOGICAL_MAXIMUM (7)
  0x95, 0x01, //   REPORT_COUNT (1)
  0x75, 0x04, //   REPORT_SIZE (4)
  0x81, 0x02, //   INPUT (Data,vAr,Abs)

  0x05, 0x01, //   USAGE_PAGE (gene des)
  0x09, 0x30, //   usage x
  0x09, 0x31, //   usage y
  0x09, 0x32, //   usage z
  0x09, 0x35, //   usage rz
  0x15, 0x81, //   LOGICAL_MINIMUM (-127)
  0x25, 0x7f, //   LOGICAL_MAXIMUM (127)
  0x95, 0x04, //   REPORT_COUNT (4) 全部で8つ
  0x75, 0x08, //   REPORT_SIZE (8) 各キーにつき1ビット
  0x81, 0x02, //   INPUT (Data,vAr,Abs)
  0xc0,    // END_COLLECTION
  0xc0 //end collection
};

class BleHidJoystick {
    class ServerCallbacks: public BLEServerCallbacks {
        void onConnect(BLEServer* pServer) {
          BleHidJoystick::getInstance()->set_connected(true);
          Serial.println("Connect");
        }

        void onDisconnect(BLEServer* pServer) {
          BleHidJoystick::getInstance()->set_connected(false);
          Serial.println("Disconnect");
        }
    };

    class BleHidOutputReport : public BLECharacteristicCallbacks {
        void onWrite(BLECharacteristic* me) {
          uint8_t* value = (uint8_t*)(me->getValue().c_str());
        }
    };

    BleHidJoystick() {
      connected = false;
      memset(&report, 0, sizeof(key_report_t));
      modifier_keys = 0;
    }
    bool connected;
    BLEHIDDevice* hid;
    BLEServer* pServer;
    BLECharacteristic* input;
    BLECharacteristic* output; 
    key_report_t report;
    uint8_t modifier_keys;
    
  public:
    BLECharacteristic* pCharacteristic;
    static BleHidJoystick* getInstance() {
      if (!instance)
        instance = new BleHidJoystick();
      return instance;
    };
    void init() {
      BLEDevice::init("ESP32_BLE_jst1");
      pServer = BLEDevice::createServer();
      pServer->setCallbacks(new ServerCallbacks());

      BLEDevice::setMTU(23);
      hid = new BLEHIDDevice(pServer);
      input = hid->inputReport(1);
      output = hid->outputReport(1);
      output->setCallbacks(new BleHidOutputReport());
      hid->manufacturer()->setValue("unko");
      hid->pnp(0x02, 0xe502, 0xa111, 0x0210);
      hid->hidInfo(0x00, 0x01); // country == 0, flags == 1 ( providing wake-up signal to a HID host)
      hid->reportMap((uint8_t*)reportMap, sizeof(reportMap));

      hid->startServices();
      
      BLEService *pService = pServer->createService(SERVICE_UUID);
      // Create a BLE Characteristic
      pCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_NOTIFY);
      // Create a BLE Descriptor
      pCharacteristic->addDescriptor(new BLE2902());

      pService->start();

      BLEAdvertising *pAdvertising = pServer->getAdvertising();
      pAdvertising->setAppearance(HID_JOYSTICK);
      pAdvertising->addServiceUUID(hid->hidService()->getUUID());
      pAdvertising->addServiceUUID(SERVICE_UUID);
      pAdvertising->start();

      BLESecurity *pSecurity = new BLESecurity();
      pSecurity->setAuthenticationMode(ESP_LE_AUTH_BOND);
    }

    bool is_connected() const {
      return connected;
    }
    void set_connected(bool f) {
      connected = f;
    }
    void sendReport() {
      if (is_connected()) {
        input->setValue((uint8_t*)&report, sizeof(key_report_t));
        input->notify();
      }
    }
    // keyboard interface.
    void report_stk(uint8_t X, uint8_t Y, uint8_t Z, uint8_t Rz) {
      report.sdtb[0] = X - 0x80;
      report.sdtb[1] = Y - 0x80;
      report.sdtb[2] = Z - 0x80;
      report.sdtb[3] = Rz - 0x80;
      sendReport();
    }
    void report_hat(uint8_t key) {
      report.sdta[1] &= (0 << 4);
      report.sdta[1] |= (key << 4);
      sendReport();
    }
    void report_press(uint8_t key) {
      if (key < 9)report.sdta[0] |= (1 << (key - 1));
      else if (key < 13)report.sdta[1] |= (1 << (key - 9));
      sendReport();
    }
    void report_release(uint8_t key) {
      if (key < 9)report.sdta[0] &= ~(1 << (key - 1));
      else if (key < 13)report.sdta[1] &= ~(1 << (key - 9));
      sendReport();
    };
};
