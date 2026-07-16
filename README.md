# SHTC3_v2

WLED usermod for Sensirion SHTC3 temperature/humidity sensor via I2C.

## Features

- Periodic SHTC3 readings (temperature + relative humidity)
- Publishes values to `/json/info` in both:
  - `u` (Info page style)
  - `sensor` (sensor namespace)
- Adds a headline row in the Info tab so the values are clearly identified as SHTC3 sensor readings
- Publishes temperature and humidity over MQTT after each successful read
- Configurable enable state and polling interval
- Optional Fahrenheit output for temperature display and MQTT

## Configuration

The usermod writes/reads config under key `SHTC3`.

- `Enabled` (bool): enable sensor reading
- `CheckInterval` (int, seconds): read interval, range 5..600
- `UseFahrenheit` (bool): show temperature in Fahrenheit instead of Celsius

## JSON output

In `/json/info`:

- `u["Temperature"] = [value, "°C"]`
- `u["Humidity"] = [value, "%"]`
- `u["SHTC3 Sensor"] = ["Temperature and Humidity"]`
- `sensor["SHTC3 Temperature"] = [value, "°C"]`
- `sensor["SHTC3 Humidity"] = [value, "%"]`

When Fahrenheit is enabled, the temperature unit changes to `°F` in both JSON outputs and MQTT.

When unavailable, values are reported as `null`.

If the sensor has reported at least one valid reading, the JSON output keeps the last known good values across transient read failures instead of dropping back to `null` immediately.

## Installation

1. Add `custom_usermods = https://github.com/lost-hope/SHTC3_v2.git` in your enviroment in `platformio_override.ini`.
2. Build and flash WLED as usual.

## Notes

- SHTC3 uses a fixed I2C address (`0x70`).
- Keep SDA/SCL wiring and pull-ups according to your board requirements.
