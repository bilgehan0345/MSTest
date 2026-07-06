# ESP32 CAN Bus Test Firmware

Motor sürücüsü (72V BLDC) ile CAN bus iletişimini test etmek için üç sketch.

---

## 📁 Dosyalar

| Sketch | Açıklama |
|---|---|
| `twai_sniffer/` | ESP32 dahili TWAI + TJA1050 transceiver |
| `mcp2515_sniffer/` | MCP2515 SPI modülü |
| `baud_scanner/` | Baud rate bilinmiyorsa otomatik tarar |

---

## 🔌 Neden Bu Kütüphaneler?

### TWAI için
- **Harici kütüphane gerekmez** — ESP32 Arduino core içinde gelen `driver/twai.h` kullanılır.
- Doğrudan Espressif'in kendi sürücüsü olduğu için en stabil çözüm.
- ESP-IDF ve Arduino framework'te aynı API çalışır.

### MCP2515 için
- **`autowp/autowp-mcp2515`** kütüphanesi önerilir.
- Seeed'in eski `mcp_can`'a göre avantajları:
  - Extended 29-bit ID tam destekli
  - `setListenOnlyMode()` native API'si var
  - Interrupt-driven RX desteği
  - Aktif bakım (2024+ commit'leri)

---

## 📦 Kütüphane Kurulumu

### Arduino IDE için:

1. `Tools` → `Manage Libraries` aç
2. **MCP2515 için:**
   - Arama: `mcp2515`
   - **"MCP2515 by autowp"** → `Install`
3. **ESP32 board kurulumu** (yoksa):
   - `Tools` → `Board` → `Board Manager`
   - Arama: `esp32`
   - **"esp32 by Espressif Systems"** → `Install` (v2.0.x veya üstü)

### PlatformIO için (`platformio.ini`):
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps =
    autowp/mcp2515 @ ^1.0.1
```

---

## 🔧 Donanım Bağlantıları

### TWAI + TJA1050 (twai_sniffer)

```
ESP32           TJA1050
------          -------
GPIO4  ------>  TXD
GPIO5  <------  RXD
3.3V   ------>  VCC  (TJA1050 3.3V toleranslıdır)
GND    ------>  GND

TJA1050         CAN Bus
-------         -------
CANH   <----->  CAN-H
CANL   <----->  CAN-L
```

> ⚠️ **120Ω terminatör:** CAN bus'ın her iki ucuna 120Ω direnç bağla.  
> Motor sürücüsünde zaten varsa sadece ESP32 ucuna tak.

### MCP2515 SPI (mcp2515_sniffer)

```
ESP32           MCP2515 Modülü
------          --------------
GPIO18 ------>  SCK
GPIO23 ------>  SI (MOSI)
GPIO19 <------  SO (MISO)
GPIO5  ------>  CS
GPIO4  <------  INT (isteğe bağlı, interrupt için)
3.3V   ------>  VCC  ⚠️ Modülün 3.3V kabul ettiğini doğrula!
GND    ------>  GND
```

> ⚠️ **Osilatör frekansı:** Modülündeki kristale bak. Çoğu Çin modülü **8 MHz**.  
> `MCP2515_OSCILLATOR_FREQ` değerini `MCP_8MHZ` veya `MCP_16MHZ` yap.

---

## 🚀 Kullanım

### 1. Önce Baud Rate Taraması (baud_scanner)

Baud rate bilmiyorsan:
1. `baud_scanner.ino`'yu yükle
2. Serial Monitor'ü aç (115200 baud)
3. Hangi baud rate'de frame geldiğini not al

### 2. Sniffer Çalıştır (twai_sniffer veya mcp2515_sniffer)

1. Doğru sketch'i aç
2. Üstteki `#define`'ları kendi pinlerine göre ayarla:
   ```cpp
   #define CAN_TX_PIN   GPIO_NUM_4   // TJA1050 TXD
   #define CAN_RX_PIN   GPIO_NUM_5   // TJA1050 RXD
   #define CAN_TIMING   TWAI_TIMING_CONFIG_500KBITS()
   #define LISTEN_ONLY_MODE  true    // Güvenli başlangıç
   #define CSV_OUTPUT_ENABLED  true  // Excel için
   ```
3. Yükle, Serial Monitor'ü 115200 baud'da aç

### 3. CSV Çıktısını Excel'e Aktarma

Serial Monitor çıktısını bir `.txt` dosyasına kopyala, sonra:
1. Excel → `Veri` → `Metinden/CSV'den`
2. Dosyayı seç
3. `CSV,` ile başlayan satırları filtrele (diğer satırları sil)
4. Virgülle ayır, tüm sütunlar hazır

**Alternatif:** Sadece CSV satırları almak için çıktıyı şöyle filtrele:
```
grep "^CSV," output.txt > canlog.csv
```

### 4. Test Frame Gönderme (send komutu)

`LISTEN_ONLY_MODE  false` yap, sonra Serial Monitor'den:

```
send 0x201 01 02 03 04
send 0x100 FF 00
send 0xE000 AA BB CC DD EE FF 00 11
```

> 💡 **Extended ID:** 0x7FF'den büyük ID'ler otomatik olarak 29-bit extended olarak gönderilir.

---

## 🔍 Serial Monitor Çıktısı Örneği

```
========================================
  ESP32 TWAI CAN Sniffer
========================================
Mod: LISTEN-ONLY (sadece dinle)
CSV satirlari 'CSV,' ile baslar
----------------------------------------
millis,id_hex,ide,dlc,byte0,byte1,...
[1234 ms] ID: 0xE000 (EXT)  DLC: 5  Data: 02 0E AA BB CC
CSV,1234,0xE000,EXT,5,02,0E,AA,BB,CC,--,--,--
[1250 ms] ID: 0x200 (STD)   DLC: 5  Data: 00 FF 00 00 01
CSV,1250,0x200,STD,5,00,FF,00,00,01,--,--,--

--- TWAI Durum --- (her 10 saniyede)
  TX hata sayaci: 0
  RX hata sayaci: 0
  Bus hata durumu: ÇALIŞIYOR
```

---

## ❗ Sorun Giderme

| Belirti | Muhtemel Sebep | Çözüm |
|---|---|---|
| `TWAI driver kurulamadi` | Pin çakışması veya GPIO sorunu | `CAN_TX_PIN` / `CAN_RX_PIN` başka GPIO dene |
| `TWAI baslatılamadi` | TJA1050 bağlantı sorunu | GND ortak, VCC kontrol et |
| Frame gelmiyor | Yanlış baud rate | `baud_scanner.ino`'yu çalıştır |
| Frame gelmiyor | 120Ω terminatör yok | Bus her iki ucuna 120Ω tak |
| `Bus-off` durumu | Baud rate uyuşmuyor veya gürültü | Baud rate dene, kablo kısalt |
| MCP2515 reset hatası | SPI bağlantı sorunu | SCK/MOSI/MISO/CS pinleri kontrol et |
| MCP2515 osilatör hatası | Kristal frekansı yanlış | Modüldeki kristale bak (8 veya 16 MHz) |

---

## 📌 Proje Notları

- Bu firmware mevcut `TUFAN-AKS` projesindeki CAN protokol tablosundan bağımsız çalışır.
- BMS (Lithium Balance c-BMS) frame'leri **29-bit Extended ID** kullanır (`0xE000` vb.) — her iki sketch de extended ID'yi destekler.
- Motor sürücüsü frame'leri **11-bit Standard ID** kullanır (`0x100`, `0x200`).
- Mevcut projeden bilinen baud rate: **500 kbps** (`CAN_TIMING_CONFIG_500KBITS()`).
