/*
*******************************************************************************
* Copyright (c) 2025 by AtomS3Stack
* SPDX-FileCopyrightText: 2025 AtomS3Stack Technology CO LTD
*
* SPDX-License-Identifier: MIT
* Equipped with ATOM DTU NB MQTT sample source code
* describe: ATOM DTU NB MQTT Clien Example.
* Libraries:
    - [TinyGSM - modify](https://github.com/m5stack/TinyGSM.git)   
* lib_deps = 
	  AtomS3stack/AtomS3Atom@^0.1.2
	  fastled/FastLED@^3.9.14
	  knolleary/PubSubClient@^2.8
	  vshymanskyy/StreamDebugger@^1.0.1
* date：2025/3/26
*******************************************************************************
*/
// 1. Define the Serial port used for displaying debug outputs (usually Serial)
#define SerialMon Serial
#define TINY_GSM_DEBUG SerialMon

// 2. Enable raw AT command dumping
// #define DUMP_AT_COMMANDS

// --- Make sure these are DEFINED BEFORE including the library ---
// #include <TinyGsmClient.h>

#include <M5AtomS3.h>
#include "ATOM_DTU_NB.h"
#include <PubSubClient.h>
#include <TinyGsmClient.h>
#include <time.h>
#include <sys/time.h>

#include <ModbusMaster.h>

ModbusMaster sensorNode;

#define RS485_TX_PIN 7
#define RS485_RX_PIN 8


#define MQTT_BROKER   "lon1.blynk.cloud"  //thingscloud gz-3 is a zone, please change to your own corresponding zone
#define MQTT_PORT     1883        //Port number

#define UPLOAD_INTERVAL 10000
#define mqtt_devid "device1" //Device Arbitrary name
#define mqtt_pubid "device"        //username
#define mqtt_password "exv5-ruwvPVvyfFPV6TXRKlknmtnwm1_" //password projectkey


// Receive device properties to get the command topic
#define ONENET_TOPIC_GET "testtopic/10Download" 
// Send data subject on the device
#define ONENET_TOPIC_POST  "testtopic/10Download"
int num=0;
uint32_t lastReconnectAttempt = 0;

//#define DUMP_AT_COMMANDS    //If you need to debug, you can open this macro definition and TinyGsmClientSIM7028.h line 13 //#define TINY_GSM_DEBUG Serial
#ifdef DUMP_AT_COMMANDS 
#include <StreamDebugger.h>
    StreamDebugger debugger(SerialAT, SerialMon);
    TinyGsm modem(debugger, ATOM_DTU_SIM7028_RESET);
#else
    TinyGsm modem(SerialAT, ATOM_DTU_SIM7028_RESET);
#endif

TinyGsmClient tcpClient(modem);
PubSubClient mqttClient(MQTT_BROKER, MQTT_PORT, tcpClient);

void mqttCallback(char *topic, byte *payload, unsigned int len);
bool mqttConnect(void);
void nbConnect(void);
void initModbus(void);
void pollSensorAndPublish(void);
bool setModbusRelayState(bool);

void log(String info) {
    SerialMon.println(info);
}

void setup() {
    AtomS3.begin(true);
    // delay(10000);
    Serial.println(">>ATOM DTU NB MQTT TEST");
    SerialAT.begin(SIM7028_BAUDRATE, SERIAL_8N1, ATOM_DTU_SIM7028_RX,
                   ATOM_DTU_SIM7028_TX);
    AtomS3.dis.drawpix(0x0000ff);
    nbConnect();
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);                    // Set the server to which the client is connected.  server using port 1883
    mqttClient.setCallback(mqttCallback);
    initModbus();
}

void loop() {
    static unsigned long timer = 0;

    if (!mqttClient.connected()) {
        log(">>MQTT NOT CONNECTED");
        log(mqttClient.state());
        // Reconnect every 10 seconds
        AtomS3.dis.drawpix(0xff0000);
        uint32_t t = millis();
        if (t - lastReconnectAttempt > 3000L) {
            lastReconnectAttempt = t;
            if (mqttConnect()) {
                lastReconnectAttempt = 0;
            }
        }
        delay(100);
    }
    if (millis() >= timer) {
        timer = millis() + UPLOAD_INTERVAL;
        if (mqttClient.connected())
        {

          /// First concatenate the json string
          char param[120];
          sprintf(param, "{\"num\":%d}",num); /// We write the data to be uploaded in the param
          num+=1;
          if(num>256){
            num=0;
          }

        //   log("public the data:"); 
        //   log(param);
        //   log("\n");
       
        //   mqttClient.publish(ONENET_TOPIC_POST, param);

          pollSensorAndPublish();

          delay(500);
          
        }
    }
    AtomS3.dis.drawpix(0x00ff00);
    mqttClient.loop();
}

void mqttCallback(char *topic, byte *payload, unsigned int len) {
    char info[len + 1];  // Add a location for the null termination character
    memcpy(info, payload, len);
    info[len] = '\0';  // Add the null terminator
    log("Message arrived:"+String(info));
    log("Topic received: " + String(topic));  // Print the received topic
   
}

bool mqttConnect(void) {
    log("Connecting to ");
    log(MQTT_BROKER);
  
    bool status =mqttClient.connect(mqtt_devid, mqtt_pubid, mqtt_password); 
    if (status == false) {
        int errorCode = mqttClient.state();
        log("MQTT Connection failed with error code: " + String(errorCode));
        return false;
    }
    log("MQTT CONNECTED!");
   
    mqttClient.subscribe(ONENET_TOPIC_GET);
    return mqttClient.connected();
}

void nbConnect() {
  SerialMon.println("Waiting for network...");
  
  // The SIM7028 automatically connects to the APN behind the scenes 
  // once it registers to a base station tower.
  if (!modem.waitForNetwork(60000L)) { 
    SerialMon.println("Network registration failed.");
    delay(3000);
    return;
  }
  
  SerialMon.println("Network registered and ready!");
}

void initModbus() {
    log("Initializing ESP32-S3 Native Hardware Serial...");
    
    // 1. Force the previous engine allocation states clear
    Serial2.end();
    delay(100);
    
    // 2. Initialize Serial 2 on the ESP32-S3 matrix.
    // Notice the exact parameter routing order: Baud, Mode, RX_Pin, TX_Pin
    Serial2.begin(9600, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
    
    // 3. CRITICAL FOR ESP32-S3: Force the internal serial engine to release 
    // the half-duplex line listening state machine automatically.
    Serial2.setPins(RS485_RX_PIN, RS485_TX_PIN); 
    
    // 4. Bind the Modbus node object to our clean ESP32-S3 driver pipeline
    sensorNode.begin(1, Serial2);
    
    log("ESP32-S3 Modbus Engine Stabilized on Pins G7/G8.");
}

static bool relayState = false; // Track the current state of the relay

void pollSensorAndPublish() {
    uint8_t result;
    
    // --- LAYOUT TEST A: Try starting at Address 0x0001 ---
    log("Polling XY-MD02 (Attempting Register Offset 0x0001)...");
    result = sensorNode.readInputRegisters(0x0001, 2);
    
    // If Offset 1 fails with an address exception, automatically drop back to Offset 2
    if (result == 0x02) {
        log("Address Exception 0x02 hit on offset 1. Attempting Register Offset 0x0002...");
        result = sensorNode.readInputRegisters(0x0002, 2);
    }

    // --- EVALUATE THE FINAL RETURN FRAME ---
    if (result == sensorNode.ku8MBSuccess) {
        // Snatch the data pairs from the running index offsets safely
        float temperature = (int16_t)sensorNode.getResponseBuffer(0x00) / 10.0;
        float humidity    = (int16_t)sensorNode.getResponseBuffer(0x01) / 10.0;
        
        log("🎉 SUCCESS! Live Environmental Telemetry Captured:");
        log("--> Temperature: " + String(temperature, 1) + "°C");
        log("--> Humidity:    " + String(humidity, 1) + "%");
        
        // Feed the verified metrics into your stable Blynk pipeline
        // if (isMqttConnected) {
            char payload[120];
            sprintf(payload, "{\"temp\":%.1f,\"hum\":%.1f}", temperature, humidity); 
            // publishHardwareMQTT(ONENET_TOPIC_POST, payload);
            mqttClient.publish(ONENET_TOPIC_POST, payload);

            relayState = !relayState; // Toggle the relay state for demonstration
            setModbusRelayState(relayState);
        // }
    } else {
        // Log standard debug flags (e.g., 0xE2 = Hardware line drop, 0x03 = Illegal Data Value)
        log("Modbus Pipeline Error Frame: 0x" + String(result, HEX));
    }
}

#define SENSOR_SLAVE_ID 1
#define RELAY_SLAVE_ID  2

#define RELAY_CONTROL_REG 1
#define RELAY_VAL_ON      256  // 0x0100
#define RELAY_VAL_OFF     512  // 0x0200

// Helper function to set the relay state
bool setModbusRelayState(bool turnOn) {
    // 1. Temporarily re-bind the Modbus master instance to talk to Slave ID 2
    sensorNode.begin(RELAY_SLAVE_ID, Serial2);
    delay(10); 

    uint16_t controlValue = turnOn ? RELAY_VAL_ON : RELAY_VAL_OFF;
    log("Sending Relay " + String(turnOn ? "ON" : "OFF") + " command to Slave 2...");

    // 2. Execute Function Code 06 (Write Single Register)
    uint8_t result = sensorNode.writeSingleRegister(RELAY_CONTROL_REG, controlValue);

    // 3. Immediately switch the binding token back to Slave ID 1 so the sensor polling doesn't break
    sensorNode.begin(SENSOR_SLAVE_ID, Serial2);

    if (result == sensorNode.ku8MBSuccess) {
        log("🎉 Relay State Changed Successfully!");
        return true;
    } else {
        log("Relay Command Failed! Modbus Error: 0x" + String(result, HEX));
        return false;
    }
}