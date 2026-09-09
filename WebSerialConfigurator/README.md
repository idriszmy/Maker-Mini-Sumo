# Web Serial Configurator

Nine-page configurator for MakerMiniSumo_RC and MakerMiniSumo_AutoRC using plain
HTML, CSS and JavaScript. Serve this directory on localhost or HTTPS and open
in desktop Chrome/Edge. No build dependencies or simulated device data.

Main provides connection and motor alignment. Firmware/version appears above
Connect robot only after connecting. AutoRC enables Auto Routine with live
sensors below its settings, plus seven strategy pages with HLH dedicated to
defense. RC-only firmware exposes Main functionality. All interface text is English.

AutoRC locks motors for configuration until reset. Save settings, disconnect USB
and reset with START released / IR at STOP before testing motion.

WebUI 1.1.0 groups Auto Routine into Search, Backoff and Attack. Backoff pause
and edge threshold are handled by firmware; live sensors omit internal state.

WebUI 1.2.0 adds RC channel mapping on Main for RC/AutoRC 1.2.0. The default is
GPIO1 throttle/GPIO2 steering; the alternate selection swaps those roles.

WebUI 1.3.0 opens the browser's serial-port picker directly from Connect. AutoRC 1.3.0 live sensors
show raw edge ADC readings plus the potentiometer's raw ADC and 25-75% IR
sensitivity.

WebUI 1.3.1 shows the connected firmware version beside Connect/Disconnect,
shortens the trim ADC label and provides Disconnect at the top right of every
Auto page.

WebUI 1.3.2 places that Disconnect control inside each Auto/Strategy panel.

AutoRC and WebUI 1.4.0 add valid RC pulse widths for physical GPIO1 and GPIO2
to live sensors. A missing, stale or out-of-range pulse is shown as No signal.

The protocol, field ranges, EEPROM layout and verification status are maintained
in [the project specification](../docs/PROJECT_PLAN.md#autorc-100--implementation-september-2026).

Run `node tests/webui_test.cjs` from the repository root. Tests use a small fake DOM, not a real USB connection.
