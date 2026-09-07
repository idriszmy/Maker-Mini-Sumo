# Web Serial Configurator

Aplikasi web untuk membaca, melaras dan menyimpan forward/backward motor
alignment pada Maker Mini Sumo melalui USB serial.

## Status

Versi pertama telah dilaksanakan menggunakan HTML, CSS dan JavaScript biasa.
Ia boleh digunakan dengan mock data atau disambungkan kepada firmware melalui
Web Serial dalam Chrome/Edge desktop.

Spesifikasi rasmi projek berada di
[`../docs/PROJECT_PLAN.md`](../docs/PROJECT_PLAN.md).

## Struktur yang dirancang

```text
WebSerialConfigurator/
├── index.html
├── styles.css
├── app.js
└── README.md
```

Tiada framework atau build dependency diperlukan.
