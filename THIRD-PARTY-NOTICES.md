# Third-party notices

Most of this repository is covered by the proprietary license in [LICENSE](LICENSE). The parts listed below belong to other people. They stay under their own licenses, and the proprietary license does not apply to them.

## Libraries compiled into the HEX files

The firmware is built with these open-source libraries. They are not stored in this repository: `tools/build_firmware.sh` downloads the exact versions listed here when it builds.

| Component | Version | License | Source | Used in |
|---|---|---|---|---|
| Arduino AVR core (incl. SPI, SoftwareSerial, EEPROM) | 1.8.6 | LGPL-2.1-or-later | https://github.com/arduino/ArduinoCore-avr | All firmware |
| MD_Parola | v3.7.7 | LGPL-2.1 | https://github.com/MajicDesigns/MD_Parola | 05 |
| MD_MAX72XX | v3.5.1 | LGPL-2.1 | https://github.com/MajicDesigns/MD_MAX72XX | 05 |
| LiquidCrystal | 1.0.7 | LGPL-2.1-or-later | https://github.com/arduino-libraries/LiquidCrystal | 08, 10, 11, 12, 13, 15 |
| Servo | 1.2.2 | LGPL-2.1 | https://github.com/arduino-libraries/Servo | 06, 13, 14 |

The LGPL lets anyone who receives a compiled program containing these libraries replace or relink the library part. The firmware source in this repo, together with `tools/build_firmware.sh`, is what makes that possible. If you get permission to ship devices built from this repo, keep this notice with them and provide the LGPL texts linked above.

## Files in this repository that are not mine

| Path | Owner / origin | Notes |
|---|---|---|
| `projects/05-led-matrix-scrolling-display/firmware/Parola_Scrolling/Parola_Scrolling.ino` | Based on the Parola_Scrolling example from MD_Parola by MajicDesigns (Marco Colli) | Treated as LGPL-2.1, like the library it comes from. Only my changes (message text, settings, comments) are mine. |
| `projects/05-led-matrix-scrolling-display/firmware/build/Parola_Scrolling.hex` and `projects/05-led-matrix-scrolling-display/proteus/Arduino_Code.ino.hex` | Compiled from the sketch above | Same terms as the sketch. |
| `projects/06-gas-smoke-detector-gsm/proteus/library/` (GasSensorsTEP.LIB, .IDX, GasSensorTEP.HEX) | The Engineering Projects (www.TheEngineeringProjects.com) | Proteus simulation models, included only so the project opens. All rights belong to their owner. Download them from the original site if you need to redistribute them. |
| Proteus component models and symbols inside the `.pdsprj` files | Labcenter Electronics and the library authors | Using them requires your own licensed copy of Proteus. |

## Tools used but not distributed

GCC for AVR, avr-libc, binutils, simavr, matplotlib and schemdraw are used to build, test and draw this project. They are not included in the repository and keep their own licenses.
