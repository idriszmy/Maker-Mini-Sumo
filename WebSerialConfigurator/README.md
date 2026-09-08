# Web Serial Configurator

Nine-page configurator for MakerMiniSumo_RC and MakerMiniSumo_AutoRC using plain
HTML, CSS and JavaScript. Serve this directory on localhost or HTTPS and open
in desktop Chrome/Edge. No build dependencies or simulated device data.

The home page provides connection, firmware/version, requested live sensors and
motor alignment. AutoRC enables Auto behaviour and seven strategy pages, with
HLH dedicated to defense. RC-only firmware exposes home-page functionality.

AutoRC locks motors for configuration until reset. Save settings, disconnect USB
and reset with START released / IR at STOP before testing motion.

The protocol, field ranges, EEPROM layout and verification status are maintained
in [the project specification](../docs/PROJECT_PLAN.md#autorc-100--implementation-september-2026).

Run `node tests/webui_test.cjs` from the repository root. Tests use a small fake DOM, not a real USB connection.
