/*
*******************************************************************************
* ATOM DTU NB — Modbus to MQTT to Blynk with OTA
*******************************************************************************
*/
#define SerialMon Serial
#define TINY_GSM_DEBUG SerialMon

// #define DUMP_AT_COMMANDS

#include <M5AtomS3.h>
#include "ATOM_DTU_NB.h"
#include <PubSubClient.h>
#include <TinyGsmClient.h>
#include <ArduinoHttpClient.h>
#include <Update.h>
#include <time.h>
#include <sys/time.h>
#include <ModbusMaster.h>

ModbusMaster sensorNode;

#define BLYNK_TEMPLATE_ID  "TMPL4GQQAzx08"
#define BLYNK_AUTH_TOKEN   "NrEsQx97BPbzVHXS1xi0TVcB2bhdOS42"

#define FIRMWARE_VERSION    "1.1.1"
#define FIRMWARE_BUILD_DATE __DATE__ " " __TIME__

// Blynk firmware identification tag — embedded in the binary so Blynk.Air
// can read the version, type, and build date without running the firmware.
#define BLYNK_FIRMWARE_VERSION  FIRMWARE_VERSION
#define BLYNK_FIRMWARE_TYPE     BLYNK_TEMPLATE_ID
#define BLYNK_RPC_LIB_VERSION   "0.0.0"          // NCP lib version (N/A for raw MQTT)

#define BLYNK_PARAM_KV(k, v)    k "\0" v "\0"
volatile const char firmwareTag[] = "blnkinf\0"
    BLYNK_PARAM_KV("mcu"    , BLYNK_FIRMWARE_VERSION)
    BLYNK_PARAM_KV("fw-type", BLYNK_FIRMWARE_TYPE)
    BLYNK_PARAM_KV("build"  , FIRMWARE_BUILD_DATE)
    BLYNK_PARAM_KV("blynk"  , BLYNK_RPC_LIB_VERSION)
    "\0";

#define MQTT_BROKER   "fra1.blynk.cloud"
#define MQTT_PORT     1883

#define UPLOAD_INTERVAL 1800000
#define mqtt_devid    "device1"
#define mqtt_pubid    "device"
#define mqtt_password BLYNK_AUTH_TOKEN

// Blynk MQTT topics — the broker routes by auth token internally;
// device-side topics do NOT include the token prefix.
// OTA payload from Blynk.Air: {"url":"http://...","size":354448}
#define BLYNK_OTA_TOPIC   "downlink/ota/json"
#define BLYNK_RELAY_TOPIC "downlink/ds/Relay"
#define BLYNK_INFO_TOPIC  "info/mcu"

#define BLYNK_BATCH_TOPIC "batch_ds"

uint32_t lastReconnectAttempt = 0;
unsigned long pollTimer = 0;

static bool otaPending = false;
static char pendingOtaUrl[256] = {0};
static bool relayState = false;

#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
    StreamDebugger debugger(SerialAT, SerialMon);
    TinyGsm modem(debugger, ATOM_DTU_SIM7028_RESET);
#else
    TinyGsm modem(SerialAT, ATOM_DTU_SIM7028_RESET);
#endif

TinyGsmClient tcpClient(modem);
PubSubClient  mqttClient(MQTT_BROKER, MQTT_PORT, tcpClient);

void mqttCallback(char *topic, byte *payload, unsigned int len);
bool mqttConnect(void);
void nbConnect(void);
void initModbus(void);
void pollSensorAndPublish(void);
bool setModbusRelayState(bool);
void performOtaUpdate(const char* url);

void log(String info) {
    SerialMon.println(info);
}

void setup() {
    AtomS3.begin(false);
    Serial.println(">>ATOM DTU NB  FW:" FIRMWARE_VERSION "  Built:" FIRMWARE_BUILD_DATE);
    SerialAT.begin(SIM7028_BAUDRATE, SERIAL_8N1, ATOM_DTU_SIM7028_RX,
                   ATOM_DTU_SIM7028_TX);
    nbConnect();
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setKeepAlive(300);    // 300 s — NB-IoT needs long keepalive
    mqttClient.setSocketTimeout(30); // 30 s socket timeout to handle cellular latency spikes
    mqttClient.setCallback(mqttCallback);
    initModbus();
    pollTimer = millis() + 10000UL; // give XY-MD02 10 s to finish startup before first poll
}

void loop() {
    if (otaPending) {
        otaPending = false;
        log(">>OTA: Initiating firmware update...");
        mqttClient.disconnect();
        delay(4000);  // SIM7028 needs ~3-4s to fully release socket after close
        performOtaUpdate(pendingOtaUrl);
        // performOtaUpdate reboots on success; if we reach here the update failed.
    }

    if (!mqttClient.connected()) {
        log(">>MQTT NOT CONNECTED rc=" + String(mqttClient.state()));
        uint32_t t = millis();
        if (t - lastReconnectAttempt > 3000L) {
            lastReconnectAttempt = t;
            if (mqttConnect()) {
                lastReconnectAttempt = 0;
            }
        }
        delay(100);
    }
    if (millis() >= pollTimer) {
        pollTimer = millis() + UPLOAD_INTERVAL;
        if (mqttClient.connected()) {
            pollSensorAndPublish();
            delay(500);
        }
    }
    mqttClient.loop();
}

void mqttCallback(char *topic, byte *payload, unsigned int len) {
    char info[len + 1];
    memcpy(info, payload, len);
    info[len] = '\0';
    log("MQTT <<< [" + String(topic) + "] " + String(info));

    if (strcmp(topic, "downlink/ping") == 0) {
        log("PING received — PUBACK sent automatically");
        return;
    }

    if (strcmp(topic, BLYNK_RELAY_TOPIC) == 0) {
        bool on = (strcmp(info, "true") == 0 || strcmp(info, "1") == 0);
        relayState = on;
        setModbusRelayState(on);
        return;
    }

    // OTA trigger — only act on the designated Blynk downlink topic.
    if (strcmp(topic, BLYNK_OTA_TOPIC) == 0) {
        const char* urlKey = "\"url\":\"";
        char* urlStart = strstr(info, urlKey);
        if (urlStart != nullptr) {
            urlStart += strlen(urlKey);
            char* urlEnd = strchr(urlStart, '"');
            if (urlEnd != nullptr) {
                *urlEnd = '\0';
                if (strncmp(urlStart, "http://", 7) == 0) {
                    strncpy(pendingOtaUrl, urlStart, sizeof(pendingOtaUrl) - 1);
                    pendingOtaUrl[sizeof(pendingOtaUrl) - 1] = '\0';
                    otaPending = true;
                    log("OTA: URL queued: " + String(pendingOtaUrl));
                } else {
                    log("OTA: Only HTTP supported. Got: " + String(urlStart));
                }
            }
        }
    }
}

bool mqttConnect(void) {
    log("Connecting to " MQTT_BROKER "...");
    bool status = mqttClient.connect(mqtt_devid, mqtt_pubid, mqtt_password);
    if (!status) {
        log("MQTT failed rc=" + String(mqttClient.state()));
        return false;
    }
    log("MQTT CONNECTED  FW:" FIRMWARE_VERSION);

    // Publish device info so Blynk.Air can track firmware version and
    // determine whether an OTA push is needed.
    char info[256];
    snprintf(info, sizeof(info),
        "{\"tmpl\":\"%s\",\"ver\":\"%s\",\"build\":\"%s\",\"type\":\"%s\",\"rxbuff\":%d}",
        BLYNK_FIRMWARE_TYPE,
        BLYNK_FIRMWARE_VERSION,
        FIRMWARE_BUILD_DATE,
        BLYNK_FIRMWARE_TYPE,
        MQTT_MAX_PACKET_SIZE);
    mqttClient.publish(BLYNK_INFO_TOPIC, info);

    mqttClient.subscribe(BLYNK_OTA_TOPIC);
    mqttClient.subscribe(BLYNK_RELAY_TOPIC);
    mqttClient.subscribe("downlink/ping", 1); // QoS 1 — broker expects PUBACK, sent automatically by PubSubClient
    return mqttClient.connected();
}

void nbConnect() {
    SerialMon.println("Waiting for NB-IoT network...");
    if (!modem.waitForNetwork(60000L)) {
        SerialMon.println("Network registration failed.");
        delay(3000);
        return;
    }
    SerialMon.println("NB-IoT network ready!");
}

void initModbus() {
    log("Initializing ESP32-S3 UART for Modbus...");
    Serial2.end();
    delay(100);
    Serial2.begin(9600, SERIAL_8N1, ATOM_DTU_RS485_RX, ATOM_DTU_RS485_TX);
    Serial2.setPins(ATOM_DTU_RS485_RX, ATOM_DTU_RS485_TX);
    sensorNode.begin(1, Serial2);
    log("Modbus ready on G7/G8.");
}

void pollSensorAndPublish() {
    uint8_t result;
    log("Polling XY-MD02...");
    result = sensorNode.readInputRegisters(0x0001, 2);
    if (result == 0x02) {
        result = sensorNode.readInputRegisters(0x0002, 2);
    }

    if (result == sensorNode.ku8MBSuccess) {
        float temperature = (int16_t)sensorNode.getResponseBuffer(0x00) / 10.0;
        float humidity    = (int16_t)sensorNode.getResponseBuffer(0x01) / 10.0;
        log("--> Temp: " + String(temperature, 1) + "C  Hum: " + String(humidity, 1) + "%");

        char payload[120];
        sprintf(payload, "{\"Temperature\":%.1f,\"Humidity\":%.1f}", temperature, humidity);
        mqttClient.publish(BLYNK_BATCH_TOPIC, payload);
    } else {
        log("Modbus error: 0x" + String(result, HEX));
    }
}

#define SENSOR_SLAVE_ID   1
#define RELAY_SLAVE_ID    2
#define RELAY_CONTROL_REG 1
#define RELAY_VAL_ON      256   // 0x0100
#define RELAY_VAL_OFF     512   // 0x0200

bool setModbusRelayState(bool turnOn) {
    sensorNode.begin(RELAY_SLAVE_ID, Serial2);
    delay(10);
    uint16_t controlValue = turnOn ? RELAY_VAL_ON : RELAY_VAL_OFF;
    log("Relay " + String(turnOn ? "ON" : "OFF") + " -> Slave 2");
    uint8_t result = sensorNode.writeSingleRegister(RELAY_CONTROL_REG, controlValue);
    sensorNode.begin(SENSOR_SLAVE_ID, Serial2);
    if (result == sensorNode.ku8MBSuccess) {
        log("Relay OK");
        return true;
    }
    log("Relay error: 0x" + String(result, HEX));
    return false;
}

// ---------------------------------------------------------------------------
// OTA firmware update over cellular HTTP
// ---------------------------------------------------------------------------
void performOtaUpdate(const char* url) {
    log("OTA: Downloading from " + String(url));

    // Parse http://host[:port]/path
    char host[128] = {0};
    char path[256] = {0};
    int  port = 80;

    const char* p = url + 7;  // skip "http://"
    const char* slash = strchr(p, '/');
    if (slash != nullptr) {
        size_t hostLen = (size_t)(slash - p);
        strncpy(host, p, min(hostLen, sizeof(host) - 1));
        strncpy(path, slash, sizeof(path) - 1);
    } else {
        strncpy(host, p, sizeof(host) - 1);
        path[0] = '/'; path[1] = '\0';
    }

    char* colon = strchr(host, ':');
    if (colon != nullptr) {
        port = atoi(colon + 1);
        *colon = '\0';
    }

    log("OTA: Host=" + String(host) + " Port=" + String(port) + " Path=" + String(path));

    // Explicitly stop and drain the socket before reuse. ArduinoHttpClient's
    // connect() calls stop() internally, which re-sends AT+CIPCLOSE on an
    // already-closed socket. The error response would corrupt the next
    // AT+CIPRXGET=1 exchange, so we stop cleanly here first.
    tcpClient.stop();
    delay(1000);

    HttpClient http(tcpClient, host, port);
    http.setTimeout(30000);

    int err = http.get(path);
    if (err != 0) {
        log("OTA: HTTP GET failed err=" + String(err));
        return;
    }

    int statusCode = http.responseStatusCode();
    log("OTA: HTTP " + String(statusCode));
    if (statusCode != 200) {
        log("OTA: Aborting on non-200 status");
        http.stop();
        return;
    }

    int contentLength = http.contentLength();
    log("OTA: Content-Length=" + String(contentLength));

    if (!Update.begin(contentLength > 0 ? contentLength : UPDATE_SIZE_UNKNOWN)) {
        log("OTA: Update.begin failed: " + String(Update.errorString()));
        http.stop();
        return;
    }

    uint8_t buf[512];
    int totalWritten = 0;
    int lastLogBand = -1;

    while (http.connected() || http.available()) {
        int avail = http.available();
        if (avail > 0) {
            int toRead = min(avail, (int)sizeof(buf));
            if (contentLength > 0) {
                toRead = min(toRead, contentLength - totalWritten);
            }
            int bytesRead = http.read(buf, toRead);
            if (bytesRead > 0) {
                size_t written = Update.write(buf, (size_t)bytesRead);
                if (written != (size_t)bytesRead) {
                    log("OTA: Flash write error: " + String(Update.errorString()));
                    http.stop();
                    Update.abort();
                    return;
                }
                totalWritten += bytesRead;
                if (contentLength > 0) {
                    int band = (totalWritten * 10) / contentLength;
                    if (band != lastLogBand) {
                        lastLogBand = band;
                        log("OTA: " + String(band * 10) + "% (" + String(totalWritten) + "/" + String(contentLength) + ")");
                    }
                    if (totalWritten >= contentLength) break;
                }
            }
        } else {
            delay(10);
        }
    }

    http.stop();
    log("OTA: Download complete — " + String(totalWritten) + " bytes");

    if (Update.end()) {
        if (Update.isFinished()) {
            log("OTA: SUCCESS! Rebooting in 2s...");
            delay(2000);
            ESP.restart();
        } else {
            log("OTA: Incomplete — expected " + String(contentLength) + " wrote " + String(totalWritten));
        }
    } else {
        log("OTA: Update.end failed: " + String(Update.errorString()));
    }
}
