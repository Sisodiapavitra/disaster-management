# Sentinel Grid

Rule-based environmental hazard monitoring dashboard for the ESP32 + sensor prototype.

## Run it

Install Python 3.10+ and run:

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
python -m uvicorn main:app --reload --host 127.0.0.1 --port 8000
```

Open `http://127.0.0.1:8000`. `run_server.bat` starts the same server on Windows.

## ESP32 input

Upload [node1_flood.ino](node1_flood.ino) to ESP32 #1 and [node2_central_gateway.ino](node2_central_gateway.ino) to ESP32 #2. Both sketches use the same LoRa settings and packet format. Connect only ESP32 #2 to the laptop by USB.

Use the dashboard's **Connect ESP32** button and enter the central ESP32 COM port. FastAPI reads one JSON object per line over USB serial at 115200 baud. Supported keys:

```json
{"smoke_index":1850,"temperature":31.6,"pressure":1008.4,"water_distance_cm":22.4,"rain_value":3200,"fire_status":"WARNING","flood_status":"NORMAL","overall_status":"WARNING"}
```

The central sketch prints telemetry in this format. FastAPI owns the USB connection, evaluates the thresholds, and exposes the decision to the dashboard.

## API endpoints

- `GET /api/state` returns readings, thresholds, and risk states.
- `POST /api/telemetry` accepts ESP32 JSON telemetry.
- `POST /api/serial/connect` starts the central ESP32 USB reader.
- `POST /api/serial/disconnect` stops the central ESP32 USB reader.
- `PUT /api/thresholds` updates the rule thresholds.
- `GET /docs` opens the interactive FastAPI documentation.

## Default thresholds

- Forest fire: MQ ADC `1800` warning, `2500` danger
- Air quality: PM2.5 `60` watch, `150` critical
- Flood distance: `15 cm` warning, `8 cm` danger; rain ADC: `2500` warning, `1500` danger

Thresholds can be edited in the dashboard. These are starting values for a prototype and should be calibrated against the actual sensor, site, and local authority guidance before operational use.
