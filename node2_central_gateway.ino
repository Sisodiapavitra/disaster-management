#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>

// Central gateway. Receives Node 1 over LoRa and sends JSON over USB.
// FastAPI reads the USB serial port at 115200 baud.

#define LORA_SS 5
#define LORA_RST 14
#define LORA_DIO0 26
#define LORA_FREQUENCY 433E6

#define MQ_PIN 34
#define SDA_PIN 21
#define SCL_PIN 22

#define GREEN_LED 25
#define YELLOW_LED 27
#define RED_LED 33

#define FIRE_WARNING 1800
#define FIRE_DANGER 2500
#define TELEMETRY_INTERVAL_MS 1000
#define FLOOD_TIMEOUT_MS 10000

Adafruit_BMP280 bmp;
bool bmpReady = false;

float floodDistanceCm = 0.0;
int floodRain = 4095;
String floodStatus = "UNKNOWN";
unsigned long lastFloodPacket = 0;

int smokeValue = 0;
float temperature = 0.0;
float pressure = 0.0;
String fireStatus = "NORMAL";
String overallStatus = "NORMAL";
unsigned long lastTelemetry = 0;

void setLEDStatus(const String& status) {
  digitalWrite(GREEN_LED, status == "NORMAL" ? HIGH : LOW);
  digitalWrite(YELLOW_LED, status == "WARNING" ? HIGH : LOW);
  digitalWrite(RED_LED, status == "DANGER" ? HIGH : LOW);
}

void calculateFireStatus() {
  if (smokeValue >= FIRE_DANGER) {
    fireStatus = "DANGER";
  } else if (smokeValue >= FIRE_WARNING) {
    fireStatus = "WARNING";
  } else {
    fireStatus = "NORMAL";
  }
}

void calculateFloodStatusFromValues() {
  if (floodDistanceCm > 0 && floodDistanceCm <= 8.0) {
    floodStatus = "DANGER";
  } else if (floodDistanceCm > 0 && floodDistanceCm <= 15.0) {
    floodStatus = "WARNING";
  } else if (floodRain <= 1500) {
    floodStatus = "DANGER";
  } else if (floodRain <= 2500) {
    floodStatus = "WARNING";
  } else {
    floodStatus = "NORMAL";
  }
}

void calculateOverallStatus() {
  // Do not keep an old flood warning after communication is lost.
  if (lastFloodPacket > 0 && millis() - lastFloodPacket > FLOOD_TIMEOUT_MS) {
    floodStatus = "UNKNOWN";
  }

  if (fireStatus == "DANGER" || floodStatus == "DANGER") {
    overallStatus = "DANGER";
  } else if (fireStatus == "WARNING" || floodStatus == "WARNING") {
    overallStatus = "WARNING";
  } else {
    overallStatus = "NORMAL";
  }
  setLEDStatus(overallStatus);
}

void readFireSensors() {
  smokeValue = analogRead(MQ_PIN);
  if (bmpReady) {
    temperature = bmp.readTemperature();
    pressure = bmp.readPressure() / 100.0F;
  }
  calculateFireStatus();
}

void receiveFloodPacket() {
  int packetSize = LoRa.parsePacket();
  if (!packetSize) {
    return;
  }

  String incoming;
  while (LoRa.available()) {
    incoming += static_cast<char>(LoRa.read());
  }
  incoming.trim();

  // Expected packet: FLOOD,distance_cm,rain_adc,status
  if (!incoming.startsWith("FLOOD,")) {
    return;
  }

  int comma1 = incoming.indexOf(',');
  int comma2 = incoming.indexOf(',', comma1 + 1);
  int comma3 = incoming.indexOf(',', comma2 + 1);
  if (comma1 < 0 || comma2 < 0 || comma3 < 0) {
    return;
  }

  floodDistanceCm = incoming.substring(comma1 + 1, comma2).toFloat();
  floodRain = incoming.substring(comma2 + 1, comma3).toInt();
  floodStatus = incoming.substring(comma3 + 1);
  floodStatus.trim();
  lastFloodPacket = millis();
  calculateFloodStatusFromValues();
}

void sendUsbTelemetry() {
  // One JSON object per line. FastAPI ignores other serial text safely.
  Serial.print("{\"smoke_index\":");
  Serial.print(smokeValue);
  Serial.print(",\"temperature\":");
  Serial.print(temperature, 1);
  Serial.print(",\"pressure\":");
  Serial.print(pressure, 1);
  Serial.print(",\"water_distance_cm\":");
  Serial.print(floodDistanceCm, 1);
  Serial.print(",\"rain_value\":");
  Serial.print(floodRain);
  Serial.print(",\"fire_status\":\"");
  Serial.print(fireStatus);
  Serial.print("\",\"flood_status\":\"");
  Serial.print(floodStatus);
  Serial.print("\",\"overall_status\":\"");
  Serial.print(overallStatus);
  Serial.println("\"}");
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  pinMode(MQ_PIN, INPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  setLEDStatus("NORMAL");

  Wire.begin(SDA_PIN, SCL_PIN);
  bmpReady = bmp.begin(0x76) || bmp.begin(0x77);

  SPI.begin(18, 19, 23, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQUENCY)) {
    setLEDStatus("DANGER");
    while (true) {
      delay(1000);
    }
  }

  LoRa.setTxPower(17);
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);

  readFireSensors();
  calculateOverallStatus();
}

void loop() {
  receiveFloodPacket();
  readFireSensors();
  calculateOverallStatus();

  if (millis() - lastTelemetry >= TELEMETRY_INTERVAL_MS) {
    lastTelemetry = millis();
    sendUsbTelemetry();
  }

  delay(20);
}
