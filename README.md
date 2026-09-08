# Maker Mini Sumo

**English** | [Bahasa Melayu](README.ms.md)

RC firmware and a Web Serial configurator for the Cytron Maker Mini Sumo
Controller.


## AutoRC firmware and nine-page configurator

`Arduino/MakerMiniSumo_AutoRC/MakerMiniSumo_AutoRC.ino` adds Auto strategies
(DIP 0–6), defense on HLH and interrupt-driven RC on HHH. The WebUI identifies
firmware/version, reads live sensors and edits alignment, Auto Routine and
five-row strategies. HLH has configurable forward repetitions and exits early
on opponent detection.

AutoRC 1.1.0 groups routine tuning into Search, Backoff and Attack. Edge
sensitivity uses the onboard potentiometer and the first surface reading at run
start. WebUI 1.1.0 shows live sensors without the internal state value.

START/button and IR share D2: release the button / keep IR at STOP when powering
on or resetting. AutoRC locks motors during WebUI configuration; save, disconnect
USB and reset before driving. Open the sketch directly from this repository.
See [the project specification](docs/PROJECT_PLAN.md#autorc-100--implementation-september-2026)
for protocol, defaults, tuning details and physical verification still required.

## Open WebUI

[Open Maker Mini Sumo Web Serial Configurator](https://idriszmy.github.io/Maker-Mini-Sumo/)

Use Chrome or Edge on a computer connected to the board through USB.

## Project structure

- `Arduino/MakerMiniSumo_RC/` — Arduino firmware for RC control and motor alignment.
- `WebSerialConfigurator/` — web application for configuration through Web
  Serial.
- `docs/PROJECT_PLAN.md` — official project specification and status.

## Firmware

Target board: Maker Mini Sumo Controller (ATmega328P / Arduino Uno compatible).

The current firmware provides:

- RC throttle and steering control using pin-change interrupts.
- Failsafe when the RC signal is lost.
- Separate forward and backward motor alignment.
- Live alignment using the DIP switches and potentiometer.
- Alignment storage in EEPROM.
- Buzzer feedback for power-on, alignment modes and EEPROM saves.

## Motor Alignment Guide

Motor alignment balances the maximum speed of the left and right motors so the
robot travels in a straighter line. Forward and backward alignment values are
stored separately.

> Make sure the test area is safe and release the throttle to neutral before
> changing the DIP switches or saving an alignment value.

### Method 1: DIP switches and potentiometer

The motors do not run automatically in an alignment mode. Move the robot with
the RC remote while making adjustments.

#### Forward alignment

1. Release the throttle to neutral.
2. Set the DIP switches to `LHL` / OFF-ON-OFF.
3. The LED repeatedly flashes once and the buzzer beeps once.
4. Drive the robot forward using the RC remote.
5. Adjust the potentiometer until the robot travels straight:
   - Centre: left motor 100%, right motor 100%.
   - Turn the potentiometer left: reduce the left motor to a minimum of 75%.
   - Turn the potentiometer right: reduce the right motor to a minimum of 75%.
6. Repeat the forward movement and adjustment until the alignment is
   satisfactory.
7. Release the throttle to neutral, then press and hold START for 2 seconds.
8. The LED flashes rapidly and the buzzer plays a confirmation sound when the
   forward value has been saved to EEPROM.

In this mode, the potentiometer only affects forward movement. Backward
movement continues to use the backward alignment stored in EEPROM.

#### Backward alignment

1. Release the throttle to neutral.
2. Set the DIP switches to `HLH` / ON-OFF-ON.
3. The LED repeatedly flashes twice and the buzzer beeps twice.
4. Drive the robot backward using the RC remote.
5. Adjust the potentiometer until the robot travels straight:
   - Centre: left motor 100%, right motor 100%.
   - Turn the potentiometer left: reduce the left motor to a minimum of 75%.
   - Turn the potentiometer right: reduce the right motor to a minimum of 75%.
6. Repeat the backward movement and adjustment until the alignment is
   satisfactory.
7. Release the throttle to neutral, then press and hold START for 2 seconds.
8. The LED flashes rapidly and the buzzer plays a confirmation sound when the
   backward value has been saved to EEPROM.

In this mode, the potentiometer only affects backward movement. Forward
movement continues to use the forward alignment stored in EEPROM.

When finished, return the DIP switches to `LLL` / OFF-OFF-OFF for Normal RC
mode. The firmware will use both alignment values stored in EEPROM.

### Method 2: WebUI

1. Upload the latest firmware to the Maker Mini Sumo Controller.
2. Set the DIP switches to `LLL` / OFF-OFF-OFF.
3. Connect the board to a laptop using a USB data cable.
4. Close the Arduino Serial Monitor and any other application using the serial
   port.
5. Open the [Maker Mini Sumo Web Serial Configurator](https://idriszmy.github.io/Maker-Mini-Sumo/)
   using Chrome or Edge on a desktop or laptop.
6. Choose `Select new port...`, select the board's serial port, then press
   `Connect`.
7. Once connected, the WebUI reads the forward and backward alignment values
   from EEPROM and updates both sliders.
8. Adjust the required slider:
   - A negative value down to `-25`: reduce the left motor to 75%.
   - A value of `0`: both motors remain at 100%.
   - A positive value up to `+25`: reduce the right motor to 75%.
9. Release the slider to save that direction's value directly to EEPROM.
10. Wait for the buzzer confirmation and the `Saved` status in the WebUI.
11. Test the robot using the RC remote and repeat the adjustment if required.

Make sure the USB cable cannot interfere with the moving robot. The safest
option is to save the value, disconnect the WebUI and USB cable, and then test
the robot. Reconnect if further adjustment is required.

## Web Configurator status

The first version of the Web Serial configurator and firmware protocol has
been built. See [`docs/PROJECT_PLAN.md`](docs/PROJECT_PLAN.md) for the
specification, protocol, safety notes and hardware testing milestones.
