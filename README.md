# ATOM DTU NB

## Summarize

Contains case programs related to the M5Stack-ATOM_DTU_NB family. The hardware is based on SIM7020 series module to realize NB-IOT communication function.

## Libraries

- [PubSubClient](https://github.com/knolleary/pubsubclient.git)
- [TinyGsm](https://github.com/m5stack/TinyGSM.git)
- [StreamDebugger](https://github.com/vshymanskyy/StreamDebugger.git)
- [arduino-mqtt](https://github.com/256dpi/arduino-mqtt)

## Handy CSQ / RSSI / Signal Strength Mapping

| CSQ Value | RSSI (dBm)           | Description      |
| --------- | -------------------- | ---------------- |
| 0         | -113 dBm or less     | No signal        |
| 1-2       | -111 dBm to -109 dBm | Very poor signal |
| 3-9       | -107 dBm to -93 dBm  | Poor signal      |
| 10-14     | -91 dBm to -83 dBm   | Fair signal      |
| 15-19     | -81 dBm to -73 dBm   | Good signal      |
| 20-30     | -71 dBm to -53 dBm   | Very good signal |
| 31        | -51 dBm or more      | Excellent signal |

# StreamDebugger

It just allows for easier debugging of serial-based communications on the Arduino.

The StreamDebugger class is an Arduino Stream that dumps all data to another Stream for debugging purposes.

## Usage：

```c
//#define DUMP_AT_COMMANDS    //If you need to debug, you can open this macro definition and TinyGsmClientSIM7020.h line 13 //#define TINY_GSM_DEBUG Serial
#ifdef DUMP_AT_COMMANDS 
#include <StreamDebugger.h>
    StreamDebugger debugger(SerialAT, SerialMon);
    TinyGsm modem(debugger, ATOM_DTU_SIM7020_RESET);
#else
    TinyGsm modem(SerialAT, ATOM_DTU_SIM7020_RESET);
#endif
```

## Related link

[Document & AT 命令](https://docs.m5stack.com/en/atom/atom_dtu_nb)

## MQTT

If you want to publish set QoS1 or Qos2, use this parameter[arduino-mqtt](https://github.com/256dpi/arduino-mqtt)

### Pubsubclient  Limitations:

​	It can only publish QoS 0 messages. It can subscribe at QoS 0 or QoS 1.

### Arduino-mqtt

Note that mqttClient.setOptions(65, true, 15000); //With timeout = 1000, it went wrong

If you do not add this one, it will fail and disconnect when the publishing topic sends data

```c
#include <MQTT.h>
MQTTClient mqttClient;
```

```c
void setup() {
    M5.begin(true, false, true);
    Serial.println(">>ATOM DTU NB MQTT TEST");
    SerialAT.begin(SIM7020_BAUDRATE, SERIAL_8N1, ATOM_DTU_SIM7020_RX,
                   ATOM_DTU_SIM7020_TX);
    M5.dis.fillpix(0x0000ff);
    nbConnect();
     // Connect to MQTT Broker
    mqttClient.setOptions(65, true, 15000);  //With timeout = 1000, it went wrong
    mqttClient.begin(MQTT_BROKER, MQTT_PORT, tcpClient);
    mqttClient.onMessage(messageReceived);
}
```

```c
void connect() {
    M5.dis.fillpix(0xff0000);
    Serial.print("\nconnecting...");
    String mqttid = ("MQTTID_" + String(random(65536)));
    while (!mqttClient.connect(mqttid.c_str(), MQTT_USERNAME, MQTT_PASSWORD)) {
        Serial.print(".");
        Serial.print(mqttClient.lastError());
        Serial.print(" - ");
        Serial.println(mqttClient.returnCode());
        delay(1000);
    }

  Serial.println("\nconnected!");

  mqttClient.subscribe(MQTT_U_TOPIC ,1);
  // client.unsubscribe("/hello");
}
```

```c
// publish a message roughly every second.
    if (millis() - lastMillis > 5000) {
        lastMillis = millis();
        //const char topic[] - Represents the topic of the message to be published, which is an array of strings.
        //const char payload[] - Represents the content of the message and is also an array of strings.
        //int length - Indicates the length (in bytes) of the message content.
        //bool retained - Indicates whether messages are retained. If set to true, the server sends the last retained message to subscribers. If set to false, the message is not retained.
        //int qos - Indicates the Quality of service level (QoS), which can be 0, 1, or 2.
        bool pub=mqttClient.publish(MQTT_U_TOPIC, payload,length,retained,qos);
        if(pub){
            log("send data!!!!");
        }
        else{
             log("send data error !!!!");
        }
        mqttClient.lastPacketID();
    }   
```

