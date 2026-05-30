
#include <Arduino.h>

#define TINY_GSM_MODEM_SIM7028

#define SerialMon Serial
#define MONITOR_BAUDRATE 115200

#define SerialAT Serial1
#define SIM7028_BAUDRATE 115200
#define ATOM_DTU_SIM7028_RESET -1

#define ATOM_DTU_SIM7028_EN    12
#define ATOM_DTU_SIM7028_TX    5
#define ATOM_DTU_SIM7028_RX    6

#define ATOM_DTU_RS485_TX 7
#define ATOM_DTU_RS485_RX 8