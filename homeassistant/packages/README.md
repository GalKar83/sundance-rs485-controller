# Home Assistant package (the short way to install)

Instead of creating helpers, sensors, automations and scripts one by one, copy **one file** and add **one line**.

## Requirements for the dashboard (install these first)

The package itself needs nothing extra. **The dashboard pages do: without the three frontend add-ons below most cards show
"Custom element doesn't exist" and the pages do not work.** Install them from [HACS](https://hacs.xyz) (HACS → Frontend), then reload the browser:

| Add-on | Used for | Link |
|---|---|---|
| **Mushroom** | almost every row and button (about 80 cards) | [piitaya/lovelace-mushroom](https://github.com/piitaya/lovelace-mushroom) |
| **ApexCharts Card** | the temperature and power graphs | [RomRider/apexcharts-card](https://github.com/RomRider/apexcharts-card) |
| **card-mod** | card styling (borders, dimmed locked rows, small text) | [thomasloven/lovelace-card-mod](https://github.com/thomasloven/lovelace-card-mod) |

Also needed for the phone messages: the **Home Assistant Companion app** on the phone (it provides the `notify.mobile_app_...` service).

## 1. Install the core

1. Install the ESPHome part first (the spa entities must exist).
2. In `configuration.yaml` enable packages (once):
   ```yaml
   homeassistant:
     packages: !include_dir_named packages
   ```
3. Create a folder `packages` next to `configuration.yaml` and copy [`sundance_spa.yaml`](sundance_spa.yaml) into it.
4. Settings → System → Restart (a full restart; the helpers are created on start).

## 2. Set your phone (the only required step)

Add the dashboard page [`../dashboard/hot_tub_setup_view.yaml`](../dashboard/hot_tub_setup_view.yaml) (see the dashboard notes in the
[Home Assistant README](../README.md)), open **Hot Tub Setup**, and type your phone's notify service
(`notify.mobile_app_<your phone>`) into **Notify service of your phone**. Until then the spa works, but no phone messages are sent.

## 3. Optional modules (copy only the ones you want)

Each file is independent. Copy it into the same `packages` folder, restart, and fill in its fields on the **Hot Tub Setup** page.
They are in [`../optional-packages`](../optional-packages) (outside `packages`, so they are not loaded by accident).

| File | What it gives | What you must enter |
|---|---|---|
| `spa_audio.yaml` | which phone is connected to the spa speaker, play / pause / next from the Hot Tub page; the Bluewave standby / playing power (added to the spa's power and energy) and its two sliders on the Settings page | the Bluetooth MAC address of the spa audio (and, on the Settings page, the Bluewave standby and playing watts); per phone the Companion app *Bluetooth connection* and *Media session* sensors and its notify service |
| `spa_power_measurement.yaml` | compares the spa power estimate (circulation, jet pump, heater) with your whole-house load (measurement only, changes nothing) | the entity id of your whole-house power sensor (W) |
| `spa_panel_used_while_away.yaml` | a phone alert when someone presses a panel button while you are away | your `person.` entity |

> **Many entities are for debugging.** The ESPHome device exposes many diagnostic / raw-value entities because the project is still in development; they are kept so that you can be asked for their output when a bug is found. You can hide or disable them (details: [main README §12](../../README.md#12-diagnostics-entities)).

## What the package contains

Helpers (`input_*`, `counter`), the 32 `utility_meter` sensors, the template sensors (power, filter / bulb / water dates, plan, quiet hours, ...),
22 automations and 5 scripts. They are the same objects as in the per-file folders; those folders stay for anyone who prefers to create things
through the UI. Use **one way or the other, not both**, or the entities will be duplicated.

## Notes

- The dashboard needs Mushroom, ApexCharts Card and card-mod (see Requirements above).
- Entities created from a package cannot be renamed or edited in the UI helper screen; change the YAML instead.
- Not tested on a fresh Home Assistant yet (the author's own system uses the per-file objects). Please report problems.
