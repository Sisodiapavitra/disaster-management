from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
from random import uniform
import threading
from typing import Any

from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

try:
    import serial
except ImportError:
    serial = None

BASE_DIR = Path(__file__).resolve().parent

app = FastAPI(title="Sentinel Grid API", version="1.0.0")
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["GET", "POST", "PUT"],
    allow_headers=["*"],
)

class Telemetry(BaseModel):
    smoke_index: float | None = Field(default=None, ge=0)
    temperature: float | None = None
    pressure: float | None = None
    pm2_5: float | None = Field(default=None, ge=0)
    pm10: float | None = Field(default=None, ge=0)
    atmospheric_pressure: float | None = None
    water_distance_cm: float | None = Field(default=None, ge=0)
    rain_value: float | None = Field(default=None, ge=0)
    rainfall: float | None = Field(default=None, ge=0)
    forest_battery: float | None = Field(default=None, ge=0, le=100)
    flood_battery: float | None = Field(default=None, ge=0, le=100)

class Thresholds(BaseModel):
    smokeWatch: float = Field(default=1800, gt=0)
    smokeCritical: float = Field(default=2500, gt=0)
    pmWatch: float = Field(default=60, gt=0)
    pmCritical: float = Field(default=150, gt=0)
    floodWarningDistance: float = Field(default=15, gt=0)
    floodDangerDistance: float = Field(default=8, gt=0)
    rainWarning: float = Field(default=2500, gt=0)
    rainDanger: float = Field(default=1500, gt=0)


class SerialConnection(BaseModel):
    port: str = Field(min_length=1)
    baud_rate: int = Field(default=115200, ge=1200, le=2000000)

readings: dict[str, float] = {
    "smoke": 18, "forestTemp": 31.4, "forestBattery": 87,
    "pm25": 24, "pm10": 42, "pressure": 1008,
    "waterDistance": 30.0, "rain": 4095, "floodBattery": 92,
}
rules = Thresholds()
last_updated = datetime.now(timezone.utc)
serial_thread: threading.Thread | None = None
serial_stop = threading.Event()
serial_state = {"connected": False, "port": None, "error": None}
legacy_serial_values: dict[str, Any] = {}

ALIASES = {
    "smoke_index": "smoke", "smokeValue": "smoke", "temperature": "forestTemp", "pm2_5": "pm25",
    "pressure": "pressure", "pm10": "pm10", "atmospheric_pressure": "pressure", "water_distance_cm": "waterDistance",
    "waterDistance": "waterDistance",
    "rainfall": "rain", "rain_value": "rain", "forest_battery": "forestBattery", "flood_battery": "floodBattery",
}


def risk_level(value: float, watch: float, critical: float) -> str:
    if value >= critical:
        return "critical"
    if value >= watch:
        return "watch"
    return "normal"


def flood_risk(distance: float, rain: float) -> str:
    if (distance > 0 and distance <= rules.floodDangerDistance) or rain <= rules.rainDanger:
        return "critical"
    if (distance > 0 and distance <= rules.floodWarningDistance) or rain <= rules.rainWarning:
        return "watch"
    return "normal"


def evaluate() -> dict[str, Any]:
    states = {
        "forest": risk_level(readings["smoke"], rules.smokeWatch, rules.smokeCritical),
        "air": risk_level(readings["pm25"], rules.pmWatch, rules.pmCritical),
        "flood": flood_risk(readings["waterDistance"], readings["rain"]),
    }
    overall = "critical" if "critical" in states.values() else "watch" if "watch" in states.values() else "normal"
    return {"states": states, "overall": overall}


def state_payload() -> dict[str, Any]:
    decision = evaluate()
    return {
        "readings": readings,
        "thresholds": rules.model_dump(),
        "states": decision["states"],
        "overall": decision["overall"],
        "updated_at": last_updated.isoformat(),
        "serial": serial_state,
    }


def ingest_serial_line(line: str) -> None:
    line = line.strip()
    if not line:
        return

    try:
        payload = json.loads(line)
    except json.JSONDecodeError:
        payload = None

    if isinstance(payload, dict):
        receive_telemetry(Telemetry.model_validate(payload))
        return

    # Accept the human-readable lines emitted by the current gateway sketch.
    # JSON remains preferred, but this keeps the existing firmware usable.
    matches = (
        (r"^(?:Water )?Distance:\s*(-?\d+(?:\.\d+)?)\s*cm", "water_distance_cm", float),
        (r"^Rain(?: Sensor)?:\s*(\d+(?:\.\d+)?)", "rain_value", float),
        (r"^MQ Value:\s*(\d+(?:\.\d+)?)", "smoke_index", float),
        (r"^Temperature:\s*(-?\d+(?:\.\d+)?)", "temperature", float),
        (r"^Pressure:\s*(-?\d+(?:\.\d+)?)", "pressure", float),
    )
    for pattern, key, converter in matches:
        match = re.search(pattern, line, re.IGNORECASE)
        if match:
            legacy_serial_values[key] = converter(match.group(1))
            receive_telemetry(Telemetry.model_validate(legacy_serial_values))
            return

    status_match = re.search(r"^(Fire|Flood|Overall)(?: Status| Decision)?:\s*(NORMAL|WARNING|DANGER|UNKNOWN)", line, re.IGNORECASE)
    if status_match:
        legacy_serial_values[f"{status_match.group(1).lower()}_status"] = status_match.group(2).upper()


def serial_worker(port: str, baud_rate: int) -> None:
    global serial_state
    if serial is None:
        serial_state = {"connected": False, "port": port, "error": "Install pyserial with pip install pyserial"}
        return
    try:
        with serial.Serial(port, baud_rate, timeout=1) as connection:
            serial_state = {"connected": True, "port": port, "error": None}
            while not serial_stop.is_set():
                line = connection.readline().decode("utf-8", errors="ignore").strip()
                if line:
                    ingest_serial_line(line)
    except Exception as error:
        serial_state = {"connected": False, "port": port, "error": str(error)}


def stop_serial() -> None:
    serial_stop.set()
    if serial_thread and serial_thread.is_alive():
        serial_thread.join(timeout=2)


@app.get("/api/health")
def health() -> dict[str, str]:
    return {"status": "ok", "service": "sentinel-grid"}


@app.post("/api/serial/connect")
def connect_serial(payload: SerialConnection) -> dict[str, Any]:
    global serial_thread, serial_stop, serial_state
    stop_serial()
    serial_stop = threading.Event()
    serial_state = {"connected": False, "port": payload.port, "error": None}
    serial_thread = threading.Thread(target=serial_worker, args=(payload.port, payload.baud_rate), daemon=True)
    serial_thread.start()
    return state_payload()


@app.post("/api/serial/disconnect")
def disconnect_serial() -> dict[str, Any]:
    global serial_state
    stop_serial()
    serial_state = {"connected": False, "port": None, "error": None}
    return state_payload()


@app.get("/api/state")
def get_state() -> dict[str, Any]:
    return state_payload()


@app.post("/api/telemetry")
def receive_telemetry(payload: Telemetry) -> dict[str, Any]:
    global last_updated
    values = payload.model_dump(exclude_none=True)
    for key, value in values.items():
        readings[ALIASES[key]] = value
    last_updated = datetime.now(timezone.utc)
    return state_payload()


@app.post("/api/demo-tick")
def demo_tick() -> dict[str, Any]:
    global last_updated
    readings["smoke"] = max(10, readings["smoke"] + uniform(-3.8, 4.2))
    readings["pm25"] = max(12, readings["pm25"] + uniform(-2.5, 2.5))
    readings["waterDistance"] = max(2, readings["waterDistance"] + uniform(-0.8, 0.8))
    last_updated = datetime.now(timezone.utc)
    return state_payload()


@app.put("/api/thresholds")
def update_thresholds(payload: Thresholds) -> dict[str, Any]:
    global rules
    if payload.smokeWatch >= payload.smokeCritical or payload.pmWatch >= payload.pmCritical or payload.floodDangerDistance >= payload.floodWarningDistance or payload.rainDanger >= payload.rainWarning:
        raise HTTPException(status_code=422, detail="Fire and air Watch thresholds must be lower than Critical; flood Danger thresholds must be lower than Warning thresholds")
    rules = payload
    return state_payload()


app.mount("/", StaticFiles(directory=BASE_DIR, html=True), name="static")


@app.get("/")
def dashboard() -> FileResponse:
    return FileResponse(BASE_DIR / "index.html")
