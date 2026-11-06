# Optional: measuring the spa's power against the whole house

**Only relevant if you have a whole-house power sensor.** The spa does not report its own power.
This setup estimates it from the loads that are running (see [the Home Assistant README §3](../../README.md#3-electrical-power-and-energy)) and checks that estimate against the house's consumption.
It is a measurement, not a calibration: **nothing here changes the Power settings** (the numbers
kept on the ESP) — they are changed by hand if the measurements show a consistent gap.

Entity names will differ on your system. In particular `sensor.load_power` (the whole-house power)
and the two helpers built on it (`House Load Energy (Spa Calibration)`, `House Load Mean 10 Min
(Spa Calibration)`) are the owner's; use your own sensor and names, and adjust the entity ids the
automations read. Without a whole-house power sensor, skip this folder.

| File | What it does |
|---|---|
| `automations/spa_power_measurement_circulation.yaml` | each night, the house load during the filter cycle minus the quiet hours after it = the circulation power; logged to the logbook with the difference from the setting. The night is skipped when the heater or a jet ran, or the quiet base was unstable |
| `automations/spa_power_measurement_jet_pump.yaml` | after a jet run that started from 10 min of idle, the house load during the jets minus the 10-minute means before and after = the jet-pump power |
| `automations/spa_power_measurement_weekly_summary.yaml` | Saturdays 08:00, a phone notice: per measurement how many valid results, the average measured power against the setting, the average difference in %, and how many were skipped |
| `helpers/input_text.yaml` | `spa_power_measure_circulation` / `spa_power_measure_jet_pump`: the last 10 results of each measurement (`minutes,measured_W,difference_%` or `minutes,x` for a skipped one), which the weekly summary reads |
| `helpers/config_entry_helpers.yaml` | the whole-house energy integration and its 10-minute mean (statistics) |

The two measurement automations write each result to the logbook (name *Spa power measurement*) and
to the `input_text` helpers above. The Settings page also shows a *Power measurement · last 24 hours*
line built from the same helpers.
