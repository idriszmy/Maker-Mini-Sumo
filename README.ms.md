# Maker Mini Sumo

**Bahasa Melayu** | [English](README.md)

Firmware RC dan Web Serial configurator untuk Cytron Maker Mini Sumo
Controller.


## Firmware AutoRC dan configurator sembilan halaman

`Arduino/MakerMiniSumo_AutoRC/MakerMiniSumo_AutoRC.ino` menyediakan strategi Auto
(DIP 0–6), defense pada HLH dan RC menggunakan interrupt pada HHH. WebUI
memaparkan firmware/versi, live sensor, alignment, Auto Routine dan strategi
lima baris. HLH menyediakan bilangan ulangan maju dan exit awal apabila lawan dikesan.

AutoRC 1.1.0 mengumpulkan tuning kepada Search, Backoff dan Attack. Edge
sensitivity menggunakan potentiometer dan bacaan permukaan pertama ketika run
bermula. WebUI 1.1.0 memaparkan live sensor tanpa nilai state dalaman.

AutoRC 1.1.1 menunggu input START/IR stabil ketika power-on dan mesti membaca
keadaan idle sebelum menerima arahan mula.

Button START dan IR berkongsi D2: lepaskan button / pastikan IR pada STOP ketika
power-on atau reset. AutoRC mengunci motor sepanjang konfigurasi WebUI; simpan,
disconnect USB dan reset sebelum menguji gerakan. Buka sketch terus daripada
repository ini. Rujuk [spesifikasi projek](docs/PROJECT_PLAN.md#autorc-100--implementation-september-2026)
untuk protokol, default, tuning dan ujian fizikal yang masih diperlukan.

## Open WebUI

[Open Maker Mini Sumo Web Serial Configurator](https://idriszmy.github.io/Maker-Mini-Sumo/)

Gunakan Chrome atau Edge pada komputer yang disambungkan ke board melalui USB.

## Struktur projek

- `Arduino/MakerMiniSumo_RC/` — firmware Arduino untuk kawalan RC dan motor alignment.
- `WebSerialConfigurator/` — aplikasi web untuk konfigurasi melalui Web
  Serial.
- `docs/PROJECT_PLAN.md` — spesifikasi dan status rasmi projek.

## Firmware

Board sasaran: Maker Mini Sumo Controller (ATmega328P / Arduino Uno compatible).

Firmware semasa menyediakan:

- Kawalan RC throttle dan steering menggunakan pin-change interrupt.
- Failsafe apabila signal RC hilang.
- Forward dan backward motor alignment yang berasingan.
- Live alignment menggunakan DIP switch dan potentiometer.
- Simpanan alignment dalam EEPROM.
- Buzzer feedback untuk power-on, alignment mode dan EEPROM save.

## Motor Alignment Guide

Alignment mengimbangkan kelajuan maksimum motor kiri dan kanan supaya robot
bergerak lebih lurus. Forward dan backward mempunyai nilai alignment yang
berasingan.

> Pastikan kawasan ujian selamat dan lepaskan throttle ke neutral sebelum
> menukar DIP switch atau menyimpan alignment.

### Cara 1: DIP switch dan potentiometer

Motor tidak bergerak secara automatik dalam alignment mode. Gunakan remote RC
untuk menggerakkan robot semasa membuat pelarasan.

#### Forward alignment

1. Lepaskan throttle ke neutral.
2. Tetapkan DIP switch kepada `LHL` / OFF-ON-OFF.
3. LED akan memberi 1 flash berulang dan buzzer berbunyi 1 kali.
4. Gerakkan robot ke hadapan menggunakan remote RC.
5. Laraskan potentiometer sehingga robot bergerak lurus:
   - Tengah: motor kiri 100%, motor kanan 100%.
   - Pusing ke kiri: kurangkan motor kiri sehingga minimum 75%.
   - Pusing ke kanan: kurangkan motor kanan sehingga minimum 75%.
6. Ulangi gerakan forward dan pelarasan sehingga alignment memuaskan.
7. Lepaskan throttle ke neutral, kemudian tekan dan tahan START selama 2 saat.
8. LED berkelip laju dan buzzer memainkan bunyi confirmation apabila nilai
   forward berjaya disimpan ke EEPROM.

Dalam mode ini, potentiometer hanya memberi kesan pada gerakan forward.
Gerakan backward masih menggunakan backward alignment daripada EEPROM.

#### Backward alignment

1. Lepaskan throttle ke neutral.
2. Tetapkan DIP switch kepada `HLH` / ON-OFF-ON.
3. LED akan memberi 2 flash berulang dan buzzer berbunyi 2 kali.
4. Gerakkan robot ke belakang menggunakan remote RC.
5. Laraskan potentiometer sehingga robot bergerak lurus:
   - Tengah: motor kiri 100%, motor kanan 100%.
   - Pusing ke kiri: kurangkan motor kiri sehingga minimum 75%.
   - Pusing ke kanan: kurangkan motor kanan sehingga minimum 75%.
6. Ulangi gerakan backward dan pelarasan sehingga alignment memuaskan.
7. Lepaskan throttle ke neutral, kemudian tekan dan tahan START selama 2 saat.
8. LED berkelip laju dan buzzer memainkan bunyi confirmation apabila nilai
   backward berjaya disimpan ke EEPROM.

Dalam mode ini, potentiometer hanya memberi kesan pada gerakan backward.
Gerakan forward masih menggunakan forward alignment daripada EEPROM.

Selepas selesai, tetapkan DIP switch kembali kepada `LLL` / OFF-OFF-OFF untuk
Normal RC mode. Firmware akan menggunakan kedua-dua nilai alignment yang telah
disimpan dalam EEPROM.

### Cara 2: WebUI

1. Upload firmware terkini ke Maker Mini Sumo Controller.
2. Tetapkan DIP switch kepada `LLL` / OFF-OFF-OFF.
3. Sambungkan board ke laptop menggunakan USB data cable.
4. Tutup Arduino Serial Monitor dan mana-mana aplikasi lain yang menggunakan
   serial port tersebut.
5. Buka [Maker Mini Sumo Web Serial Configurator](https://idriszmy.github.io/Maker-Mini-Sumo/)
   menggunakan Chrome atau Edge desktop.
6. Pilih `Select new port...`, pilih serial port board, kemudian tekan
   `Connect`.
7. Selepas tersambung, WebUI membaca forward dan backward alignment daripada
   EEPROM dan mengemas kini kedua-dua slider.
8. Laraskan slider yang diperlukan:
   - Nilai negatif hingga `-25`: kurangkan motor kiri sehingga 75%.
   - Nilai `0`: kedua-dua motor pada 100%.
   - Nilai positif hingga `+25`: kurangkan motor kanan sehingga 75%.
9. Lepaskan slider untuk menyimpan nilai arah tersebut terus ke EEPROM.
10. Tunggu bunyi confirmation daripada buzzer dan status `Saved` pada WebUI.
11. Uji robot menggunakan remote RC dan ulangi pelarasan jika perlu.

Untuk ujian bergerak, pastikan kabel USB tidak mengganggu robot. Pilihan paling
selamat ialah simpan nilai, disconnect WebUI dan kabel USB, kemudian uji robot.
Sambungkan semula jika pelarasan tambahan diperlukan.

## Status Web Configurator

Versi pertama Web Serial configurator dan protokol firmware telah dibina.
Rujuk [`docs/PROJECT_PLAN.md`](docs/PROJECT_PLAN.md) untuk spesifikasi,
protokol, keselamatan dan milestone ujian hardware.
