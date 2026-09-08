# Web Serial Configurator

Nine-page configurator for MakerMiniSumo_RC and MakerMiniSumo_AutoRC using plain
HTML, CSS and JavaScript. Serve this directory on localhost or HTTPS and open
in desktop Chrome/Edge. No build dependencies or simulated device data.

Main provides connection and motor alignment. Firmware/version appears above
Connect robot only after connecting. AutoRC enables Auto behaviour with live
sensors below its settings, plus seven strategy pages with HLH dedicated to
defense. RC-only firmware exposes Main functionality. All interface text is English.

AutoRC locks motors for configuration until reset. Save settings, disconnect USB
and reset with START released / IR at STOP before testing motion.

The protocol, field ranges, EEPROM layout and verification status are maintained
in [the project specification](../docs/PROJECT_PLAN.md#autorc-100--implementation-september-2026).

Run `node tests/webui_test.cjs` from the repository root. Tests use a small fake DOM, not a real USB connection.
