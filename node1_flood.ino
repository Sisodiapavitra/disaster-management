#include <SPI.h>
#include <LoRa.h>

// Flood sensor node. Sends one LoRa packet every two seconds.

#define TRIG_PIN 32
#define ECHO_PIN 35
#define RAIN_PIN 34

#define LORA_SS 5
#define LORA_RST 14
#define LORA_DIO0 26
#define LORA_FREQUENCY 433E6

#define FLOOD_WARNING_DISTANCE 15.0
#define FLOOD_DANGER_DISTANCE 8.0
#define RAIN_WARNING 2500
#define RAIN_DANGER 1500

float readDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) {
    return -1.0;
  }
  return duration * 0.0343 / 2.0;
}

String floodStatus(float distanceCm, int rainValue) {
  if (distanceCm > 0 && distanceCm <= FLOOD_DANGER_DISTANCE) {
    return "DANGER";
  }
  if (distanceCm > 0 && distanceCm <= FLOOD_WARNING_DISTANCE) {
    return "WARNING";
  }
  if (rainValue <= RAIN_DANGER) {
    return "DANGER";
  }
  if (rainValue <= RAIN_WARNING) {
    return "WARNING";
  }
  return "NORMAL";
}

void sendFloodPacket(float distanceCm, int rainValue, const String& status) {
  // The central gateway expects: FLOOD,distance_cm,rain_adc,status
  LoRa.beginPacket();
  LoRa.print("FLOOD,");
  LoRa.print(distanceCm < 0 ? 0.0 : distanceCm, 1);
  LoRa.print(",");
  LoRa.print(rainValue);
  LoRa.print(",");
  LoRa.print(status);
  LoRa.endPacket();
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(RAIN_PIN, INPUT);

  SPI.begin(18, 19, 23, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQUENCY)) {
    while (true) {
      delay(1000);
    }
  }

  LoRa.setTxPower(17);
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
}

void loop() {
  float distanceCm = readDistanceCm();
  int rainValue = analogRead(RAIN_PIN);
  String status = floodStatus(distanceCm, rainValue);

  sendFloodPacket(distanceCm, rainValue, status);

  Serial.print("FLOOD,distance_cm=");
  Serial.print(distanceCm);
  Serial.print(",rain_adc=");
  Serial.print(rainValue);
  Serial.print(",status=");
  Serial.println(status);

  delay(2000);
}
