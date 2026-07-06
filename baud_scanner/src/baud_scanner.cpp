// =============================================================================
// baud_scanner.cpp
// ESP32 TWAI — Otomatik Baud Rate Tarayıcı
//
// Baud rate'i bilmiyorsan bu sketch'i çalıştır:
//   Her baud rate'i 3 saniye dener, frame gelirse buldu demektir.
//   Denenen baud rate'ler: 250 kbps, 500 kbps, 1 Mbps, 125 kbps
//
// Donanım bağlantısı (TJA1050 transceiver ile):
//   ESP32 GPIO4  --> TJA1050 TXD
//   ESP32 GPIO5  --> TJA1050 RXD
//   TJA1050 CANH --> CAN-H hattı
//   TJA1050 CANL --> CAN-L hattı
// =============================================================================

#include <Arduino.h>
#include "driver/twai.h"

// ---------------------------------------------------------------------------
// PIN TANIMLARI — buradan değiştir
// ---------------------------------------------------------------------------
#define CAN_TX_PIN  GPIO_NUM_4
#define CAN_RX_PIN  GPIO_NUM_5

// Her baud rate için deneme süresi (milisaniye)
#define TEST_DURATION_MS  3000

// ---------------------------------------------------------------------------
// Denenecek tek baud rate: 500 kbps
// ---------------------------------------------------------------------------
struct BaudEntry {
  const char* name;
  twai_timing_config_t timing;
};

static twai_timing_config_t t_500k = TWAI_TIMING_CONFIG_500KBITS();

BaudEntry baudList[] = {
  { "500 kbps",  t_500k },
};
const int BAUD_COUNT = sizeof(baudList) / sizeof(baudList[0]);

// ---------------------------------------------------------------------------
// Belirli bir baud rate ile TWAI başlat
// ---------------------------------------------------------------------------
bool startTwai(twai_timing_config_t &t_config) {
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN,
                                                                 TWAI_MODE_LISTEN_ONLY);
  twai_filter_config_t  f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g_config, &t_config, &f_config) != ESP_OK) return false;
  if (twai_start() != ESP_OK) {
    twai_driver_uninstall();
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// TWAI durdur ve kaldır
// ---------------------------------------------------------------------------
void stopTwai() {
  twai_stop();
  twai_driver_uninstall();
  delay(100);  // Sürücünün tamamen kapanması için bekle
}

// ---------------------------------------------------------------------------
// Belirtilen sürede frame gelip gelmediğini kontrol et
// Gelen frame sayısını döndürür
// ---------------------------------------------------------------------------
int tryBaud(twai_timing_config_t &t_config, unsigned long durationMs) {
  if (!startTwai(t_config)) return -1;  // -1 = başlatma hatası

  int frameCount = 0;
  unsigned long start = millis();

  while (millis() - start < durationMs) {
    twai_message_t msg;
    if (twai_receive(&msg, pdMS_TO_TICKS(50)) == ESP_OK) {
      frameCount++;
      // İlk frame'i yazdır (baud rate bulundu sinyali)
      if (frameCount == 1) {
        Serial.print(F("    -> İlk frame: ID=0x"));
        Serial.print(msg.identifier, HEX);
        Serial.print(F(" DLC="));
        Serial.println(msg.data_length_code);
      }
    }
  }

  stopTwai();
  return frameCount;
}

// ---------------------------------------------------------------------------
// setup() — sürekli tarama hazırlığı
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println(F("============================================="));
  Serial.println(F("  ESP32 TWAI Otomatik Baud Rate Tarayici"));
  Serial.println(F("  baud_scanner.cpp"));
  Serial.println(F("============================================="));
  Serial.print(F("Sadece 500 kbps denenecek, her deneme "));
  Serial.print(TEST_DURATION_MS / 1000);
  Serial.println(F(" saniye sürecek."));
  Serial.println(F("---------------------------------------------"));
}

// ---------------------------------------------------------------------------
// loop() — sürekli tarama
// ---------------------------------------------------------------------------
void loop() {
  for (int i = 0; i < BAUD_COUNT; i++) {
    Serial.print(F("Deneniyor: "));
    Serial.print(baudList[i].name);
    Serial.print(F(" ... "));

    int frames = tryBaud(baudList[i].timing, TEST_DURATION_MS);

    if (frames < 0) {
      Serial.println(F("[HATA] Baslatma basarisiz"));
    } else if (frames == 0) {
      Serial.println(F("frame YOK"));
    } else {
      Serial.print(frames);
      Serial.println(F(" frame alindi!"));
    }
  }

  Serial.println(F("---------------------------------------------"));
  Serial.println(F("Yeni döngü 2 saniye sonra başlıyor..."));
  Serial.println(F("---------------------------------------------"));
  delay(2000);
}
