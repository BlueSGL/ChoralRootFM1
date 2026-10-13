# Sequential Prophet-5 factory programs

Source: [Sequential official sounds](https://sequential.com/support/download/prophet-5-10-sounds/),
[factory archive v1.03](https://sequential.com/wp-content/uploads/2025/03/Prophet-510-Factory-Programs-ReadMe1.03.zip).

`prophet5-v1.03.syx` is the unmodified `P5_Factory_Programs_USER_v1.03.syx`
from that archive: 200 native single-program dumps. These are Sequential’s
patch data, not invented Melodee presets.

SHA-256: `1d6e9dec2753a08ac6d07b98f0f4e755e33b41de11dc3bed46fa23217619d232`.

Factory patch data remains attributed to Sequential; the software license does
not relicense this data.

(The text above is Melodee's `assets/prophet5-factory/README.md`, Kerem Kilic, <https://github.com/keremimo/melodee>,
from which this file and the `.syx` were taken unchanged, less its paragraph on Melodee's own user collection.)

## In ChoralRoot

`tools/gen_prophet_factory.py` checks the file's SHA-256 and turns it into `build/gen/melodee_prophet_factory.h` at
build time: the 200 programs packed (55 bytes each) and their PRESETS names, PROPHET's factory presets 01..200
(docs/PROPHET.md). They are in the build only with `FELUCCA_P5_FACTORY` 1 (the default for now):
`FELUCCA_P5_FACTORY=0 ./build.sh` builds the engine with INIT PROPHET alone and none of this data in the image.

The programs are Sequential's (Sequential LLC, 2020-2025). They are included as sound data for compatibility with the
instrument; no licence is granted for them by this project, and whether a ChoralRoot release carries them is not
decided (docs/PROPHET.md "Open decisions").
