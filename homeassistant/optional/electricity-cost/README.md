# Spa electricity cost (optional)

Prices the spa's electricity use in your own currency with a flat price per kWh and, if you have solar panels or a
battery, values the part of the spa's power that does not come from the grid at your feed-in price (or a battery price)
instead of the purchase price.

It only reads values. It does not change the spa, the firmware or any other entity.

## How the cost is calculated

Every 10 seconds:

1. **Price.** The helper `Spa Cost Flat Price` (what you pay per kWh, including the tax you want counted).
2. **Where the spa's power comes from.** Electric power is not tied to a source, so the spa takes its power from
   the house supply **in proportion**. With the grid purchase `G` (the meter, positive = buying, 0 when selling), the
   battery discharge `B` and the rest of the house load `P` (the panels: house load minus `G` minus `B`, 0 if
   negative), the share of each source is `G` / (`G` + `B` + `P`), `B` / (...) and `P` / (...). Example: the house
   buys 33 W and the battery gives 300 W: the spa is about 10 % grid and 90 % battery, not 100 % grid because its own
   15 W happens to be below the 33 W of the meter. With no house load sensor the split is grid / battery only (the
   panels when both are zero). With no grid sensor everything is priced at the purchase price.
3. **Cost rate** (currency per hour) = grid kW x purchase price + solar kW x feed-in price + battery kW x battery price.
   The feed-in price is what you would have earned if the energy had been sold, so selling less is a cost.
   The battery is priced like your own generation (the feed-in price) unless you turn on the switch `Spa Cost Separate Battery Price` and set `Spa Cost Battery Price`, for example to include conversion losses.
4. The rate is integrated into a running total, `sensor.spa_electricity_cost`, with daily, weekly and monthly meters.

If you have no grid sensor, leave `grid_power_entity` empty: all of the spa's power is priced at the purchase price. `load_power_entity` (the whole-house consumption in W) is optional; it sharpens the split when the panels feed the house at the same time.

This is an **allocation model, not a bill**. Your utility bills by its own meter and rules (net metering, billing
periods, fixed charges); the fixed and capacity charges are not included because they do not depend on the spa.
The spa's power is an estimate from the ESP, not a measurement.

## Install

1. Copy `optional-packages/spa_electricity_cost.yaml` to `<config>/packages/` (packages must be enabled).
2. Edit the block between `EDIT HERE` and `END EDIT HERE`: your entity ids and the sign conventions.
3. Restart Home Assistant (a YAML check first is wise), then set the helpers: `Spa Cost Currency`
   (a symbol such as ₪ $ € for the cards), `Spa Cost Currency Code` (letters such as ILS, shown in brackets in the
   dropdown; leave it empty for no brackets), `Spa Cost Flat Price`, `Spa Cost Feed-in Price` and, if you want a separate
   battery price, the switch `Spa Cost Separate Battery Price` with `Spa Cost Battery Price`. Prices are what you pay, in
   your currency, per kWh, **including** any tax you want counted.

Set your currency symbol in the line `unit_of_measurement` of the cost-rate sensor (for example `₪/h`, `$/h`, `€/h`): the total, the daily / weekly / monthly meters and the graphs take the plain symbol from it, so they show values like 1.52 ₪. The helper `Spa Cost Currency` is only a label for cards.

Tip: the sensors change every 10 seconds; if the database size matters, exclude `sensor.spa_electricity_cost_rate`
from the recorder (the total and the meters are enough).

## Changing prices (from a date)

The helpers `Spa Cost Flat Price`, `Feed-in Price` and `Battery Price` are the prices **in force**; do not edit them by
hand. To change prices, type the new ones into the matching `Spa Cost Next ...` helpers, set `Spa Cost Next Prices From`
(a date, the prices change at 00:00) and switch on `Spa Cost Schedule Price Change`. The automation
`Spa - Cost apply scheduled prices` copies them into force at midnight of that date. If the date has already begun, they
apply at once and the difference is also corrected back to the start of that date (script `Spa - Cost retro correction`:
the spa energy since then, grid / battery from the recorder, priced with the new minus the old prices, added to the total
and to the day / week / month meters; bars of past days in the statistics graph keep their old values, the correction
shows in the period it was made). `Spa - Cost sync next prices` then resets the Next helpers to the prices in force.
Switching the switch off before the date cancels the change. The total, the meters and the graphs keep what was already
counted.

## After the ESP was offline (Wi-Fi outage)

While the ESP is offline its power is unknown, so the live cost stops. The ESP keeps counting its energy, so when it is back
the package catches up (automations "Spa - Cost gap start" and "Spa - Cost catch-up after outage"): the energy counted
during the gap (counter difference) is spread over the gap by the ESP's hourly profile of the last 24 hours, priced in
5 minute steps with the grid / battery power Home Assistant recorded in the gap (recorder statistics), and added to the
total as one tick of the rate sensor (the attribute live_rate is the rate without it). Gaps from 30 s to 22 h are caught
up; a logbook entry shows the result. A bin with no recorded grid data uses the previous bin's values. Edit the ESP entity
ids in those two automations if yours differ. Running the catch-up automation by hand (Run actions) uses the current
energy counter as the end of the gap.

## Dashboard

`dashboard.yaml` has the cards for the Hot Tub dashboards: an "Electricity Cost" section for the Settings view (currency,
the price in use, the prices in force and the fields to change them from a date), a statistics card for the Hot Tub
view, shown when the helper `input_select.spa_history_metric` is set to the cost option (add the option `Cost` to the
helper; the package automation `Spa - Cost history option label` renames it to `Cost (CODE)` after the helper
`Spa Cost Currency Code`, for example `Cost (ILS)`, and to plain `Cost` when the code is empty) and an "Energy Cost" card
with today / week / month.

## Entities

| Entity | Meaning |
|---|---|
| `sensor.spa_electricity_price` | price per kWh now |
| `sensor.spa_electricity_cost_rate` | currency per hour now, attributes `live_rate`, `grid_kw`, `solar_kw`, `battery_kw`, `price` |
| `sensor.spa_electricity_cost` | running total |
| `sensor.spa_electricity_cost_daily` / `_weekly` / `_monthly` | meters |

## Time-of-use tariffs

A version with time-of-use tariffs (rules by months, days, hours and holidays, editable in the dashboard, with an Israel
Electric Corporation example) was developed and is left out for now to keep this simple. If it interests you, open an issue.
