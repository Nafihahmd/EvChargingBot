#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>

/********** MACROS ************/
#define NSS 0
#define RESET 4
#define DIO0 5

/********** Global Variables ************/
int counter = 0;

void setup() {
  Serial.begin(9600);
  while (!Serial);

  Serial.println("LoRa Receiver");
  // Set LED Flash as output
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN,HIGH);

  LoRa.setPins(NSS, RESET, DIO0);
  if (!LoRa.begin(433E6)) {
    Serial.println("Starting LoRa failed!");
    while (1);
  }
}

void loop() {
  // try to parse packet
  char receivedPacket;
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    // received a packet
    Serial.print("Received packet '");

    // read packet
    while (LoRa.available()) {
      receivedPacket = (char)LoRa.read();
      Serial.print(receivedPacket);
      if(receivedPacket)
      {   
        digitalWrite(LED_BUILTIN, LOW);
        Serial.println("LED ON");
      }
      else        
        digitalWrite(LED_BUILTIN, HIGH);
    }

    // print RSSI of packet
    Serial.print("' with RSSI ");
    Serial.println(LoRa.packetRssi());
  }
}