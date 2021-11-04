#include <usbhid.h>
#include <hiduniversal.h>
#include <usbhub.h>
#include <SPI.h>
#include "hidjoystickrptparser.h"
#include "blecontroll.h"
#include "esp_system.h"

USB Usb;
USBHub Hub(&Usb);
HIDUniversal Hid(&Usb);
JoystickEvents JoyEvents;
JoystickReportParser Joy(&JoyEvents);
BleHidJoystick* ble = 0;

hw_timer_t *timer = NULL;

JoystickReportParser::JoystickReportParser(JoystickEvents *evt) : joyEvents(evt), oldHat(0xDE), oldButtons(0) {
  for (uint8_t i = 0; i < RPT_GEMEPAD_LEN; i++)oldPad[i] = 0xD;
}

void JoystickReportParser::Parse(USBHID *hid, bool is_rpt_id, uint8_t len, uint8_t *buf) {
  bool match = true;
  // Checking if there are changes in report since the method was last called
  for (uint8_t i = 0; i < RPT_GEMEPAD_LEN; i++) {
    if (buf[i] != oldPad[i]) {
      match = false;
      break;
    }
  }

  // Calling Game Pad event handler
  if (!match && joyEvents) {
    joyEvents->OnGamePadChanged((const GamePadEventData*)buf);
    for (uint8_t i = 0; i < RPT_GEMEPAD_LEN; i++) oldPad[i] = buf[i];
  }

  uint8_t hat = (buf[5] & 0xF);
  // Calling Hat Switch event handler
  if (hat != oldHat && joyEvents) {
    joyEvents->OnHatSwitch(hat);
    oldHat = hat;
  }

  uint16_t buttons = (0x0000 | buf[6]);
  buttons <<= 4;
  buttons |= (buf[5] >> 4);
  uint16_t changes = (buttons ^ oldButtons);
  // Calling Button Event Handler for every button changed
  if (changes) {
    for (uint8_t i = 0; i < 0x0C; i++) {
      uint16_t mask = (0x0001 << i);
      if (((mask & changes) > 0) && joyEvents) {
        if ((buttons & mask) > 0)joyEvents->OnButtonDn(i + 1);
        else joyEvents->OnButtonUp(i + 1);
      }
    }
    oldButtons = buttons;
  }
}

void JoystickEvents::OnGamePadChanged(const GamePadEventData *evt) {
  ble->report_stk(evt->X, evt->Y, evt->Z2, evt->Rz);
}

void JoystickEvents::OnHatSwitch(uint8_t hat) {
  ble->report_hat(hat);
}

void JoystickEvents::OnButtonUp(uint8_t but_id) {
  ble->report_release(but_id);
}

void JoystickEvents::OnButtonDn(uint8_t but_id) {
  ble->report_press(but_id);
}

void IRAM_ATTR onTime(){
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_33, 0);
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  pinMode(pin1, INPUT);
  pinMode(pin2, INPUT);
  pinMode(pinH, OUTPUT);
  pinMode(pinL, OUTPUT);
  digitalWrite(pinL, LOW);
  digitalWrite(pinH, HIGH);

#if !defined(__MIPSEL__)
  while (!Serial); // Wait for serial port to connect - used on Leonardo, Teensy and other boards with built-in USB CDC serial connection
#endif
  Serial.println("St");
  ble = BleHidJoystick::getInstance();
  ble->init();

  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTime, true);
  timerAlarmWrite(timer, 5*1000000,false);
  timerAlarmEnable(timer);
  
  while (!ble->is_connected()) {
    delay(400);
    if (ble->is_connected() && Usb.Init() != -1) {
      Hid.SetReportParser(0, &Joy);
    }
  }
}

void loop() {
  if (ble->is_connected()) {
    timerWrite(timer, 0);
    Serial.println("read called");
    uint8_t value[4];
    uint16_t in1 = analogRead(pin1);
    uint16_t in2 = analogRead(pin2);

    *((uint16_t*)value) = in1;
    *(uint16_t*)&value[2] = in2;

    ble->pCharacteristic->setValue(value, 4);
    ble->pCharacteristic->notify();
  }
  Usb.Task();
}
