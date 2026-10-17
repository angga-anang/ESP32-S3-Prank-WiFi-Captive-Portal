# ESP32-S3 Prank WiFi Captive Portal

Proyek firmware ESP32-S3 untuk membuat Access Point (Wi-Fi) palsu dengan fitur **Captive Portal**. Ketika korban terhubung ke Wi-Fi dan membuka halaman browser/portal, audio prank akan diputar di perangkat korban dan memunculkan pesan kejutan. 

Dilengkapi dengan **Panel Admin Web** lengkap untuk mengganti nama Wi-Fi (SSID), upload audio kustom, mengatur volume, hingga fitur auto-kick klien.

---

## Fitur Utama

- **Otomatis Captive Portal**: DNS Server otomatis mengalihkan semua request browsing korban langsung ke halaman prank portal (`192.168.4.1`).
- **Audio Prank di Browser**: Audio diputar langsung melalui browser HP/laptop korban menggunakan HTML5 Audio.
- **Panel Admin Lengkap (`/admin`)**:
  - Ganti nama Wi-Fi (SSID) dan Password AP (bisa Open atau WPA2).
  - Ganti kata sandi login Admin.
  - Atur **Auto-Kick** (otomatis putus koneksi semua klien setiap X detik).
  - Atur **Volume** (0 - 100%) dan **Reveal Delay** (waktu jeda sebelum audio/pesan prank muncul).
  - **Upload Audio Baru**: Bisa upload file audio (`.wav`, `.mp3`) langsung dari panel web tanpa perlu flash ulang!
  - Informasi status memori, uptime, dan restart ESP32 dari jarak jauh.
- **Penyimpanan NVS & LittleFS**: Pengaturan disimpan permanen di Non-Volatile Storage (NVS) dan file audio disimpan di sistem file LittleFS.

---

## Spesifikasi Hardware

- **Board**: ESP32-S3 (ESP32-S3-DevKitC-1 atau board ESP32-S3 lainnya)
- **Flash Size**: 16MB (Quad SPI)
- **Partisi**: Custom partition (`partitions.csv`) dengan partisi aplikasi besar (~8MB) dan partisi LittleFS (~8MB).

---

## Struktur Folder

```text
PRANK-WIFI/
├── .gitignore          # File ignore Git untuk PlatformIO & dump binary
├── partitions.csv      # Skema tabel partisi (Factory App + LittleFS)
├── platformio.ini      # Konfigurasi build PlatformIO ESP32-S3
├── README.md           # Dokumentasi proyek
├── data/               # Direktori data LittleFS (di-upload via uploadfs)
│   └── prank.wav       # File audio prank bawaan
├── src/
│   └── main.cpp        # Kode sumber C++ utama (Web server, DNS, logic)
└── flash_dump/         # (Opsional) File dump binary mentah asli dari flash
```

---

## Persiapan Lingkungan (Prerequisites)

1. **Install VS Code** dan pasang ekstensi **PlatformIO IDE**.
   - Atau bisa menggunakan PlatformIO Core (CLI) melalui Terminal/Command Prompt.
2. Pasang driver USB yang sesuai jika board menggunakan bridge USB-to-UART (CH340 / CP2102), atau gunakan native USB ESP32-S3.
3. Hubungkan board ESP32-S3 ke komputer menggunakan kabel data USB.

---

## Panduan Build & Flash

Proyek ini memerlukan **2 tahap flash**:
1. **Flash Firmware** (kode program `main.cpp`)
2. **Flash Filesystem** (file audio `prank.wav` di folder `data/`)

### 1. Sesuaikan Port COM (Jika Perlu)
Buka file `platformio.ini`, sesuaikan baris `upload_port` dan `monitor_port` dengan COM port ESP32-S3 Anda (contoh: `COM5` di Windows atau `/dev/ttyUSB0` di Linux/Mac):
```ini
upload_port = COM5
monitor_port = COM5
```
*(Bisa juga dihapus baris upload_port-nya agar PlatformIO mendeteksi port secara otomatis).*

---

### 2. Cara Flash Menggunakan PlatformIO IDE (VS Code GUI)

#### Langkah A: Flash Program
1. Klik icon **PlatformIO** (icon kepala semut) di sidebar kiri VS Code.
2. Buka dropdown **`esp32-s3`** -> **General**.
3. Klik **Build** untuk memastikan tidak ada error.
4. Klik **Upload** untuk mengunggah firmware ke ESP32-S3.

#### Langkah B: Flash Audio / Filesystem (PENTING!)
1. Pada menu **`esp32-s3`**, buka bagian **Platform**.
2. Klik **Upload Filesystem Image** (`uploadfs`).
3. Tunggu hingga proses upload LittleFS selesai. Langkah ini wajib agar file `prank.wav` tersimpan di flash ESP32.

---

### 3. Cara Flash Menggunakan Terminal (PlatformIO CLI)

Jalankan perintah berikut di terminal root proyek:

```bash
# 1. Build & Upload Firmware
pio run --target upload

# 2. Upload Filesystem LittleFS (Audio)
pio run --target uploadfs

# 3. Buka Serial Monitor (Opsional untuk cek log)
pio device monitor
```

---

## Cara Penggunaan

1. Nyalakan ESP32-S3.
2. Hubungkan HP atau Laptop ke jaringan Wi-Fi baru:
   - **Default SSID**: `<Wifi-Gratis`
   - **Password**: *(Tidak ada / Open Wi-Fi)*
3. Biasanya notifikasi *Sign in to Wi-Fi network* (Captive Portal) akan otomatis muncul di HP.
4. Jika tidak muncul otomatis, buka browser dan ketik alamat:
   ```
   http://192.168.4.1
   ```
5. Korban akan melihat portal login/internet gratis. Saat menekan tombol **Lanjutkan**, audio prank akan berputar dan pesan **"ANDA KENA PRANK!"** akan tampil!

---

## Mengakses Panel Admin

Untuk mengubah konfigurasi:
1. Hubungkan perangkat Anda ke Wi-Fi ESP32.
2. Buka browser dan kunjungi:
   ```
   http://192.168.4.1/admin
   ```
3. Masukkan password admin default:
   - **Password**: `admin`
4. Di panel ini Anda dapat:
   - Mengganti SSID dan Password Wi-Fi
   - Mengganti Password Admin
   - Mengubah durasi auto-kick dan volume audio
   - Meng-upload file audio baru pengganti suara prank
   - Melakukan restart ESP32

---

## Troubleshooting

- **ESP32 Gagal Masuk Flash Mode**:
  Tahan tombol **BOOT**, tekan tombol **RESET** 1x, lalu lepaskan tombol BOOT sebelum memulai proses upload.
- **Audio Tidak Berbunyi di Browser**:
  Pastikan Anda telah menjalankan perintah **Upload Filesystem Image** (`uploadfs`). Jika belum, file `prank.wav` tidak ada di flash.
- **Port COM Berubah**:
  Cek Device Manager (Windows) untuk memastikan nomor port COM ESP32 Anda.

---

## Lisensi
Proyek ini dibuat untuk tujuan edukasi, pengujian jaringan captive portal, dan hiburan semata. Gunakan secara bijak!
