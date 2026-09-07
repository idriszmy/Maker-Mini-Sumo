# Maker Mini Sumo Project Plan

Dokumen ini ialah Single Source of Truth untuk skop, keputusan dan status
projek Maker Mini Sumo.

## Matlamat

Menyediakan satu projek yang mengandungi:

1. Firmware RC untuk Maker Mini Sumo Controller.
2. Forward dan backward motor alignment yang disimpan dalam EEPROM.
3. Physical alignment menggunakan DIP switch, potentiometer dan START button.
4. Web Serial configurator sebagai pilihan alignment tambahan.

## Struktur repository

```text
Maker-Mini-Sumo/
├── Arduino/
│   └── MakerMiniSumo_RC/
│       └── MakerMiniSumo_RC.ino
├── WebSerialConfigurator/
│   ├── index.html
│   ├── styles.css
│   ├── app.js
│   └── README.md
├── docs/
│   └── PROJECT_PLAN.md
├── .gitignore
└── README.md
```

Canonical local repository yang dirancang:

```text
/Users/idris/Documents/GitHub/Maker-Mini-Sumo/
```

Fail di dalam repository ialah sumber utama. Sketch boleh dibuka terus melalui
`File > Open` dalam Arduino IDE; salinan kedua atau symbolic link dalam
`Documents/Arduino` tidak diperlukan.

## Status semasa

| Komponen | Status |
|---|---|
| Public GitHub repository | Siap |
| RC firmware | Siap dan lulus compile untuk Arduino Uno |
| Physical forward alignment | Siap dalam firmware |
| Physical backward alignment | Siap dalam firmware |
| EEPROM alignment storage | Siap dalam firmware |\n| Buzzer feedback | Siap dalam firmware |
| Web Serial firmware protocol | Siap, menunggu ujian board fizikal |
| WebUI | Versi pertama siap, menunggu ujian board fizikal |
| GitHub Pages deployment | Siap |
| Ujian pada robot fizikal | Belum dibuat |
| Pindah working copy ke `Documents/GitHub` | Siap dalam versi ini |

## RC firmware

Board sasaran ialah Maker Mini Sumo Controller berasaskan ATmega328P dan
Arduino Uno compatible. RC receiver menggunakan:

- `GPIO1` untuk throttle/speed.
- `GPIO2` untuk steering.

Kedua-dua signal RC ditangkap menggunakan pin-change interrupt `PCINT1`:

- `GPIO1 / A2 / PCINT10` untuk throttle/speed.
- `GPIO2 / A3 / PCINT11` untuk steering.
- ISR merekod masa rising edge, pulse width pada falling edge dan masa pulse
  terakhir.
- Main loop membaca snapshot atomic supaya data 16-bit dan 32-bit tidak berubah
  di tengah bacaan pada ATmega328P 8-bit.
- Tiada penggunaan `pulseIn()`, jadi loop tidak menunggu pulse RC secara
  blocking.

Firmware mesti mengekalkan failsafe: kedua-dua motor berhenti apabila salah
satu signal RC hilang, lebih lama daripada 30 ms, atau mempunyai pulse width
di luar julat sah `750-2250 us`.

### RC deadband

Deadband `0.1` digunakan pada kedua-dua channel dan output di-rescale selepas
deadband:

```text
Input -0.10 hingga +0.10 → Output 0
Input +0.10 hingga +1.00 → Output 0 hingga +1.00
Input -0.10 hingga -1.00 → Output 0 hingga -1.00
```

Ini mengelakkan motor PWM melompat terus ke sekitar 25 apabila stick baru
keluar daripada deadband. Alignment direction menggunakan tanda output
throttle selepas rescale, supaya forward dan backward trim terus aktif selepas
deadband.

## Physical alignment

### DIP modes

| DIP (SW1, SW2, SW3) | Nilai | Fungsi | Corak LED |
|---|---:|---|---|
| `LLL` / OFF-OFF-OFF | 0 | Normal RC | Status pergerakan |
| `LHL` / OFF-ON-OFF | 2 | Forward alignment | 1 flash dan 1 beep ketika masuk mode |
| `HLH` / ON-OFF-ON | 5 | Backward alignment | 2 flash dan 2 beep ketika masuk mode |

Semua kombinasi DIP selain dua alignment mode menggunakan nilai alignment
daripada EEPROM.

### Potentiometer trim

```text
Pot kiri             Pot tengah             Pot kanan
L=75%, R=100%        L=100%, R=100%         L=100%, R=75%
```

- Dalam forward alignment, potentiometer hanya memberi kesan ketika throttle
  forward. Gerakan backward menggunakan backward trim daripada EEPROM.
- Dalam backward alignment, potentiometer hanya memberi kesan ketika throttle
  backward. Gerakan forward menggunakan forward trim daripada EEPROM.
- Arah gerakan ditentukan daripada throttle sebelum steering mixing.
- Remote RC kekal sebagai satu-satunya kawalan pergerakan motor.

### Save alignment

1. Lepaskan throttle ke neutral.
2. Tekan dan tahan START selama 2 saat.
3. Motor berhenti semasa START ditekan.
4. Trim semasa disimpan ke EEPROM dengan `EEPROM.update()`.
5. LED berkelip laju sebagai pengesahan.
6. RC aktif semula selepas button dilepaskan dan save feedback selesai.

Alamat EEPROM `16-19` digunakan untuk data alignment supaya tidak bertindih
dengan alamat `0-3` yang digunakan oleh CytronMakerSumo untuk edge sensor.

## Web Serial configurator

### Sasaran browser

- Chrome atau Edge desktop.
- HTTPS diperlukan pada production; `localhost` digunakan semasa development.
- Arduino Serial Monitor mesti ditutup sebelum WebUI membuka port.
- Browser permission diberi oleh pengguna melalui device chooser.

### Connection UI

WebUI menyediakan:

- Dropdown senarai serial port yang pernah diberi permission.
- `Refresh` untuk memuat semula senarai port yang telah dibenarkan.
- Pilihan `Select new port...` untuk membuka browser device chooser.
- `Connect` dan `Disconnect`.
- Connection state dan mesej error yang mudah difahami.

Selepas sambungan berjaya:

1. Tunggu board selesai reset dan menghantar handshake.
2. Sahkan bahawa device ialah firmware Maker Mini Sumo yang disokong.
3. Baca forward dan backward trim daripada EEPROM.
4. Kemas kini slider dan bacaan kelajuan motor pada WebUI.

### Alignment sliders

WebUI mempunyai dua signed sliders:

- Forward alignment: `-25` hingga `+25`.
- Backward alignment: `-25` hingga `+25`.

Garisan di tengah menandakan nilai `0`:

```text
Reduce left             Centre             Reduce right
    -25  -----------------|-----------------  +25
```

Paparan di kiri dan kanan slider menunjukkan motor maximum speed sebenar:

- Nilai negatif mengurangkan motor kiri daripada 100% sehingga 75%.
- Nilai `0` menetapkan kedua-dua motor kepada 100%.
- Nilai positif mengurangkan motor kanan daripada 100% sehingga 75%.

Event slider:

- `input`: kemas kini visual sahaja semasa slider digerakkan.
- `change`: hantar nilai selepas slider dilepaskan atau disahkan melalui
  keyboard/touch.
- Firmware menghentikan motor seketika dan memainkan bunyi confirmation apabila\n  menerima arahan save daripada WebUI.\n- WebUI hanya memaparkan `Saved` selepas firmware memberi respons berjaya.

## Serial protocol

Versi pertama menggunakan arahan teks newline-delimited supaya ringan dan
mudah diuji melalui Serial Monitor.

```text
HELLO
GET CONFIG
SAVE F -12
SAVE B 8
```

Contoh respons:

```text
OK DEVICE=MAKER_MINI_SUMO VERSION=1
CONFIG F=-12 B=8
OK SAVED F=-12
OK SAVED B=8
ERROR INVALID_VALUE
```

Keperluan protokol:

- Terima trim hanya dalam range `-25` hingga `+25`.
- Abaikan arahan yang tidak lengkap atau tidak dikenali.
- EEPROM hanya ditulis untuk arahan `SAVE` yang sah.
- Setiap arahan menerima respons kejayaan atau error.
- Sambungan atau arahan Web Serial tidak boleh mematikan RC failsafe.
- WebUI tidak menyediakan arahan untuk menggerakkan motor.

## Development dan deployment

Workflow:

```text
Edit local -> Test local -> Commit -> Push -> Deploy GitHub Pages
```

WebUI dibina dahulu dalam `WebSerialConfigurator/` menggunakan HTML, CSS dan
JavaScript biasa. Selepas fungsi local stabil, GitHub Actions akan menerbitkan
folder tersebut ke GitHub Pages.

## Milestone seterusnya

1. Buka `Documents/GitHub/Maker-Mini-Sumo` sebagai Codex project.
2. Upload firmware terkini dan uji Serial Monitor pada board sebenar.
3. Sambungkan WebUI kepada board sebenar.
4. Uji read/save EEPROM dan reconnect.
5. Aktifkan deployment GitHub Pages.

## Perkara yang belum disahkan

- USB serial connection dengan board fizikal.
- Reset/handshake timing apabila browser membuka port.
- RC receiver dan Web Serial berjalan serentak tanpa latency bermasalah.
- Forward/backward alignment pada permukaan sebenar.
- EEPROM kekal betul selepas power cycle.
- Browser dan USB serial adapter yang akan digunakan oleh pengguna akhir.
