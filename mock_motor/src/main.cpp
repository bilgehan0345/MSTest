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

// ---------------------------------------------------------------------------
// ARAÇ VE HIZ HESAPLAMA AYARLARI (Kendinize göre değiştirebilirsiniz)
// ---------------------------------------------------------------------------
#define GEAR_RATIO 1.0            // Vites / Redüktör Oranı (Doğrudan tekerleğe bağlıysa 1.0)
#define WHEEL_RADIUS_M 0.30       // Tekerlek Yarıçapı (metre cinsinden, örn: 30cm = 0.30)
#define WHEEL_CIRCUMFERENCE (2.0 * 3.14159 * WHEEL_RADIUS_M) // Tekerlek Çevresi


struct can_frame rxMsg; // Alınan mesajlar için
struct can_frame txMsg; // Gönderilen mesajlar için
MCP2515 mcp2515(MCP_CS_PIN); 

unsigned long lastSendTime = 0;
unsigned long lastRpmUpdateTime = 0;

int rpm = 0; // Motorun anlık devri
int targetRpm = 0;  // Karşı taraftan gelen hedef devir

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
      int newTargetRpm = (rxMsg.data[0] << 8) | rxMsg.data[1];
      
      if (newTargetRpm != targetRpm) {
        targetRpm = newTargetRpm;
      }
    }
  }

  // 2. ADIM: Gerçekçilik katmak için motoru yavaş yavaş hedef RPM'e yaklaştır (Atalet Simülasyonu)
  // Her 50ms'de bir devri hedefe doğru ufak ufak artır/azalt. İsterseniz direkt "rpm = targetRpm;" de yapabilirsiniz.
  if (millis() - lastRpmUpdateTime > 50) {
    lastRpmUpdateTime = millis();
    if (rpm < targetRpm) rpm += 20;
    if (rpm > targetRpm) rpm -= 20;
    // Tam üzerine oturması için hassasiyet ayarı
    if (abs(rpm - targetRpm) <= 20) rpm = targetRpm;
  }

  // 3. ADIM: Her 500ms'de bir kendi anlık (gerçek) devrimizi karşı tarafa bildir.
  if (millis() - lastSendTime > 500) {
    lastSendTime = millis();

    txMsg.can_id  = 0x123; // Bizim durum mesajı ID'miz
    txMsg.can_dlc = 8;     // 8 Byte uzunluğunda veri

    // Verileri paketle
    txMsg.data[0] = (rpm >> 8) & 0xFF; // Anlık RPM (High Byte)
    txMsg.data[1] = rpm & 0xFF;        // Anlık RPM (Low Byte)
    txMsg.data[2] = 0x00;                     // Bos (Eskiden sicaklik vardi)
    txMsg.data[3] = 240;                      // Voltaj (Sabit 24.0V)
    txMsg.data[4] = 0x00; 
    txMsg.data[5] = 0x00;
    txMsg.data[6] = 0x00;
    // Durum: Motor dönüyorsa 1, duruyorsa 0
    txMsg.data[7] = (rpm > 0) ? 0x01 : 0x00; 

    MCP2515::ERROR err = mcp2515.sendMessage(&txMsg);

    if (err == MCP2515::ERROR_OK) {
      static int lastPrintedRpm = -1;
      
      // Sadece RPM'de bir değişiklik varsa ekrana yazdır (Spam'i önler)
      if (rpm != lastPrintedRpm) {
        // Hız (km/h) = (RPM / Dişli Oranı) * Tekerlek Çevresi(m) * 60(dk) / 1000(m)
        int speed = (rpm / GEAR_RATIO) * WHEEL_CIRCUMFERENCE * 60.0 / 1000.0;
        
        Serial.printf("CAN TX -> ID: 0x%X | Hedef RPM: %d | Gercek RPM: %d | Hiz: %d km/h\n", 
                      txMsg.can_id, targetRpm, rpm, speed);
        lastPrintedRpm = rpm;
      }
    } else {
      static unsigned long lastErrorTime = 0;
      if (millis() - lastErrorTime > 2000) { // Hataları 2 saniyede bir yazdır
        Serial.printf("[HATA] Gonderim basarisiz! (err=%d)\n", err);
        lastErrorTime = millis();
      }
    }
  }
}
