# Maker Mini Sumo

Firmware RC dan Web Serial configurator untuk Cytron Maker Mini Sumo
Controller.

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
- Simpanan alignment dalam EEPROM.\n- Buzzer feedback untuk power-on, alignment mode dan EEPROM save.

## Status Web Configurator

Versi pertama Web Serial configurator dan protokol firmware telah dibina.
Rujuk [`docs/PROJECT_PLAN.md`](docs/PROJECT_PLAN.md) untuk flow pengguna,
protokol, keselamatan dan milestone ujian hardware.
