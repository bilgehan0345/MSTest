// =============================================================================
// mock_motor/src/main.cpp
// Sahte (Mock) Motor Sürücü Simülasyonu
// CAN hattına periyodik olarak RPM, sıcaklık, voltaj ve durum bilgisi basar.
// =============================================================================

#include <Arduino.h>
#include <SPI.h>
#include <mcp2515.h>

// ---------------------------------------------------------------------------
// PIN TANIMLARI (Standart VSPI + Seçili CS/INT)
// ---------------------------------------------------------------------------
#define MCP_CS_PIN   5
#define MCP_INT_PIN  4 // Sizin devrenizde kullandığınız INT pini

// ---------------------------------------------------------------------------
// CAN AYARLARI
// ---------------------------------------------------------------------------
// Kristal 16 MHz ise alttaki satırı MCP_16MHZ olarak değiştirin!
#define MCP2515_OSCILLATOR_FREQ  MCP_8MHZ 
#define CAN_BAUD_RATE  CAN_500KBPS

struct can_frame canMsg;
MCP2515 mcp2515(MCP_CS_PIN); 

unsigned long lastSendTime = 0;
int fakeRPM = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("========================================");
  Serial.println("  ESP32 Sahte (Mock) Motor Sürücü");
  Serial.println("========================================");

  // Standart ESP32 VSPI pinlerini başlatır (SCK:18, MISO:19, MOSI:23, CS:5)
  SPI.begin(); 
  
  if (mcp2515.reset() != MCP2515::ERROR_OK) {
    Serial.println("[HATA] MCP2515 reset basarisiz. SPI baglantilarini kontrol et.");
    while(1) { delay(1000); }
  }
  
  if (mcp2515.setBitrate(CAN_BAUD_RATE, MCP2515_OSCILLATOR_FREQ) != MCP2515::ERROR_OK) {
    Serial.println("[HATA] Baud rate ayarlanamadi. Kristal hizini kontrol et.");
    while(1) { delay(1000); }
  }
  
  mcp2515.setNormalMode(); // Veri gönderip alabileceğimiz normal moda geç
  
  Serial.println("[OK] MCP2515 Baslatildi. Sahte veriler gonderilmeye basliyor...");
}

void loop() {
  // Her 500ms'de bir sahte verileri CAN hattına gönder
  if (millis() - lastSendTime > 500) {
    lastSendTime = millis();

    // Sahte verileri simüle et (RPM artsın)
    fakeRPM += 150; 
    if (fakeRPM > 3000) fakeRPM = 0;

    // Mesaj ID ve uzunluğunu belirle
    canMsg.can_id  = 0x123; // Örnek Motor Durum ID'si
    canMsg.can_dlc = 8;     // 8 Byte uzunluğunda veri

    // Verileri paketle
    canMsg.data[0] = (fakeRPM >> 8) & 0xFF; // RPM (High Byte)
    canMsg.data[1] = fakeRPM & 0xFF;        // RPM (Low Byte)
    canMsg.data[2] = 45;                    // Motor Sıcaklığı (Örn: 45 derece)
    canMsg.data[3] = 240;                   // Voltaj (Örn: 24.0V -> 240)
    canMsg.data[4] = 0x00; 
    canMsg.data[5] = 0x00;
    canMsg.data[6] = 0x00;
    canMsg.data[7] = 0x01; // Durum: 1 (Motor çalışıyor)

    // Mesajı hatta gönder
    MCP2515::ERROR err = mcp2515.sendMessage(&canMsg);

    if (err == MCP2515::ERROR_OK) {
      Serial.printf("CAN TX -> ID: 0x%X | RPM: %d | Sicaklik: %dC | Voltaj: 24.0V\n", canMsg.can_id, fakeRPM, canMsg.data[2]);
    } else {
      Serial.printf("[HATA] Gonderim basarisiz! Hata Kodu: %d (Kablolari/Terminatoru kontrol et)\n", err);
    }
  }
}
