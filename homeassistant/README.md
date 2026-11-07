# Home Assistant side

Everything here runs in Home Assistant on top of the ESP's entities: helpers, automations,
scripts and the dashboard pages. It is the owner's complete working setup, exported from Home
Assistant's own files with the personal names replaced. Nothing in it is needed for the component
to work; take what you want.

- [`automations/`](automations) — one YAML file per automation (19; the optional power-measurement ones are in
  [`optional/`](optional/power-measurement)). Paste it into a new
  automation with *Edit in YAML*.
- [`scripts/`](scripts) — `spa_notify` (the quiet-hours notification queue), and the plan scripts
  `spa_plan_add`, `spa_plan_delete` and `spa_target_step`.
- [`helpers/`](helpers) — the helpers the automations use: `input_boolean`, `input_number`,
  `input_text`, `input_datetime`, `input_select` and `counter` (create them in the UI with these
  options, or paste them into `configuration.yaml`), and `config_entry_helpers.yaml` with the template,
  utility-meter, integration and statistics helpers that the UI creates as config entries
  (`domain` = helper type, `options` = its settings; the `device_id` was removed — assign them to
  your own spa device).
- [`dashboard/`](dashboard) — the three pages: `hot_tub_view.yaml`, `hot_tub_settings_view.yaml`
  and `hot_tub_plans_view.yaml` (screenshots in the main README §4). They are *sections* views;
  add each to a dashboard with its raw configuration editor. The owner's outdoor weather block was
  left out of the Hot Tub page.
- [`optional/power-measurement/`](optional/power-measurement) — measuring the spa's power against
  the whole-house consumption, with a weekly phone summary. **Only relevant if you have a
  whole-house power sensor**, and then the entity names will differ; its own README explains it.
- The exports are taken as they are in the running system. Some helpers keep an older name in
  their storage id; the files use the entity id that the automations refer to. Notes on a few:
  `Spa Power` (template) is still used by the dashboard's power tile and the circulation
  measurement automation; the `House Load …` helpers belong to the optional power measurement, which needs a
  whole-house power sensor (they are in `optional/`);
  The earlier `Energy` integration (Spa Energy) was removed once the ESP counted energy itself
  (§3), so it is not in the export. The entry titled `Spa Energy Test` is **not** a test: it is the
  Energy Total meter (`sensor.sundance_spa_energy_total`) that the energy chart and the outage
  correction use; only its entry title is old.

Replace before use:

- `notify.mobile_app_your_phone` with your phone's notify action.
- `person.owner` and `device_tracker.owner_presence` with your person and presence entities.
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
7. [Ideas not built yet](#7-ideas-not-built-yet)

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
- **Event-driven, not polled.** The heating plans compute their start once and set a `time`
  trigger on an `input_datetime`; they are recomputed only when something they depend on changes
  (a plan, the water temperature, the Heat Rate, the ESP coming back). The reminders and their
  answers are separate triggers, so an HA restart does not lose a question.
- **The panel unit rules.** Everything the owner types or sees (plan temperature, "Set" answer,
  notices) is in the unit the panel shows (`sensor.sundance_spa_temperature_format`); the stored
  values and the climate entity stay in °C.
- **Change only what was asked.** A setting is sent to the spa only from an explicit pick on the
  dashboard or a notification answer, never to "sync" a value.

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

### Heating plans, reminders and settings (added 3–6 Nov)

| Entity | Type | Used by |
|---|---|---|
| `input_text.spa_plan_1` … `_5` | text, max 60 | one heating plan each: `R|S;temp_c;HH:MM;once|daily|weekday digits (Mon=0…Sun=6);YYYY-MM-DD or -`. R = ready by, S = start heating. Empty = free slot |
| `input_boolean.spa_plan_1_on` … `_5_on` | toggle | pauses a plan without deleting it |
| `input_select.spa_plan_mode` / `spa_plan_repeat` | dropdown | the "Add a plan" form: Ready by / Start heating; Once / Daily / Chosen days |
| `input_select.spa_plan_temperature` | dropdown | the form's temperature in the panel unit (18.0–40.0 °C in halves, 65–104 °F) |
| `input_boolean.spa_plan_sun` … `_sat` | toggle | days for "Chosen days" |
| `input_datetime.spa_plan_when` | date + time | the form's time; `input_text.spa_plan_when_auto` remembers the value the automation last wrote |
| `input_datetime.spa_plan_next_start` / `spa_plan_ready_check` | date + time | the stored heating start and the ready-by check of the next plan (time triggers) |
| `input_text.spa_ready_by_state` | text, max 100 | the running plan: `plan_ts;previous setpoint;target;phase;ref_ts` (phase heat / warned / asked / done) |
| `input_text.spa_ready_notified` | text, max 20 | plan_ts already announced as ready |
| `input_text.spa_plan_previous_heater_mode` | text, max 10 | Eco / Day to restore after a plan switched the mode to Auto |
| `input_number.spa_heat_rate`, `input_number.spa_ready_by_margin` | box | learned heating rate (°C/h) and safety margin (min) |
| `input_datetime.spa_last_bromine` | date + time | last bromine addition (reminder every 4 days) |
| `input_datetime.spa_previous_soak` | date + time | the soak before the last one (shown on the page as *Previous Soak*) |
| `input_datetime.spa_last_power_cut` | date + time | when the spa last lost power (outage automation) |
| `input_select.spa_set_pf_start` / `_filter_change` / `_clearray` / `_water_refresh` | dropdown | the settings page dropdowns; their options are rebuilt from the spa's grid |
| `sensor.spa_next_plan`, `sensor.spa_water_f`, `sensor.spa_target_f` | template | text of the next enabled plan for the page; water and target in °F for the °F variants of the cards |

Scripts: `script.spa_plan_add` (first free slot; refuses with a persistent notification when there
is no free slot, a one-off is in the past, or no day is chosen), `script.spa_plan_delete(slot)`,
and `script.spa_target_step` (the −/+ on the Temperature row: one panel step in the panel unit).

### Run-time counters (utility_meter on the ESP totals)

Since p29 the ESP counts the on-times itself (main README §4). The *Today* counters are **daily
utility meters** on the ESP's Total Time sensors, *Week* and *Month* are utility meters on the same
totals, all with *always available* on. (Before p29 `history_stats` counted today's on-time from
the recorder, from `{{ today_at() }}` to `{{ now() }}`; that is the table below, the entity IDs
are unchanged.) A meter whose source returns from `unavailable` adds the difference, so a Wi-Fi
drop does not lose time.

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

> **Since p29 the ESP does this itself** (Electrical Power, Energy Counter, the Total Time sensors).
> The Energy Today / Week / Month / Year / Total meters are utility meters on the ESP's Energy
> Counter, which keeps counting while Home Assistant is away. The template and integration below
> are the earlier Home Assistant-side version: the Spa Energy integration has been removed, and the
> Spa Power template is kept only for the dashboard's power tile and the optional measurement; the
> Wi-Fi-drop correction in §4 still uses the same schedule model.

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
| `Spa - Heating plans` | runs the plans of the Scheduled Plans page | details below |
| `Spa - Heating plans: ready notice` | phone notice "Spa ready" the moment the water reaches a running plan's target | once per plan |
| `Spa - Ready by: ask what next` | after the plan: back to the old set point, keep, or type a new one | details below |
| `Spa - Plan form: When stays in the future` | keeps the form's time at the earliest possible moment | one-off plans only |
| `Spa - Plan temperature follows the panel unit` | swaps the form's temperature list between °C and °F | also at HA start |
| `Spa - Settings dropdowns: follow the spa` / `send a change` | the settings page dropdowns show the spa's value and send a pick through the ESP's number controls | details below |
| `Spa - Bromine reminder` | 08:00, from 4 calendar days after the last addition | Yes / No buttons |
| `Spa - Maintenance reminder` | 08:00, from 8 calendar days after the last maintenance | Done / Not yet |
| `Spa - Filter rinse reminder` | 08:00, from 30 calendar days after the last rinse | Done / Not yet |

### Heating plans

Plans live in `input_text.spa_plan_1…5` and are added from the **Scheduled Plans** page. A plan has
a mode, a target temperature, a time and a repeat:

- **Ready by** — the water should be at the target at the plan time. Heating starts at
  `plan time − ((target − water) / Heat Rate + margin)`. If the water is still more than 0.4° short
  at the plan time, one *Spa not ready yet* notice gives the temperature now and the expected
  minutes left.
- **Start heating** — heating starts at the plan time.
- **Repeat** — Once, Daily, or chosen weekdays. Repeating plans stay until deleted. A one-off
  plan leaves the list when its plan time has passed **and** the water has reached the target
  (whichever is later); one that never started (ESP offline) is removed 10 minutes after its time.
  A plan can be switched off without deleting it; turning it off does not stop a heating that has
  already started.
- **The heating itself** is a set point change (`climate.set_temperature`); the spa's thermostat
  does the rest. If Heater Mode is **Eco or Day**, which only let the heater run in part of the day
  (main README §11), the automation switches it to **Auto** for the plan, remembers the old mode
  in `input_text.spa_plan_previous_heater_mode`, and the question below restores it.
- **Event-driven.** The next plan within 8 days is recomputed whenever a plan changes, the water
  temperature moves (every 0.5 °C / 1 °F), the Heat Rate or margin changes, HA starts or the ESP
  comes back. The start is stored in `input_datetime.spa_plan_next_start`; a `time` trigger on
  that helper starts the heating exactly then. (An earlier version polled every 5 minutes.)
- **Heat Rate** is learned by `Spa - Record Last Heating`: after a heating of at least 20 minutes
  and 1°, `new = 0.7 × old + 0.3 × measured`. Because it covers whole runs, the thermostat
  overshoot (main README §11) is already in it.
- **Notices** (through `script.spa_notify`): *Spa heating* ("Heating from 34.5°C to 35.5°C for 15:30,
  about 25 min of heating"), *Spa not ready yet*, *Spa ready*. All use the panel unit.

**Ask what next.** When a soak is recorded after a plan (from one hour before the water should be
ready), or two hours after it should have been ready if nobody soaked, the phone asks *Back to
<previous>, keep <target>, or set a new temperature?*. "Set" opens a text field in the panel unit
(whole °F 65–104 or °C 18–40 in halves) and re-asks, up to five rounds, after an invalid value.
Not in the quiet hours (asked at 06:30); no answer within 12 h changes nothing.

**Plan form.** The form's *When* is kept at the earliest possible moment for one-off plans: now for
*Start heating*, now + (target − water) / Heat Rate for *Ready by*. An untouched form follows that
time; a later time chosen by the owner is left alone until it is overtaken.

### Settings dropdowns

The Settings page offers the discrete settings as dropdowns (not sliders): Primary Filter start
hour (00:00–23:00), Filter Change (Off, 10–180 days in tens), ClearRay (Off, 10–360 in tens and
365), Water Refresh (Off, 30–180 in steps of 30). `follow the spa` keeps each dropdown on the value
read from the spa (hourly, and on every change); a current value off the grid, such as 141 days or
`Change Now`, is put at the top of its list. `send a change` calls `number.set_value` on the
matching ESP control when the dropdown is set to a different grid value; the ESP walks the menu
(main README §10), and a failed change makes the dropdown return to the old value because it
follows the read-back. Primary Filter cycles and duration, Heater Mode and the temperature format
are plain ESP selects shown on the page.

### Reminders

Three morning reminders, each at 08:00, each with phone buttons that are their own triggers:

| Reminder | Asks when | Buttons | Moves the date |
|---|---|---|---|
| Bromine | 4 or more calendar days after `input_datetime.spa_last_bromine` (or never) | Yes / No | Yes sets it to now |
| Maintenance | 8 or more calendar days after `input_datetime.last_maintenance` | Done / Not yet | Done sets it to now and adds one to the maintenance count; a maintenance session recorded by the soak/maintenance question also moves it and clears the question |
| Filter rinse | 30 or more calendar days after `input_datetime.last_rinse_filter` | Done / Not yet | Done sets it to now; the power-cut rinse question and a manual edit also clear it |

Not yet, No, or no answer: asked again the next morning. The dashboard shows each *Next* date.
| `spa_power_measurement_circulation`, `spa_power_measurement_jet_pump`, `spa_power_measurement_weekly_summary` | optional (`optional/power-measurement/`): measure circulation / jet power from the house load, log each result, and send a weekly summary to the phone | never change the settings, only measure and report |

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

**Wi-Fi drop (date correct).** Since p29 the ESP kept counting during the drop, so the *total* is
already right when it reconnects: the ESP's Energy Counter is the truth. What is wrong is only
*when* it was spent: the jump lands in the reconnect hour. The automation therefore waits for a
fresh Energy Counter, takes `x = counter now − the last 5-minute statistics row before the drop`
and spreads the part it can explain, `scale × estimate` with `scale = min(1, x / estimate)`, back
over the past hours; the rest stays in the reconnect hour. The meters for today, week, month and
year get the part before their last reset removed. The estimate is the schedule model below. (The
ESP also publishes *Energy Last 24 Hours*, which could give the real hours; see §7.) The estimate
goes minute by minute through the outage:

- controller always;
- circulation inside the Primary Filter cycles (Start, Cycles and Duration as read from the spa);
- one jet pump during the daily 1-minute cleanup at the first cycle start + 1 min;
- nothing for heating or manual use, which cannot be known.

The minutes are grouped by hour, then:

1. **Hourly statistics.** The recorder writes **no hourly row at all** while the source sensor is
   unavailable. Missing hours get a new row with `recorder.import_statistics`: the sum from the
   hour before plus the (scaled) estimate so far. Existing rows get the estimate up to their hour,
   and the last completed hour gets all of it.
2. **Meters.** `utility_meter.calibrate` moves Today, Week, Month and Year by the part that
   belongs to the time before their last reset. Step 1 alone does not stick (§6).
3. A logbook line records the outage and the kWh moved.

Not tested on a real drop since the rewrite.

The schedule is read from the spa at the time of the outage, so a changed filter schedule is
followed automatically.

### Spa audio (who is connected, what is playing)

The spa's Bluetooth audio system is powered from the controller board but reports nothing on the
RS-485 bus (and its connection is invisible to a nearby ESP32: a classic Bluetooth audio link is
not a BLE advertisement, and phones use random addresses). So the phones report it themselves,
with the Home Assistant Companion app on Android:

1. In each phone's app: *Settings → Companion app → Manage sensors* → enable **Bluetooth
   connection** (Nearby devices permission) and **Media session** (Notification access).
2. `input_text.spa_audio_bluetooth_mac` holds the **MAC address** of the spa's audio system (not the
   name, which can be renamed), e.g. `AA:BB:CC:DD:EE:FF`. The Bluetooth connection sensor lists its
   connected devices as `"AA:BB:CC:DD:EE:FF (name)"`; connect once and read the address there.
3. Two template binary sensors, one per phone (*Spa Audio Person 1 / Person 2 Connected*), are on
   when the MAC is in that phone's `connected_paired_devices`.
4. *Spa Audio* (template sensor) gives `Person 1`, `Person 2` or `Not connected` (the one that
   connected last if both are).
5. *Spa Audio Now Playing* reads that person's **Media session** sensor, which keeps one set of
   attributes per app (`title_<package>`, `artist_<package>`, `playback_state_<package>`): it takes
   the app that is playing, otherwise the first, and gives `Title – Artist` (or `Paused: …`). This
   covers any app that publishes a media session (Spotify, YouTube, YouTube Music, podcasts).

The Hot Tub page shows one row, *Spa Audio*, under *Current Status*: the person and what is playing.
Replace the phone entity ids in the templates (`sensor.person1_phone_bluetooth_connection`,
`..._media_session`) with your own. iPhones have no such sensors.

## 5. Dashboard

There are three pages: **Hot Tub**, **Settings** (`hot-tub-settings`) and **Scheduled Plans**
(`hot-tub-plans`), all in one dashboard. Most rows are Mushroom template cards, one line each, with
the numbers and units in smaller type (card-mod); several are shown in °C or °F depending on
`sensor.sundance_spa_temperature_format`.

The Hot Tub view uses the *sections* layout and has these sections:

- **Control panel:** Jet 1, Jet 2, the two circulation states, and the light mode list on one
  row. A Temperature row with −/+ (one panel step), and *Current Status*, which also says
  `Heating blocked (Eco until 17:00)` when the heater mode keeps the heater off and the water is
  at least 1° under the target.
- **Next plan:** one row that appears only when an enabled future plan exists (`sensor.spa_next_plan`).
- **Temperature chart:** 24 h of water and target temperature, with heating runs in red; the
  header numbers use the raw value and `update_delay` so they follow live.
- **Energy:** today / week / month tiles and one bar chart.
- **Soaking and Maintenance:** last and previous session, totals and counts, with the bromine
  row and its *Next* date.
- **Service:** remaining days and last replacement for water, filter and ClearRay bulb; the
  maintenance and filter-rinse rows with their *Next* dates.
- **Run Time:** heater, Pump 1 and Pump 2.
- **Last Heating:** "lowest → highest · minutes · start".

**Scheduled Plans:** the plan rows (info card, on/off icon, trash icon with confirmation) and the
*Add a plan* form (mode, temperature, When, repeat, days, Add button) with the Heat Rate and margin.
A plan is deleted only with the trash icon, never by tapping its row.

**Settings:** Heating (Heater Mode with an Eco/Day explanation card, Primary Filter start / cycles /
duration — the cycles and duration are not greyed out in Eco or Day because they apply in every
mode), the three service counters as dropdowns, temperature format, language (display only, with
no write), the spa clock with *Sync* and *Read* buttons (hold to press), the six Power values, a full-width *Power measurement · last 24 hours* line under them (the measured
power against the setting from the optional power measurement, same text as its weekly summary;
without a house power sensor it only says "no valid measurement"), and a Diagnostics block
(Status, Wi-Fi signal, Decoder Notice).

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
- **Jinja turns numeric-looking text into numbers.** A variable holding `"141"` becomes the number
  141 and no longer equals the state string, so a dropdown comparison never matched and the
  *send a change* automation once sent the current values to the spa. Compare states as text
  inline (`| string`), never through a variable. Also: a variable rendered with `tojson` that
  holds booleans stays a string (use `{{ dict }}`); HA's Jinja has no `extract` filter; a
  `variables` action inside a `repeat` only changes variables that were defined outside it, so
  define the answers before the loop.
- **Saving an automation cancels every wait.** The plan and reminder logic therefore keeps its
  state in helpers and reacts to triggers. The one place that still waits inside the automation is
  the "ask what next" question (it waits for the phone answer); a reload at that moment drops it
  without an answer, which leaves the plan state at `asked`. Moving it to answer triggers, as the
  reminders do, is on the list in §7.
- **A utility meter ignores a decrease at its source.** After a source swap the first update was
  swallowed (about a minute lost); calibrate the meter once afterwards.
- **`history_stats` cannot be set by hand.** It recomputes from the recorder history of its
  source. Pointing it at a new entity starts that day from the new entity's history. To avoid a
  short day, switch the source after midnight, once the new entity has a full day of history.
- **Measured power is a moving target.** The house-load measurements of the circulation vary
  from night to night (other loads in the house never fully stop). Letting them adjust the setting
  made it wander, so the power values are fixed settings on the ESP, and the two measurement
  automations only log, for occasional review.

## 7. Ideas not built yet

Collected while building the rest; none of these is needed for the component. The first group is
automation work, the second group tests that are still open.

**Automations and helpers**

- **"Ask what next" on triggers.** It still polls every 5 minutes for the "2 h after it should have
  been ready" case and waits for the phone answer inside the automation. Exact timing (a `time`
  trigger on a stored moment) and answer triggers like the reminders would remove both.
- **Real hours for a Wi-Fi drop.** The ESP's *Energy Last 24 Hours* (24 hourly Wh values) and
  *Counters At Midnight* are published but the outage automation does not use them yet. With them
  the past hours get the real values, manual use included, and the schedule estimate is only
  needed for a drop longer than 24 hours.
- **Retire the Home Assistant-side power model.** The Spa Energy integration is already
  removed; the Spa Power template is still read by the *Power Now* tile and the circulation
  measurement. Point both at the ESP's Electrical Power, then delete the template.
- **Lock the Heat Rate.** The learned rate (`new = 0.7 × old + 0.3 × measured`) can wander after an
  odd run; an option to freeze it, or to ignore runs with a soak in the middle.
- **Maintenance count on the page** next to the soaking counts (the sensor exists).
- **Wrap long plan texts** on the Scheduled Plans page instead of shrinking them.
- **Statistics repair.** The *Today* time counters changed from `history_stats` (measurement) to
  utility meters (total increasing) on the same entity IDs; the old statistics rows keep the old
  type and can be repaired.
- **A "last settings read" time on the ESP** (a timestamp sensor): a good *Read Settings* leaves no
  trace in Home Assistant today (main README §14).
- **Per-person notifications.** Everything goes to one phone; the notify target is one line in
  `script.spa_notify` and each question automation.

**Rejected, on purpose**

- *Automatic power calibration.* The house-load measurements vary from night to night, so the
  Power values are fixed settings and the two measurement automations only log.
- *Writing a setting to "sync" a value.* A setting is sent only from an explicit pick or answer.

**Still to be seen on the real thing**

- Heating with Eco and the jets on, and the Heater Mode → Auto → back switch of a plan, live.
- A real Wi-Fi drop through the rewritten outage automation.
- The Back / Keep / Set answer after a soak, and the real Bromine / Maintenance / Filter rinse
  notification buttons.
