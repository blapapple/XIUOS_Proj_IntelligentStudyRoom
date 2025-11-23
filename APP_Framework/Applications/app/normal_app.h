#ifndef _NORMAL_APP_H
#define _NORMAL_APP_H
#include <adapter.h>
#include <stdint.h>
#include <adapter_wifi.h>
#include <cJSON.h>
#include <stdint.h>
#include "mqtt/MQTTPacket.h"
#include "mqtt/MQTTSubscribe.h"
#define lw_print printf

/* 任务配置参数 */
#define MQTT_TASK_PRIORITY    15
#define TEMPERATURE_TASK_PRIORITY    20
#define HUMIDITY_TASK_PRIORITY       20
#define LIGHT_TASK_PRIORITY       20
#define DETECT_TASK_PRIORITY       20
#define DETECT_RECEIVE_TASK_PRIORITY       20
#define SENSOR_TASK_STACK_SIZE      2048
#define MQTT_TASK_STACK_SIZE      4096
#define DETECT_TASK_STACK_SIZE      409600
#define DETECT_RECEIVE_TASK_STACK_SIZE     2048
#define SENSOR_RUN_CYCLES            10   /* 运行周期数，我改成死循环了 */
#define LOCK_TIMEOUT_MS             1000  /* 锁获取超时时间 */

extern volatile int existing_object_count;

typedef struct{
    char ipv4[20];
    char port[10];
} MqttServerAddr;

// 设备状态枚举
typedef enum {
	STATE_NO_PERSON = 0,      // 无人状态
	STATE_PERSON_ENTER,       // 无人变有人状态
	STATE_PERSON_DARK,        // 有人且暗光状态
	STATE_PERSON_BRIGHT,      // 有人且明光状态
	STATE_HIGH_HUMIDITY,      // 湿度高状态
	STATE_HIGH_TEMPERATURE,   // 温度高状态
	STATE_NORMAL              // 正常状态
} DeviceState;

// 灯光控制命令结构
typedef struct {
	uint8_t power;      // 开关: 0-关, 1-开
	uint8_t brightness; // 亮度: 0-100%
	uint8_t color_temp; // 色温: 0-冷色(2700K), 1-中性(4000K), 2-暖色(5000K)
	uint8_t color_mode; // 颜色模式: 0-自动, 1-手动
} LightControl;

// 需要的辅助函数声明
typedef struct {
    uint8_t person_present;
    uint8_t light_intensity;
    uint8_t temperature;
    uint8_t humidity;
} SensorData;


void TemperatureTask(void *parameter);
void HumidityTask(void *parameter);
void LightTask(void *parameter);
void MqttEdgeDeviceTask(MqttServerAddr* mqtt_server_addr);
void DetectTask(void *parameter);
void DetectReceivetTask(void *parameter);

int CreateAndStartTasks(char* mqtt_ipv4, char* mqtt_port);
void StopSensorTasks(void);
void MonitorSensorTasks(void);

int WifiInitAndConnect(char* ssid, char* password);
uint8_t CheckPersonPresence(void);
float GetLightIntensity(void);
float GetTemperature(void);
float GetHumidity(void);
SensorData GetSensorDataFromQueue(void);
const char* GetCurrentTimestamp(void);
void GenerateLightControl(DeviceState state, LightControl *ctrl);
void PublishDeviceStatus(int fd, SensorData sensor_data, DeviceState state, LightControl light_ctrl);
int AdapterMQTTPublish_QOS0(struct Adapter *adapter, char *topic, uint8_t* msg);

// Tests >>>>>>
static char mqtt_iot_ipaddr[] = {192, 168, 76, 154};
static char mqtt_iot_netmask[] = {255, 255, 255, 0};
static char mqtt_iot_gwaddr[] = {192, 168, 76, 136};
static char mqtt_socket_port_iot[] = "1883";
static char mqtt_ip_str_iot[] = "192.168.76.149";

// 头文件把“运行状态/任务 ID”定义成了静态变量，任何包含该头的源文件都会有各自的副本，容易导致状态不一致。应改为在 .c 中定义、在 .h 中 extern 声明
static int32_t temperature_task_id = -1;
static int32_t humidity_task_id = -1;
static int32_t mqtt_task_id = -1;
static int32_t light_task_id = -1;
static uint32_t detect_task_id = -1;
static uint32_t detect_receive_task_id = -1;

static uint8_t temperature_task_run = 1;
static uint8_t humidity_task_run = 1;
static uint8_t light_task_run = 1;
static uint8_t mqtt_task_run = 1;
static uint8_t detect_task_run = 1;
static uint8_t detect_receive_task_run = 1;

static SensorData sensor_data = {0, 0.0f, 0.0f, 0.0f};
static AdapterType g_mqtt_adapter = NULL;
int MqttTest(void);

#endif