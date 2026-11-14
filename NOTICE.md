# Licence notice

Required Notice: Copyright 2026 GalKar83 (https://github.com/GalKar83)

## What this means in plain words

You may use, copy, change and share the files listed below, and build your own projects on them, for **noncommercial**
purposes: your own spa at home, a hobby project, learning, research, or a non-profit. You may **not** sell them, sell a
product or service that is built on them, or otherwise use them for commercial purposes **without my explicit written
permission**. Commercial use is possible by agreement: contact me through GitHub (an issue on this repository, or my
profile page).

The legal text is the [PolyForm Noncommercial License 1.0.0](LICENSE). If this summary and the licence differ, the licence applies.

## Files covered by this licence

Everything that was written by GalKar83:

- `sundance_680.cpp` (the Sundance 680 logic),
- the whole [`homeassistant/`](homeassistant/README.md) folder (helpers, automations, scripts, dashboard),
- `example-680.yaml`,
- `README.md`, the other documentation and the pictures under `docs/`.

## Files not covered

These files come from [Iraithium/sundance-rs485-controller](https://github.com/Iraithium/sundance-rs485-controller) (the Sundance 780
component this repository is a fork of) and still contain a large part of its code:

- `__init__.py`, `sundance_spa.cpp`, `sundance_spa.h`,
- the folders `climate/`, `switch/`, `select/` and `button/`.

The upstream repository has **no licence file**. I cannot grant a licence for code that is not mine, so the terms above do not
cover the upstream parts of these files; they stay under whatever rights the original author has. My own changes to them are
offered under the same terms as the files above.

## Not affected

The protocol decoding that was shared with
[pete-jorgensen/SundanceHotTub2MQTT](https://github.com/pete-jorgensen/SundanceHotTub2MQTT) (Apache-2.0) stays under that
repository's licence. Facts about how the spa talks on its bus (which bit means what) are not something a licence can restrict.
