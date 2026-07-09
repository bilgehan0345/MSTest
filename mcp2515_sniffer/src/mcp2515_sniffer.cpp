// =============================================================================
// mcp2515_sniffer.cpp
// MCP2515 SPI CAN Modülü — CAN Bus Sniffer
//
// Özellikler:
//   - Listen-only mod (bus'a frame göndermez, sadece dinler)
//   - Her frame'i Serial Monitor'e güzel formatta yazdırır
//   - Aynı anda CSV formatında da yazdırır (Excel analizi için)
//   - "send 0x201 01 02 03 04" komutu ile test frame'i gönderebilirsin
//   - Baud rate değiştirmek için CAN_BAUD_RATE #define'ını değiştir
//
// Donanım bağlantısı (MCP2515 modülü ile):
//   ESP32 3.3V  --> MCP2515 VCC  (UYARI: bazı modüller 5V ister, 3.3V regülatörlü olanı kullan)
//   ESP32 GND   --> MCP2515 GND
//   ESP32 GPIO18 --> MCP2515 SCK
//   ESP32 GPIO23 --> MCP2515 SI (MOSI)
//   ESP32 GPIO19 --> MCP2515 SO (MISO)
//   ESP32 GPIO5  --> MCP2515 CS
//   ESP32 GPIO4  --> MCP2515 INT (interrupt pini — opsiyonel ama önerilir)
//   MCP2515 CANH --> CAN-H hattı
//   MCP2515 CANL --> CAN-L hattı
// =============================================================================

#include <Arduino.h>
#include <SPI.h>
#include <mcp2515.h>   // autowp-mcp2515 kütüphanesi

// ---------------------------------------------------------------------------
// PIN TANIMLARI — buradan değiştir
// ---------------------------------------------------------------------------
#define MCP_CS_PIN   5    // Chip Select pini
#define MCP_INT_PIN  4    // Interrupt pini (INT — opsiyonel, -1 yapınca polling modu)

// ---------------------------------------------------------------------------
// Hedef cihazın CAN ID'si
// ---------------------------------------------------------------------------
#define TARGET_CAN_ID 0x123

// Eğer MCP2515 modülündeki INT pini bağlı değilse veya güvenilir değilse
// burayı -1 yap ve çalışma testini polling ile yap.
// ---------------------------------------------------------------------------
// MCP2515 OSILATÖR FREKANSI — modülündeki kristale bak!
// Seçenekler: MCP_8MHZ, MCP_16MHZ, MCP_20MHZ
// ---------------------------------------------------------------------------
#define MCP2515_OSCILLATOR_FREQ  MCP_8MHZ

// ---------------------------------------------------------------------------
// BAUD RATE — sadece bu satırı değiştir
// Seçenekler: CAN_125KBPS, CAN_250KBPS, CAN_500KBPS, CAN_1000KBPS
// ---------------------------------------------------------------------------
#define CAN_BAUD_RATE  CAN_500KBPS

// ---------------------------------------------------------------------------
// MOD SEÇİMİ
// true  = Listen-only (sadece dinle, bus'a karışma)
// false = Normal mod (send komutu çalışır)
// ---------------------------------------------------------------------------
#define LISTEN_ONLY_MODE  true

// ---------------------------------------------------------------------------
// CSV LOGLAMA
// ---------------------------------------------------------------------------
#define CSV_OUTPUT_ENABLED  true

// Forward declarations
void IRAM_ATTR onCanInterrupt();
void printCsvHeader();
void printFrame(struct can_frame &frame);
bool mcpInit();
void handleSerialCommand();
void processCommand(const String &line);

// MCP2515 nesnesi oluştur
MCP2515 mcp2515(MCP_CS_PIN);

// Interrupt bayrağı (ISR'den set edilir)
volatile bool canInterruptFlag = false;

// CSV başlık bayrağı
static bool csvHeaderPrinted = false;

// ---------------------------------------------------------------------------
// Interrupt Service Routine (ISR) — MCP2515 INT pini LOW'a çekince tetiklenir
// ---------------------------------------------------------------------------
void IRAM_ATTR onCanInterrupt() {
  canInterruptFlag = true;
}

// ---------------------------------------------------------------------------
// CSV başlığını bir kere yazdır
// ---------------------------------------------------------------------------
void printCsvHeader() {
  if (CSV_OUTPUT_ENABLED && !csvHeaderPrinted) {
    Serial.println(F("millis,id_hex,ide,dlc,byte0,byte1,byte2,byte3,byte4,byte5,byte6,byte7"));
    csvHeaderPrinted = true;
  }
}

// ---------------------------------------------------------------------------
// Gelen bir frame'i güzel formatta Serial'e yazdır
// ---------------------------------------------------------------------------
void printFrame(struct can_frame &frame) {
  unsigned long ts = millis();
  bool isExtended = (frame.can_id & CAN_EFF_FLAG) != 0;
  uint32_t rawId  = frame.can_id & (isExtended ? CAN_EFF_MASK : CAN_SFF_MASK);
  bool isRtr      = (frame.can_id & CAN_RTR_FLAG) != 0;

  // --- İnsan-okunur format ---
  Serial.print(F("["));
  Serial.print(ts);
  Serial.print(F(" ms] ID: 0x"));

  if (isExtended) {
    Serial.print(rawId, HEX);
    Serial.print(F(" (EXT)"));
  } else {
    char idBuf[8];
    snprintf(idBuf, sizeof(idBuf), "%03X", rawId);
    Serial.print(idBuf);
    Serial.print(F(" (STD)"));
  }

  Serial.print(F("  DLC: "));
  Serial.print(frame.can_dlc);
  Serial.print(F("  Data:"));

  for (int i = 0; i < frame.can_dlc; i++) {
    Serial.print(F(" "));
    if (frame.data[i] < 0x10) Serial.print(F("0"));
    Serial.print(frame.data[i], HEX);
  }

  if (rawId == TARGET_CAN_ID) {
    Serial.print(F("  [TARGET]"));
    if (frame.can_dlc >= 2) {
      int rpm = (frame.data[0] << 8) | frame.data[1];
      float gearRatio = 1.0;
      float wheelRadius = 0.30;
      float wheelCircumference = 2.0 * 3.14159 * wheelRadius;
      int speed = (rpm / gearRatio) * wheelCircumference * 60.0 / 1000.0;
      Serial.printf("  | Alinan RPM: %d | Hiz: %d km/h", rpm, speed);
    }
  }

  if (isRtr) Serial.print(F("  [RTR]"));
  Serial.println();

  // --- CSV format ---
  if (CSV_OUTPUT_ENABLED) {
    Serial.print(F("CSV,"));
    Serial.print(ts);
    Serial.print(F(",0x"));
    Serial.print(rawId, HEX);
    Serial.print(F(","));
    Serial.print(isExtended ? F("EXT") : F("STD"));
    Serial.print(F(","));
    Serial.print(frame.can_dlc);
    for (int i = 0; i < 8; i++) {
      Serial.print(F(","));
      if (i < frame.can_dlc) {
        if (frame.data[i] < 0x10) Serial.print(F("0"));
        Serial.print(frame.data[i], HEX);
      } else {
        Serial.print(F("--"));
      }
    }
    Serial.println();
  }
}

// ---------------------------------------------------------------------------
// MCP2515'i başlat — hata durumunda detaylı mesaj yaz
// ---------------------------------------------------------------------------
bool mcpInit() {
  MCP2515::ERROR err;

  // Sıfırla
  err = mcp2515.reset();
  if (err != MCP2515::ERROR_OK) {
    Serial.print(F("[HATA] MCP2515 reset basarisiz, kod: "));
    Serial.println((int)err);
    Serial.println(F("       SPI baglantilarini kontrol et (CS/SCK/MOSI/MISO pinleri)."));
    return false;
  }

  // Baud rate ayarla
  err = mcp2515.setBitrate(CAN_BAUD_RATE, MCP2515_OSCILLATOR_FREQ);
  if (err != MCP2515::ERROR_OK) {
    Serial.print(F("[HATA] Baud rate ayarlanamadi, kod: "));
    Serial.println((int)err);
    Serial.println(F("       MCP2515_OSCILLATOR_FREQ degerini kontrol et (8 veya 16 MHz?)"));
    return false;
  }

  // Mod ayarla
  if (LISTEN_ONLY_MODE) {
    err = mcp2515.setListenOnlyMode();
    Serial.println(F("  Mod: LISTEN-ONLY"));
  } else {
    err = mcp2515.setNormalMode();
    Serial.println(F("  Mod: NORMAL (gönderim aktif)"));
  }

  if (err != MCP2515::ERROR_OK) {
    Serial.print(F("[HATA] Mod ayarlanamadi, kod: "));
    Serial.println((int)err);
    return false;
  }

  return true;
}

// ---------------------------------------------------------------------------
// Serial'den komut oku ve işle
// Komut formatı: send <id> <b0> <b1> ...
// Örnek: send 0x201 01 02 03 04
// ---------------------------------------------------------------------------
void handleSerialCommand() {
  static String inputLine = "";

  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      inputLine.trim();
      if (inputLine.length() > 0) {
        processCommand(inputLine);
      }
      inputLine = "";
    } else {
      inputLine += c;
    }
  }
}

void processCommand(const String &line) {
  if (!line.startsWith("send")) {
    Serial.println(F("[INFO] Bilinmeyen komut."));
    Serial.println(F("       Kullanim: send <id> <b0> <b1>..."));
    Serial.println(F("       Örnek:    send 0x201 01 02 03 04"));
    return;
  }

  // Kelimelere böl
  String tokens[10];
  int tokenCount = 0;
  String temp = line;
  temp.trim();

  while (temp.length() > 0 && tokenCount < 10) {
    int spaceIdx = temp.indexOf(' ');
    if (spaceIdx == -1) {
      tokens[tokenCount++] = temp;
      break;
    } else {
      tokens[tokenCount++] = temp.substring(0, spaceIdx);
      temp = temp.substring(spaceIdx + 1);
      temp.trim();
    }
  }

  if (tokenCount < 3) {
    Serial.println(F("[HATA] En az ID ve 1 data byte gerekli."));
    return;
  }

  uint32_t canId = strtoul(tokens[1].c_str(), nullptr, 16);
  bool isExtended = (canId > 0x7FF);

  // can_frame oluştur
  struct can_frame txFrame;
  txFrame.can_id  = isExtended ? (canId | CAN_EFF_FLAG) : canId;
  txFrame.can_dlc = tokenCount - 2;

  if (txFrame.can_dlc > 8) {
    Serial.println(F("[HATA] Maksimum 8 data byte gönderebilirsin."));
    return;
  }

  for (int i = 0; i < txFrame.can_dlc; i++) {
    txFrame.data[i] = (uint8_t)strtoul(tokens[i + 2].c_str(), nullptr, 16);
  }

  if (LISTEN_ONLY_MODE) {
    Serial.println(F("[UYARI] LISTEN_ONLY_MODE=true, frame gönderilemez."));
    Serial.println(F("        Göndermek için LISTEN_ONLY_MODE'u false yap ve yeniden yükle."));
    return;
  }

  MCP2515::ERROR err = mcp2515.sendMessage(&txFrame);
  if (err == MCP2515::ERROR_OK) {
    Serial.print(F("[TX OK] ID: 0x"));
    Serial.print(canId, HEX);
    Serial.print(F(" DLC: "));
    Serial.print(txFrame.can_dlc);
    Serial.print(F(" Data:"));
    for (int i = 0; i < txFrame.can_dlc; i++) {
      Serial.print(F(" "));
      if (txFrame.data[i] < 0x10) Serial.print(F("0"));
      Serial.print(txFrame.data[i], HEX);
    }
    Serial.println();
  } else if (err == MCP2515::ERROR_FAILTX) {
    Serial.println(F("[HATA] TX başarisiz: bus meşgul veya terminatör eksik."));
  } else {
    Serial.print(F("[HATA] sendMessage hata kodu: "));
    Serial.println((int)err);
  }
}

// ---------------------------------------------------------------------------
// setup()
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println(F("========================================"));
  Serial.println(F("  ESP32 MCP2515 CAN Sniffer"));
  Serial.println(F("  mcp2515_sniffer.cpp"));
  Serial.println(F("========================================"));
  Serial.println(F("CSV satirlari 'CSV,' ile baslar — Excel'e aktarirken filtrele."));
  Serial.println(F("Komut: send <id> <b0> <b1>...  Örnek: send 0x201 01 02 03"));
  Serial.println(F("----------------------------------------"));

  printCsvHeader();

  // SPI veri yolunu başlat (ESP32 varsayılan VSPI pinleri: SCK: 18, MISO: 19, MOSI: 23, CS/SS: 5)
  SPI.begin();

  Serial.println(F("MCP2515 baslatiliyor..."));
  if (!mcpInit()) {
    Serial.println(F("[KRİTİK] MCP2515 baslatma BAŞARISIZ. Lütfen reset at."));
    while (true) { delay(1000); }
  }

  // Interrupt pini ayarla (eğer -1 değilse polling modu)
  if (MCP_INT_PIN >= 0) {
    pinMode(MCP_INT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(MCP_INT_PIN), onCanInterrupt, FALLING);
    Serial.println(F("[OK] Interrupt modu aktif. Aynı zamanda polling ile yedek kontrol var."));
  } else {
    Serial.println(F("[OK] Polling modu aktif (INT pini kullanilmiyor)."));
  }

  Serial.println(F("[OK] MCP2515 baslatildi. CAN bus dinleniyor..."));
  Serial.println(F("========================================"));
}

// ---------------------------------------------------------------------------
// loop()
// ---------------------------------------------------------------------------
void loop() {
  bool shouldRead = false;

  // Interrupt pin bağlı olsa bile bazen INT sinyali gelmeyebilir.
  // Bu nedenle hem interrupt hem de polling kontrolü yapıyoruz.
  if (MCP_INT_PIN >= 0) {
    noInterrupts();
    bool flag = canInterruptFlag;
    canInterruptFlag = false;
    interrupts();

    shouldRead = flag || (digitalRead(MCP_INT_PIN) == LOW) || (mcp2515.checkReceive() == MCP2515::ERROR_OK);
  } else {
    shouldRead = (mcp2515.checkReceive() == MCP2515::ERROR_OK);
  }

  if (shouldRead) {
    struct can_frame rxFrame;
    MCP2515::ERROR err = mcp2515.readMessage(&rxFrame);

    if (err == MCP2515::ERROR_OK) {
      printFrame(rxFrame);
    } else if (err != MCP2515::ERROR_NOMSG) {
      Serial.print(F("[HATA] readMessage kodu: "));
      Serial.println((int)err);
    }
  }

  // Seri komutları işle
  handleSerialCommand();
}
