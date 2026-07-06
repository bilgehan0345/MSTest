// =============================================================================
// twai_sniffer.ino
// ESP32 Dahili TWAI (CAN) Controller — CAN Bus Sniffer
//
// Özellikler:
//   - Listen-only mod (bus'a frame göndermez, sadece dinler)
//   - Her frame'i Serial Monitor'e güzel formatta yazdırır
//   - Aynı anda CSV formatında da yazdırır (Excel analizi için)
//   - "send 0x201 01 02 03 04" komutu ile test frame'i gönderebilirsin
//   - Baud rate değiştirmek için sadece CAN_BAUD_RATE #define'ını değiştir
//
// Donanım bağlantısı (TJA1050 transceiver ile):
//   ESP32 GPIO4  --> TJA1050 TXD
//   ESP32 GPIO5  --> TJA1050 RXD
//   TJA1050 CANH --> CAN-H hattı
//   TJA1050 CANL --> CAN-L hattı
//   GND          --> GND (ortak)
//
// Kütüphane kurulumu:
//   Bu sketch harici kütüphane gerektirmez.
//   ESP32 Arduino core ile birlikte gelen driver/twai.h kullanılır.
//   Board Manager'da "esp32 by Espressif Systems" kurulu olmalı.
//
// Baud rate seçenekleri:
//   TWAI_TIMING_CONFIG_25KBITS()
//   TWAI_TIMING_CONFIG_50KBITS()
//   TWAI_TIMING_CONFIG_100KBITS()
//   TWAI_TIMING_CONFIG_125KBITS()
//   TWAI_TIMING_CONFIG_250KBITS()   <-- yaygın
//   TWAI_TIMING_CONFIG_500KBITS()   <-- yaygın
//   TWAI_TIMING_CONFIG_1MBITS()     <-- yaygın
// =============================================================================

#include "driver/twai.h"

// ---------------------------------------------------------------------------
// PIN TANIMLARI — buradan değiştir
// ---------------------------------------------------------------------------
#define CAN_TX_PIN  GPIO_NUM_4   // ESP32 --> TJA1050 TXD
#define CAN_RX_PIN  GPIO_NUM_5   // TJA1050 RXD --> ESP32

// ---------------------------------------------------------------------------
// BAUD RATE — sadece bu satırı değiştir
// Seçenekler: TWAI_TIMING_CONFIG_250KBITS(), _500KBITS(), _1MBITS() vs.
// ---------------------------------------------------------------------------
#define CAN_TIMING  TWAI_TIMING_CONFIG_500KBITS()

// ---------------------------------------------------------------------------
// MOD SEÇİMİ
// true  = Listen-only (sadece dinle, bus'a karışma)
// false = Normal mod (send komutu çalışır)
// ---------------------------------------------------------------------------
#define LISTEN_ONLY_MODE  false   // send komutunu test etmek istersen false yap

// ---------------------------------------------------------------------------
// CSV LOGLAMA — true yapınca her satırın yanına CSV de basar
// ---------------------------------------------------------------------------
#define CSV_OUTPUT_ENABLED  true

// CSV başlık satırını bir kere yazdırmak için bayrak
static bool csvHeaderPrinted = false;

// ---------------------------------------------------------------------------
// Yardımcı: CSV başlığını bir kere yazdır
// ---------------------------------------------------------------------------
void printCsvHeader() {
  if (CSV_OUTPUT_ENABLED && !csvHeaderPrinted) {
    Serial.println(F("millis,id_hex,ide,dlc,byte0,byte1,byte2,byte3,byte4,byte5,byte6,byte7"));
    csvHeaderPrinted = true;
  }
}

// ---------------------------------------------------------------------------
// TWAI sürücüsünü başlat
// ---------------------------------------------------------------------------
bool twaiInit() {
  // Genel yapılandırma
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN,
                                     LISTEN_ONLY_MODE ? TWAI_MODE_LISTEN_ONLY : TWAI_MODE_NORMAL);

  // Timing (baud rate)
  twai_timing_config_t t_config = CAN_TIMING;

  // Filtre: tüm frame'leri kabul et
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  // Sürücüyü kur
  if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) {
    Serial.println(F("[HATA] TWAI driver kurulamadi! Pin çakişmasi veya donanim sorunu olabilir."));
    return false;
  }

  // Sürücüyü başlat
  if (twai_start() != ESP_OK) {
    Serial.println(F("[HATA] TWAI baslatilamadi! Transceiver baglantilarini kontrol et."));
    twai_driver_uninstall();
    return false;
  }

  return true;
}

// ---------------------------------------------------------------------------
// Gelen bir frame'i güzel formatta Serial'e yazdır
// ---------------------------------------------------------------------------
void printFrame(const twai_message_t &msg) {
  unsigned long ts = millis();

  // --- İnsan-okunur format ---
  Serial.print(F("["));
  Serial.print(ts);
  Serial.print(F(" ms] ID: 0x"));

  // 29-bit extended ID ise büyük yazdır
  if (msg.extd) {
    Serial.print(msg.identifier, HEX);
    Serial.print(F(" (EXT)"));
  } else {
    // 11-bit standart ID
    char idBuf[8];
    snprintf(idBuf, sizeof(idBuf), "%03X", msg.identifier);
    Serial.print(idBuf);
    Serial.print(F(" (STD)"));
  }

  Serial.print(F("  DLC: "));
  Serial.print(msg.data_length_code);
  Serial.print(F("  Data:"));

  for (int i = 0; i < msg.data_length_code; i++) {
    Serial.print(F(" "));
    if (msg.data[i] < 0x10) Serial.print(F("0"));
    Serial.print(msg.data[i], HEX);
  }

  // RTR frame ise belirt
  if (msg.rtr) {
    Serial.print(F("  [RTR]"));
  }

  Serial.println();

  // --- CSV format ---
  if (CSV_OUTPUT_ENABLED) {
    Serial.print(F("CSV,"));
    Serial.print(ts);
    Serial.print(F(",0x"));
    Serial.print(msg.identifier, HEX);
    Serial.print(F(","));
    Serial.print(msg.extd ? F("EXT") : F("STD"));
    Serial.print(F(","));
    Serial.print(msg.data_length_code);
    for (int i = 0; i < 8; i++) {
      Serial.print(F(","));
      if (i < msg.data_length_code) {
        if (msg.data[i] < 0x10) Serial.print(F("0"));
        Serial.print(msg.data[i], HEX);
      } else {
        Serial.print(F("--"));  // DLC'den kısa frame'lerde boş göster
      }
    }
    Serial.println();
  }
}

// ---------------------------------------------------------------------------
// Serial'den komut oku ve işle
// Komut formatı: send <id> <b0> <b1> ... (en az 1 byte)
// Örnek: send 0x201 01 02 03 04
//        send 0x100 FF
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
  // Komut: send <hex_id> <hex_byte> <hex_byte> ...
  if (!line.startsWith("send")) {
    Serial.println(F("[INFO] Bilinmeyen komut. Kullanim: send <id> <b0> <b1>..."));
    Serial.println(F("       Örnek: send 0x201 01 02 03 04"));
    return;
  }

  // Kelimelere böl
  String tokens[10];
  int tokenCount = 0;
  int start = 0;
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

  if (tokenCount < 2) {
    Serial.println(F("[HATA] En az ID ve 1 data byte gerekli."));
    return;
  }

  // ID'yi parse et (0x ile ya da düz hex)
  uint32_t canId = strtoul(tokens[1].c_str(), nullptr, 16);
  bool isExtended = (canId > 0x7FF);  // 11 bitten büyükse extended

  // Frame oluştur
  twai_message_t txMsg = {};
  txMsg.identifier = canId;
  txMsg.extd = isExtended ? 1 : 0;
  txMsg.rtr = 0;
  txMsg.data_length_code = tokenCount - 2;  // send + id hariç geri kalanlar

  if (txMsg.data_length_code > 8) {
    Serial.println(F("[HATA] Maksimum 8 data byte gönderebilirsin."));
    return;
  }

  for (int i = 0; i < txMsg.data_length_code; i++) {
    txMsg.data[i] = (uint8_t)strtoul(tokens[i + 2].c_str(), nullptr, 16);
  }

  // LISTEN_ONLY modunda gönderim yapılamaz
  if (LISTEN_ONLY_MODE) {
    Serial.println(F("[UYARI] LISTEN_ONLY_MODE=true, frame gönderilemez."));
    Serial.println(F("        Göndermek için LISTEN_ONLY_MODE'u false yap ve yeniden yükle."));
    return;
  }

  // Gönder (100ms timeout)
  esp_err_t result = twai_transmit(&txMsg, pdMS_TO_TICKS(100));
  if (result == ESP_OK) {
    Serial.print(F("[TX OK] ID: 0x"));
    Serial.print(canId, HEX);
    Serial.print(F(" DLC: "));
    Serial.print(txMsg.data_length_code);
    Serial.print(F(" Data:"));
    for (int i = 0; i < txMsg.data_length_code; i++) {
      Serial.print(F(" "));
      if (txMsg.data[i] < 0x10) Serial.print(F("0"));
      Serial.print(txMsg.data[i], HEX);
    }
    Serial.println();
  } else if (result == ESP_ERR_TIMEOUT) {
    Serial.println(F("[HATA] TX timeout: TX kuyruğu dolu veya bus meşgul."));
  } else {
    Serial.print(F("[HATA] TX başarisiz, kod: "));
    Serial.println(result);
  }
}

// ---------------------------------------------------------------------------
// TWAI istatistiklerini periyodik yazdır (her 10 saniyede)
// ---------------------------------------------------------------------------
void printTwaiStatus() {
  static unsigned long lastStatusTime = 0;
  if (millis() - lastStatusTime < 10000) return;
  lastStatusTime = millis();

  twai_status_info_t status;
  if (twai_get_status_info(&status) == ESP_OK) {
    Serial.println(F("--- TWAI Durum ---"));
    Serial.print(F("  TX hata sayaci: ")); Serial.println(status.tx_error_counter);
    Serial.print(F("  RX hata sayaci: ")); Serial.println(status.rx_error_counter);
    Serial.print(F("  TX kuyruktaki:  ")); Serial.println(status.msgs_to_tx);
    Serial.print(F("  RX kuyruktaki:  ")); Serial.println(status.msgs_to_rx);
    Serial.print(F("  Bus hata durumu: "));
    switch (status.state) {
      case TWAI_STATE_STOPPED:    Serial.println(F("DURDURULDU"));   break;
      case TWAI_STATE_RUNNING:    Serial.println(F("ÇALIŞIYOR"));    break;
      case TWAI_STATE_BUS_OFF:    Serial.println(F("BUS-OFF (kablo/terminatör sorun olabilir)")); break;
      case TWAI_STATE_RECOVERING: Serial.println(F("KURTARILIYOR")); break;
      default:                    Serial.println(F("BİLİNMİYOR"));   break;
    }
    Serial.println(F("------------------"));
  }
}

// ---------------------------------------------------------------------------
// setup()
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);  // Serial port açılması için bekle

  Serial.println(F("========================================"));
  Serial.println(F("  ESP32 TWAI CAN Sniffer"));
  Serial.println(F("  twai_sniffer.ino"));
  Serial.println(F("========================================"));
  Serial.print(F("Mod: "));
  Serial.println(LISTEN_ONLY_MODE ? F("LISTEN-ONLY (sadece dinle)") : F("NORMAL (gönderim aktif)"));
  Serial.println(F("CSV satirlari 'CSV,' ile baslar — Excel'e aktarirken filtrele."));
  Serial.println(F("Komut: send <id> <b0> <b1>...  Örnek: send 0x201 01 02 03"));
  Serial.println(F("----------------------------------------"));

  // CSV başlığını yazdır
  printCsvHeader();

  // TWAI sürücüsünü başlat
  Serial.println(F("TWAI baslatiliyor..."));
  if (!twaiInit()) {
    Serial.println(F("[KRİTİK] TWAI baslatma BAŞARISIZ. Cihaz dondu. Lütfen reset at."));
    while (true) { delay(1000); }  // Sonsuz döngü — reset gerekli
  }

  Serial.println(F("[OK] TWAI baslatildi. CAN bus dinleniyor..."));
  Serial.println(F("========================================"));
}

// ---------------------------------------------------------------------------
// loop()
// ---------------------------------------------------------------------------
void loop() {
  // --- CAN frame al (50ms timeout ile bekle) ---
  twai_message_t rxMsg;
  esp_err_t result = twai_receive(&rxMsg, pdMS_TO_TICKS(50));

  if (result == ESP_OK) {
    // Frame geldi — yazdır
    printFrame(rxMsg);
  } else if (result == ESP_ERR_TIMEOUT) {
    // Timeout: normal, frame gelmedi — sessiz devam et
  } else if (result == ESP_ERR_INVALID_STATE) {
    // Sürücü kapandı veya BUS-OFF durumu
    Serial.println(F("[UYARI] TWAI gecersiz durum. Bus-off veya driver durdu."));
    delay(500);
  } else {
    // Beklenmedik hata
    Serial.print(F("[HATA] twai_receive hata kodu: "));
    Serial.println(result);
    delay(100);
  }

  // --- Serial komutları işle ---
  handleSerialCommand();

  // --- Periyodik durum raporu ---
  printTwaiStatus();
}
