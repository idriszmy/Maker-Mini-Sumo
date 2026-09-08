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
| EEPROM alignment storage | Siap dalam firmware |
| Buzzer feedback | Siap dalam firmware |
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
- Firmware menghentikan motor seketika dan memainkan bunyi confirmation apabila
  menerima arahan save daripada WebUI.
- WebUI hanya memaparkan `Saved` selepas firmware memberi respons berjaya.

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

## AutoRC 1.0.0 — implementation September 2026

The repository now also contains `Arduino/MakerMiniSumo_AutoRC/`.
RC-only firmware is version 1.1.0; its driving/alignment behaviour is unchanged,
with firmware identification and requested sensor telemetry added.

### Mode and start input

AutoRC reads DIP at boot: 0–6 select Auto opening strategies; 7 selects RC.
Change DIP, then reset to select another mode. START button and IR start share
D2 (`START` in CytronMakerSumo). A stable initial HIGH selects active-low button;
a stable initial LOW selects active-high IR. Detection requires 50 ms stability.
Release START / keep IR at STOP during power-on or reset, including USB reset.
An already-active IR signal cannot be distinguished from an idle button.
Button start has a 5-second countdown. IR start begins immediately after input
identification; IR LOW stops and latches until reset in every Auto motion state.

RC uses the original pin-change interrupt, 30 ms timeout and rescaled deadband.
AutoRC does not use the physical potentiometer alignment modes. RC throttle-based
alignment is preserved; Auto applies each motor's forward or backward trim to its
own commanded direction, including opposite-direction turns.

### Nine WebUI pages

1. Utama: port connection, firmware/version, optional 4 Hz sensor polling and alignment.
2. Auto behaviour: independent signed search speeds, attack initial/max speed and
   ramp duration, reverse/turn/pause settings and edge threshold coefficient.
3. Strategy LLL (0).
4. Strategy LLH (1).
5. Strategy LHL (2).
6. Strategy LHH (3).
7. Strategy HLL (4).
8. Strategy HLH (5): defense.
9. Strategy HHL (6).

Normal strategies contain five fixed rows: enabled, left %, right %, duration ms.
Disabled rows are skipped; all disabled goes directly to search/attack. The whole
strategy is validated before a save is applied. Durations: 1–10000 ms; motor
speeds: -100–100%. Defaults reproduce Ikedo opening motor commands for 1–4 and 6;
LLL defaults to all rows disabled. Defaults are starting points, not calibrated
movement angles or distances for a particular robot.

HLH has repetitions (0–100), wait interval (1–10000 ms), left/right forward speed
(0–100%) and move duration (1–10000 ms). It waits before each forward pulse, counts
completed pulses and exits when repetitions finish or any of the five opponent
sensors detects a target, including during a pulse. Zero repetitions skips defense.
Defaults: 3 pulses, wait 2000 ms, 50% both motors, move 50 ms.

Search defaults to straight at 35% both motors. Attack defaults to 50%, rising to
100% after 300 ms of continuous centre detection. Backoff defaults: reverse 100%
for 100 ms, turn 100% for 120 ms, stop 50 ms. Edge thresholds are sampled from the
starting surface immediately before opening, multiplied by 50% by default.
Higher coefficients trigger at higher analog values. Place both edge sensors over
the dark ring surface before starting. Both-edge detection chooses a right turn.

All motion timing uses `millis()`. IR STOP preempts all Auto states. An edge
interrupts opening/defense/fight and starts reverse → turn → pause → fight; it does
not restart the interrupted opening. The escape sequence completes without
restarting on the same edge reading. A persistent edge triggers another escape
on returning to fight. Opponent priority is centre, front-left, front-right,
left, right. Ordinary opening steps continue their timing until finished or edge/
STOP intervenes; defense alone exits early on opponent detection.

### Configuration session

AutoRC WebUI sends `CONFIG ON` after identification. This stops both motors and
latches configuration mode until reset, including in RC mode. Saving and live
sensor inspection are possible without motor motion. Disconnecting alone does
not arm the robot: unplug USB and reset with the start input idle before testing.
Configuration has no motor movement command. RC-only firmware retains its existing
RC-controlled alignment workflow; its live sensor requests do not lock motors.

### Protocol additions

115200 baud, newline-delimited text. Firmware identifiers:

```text
OK DEVICE=MAKER_MINI_SUMO FW=RC VERSION=1.1.0 PROTOCOL=1
OK DEVICE=MAKER_MINI_SUMO FW=AutoRC VERSION=1.0.0 PROTOCOL=2
```

The WebUI also supports the previous RC handshake `VERSION=1` without `FW`, with
live sensors disabled. Auto pages require recognized AutoRC protocol 2.

```text
CONFIG ON                  -> OK CONFIG
GET SENSOR                 -> SENSOR mask edgeL edgeR d2 dip state batteryV
GET AUTO                   -> AUTO <11 integers>
SAVE AUTO <11 integers>     -> OK SAVED AUTO
GET DEF                    -> DEF <5 integers>
SAVE DEF <5 integers>       -> OK SAVED DEF
GET STR 4                  -> STR 4 <20 integers>
SAVE STR 4 <20 integers>    -> OK SAVED STR 4
```

AUTO integer order: searchL, searchR, attackInitial, attackMax, attackRampMs,
reverseSpeed, reverseMs, turnSpeed, turnMs, pauseMs, edgePercent.
DEF order: repetitions, waitMs, leftSpeed, rightSpeed, moveMs.
STR order: enabled, leftSpeed, rightSpeed, durationMs, repeated five times.
STR 5 is rejected: use DEF. Existing `GET CONFIG`, `SAVE F n`, `SAVE B n` retain
alignment response format. AutoRC requires CONFIG ON before every kind of save.
Malformed/out-of-range input returns ERROR without applying the candidate values.
Serial overflow discards the entire line; receive work is bounded per control loop.
Sensor mask bits 0–4: left, front-left, centre, front-right, right; 1 = detected.
State values: 0 identify, 1 wait, 2 countdown, 3 opening, 4 defense wait,
5 defense move, 6 fight, 7 reverse, 8 turn, 9 pause, 10 stopped; 98 RC-only,
99 configuration. Sensors are sent only on request, never unsolicited.

EEPROM 0–3 remains reserved for the library; 16–19 remains compatible alignment.
Auto settings use bytes 32–347: magic, schema version, CRC16, 312-byte settings.
Writes invalidate magic first and restore it last. Interrupted/corrupt Auto saves
fall back to defaults at boot. Each save uses EEPROM.update/put, not continuous
writes. Browser save acknowledgement must match the requested command; telemetry
cannot acknowledge a save. A timeout disables editing and requires reconnect.

### Verification

- AutoRC and RC compile for `arduino:avr:uno` with CytronMakerSumo 1.2.3.
- Native tests execute the AutoRC source with fake pins, clock, motors and EEPROM:
  `clang++ -std=c++17 -Itests/fakes tests/autorc_test.cpp -o /tmp/autorc-test`
  then `/tmp/autorc-test`.
- WebUI tests: `node tests/webui_test.cjs`; syntax: `node --check WebSerialConfigurator/app.js`.
- Physical checks pending: D2 button/IR startup, IR stop at each motion phase,
  wheel polarity, neutral/transmitter-loss failsafe, edge threshold on the actual
  ring, defense detection during motion, USB reset/reconnect and EEPROM power cycle.
- This implementation is local until committed/pushed and the Pages workflow runs.
