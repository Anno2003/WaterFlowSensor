## Pendahuluan
Berikut ini adalah dokumentasi terkait dengan water flow sensor sederhana. alat ini ditujukan untuk mengukur laju aliran atau debit air dalam satuan waktu tertentu.
Alat ini menggunakan *interrupt* untuk menghitung pulsa dari sensor  *water flow*, kemudian mengonversinya menjadi satuan **L/min** dan total dalam **liter**. Selain monitoring lokal melalui OLED dan web interface, alat ini juga mendukung integrasi IoT melalui MQTT.
## Fitur
- Pengukuran **debit air (L/min)** secara real-time
- Perhitungan **total volume air (liter)**
- Kalibrasi faktor sensor (calibration factor)
- Tampilan lokal menggunakan **OLED SSD1306 (128x64)**
- Web monitoring (tanpa internet, cukup WiFi lokal)
- Dilengkapi dengan mDNS untuk memudahkan akses
- Konfigurasi WiFi menggunakan WiFiManager (captive portal)
- Integrasi MQTT untuk monitoring jarak jauh
- Penyimpanan konfigurasi & total liter ke flash (Preferences)
- API endpoint `/api/status` untuk integrasi sistem lain
## Konstruksi Alat
### Komponen Utama
- ESP32 S3 
- Water Flow Sensor (tipe YF-S201)
- OLED 128x64 I2C (SSD1306)
- Converter AC to DC 5V (Hi-Link HLK-PM01)
### Skematik

**Koneksi utama:**

| Komponen           | ESP32 Pin |
| ------------------ | --------- |
| Flow Sensor Output | GPIO 4    |
| OLED SDA           | GPIO 8    |
| OLED SCL           | GPIO 9    |
| OLED VCC           | 3.3V      |
| OLED GND           | GND       |

Flow sensor menggunakan mode `INPUT_PULLUP` dan interrupt `FALLING` untuk mendeteksi pulsa.
`attachInterrupt(digitalPinToInterrupt(FLOW_PIN), flowISR, FALLING);`
Setiap pulsa akan menambah variabel `pulseCount`.
### Software
Firmware dari alat ini dibangun menggunakan Arduino Framework untuk ESP32 dengan library berikut:
- `WiFi.h`
- `WiFiManager.h`
- `AsyncMqtt_Generic.h`
- `ESPAsyncWebServer.h`
- `Preferences.h`
- `Adafruit_SSD1306.h`
- `RemoteDebug.h`
### Cara Kerja Perhitungan
Sensor flow menghasilkan pulsa yang proporsional terhadap debit air.
Rumus dasar:
Flow (L/min) = (pulses per second) / 7.5
Pada program:
lastFlowLmin = (pulsesCal * (1000.0 / measureInterval)) / 7.5;  
totalLiters += pulsesCal / 7.5;

Dimana:
- `measureInterval = 100 ms`
- `calibrationFactor` digunakan untuk koreksi hasil pembacaan sensor

Kalibrasi dilakukan dengan mengatur parameter:
`float calibrationFactor`

## Antarmuka Web
### Halaman Utama `/`
![[{185878CD-764A-4196-B11D-0FCCD5DE403A}.png|halaman utama]]
Menampilkan:
- Flow rate (L/min)
- Total liter
- Calibration factor
- Tombol reset total
- Link ke halaman settings
Halaman ini auto-refresh setiap 1 detik.

### Halaman Settings `/settings`
![[{81FE8B20-2B42-4CEE-937D-50A6F6751CAE}.png|Halaman setting]]
Digunakan untuk mengatur:
- MQTT Host
- MQTT Port
- MQTT Topic
Konfigurasi disimpan ke flash dan akan otomatis reconnect ke broker.

### API Endpoint `/api/status`
![[{8B90C28F-D5DC-4EFF-B373-FCE1888AF4F6}.png|API endpoint]]
Akah Mengirimkan JSON:
``` JSON
{  
  "flow": 1.25,  
  "total": 12.345,  
  "cal": 1.000000  
}
```

Endpoint ini bisa digunakan untuk:
- Integrasi dashboard
- Node-RED
- Home Assistant
- Sistem monitoring lainnya
## MQTT
![[{65C79A5F-133A-45F8-90EF-B76297B82651}.png|mqtt melalui client mqtt explorer]]
Default:
- Host: `broker.emqx.io`
- Port: `1883`
- Topic: `flow_sensor/value`
Payload yang dikirim:
``` JSON
{
	"rate":1.234,
	"total":5.678
}
```

Status online juga dikirim saat berhasil terkoneksi:
`FLOW_SENSOR online`
## Penyimpanan Data
Data yang disimpan di flash (NVS Preferences):
- MQTT Host
- MQTT Port
- MQTT Topic
- Total liter
- Calibration factor
Penyimpanan dilakukan setiap 5 detik untuk menjaga data tetap aman saat listrik mati.
## Penggunaan

### Pertama Kali Digunakan
1. Nyalakan perangkat
2. ESP32 akan membuat WiFi AP dengan nama:
	FLOW_SENSOR
3. Hubungkan ke AP tersebut
4. Buka captive portal
5. Masukkan konfigurasi WiFi dan MQTT
### Monitoring Lokal
- Lihat layar OLED untuk:
    - IP Address
    - Flow rate
    - Total liter
- Akses melalui browser:
	- http://ip.yang.didapat/ (melalui ip dari yang didapat oleh alat)
	- http://flow-sensor.local (melalui mDNS)
> ![warning] pastikan perangkat telah terhubung ke jaringan lokal yang sama

> ![warning] beberapa perangkat mungkin tidak secara langsung support akses mDNS
### Reset Total Liter
Klik tombol **Reset Total** pada halaman utama antarmuka web.
### Kalibrasi Sensor
Langkah kalibrasi:
1. Siapkan wadah dengan volume diketahui (misalnya 1 liter)
2. Jalankan air hingga wadah penuh
3. Catat total liter yang terbaca
4. Hitung faktor koreksi:
	calibrationFactor = volume_asli / volume_terbaca
5. Masukkan nilai tersebut ke parameter kalibrasi
6. Simpan dan uji ulang
## Catatan Teknis
- Interval pembacaan: 100 ms
- Web server berjalan asynchronous
- MQTT reconnect otomatis
- Aman dari blocking karena menggunakan `wm.setConfigPortalBlocking(false)`
