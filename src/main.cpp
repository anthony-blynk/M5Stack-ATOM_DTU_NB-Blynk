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

          log("public the data:"); 
          log(param);
          log("\n");
       
          mqttClient.publish(ONENET_TOPIC_POST, param);
          //Send data to the topic
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

// void nbConnect(void) {
//     unsigned long start = millis();
//     log("Initializing modem...");
//     while (!modem.init()) {
//         log("waiting1...." + String((millis() - start) / 1000) + "s");
//     };

//     start = millis();
//     log("Waiting for network...");
//     while (!modem.waitForNetwork()) {
//         log("waiting2...." + String((millis() - start) / 1000) + "s");
//     }
//     log("success");
//     SerialMon.println("Waiting for GPRS connect...");
//     if (!modem.gprsConnectImpl("go.mono", "", "")) {
//         SerialMon.println("waiting...." + String((millis() - start) / 1000) + "s");
//     }
//     SerialMon.println("success");


//     // Example Query the IP address of a device
//     String ip = modem.getLocalIP();

//     log("Device IP address: " + ip);

//     log("success");
// }

