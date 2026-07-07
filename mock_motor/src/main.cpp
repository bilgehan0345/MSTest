// =============================================================================
// mock_motor/src/main.cpp
// Akıllı (Mock) Motor Sürücü Simülasyonu
// Karşı taraftan (0x123 ID) gelen hedef devir komutunu okur, motor devrini
// o hıza eşitler ve kendi anlık durumunu CAN hattına bildirir.
// =============================================================================

#include <Arduino.h>
#include <SPI.h>
#include <mcp2515.h>

// ---------------------------------------------------------------------------
// PIN TANIMLARI
// ---------------------------------------------------------------------------
#define MCP_CS_PIN   5
#define MCP_INT_PIN  4 

// ---------------------------------------------------------------------------
// CAN AYARLARI
// ---------------------------------------------------------------------------
#define MCP2515_OSCILLATOR_FREQ  MCP_8MHZ 
#define CAN_BAUD_RATE  CAN_500KBPS

struct can_frame rxMsg; // Alınan mesajlar için
struct can_frame txMsg; // Gönderilen mesajlar için
MCP2515 mcp2515(MCP_CS_PIN); 

unsigned long lastSendTime = 0;
unsigned long lastRpmUpdateTime = 0;

int currentRPM = 0; // Motorun anlık devri
int targetRPM = 0;  // Karşı taraftan gelen hedef devir

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("========================================");
  Serial.println("  ESP32 Akilli (Mock) Motor Sürücü");
  Serial.println("========================================");

  SPI.begin(); 
  
  if (mcp2515.reset() != MCP2515::ERROR_OK) {
    Serial.println("[HATA] MCP2515 reset basarisiz.");
    while(1) { delay(1000); }
  }
  
  if (mcp2515.setBitrate(CAN_BAUD_RATE, MCP2515_OSCILLATOR_FREQ) != MCP2515::ERROR_OK) {
    Serial.println("[HATA] Baud rate ayarlanamadi.");
    while(1) { delay(1000); }
  }
  
  mcp2515.setNormalMode(); 
  Serial.println("[OK] MCP2515 Baslatildi. Karsi taraftan komut bekleniyor...");
}

void loop() {
  // 1. ADIM: Karşı tarafı dinle (Gelen komutları oku)
  if (mcp2515.readMessage(&rxMsg) == MCP2515::ERROR_OK) {
    // ID 0x123 ise, komut gelmiş demektir.
    if (rxMsg.can_id == 0x123) { 
      // Gelen 8 byte'ın ilk iki byte'ını (data[0] ve data[1]) RPM olarak birleştiriyoruz.
      targetRPM = (rxMsg.data[0] << 8) | rxMsg.data[1];
      Serial.printf(">>> [KOMUT ALINDI] Hedef RPM: %d olarak guncellendi.\n", targetRPM);
    }
  }

  // 2. ADIM: Gerçekçilik katmak için motoru yavaş yavaş hedef RPM'e yaklaştır (Atalet Simülasyonu)
  // Her 50ms'de bir devri hedefe doğru ufak ufak artır/azalt. İsterseniz direkt "currentRPM = targetRPM;" de yapabilirsiniz.
  if (millis() - lastRpmUpdateTime > 50) {
    lastRpmUpdateTime = millis();
    if (currentRPM < targetRPM) currentRPM += 20;
    if (currentRPM > targetRPM) currentRPM -= 20;
    // Tam üzerine oturması için hassasiyet ayarı
    if (abs(currentRPM - targetRPM) <= 20) currentRPM = targetRPM;
  }

  // 3. ADIM: Her 500ms'de bir kendi anlık (gerçek) devrimizi karşı tarafa bildir.
  if (millis() - lastSendTime > 500) {
    lastSendTime = millis();

    txMsg.can_id  = 0x123; // Bizim durum mesajı ID'miz
    txMsg.can_dlc = 8;     // 8 Byte uzunluğunda veri

    // Verileri paketle
    txMsg.data[0] = (currentRPM >> 8) & 0xFF; // Anlık RPM (High Byte)
    txMsg.data[1] = currentRPM & 0xFF;        // Anlık RPM (Low Byte)
    txMsg.data[2] = 45;                       // Motor Sıcaklığı (Sabit 45)
    txMsg.data[3] = 240;                      // Voltaj (Sabit 24.0V)
    txMsg.data[4] = 0x00; 
    txMsg.data[5] = 0x00;
    txMsg.data[6] = 0x00;
    // Durum: Motor dönüyorsa 1, duruyorsa 0
    txMsg.data[7] = (currentRPM > 0) ? 0x01 : 0x00; 

    MCP2515::ERROR err = mcp2515.sendMessage(&txMsg);

    if (err == MCP2515::ERROR_OK) {
      Serial.printf("CAN TX -> ID: 0x%X | Hedef RPM: %d | Gercek RPM: %d | Sicaklik: %dC\n", txMsg.can_id, targetRPM, currentRPM, txMsg.data[2]);
    } else {
      Serial.printf("[HATA] Gonderim basarisiz! (err=%d)\n", err);
    }
  }
}
