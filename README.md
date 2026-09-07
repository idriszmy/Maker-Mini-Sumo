# Maker Mini Sumo

Firmware RC dan Web Serial configurator untuk Cytron Maker Mini Sumo Controller.

## Struktur projek

- `Arduino/MakerMiniSumo_RC/` — firmware Arduino untuk kawalan RC dan motor alignment.
- `WebSerialConfigurator/` — aplikasi web untuk konfigurasi melalui Web Serial.

## Firmware

Board sasaran: Maker Mini Sumo Controller (ATmega328P / Arduino Uno compatible).

Firmware semasa menyediakan:

- Kawalan RC throttle dan steering.
- Failsafe apabila signal RC hilang.
- Forward dan backward motor alignment yang berasingan.
- Live alignment menggunakan DIP switch dan potentiometer.
- Simpanan alignment dalam EEPROM.

## Status Web Configurator

Web Serial configurator masih dalam peringkat perancangan. Protokol firmware dan UI akan dibangunkan secara berperingkat.

