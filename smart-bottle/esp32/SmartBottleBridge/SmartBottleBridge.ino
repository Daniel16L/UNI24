/*
 * Smart Bottle — punte transparentă UART <-> BLE (Nordic UART Service)
 * Placă: ESP32 DevKit (Arduino core 2.x sau 3.x)
 *
 * Cablaj:  ESP32 GPIO16 (RX2) <- STM32 PC12 (UART5 TX)
 *          ESP32 GPIO17 (TX2) -> STM32 PD2  (UART5 RX)
 *          GND comun. Ambele pe 3.3 V: fără divizor de tensiune.
 *
 * Linii UART complete ("...\n") de la STM32 -> notificare BLE pe TX.
 * Scrieri BLE pe RX -> UART spre STM32 (se adaugă '\n' dacă lipsește).
 * La conectare/deconectare trimite "S,1\n" / "S,0\n" spre STM32.
 */
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define DEVICE_NAME   "SmartBottle"
#define SERVICE_UUID  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define RX_UUID       "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"   // telefon -> dispozitiv
#define TX_UUID       "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"   // dispozitiv -> telefon

#define UART_RX_PIN   16
#define UART_TX_PIN   17
#define UART_BAUD     115200
#define LED_PIN       2        // LED-ul albastru de pe majoritatea DevKit-urilor
#define LINE_MAX      32

static BLEServer *server = nullptr;
static BLECharacteristic *txChar = nullptr;
static volatile bool connected = false;
static bool wasConnected = false;

static char lineBuf[LINE_MAX];
static size_t lineLen = 0;
static bool lineOverflow = false;

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *) override { connected = true; }
  void onDisconnect(BLEServer *) override { connected = false; }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    auto v = c->getValue();                 // std::string (core 2.x) sau String (core 3.x)
    size_t n = v.length();
    if (n == 0) return;
    Serial2.write((const uint8_t *)v.c_str(), n);
    if (v.c_str()[n - 1] != '\n') Serial2.write('\n');
  }
};

static void notifyLine(const char *s, size_t n) {
  if (!connected) return;                   // nimeni nu ascultă: aruncăm
  txChar->setValue((uint8_t *)s, n);
  txChar->notify();
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  Serial.begin(115200);                     // debug pe USB
  Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

  BLEDevice::init(DEVICE_NAME);
  server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService *svc = server->createService(SERVICE_UUID);
  txChar = svc->createCharacteristic(TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  txChar->addDescriptor(new BLE2902());     // CCCD: permite clientului să activeze notificările

  BLECharacteristic *rxChar = svc->createCharacteristic(
      RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rxChar->setCallbacks(new RxCallbacks());
  svc->start();

  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);        // necesar pentru filtrul din Web Bluetooth / ble-plx
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial2.print("S,0\n");
  Serial.println("SmartBottle bridge pornit");
}

void loop() {
  // UART -> BLE, linie cu linie
  while (Serial2.available()) {
    char c = (char)Serial2.read();
    if (c == '\n') {
      if (!lineOverflow && lineLen > 0) {
        lineBuf[lineLen++] = '\n';
        notifyLine(lineBuf, lineLen);
        Serial.write((const uint8_t *)lineBuf, lineLen);
      }
      lineLen = 0;
      lineOverflow = false;
    } else if (!lineOverflow) {
      if (lineLen < LINE_MAX - 1) lineBuf[lineLen++] = c;
      else lineOverflow = true;             // linie prea lungă: aruncată
    }
  }

  // tranziții de conexiune (făcute aici, nu în callback, ca să nu blocăm stiva BLE)
  if (connected != wasConnected) {
    wasConnected = connected;
    digitalWrite(LED_PIN, connected ? HIGH : LOW);
    Serial2.print(connected ? "S,1\n" : "S,0\n");
    if (!connected) {
      delay(300);                           // lasă stiva să elibereze conexiunea
      BLEDevice::startAdvertising();        // redevine vizibil pentru o nouă conectare
    }
  }
  delay(2);
}
