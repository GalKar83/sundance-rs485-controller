# Home Assistant side

Everything here runs in Home Assistant on top of the ESP's entities: helpers, automations, a
script and a dashboard view. It is the owner's working setup, exported and cleaned up. Nothing in
it is needed for the component to work; take what you want.

- [`automations/`](automations) — one YAML file per automation. Paste it into a new automation
  with *Edit in YAML*.
- [`scripts/spa_notify.yaml`](scripts/spa_notify.yaml) — the one script.
- [`dashboard/hot_tub_view.yaml`](dashboard/hot_tub_view.yaml) — the Hot Tub page
  (see the screenshot in the main README §4).
- Helpers are created in the UI (*Settings → Devices & services → Helpers*). Their settings are
  listed below.

Replace before use:

- `notify.mobile_app_your_phone` with your phone's notify action.
- `person.owner` with your person entity.
- `sensor.load_power` with your whole-house power sensor. It is only used by the two optional
  power-measurement automations.

Entity IDs assume the ESP device is called `sundance-spa` / "Sundance Spa". The template helpers
are assigned to that device (the *Device* field in the helper's options), which is why their
entity IDs also start with `sundance_spa_`.

Contents

1. [Design rules](#1-design-rules)
2. [Helpers](#2-helpers)
3. [Electrical power and energy](#3-electrical-power-and-energy)
4. [Automations](#4-automations)
5. [Dashboard](#5-dashboard)
6. [Things learned the hard way](#6-things-learned-the-hard-way)

---

## 1. Design rules

- **Quiet hours 23:00–06:30.** No spa notification reaches the phone at night:
  - *Information* goes through `script.spa_notify`, which queues it in
    `input_text.spa_night_alerts`. The queue is sent as one notification at 06:30.
  - *Questions* (with Yes/No buttons) wait until 06:30 inside their own automation, so the
    buttons stay attached. The times they record are those of the event, not of the answer.
- **No long-lived state in waits.** Saving any automation reloads all automations and cancels
  every running wait. Anything that must survive (an open heating run, the start of a jet run) is
  kept in a helper. The automation is driven by triggers.
- **Unavailable is normal.** The spa is outdoors on Wi-Fi, and the ESP drops off for seconds to
  hours. Triggers use `not_from` / `not_to: unavailable`, or read their start time from a helper,
  so a blip in the middle of an event does not lose it or count it twice.
- **Notifications are in English**, with a unique `tag` per question. The answer's `clear_notification`
  removes the right one.

## 2. Helpers

### Storage helpers

| Entity | Type | Used by |
|---|---|---|
| `input_boolean.spa_session_merging` | toggle, `initial: false` | soak question: blocks a second question while follow-up runs are merged |
| `input_boolean.spa_panel_alert_sent` | toggle | panel-used-while-away: one alert per absence |
| `input_datetime.spa_in_use_since` | date + time | start of the current jet run; 1970-01-01 = none open |
| `input_datetime.last_soak`, `input_datetime.last_maintenance` | date + time | end of the last soak / maintenance session |
| `input_datetime.last_rinse_filter` | date + time | when the spa came back on after the last power cut answered "Yes, rinsed" |
| `input_datetime.last_replacement_filter_date`, `…_clearray_bulb_date`, `…_water_date` | date | date of the counter reset answered "Yes, replaced" |
| `counter.spa_soaking_sessions_total`, `counter.spa_maintenance_sessions_total` | counter | number of answered sessions |
| `input_number.spa_soaking_minutes_total`, `input_number.spa_maintenance_minutes_total` | box, 0–100000000, step 0.1, min | total minutes |
| `input_text.spa_last_heating` | text, max 100 | "34.0 → 35.0 °C · 31 min · 30/10 04:36" |
| `input_text.spa_heating_run_data` | text, max 60 | open heating run `start_ts;min;max` (empty = none) |
| `input_text.spa_temp_last_drop` | text, max 60 | last temperature drop `ts;temp` |
| `input_text.spa_night_alerts` | text, max 255 | notifications queued in the quiet hours |
| `input_text.spa_decoder_seen` | text, max 255 | keys already reported by the raw-decoding automation |

### Template helpers (sensor; assigned to the Sundance Spa device)

| Name → entity | State template | Unit / class / state class |
|---|---|---|
| Power → `sensor.sundance_spa_power` | see [§3](#3-electrical-power-and-energy); availability `{{ is_state('binary_sensor.sundance_spa_status', 'on') }}` | W / power / measurement |
| Soaking Time Total → `sensor.sundance_spa_soaking_time_total` | `{{ states('input_number.spa_soaking_minutes_total') \| float(0) }}` | min / duration / total_increasing |
| Maintenance Time Total → `sensor.sundance_spa_maintenance_time_total` | `{{ states('input_number.spa_maintenance_minutes_total') \| float(0) }}` | min / duration / total_increasing |
| Soaking Count Total → `sensor.sundance_spa_soaking_count_total` | `{{ states('counter.spa_soaking_sessions_total') \| int(0) }}` | – / – / total_increasing |
| Maintenance Count Total → `sensor.sundance_spa_maintenance_count_total` | `{{ states('counter.spa_maintenance_sessions_total') \| int(0) }}` | – / – / total_increasing |
| Last Soak, Last Maintenance, Last Rinse Filter → `sensor.sundance_spa_last_…` | `{% set d = states('input_datetime.last_soak') %}{{ as_datetime(d).strftime('%d/%m/%Y %H:%M') if d >= '2000' else none }}` | text |
| Last Replacement Filter / ClearRay Bulb / Water Date → `sensor.sundance_spa_last_replacement_…_date` | same with `'%d/%m/%Y'` on `input_datetime.last_replacement_…_date` | text |

The date sensors only exist to show a date in DD/MM/YYYY format on the dashboard. They show
nothing until the first date is recorded.

### Run-time counters (history_stats + utility_meter)

`history_stats` counts today's on-time from the recorder, from `{{ today_at() }}` to `{{ now() }}`.
Utility meters with *periodically resetting* on add it up per week and month.

| Today (history_stats) | Source, state | Type | Week / Month meters |
|---|---|---|---|
| Pumps Time Today | `binary_sensor.sundance_spa_pumps_running`, on | time | Pumps Time Week / Month |
| Pump 1 Time Today | `binary_sensor.sundance_spa_pump_1_running`, on | time | Pump 1 Time Week / Month |
| Pump 2 Time Today | `binary_sensor.sundance_spa_pump_2_running`, on | time | Pump 2 Time Week / Month |
| Heater Time Today | `binary_sensor.sundance_spa_heater`, on | time | Heater Time Week / Month |
| Heater Count Today | `binary_sensor.sundance_spa_heater`, on | count | Heater Count Week / Month |
| ClearRay Bulb Time Today | `sensor.sundance_spa_clearray_bulb`, `On` | time | ClearRay Bulb Time Week / Month |

Soaking / maintenance meters are utility meters on the totals above, with *periodically
resetting* off: Soaking Time Today / Week / Month, Soaking Count Today / Week / Month, and
Maintenance Count Today / Week / Month.

Meter cycles:

- **Daily** for "Today".
- **Weekly with offset 6 days** for "Week". HA's weekly cycle starts on Monday, and the offset
  moves the reset to Sunday 00:00, the first day of the week here.
- **Monthly** for "Month".
- **Always available** is on for all of them, so a short ESP outage doesn't blank the tiles.

Pump 1 includes circulation (Jet 1's low speed), so in practice Pump 1 ≈ Pumps − Pump 2. The
ClearRay counter is the same as Pump 1 (the bulb runs whenever the Jet 1 pump does), and is kept
in case that turns out not to be so.

## 3. Electrical power and energy

The spa does not report power. `sensor.sundance_spa_power` multiplies what is running by the power
values on the spa device: the six **Power …** numbers of the ESP (*Configuration* on the device
page; they are kept in the ESP's flash). Defaults and how they were found are in the main README
§11.

```jinja
{% set p1 = is_state('binary_sensor.sundance_spa_pump_1_running', 'on') %}
{% set p1_high = is_state('switch.sundance_spa_jet_1', 'on') %}
{% set p2 = is_state('binary_sensor.sundance_spa_pump_2_running', 'on') %}
{% set heat = is_state('binary_sensor.sundance_spa_heater', 'on') %}
{% set light = states('sensor.sundance_spa_light_status') not in ['Off', 'unknown', 'unavailable'] %}
{% set ctrl = states('number.sundance_spa_power_controller') | float(15) %}
{% set uv = states('number.sundance_spa_power_clearray') | float(20) %}
{% set lt = states('number.sundance_spa_power_light') | float(5) %}
{% set circ = states('number.sundance_spa_power_circulation') | float(370) %}
{% set jet = states('number.sundance_spa_power_jet_pump') | float(1630) %}
{% set heater = states('number.sundance_spa_power_heater') | float(2700) %}
{{ (ctrl + ((jet + uv) if p1_high else (circ if p1 else 0)) + (jet if p2 else 0)
   + (heater if heat else 0) + (lt if light else 0)) | round(0) }}
```

How the formula counts the loads:

- **Pump 1 at high** (the Jet 1 switch) = Jet Pump + ClearRay.
- **Pump 1 at low** = Circulation. The Circulation value is measured with the bulb on, so it
  already includes the ClearRay.
- **Pump 2** = Jet Pump.
- **Both jets during a filter cycle** count the same as both jets at any other time: Jet 1 at
  high replaces circulation, it is not added to it.

The chain from power to energy:

1. **Spa Energy.** An *Integration* (Riemann sum) helper on the power sensor gives
   `sensor.sundance_spa_energy` in kWh. Method *left*, precision 3, **max sub-interval 1 min**.
   The power only changes when something switches, so without the sub-interval no value is
   written for hours. With 5 min, an hour's energy spilled into the next hour, and a blip lost
   the rest of the interval.
2. **Energy Today / Week / Month / Year.** Utility meters on Spa Energy. Week resets on Sunday.
3. **Energy Total.** `sensor.sundance_spa_energy_total` is a utility meter on Spa Energy with
   **no cycle**.
   - **Why it exists:** it is what the bar chart and the outage correction use. A utility meter
     can be moved with `utility_meter.calibrate`, and an integration sensor cannot. The outage
     correction needs to move it (§4).
   - **Migrating an existing install:** copy the integration sensor's long-term statistics into
     the meter's, then calibrate the meter once so its state continues from the copied sum.
4. **Statistics.** Long-term statistics (hourly rows, kept forever by default) are what the bar
   chart shows. Year-over-year comparisons come from there too, not from the 10-day state
   history.

## 4. Automations

| File | What it does | Notes |
|---|---|---|
| `spa_track_in_use_start` | stores the start of a jet run in `input_datetime.spa_in_use_since` | the soak question reads it |
| `spa_ask_soaking_or_maintenance` | after a jet run of 10+ min, asks *Soaking / Maintenance / Soaking (edit time)* | details below |
| `spa_ask_filter_bulb_water_replaced` | when a service counter is reset at the panel, asks whether the item was replaced | sets the Last Replacement date only on Yes |
| `spa_after_esp_outage` | after the ESP was offline: power cut → filter-rinse question; Wi-Fi drop → adds the estimated energy | details below |
| `spa_record_last_heating` | "lowest → highest °C · minutes · start" of each heating run | stateless, see §1 |
| `spa_decoder_notice` | forwards the ESP's Decoder Notice | through `spa_notify` |
| `spa_deliver_night_alerts` | sends the overnight queue at 06:30 | also on HA start, if HA was down at 06:30 |
| `spa_panel_used_while_away` | alerts once per absence when someone presses a panel button | the Panel Button entity sees only physical presses |
| `spa_raw_decoding_values_changed` | optional, diagnostic: reports unknown status values once | only while the raw entities exist |
| `spa_power_measurement_circulation`, `spa_power_measurement_jet_pump` | optional: measure circulation / jet power from the house load and log the result | never change the settings, only log |

### Soaking or maintenance

The point is to count real soaks, and to tell them apart from runs for adding chemicals. After a
jet run of 10+ minutes ends, the automation waits 5 minutes:

- **Follow-up runs.** Another run of any length that starts within 5 minutes belongs to the same
  session (up to 6 runs). You often stay in the water between runs, so the session lasts from
  the first start to the last end, gaps included. The 5-minute wait itself is not added.
- **Short runs.** A run under 10 minutes that is not a follow-up is not asked about.
- **"Soaking (edit time)"** opens a text field for the minutes you actually spent in the water.
  It accepts digits only, 1–300. Anything else re-asks with the same notification.
- **No answer within 12 h** counts the session in neither total.
- **The start time comes from the helper, not the trigger.** The trigger is "in use → off" from
  any state, and the run start is read from `spa_in_use_since`. A Wi-Fi blip just when the jets
  stop (on → unavailable → off) once made the question disappear, because a `from: on` trigger
  never fired. `spa_session_merging` stops the run that ends inside a session from asking
  separately. It has `initial: false`, so it is cleared on every HA start.

### After an ESP outage: power cut or Wi-Fi drop

When the ESP comes back after a minute or more offline, there are two possibilities. Either the
spa itself had no power, or only the Wi-Fi dropped. The ESP restarting proves nothing, because
the ESP is powered by the spa and also reboots on its own.

**How the spa clock tells them apart.** Every spa power cut resets the spa date to
**01/01/2014** and the time to 00:00. After a reconnect:

- **~80 s:** the date arrives.
- **~2 min later:** the ESP's automatic clock sync corrects the date and time. It needs HA's
  time, so it cannot run before the reconnect.

The automation waits up to 3 minutes for the date, which is long enough to read it before it is
corrected.

**Power cut (date 01/01/2014):**

- Asks *Did you rinse your filter?*, but only if the spa was off 5+ min and came back on outside
  the quiet hours.
- On Yes, `last_rinse_filter` is set to the time the spa came back on (now minus the spa clock
  reading).
- No energy is added.

**Wi-Fi drop (date correct).** The spa ran its normal schedule unseen, so that energy is
estimated and added. The estimate goes minute by minute through the outage:

- controller always;
- circulation inside the Primary Filter cycles (Start, Cycles and Duration as read from the spa);
- one jet pump during the daily 1-minute cleanup at the first cycle start + 1 min;
- nothing for heating or manual use, which cannot be known.

The minutes are grouped by hour, then:

1. **Hourly statistics.** The recorder writes **no hourly row at all** while the source sensor is
   unavailable. Missing hours get a new row with `recorder.import_statistics`: the sum from the
   hour before plus the estimate so far. Existing rows get the estimate up to their hour, and
   the last completed hour gets all of it.
2. **Meters.** `utility_meter.calibrate` adds the same estimate to Energy Total, Today, Week,
   Month and Year. For Today, Week, Month and Year it adds only the part after that meter's last
   reset. Step 1 alone does not stick (§6).
3. A logbook line records the outage and the kWh added.

The schedule is read from the spa at the time of the outage, so a changed filter schedule is
followed automatically.

## 5. Dashboard

The Hot Tub view uses the *sections* layout and has six sections:

- **Control panel:** Jet 1, Jet 2, the two circulation states, and the light mode list on one
  row.
- **Temperature chart:** 24 h of water and target temperature, with heating runs in red.
- **Energy:** today / week / month tiles and one bar chart.
- **Soaking and Maintenance:** last session, totals and counts.
- **Service:** remaining days and last replacement for water, filter and ClearRay bulb.
- **Run Time:** heater, Pump 1 and Pump 2.

Details worth knowing:

- **Energy chart.** An `energy-date-selection` card drives a `statistics-graph` with
  `energy_date_selection: true`. The resolution follows the picker: hours for a day, days for a
  week or month, months for a year. This gives the Energy dashboard's behaviour for the spa
  alone, without adding the spa to the Energy dashboard.
- **Water temperature line.** The water reading flips 35.0 ↔ 35.5 for seconds at a time at a
  half-degree boundary, so the line takes the last value of every 10 minutes.
- **Heating overlay.** A `data_generator` draws the water temperature only while
  `heating_run` is on.

## 6. Things learned the hard way

- **`recorder.import_statistics` is not enough to correct energy.** It rewrites the hourly rows
  only. The next hour is compiled from the 5-minute rows, so the next hour's sum undoes the
  correction:
  - A whole correction disappeared at the next hour.
  - The WebSocket command `recorder/adjust_sum_statistics` moves both tables, but automations
    cannot call it.
  - What works: correct a **utility meter** both ways. Import its hourly rows, then
    `utility_meter.calibrate` it by the same amount, which moves its 5-minute chain.
- **The recorder skips hourly rows while a sensor is unavailable.** A correction that only edits
  existing rows puts a 5-hour outage into a single hour.
- **Automation reloads cancel waits.** The first "Last Heating" version waited for the end of the
  run, and was cancelled when another automation was saved in the middle of a run.
- **`history_stats` cannot be set by hand.** It recomputes from the recorder history of its
  source. Pointing it at a new entity starts that day from the new entity's history. To avoid a
  short day, switch the source after midnight, once the new entity has a full day of history.
- **Measured power is a moving target.** The house-load measurements of the circulation vary
  from night to night (other loads in the house never fully stop). Letting them adjust the setting
  made it wander, so the power values are fixed settings on the ESP, and the two measurement
  automations only log, for occasional review.
