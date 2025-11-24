#include "normal_app.h"

int WifiInitAndConnect(char* ssid, char* password)
{
    int ret = 0;
    
    // 1. 查找Wi-Fi适配器
    struct Adapter* adapter = AdapterDeviceFindByName(ADAPTER_WIFI_NAME);
    if (!adapter) {
        printf("Wi-Fi adapter not found!\n");
        return -1;
    }

    // 2. 打开Wi-Fi设备
    ret = AdapterDeviceOpen(adapter);
    if (ret != 0) {
        printf("Failed to open Wi-Fi device! Error: %d\n", ret);
        return ret;
    }
    printf("Wi-Fi device opened successfully.\n");

    AdapterDeviceDisconnect(adapter, NULL);
    PrivTaskDelay(3000);

    // 3. 配置Wi-Fi连接参数（使用宏定义）
    static struct WifiParam param;
    memset(&param, 0, sizeof(struct WifiParam));
    strncpy((char *)param.wifi_ssid, ssid, sizeof(param.wifi_ssid) - 1);
    strncpy((char *)param.wifi_pwd, password, sizeof(param.wifi_pwd) - 1);
    

    // 确保字符串以null结尾
    param.wifi_ssid[sizeof(param.wifi_ssid) - 1] = '\0';
    param.wifi_pwd[sizeof(param.wifi_pwd) - 1] = '\0';
    
    adapter->adapter_param = &param;
    
    // 4. 执行连接操作
    printf("Connecting to Wi-Fi: %s...\n", ssid);
    ret = AdapterDeviceSetUp(adapter);
    if (ret != 0) {
        printf("Wi-Fi connection failed! Error: %d\n", ret);
        AdapterDeviceClose(adapter);
        return ret;
    }
    printf("Wi-Fi connected successfully to: %s\n", ssid);

    return ret;
}

/**
 * @description: 温度传感器任务函数
 * @param parameter - 任务参数
 */
void TemperatureTask(void *parameter)
{
    printf(" Temperature sensor task started (ID: %d)\n", UserGetTaskID());
    int32_t temperature;
    while (temperature_task_run /* && cycle_count < SENSOR_RUN_CYCLES*/) {  // 死循环
        printf("\n=== Temperature Measurement ===\n");
        
        if (SensorLock(LOCK_TIMEOUT_MS) == 0){
            struct SensorQuantity* temp = GetTempQuantity();
            temperature = SensorQuantityReadValue(temp);
            SensorQuantityClose(temp);
            SensorUnlock();
            if (temperature > 0) {
                printf("Temperature : %d.%d C\n", temperature/10, temperature%10);
                sensor_data.temperature = temperature/10.0f;
            }
            else {
                printf("Temperature : %d.%d C\n", -temperature/10, -temperature%10);
                sensor_data.temperature = -temperature/10.0f;
            }
        }
        UserTaskDelay(500);
    }
}

/**
 * @description: 湿度传感器任务函数
 * @param parameter - 任务参数
 */
void HumidityTask(void *parameter)
{
    printf(" Humidity sensor task started (ID: %d)\n", UserGetTaskID());
    
    int32_t humidity;
    while (humidity_task_run /*  && cycle_count < SENSOR_RUN_CYCLES */ ) { // 死循环
        printf("\n=== Humidity Measurement  ===\n");
        if (SensorLock(LOCK_TIMEOUT_MS) == 0){
            struct SensorQuantity *humi = GetHumiQuantity();
            humidity = SensorQuantityReadValue(humi);
            SensorQuantityClose(humi);
            SensorUnlock();
            printf("Humidity : %d.%d %%RH\n", humidity/10, humidity%10);
            sensor_data.humidity = humidity/10.0f;
        }
        UserTaskDelay(500);
    }
}

void LightTask(void *parameter)
{
    printf(" Light sensor task started (ID: %d)\n", UserGetTaskID());
    int32_t light_intensity;
    
    while (light_task_run /* && cycle_count < SENSOR_RUN_CYCLES*/) {  // 死循环
        printf("\n=== Light Measurement ===\n");
        
        if (SensorLock(LOCK_TIMEOUT_MS) == 0){
            struct SensorQuantity* light = GetLightQuantity();
            light_intensity = SensorQuantityReadValue(light);
            SensorQuantityClose(light);
            SensorUnlock();
            
            if (light_intensity >= 0) {
                printf("Light Intensity : %d.%d lx\n", light_intensity/10, light_intensity%10);
                sensor_data.light_intensity = light_intensity/10.0f;
            }
            else {
                printf("Light sensor read failed: %d\n", light_intensity);
                sensor_data.light_intensity = -1.0f;  // 错误值标记
            }
        }
        /* 任务延迟0.5秒 */
        UserTaskDelay(500);
    }
}

/**
 * @description: k210人脸识别任务函数
 * @param parameter - 任务参数
 */
void DetectTask(void *parameter)
{
    printf(" K210 detect task started (ID: %d)\n", UserGetTaskID());
    k210_detect("face.json");
}

/**
 * @description: 用来接收k210人脸识别任务函数运行时产生的结果 object_exist_or_not
 * @param parameter - 任务参数
 */
void DetectReceiveTask(void *parameter)
{
    printf(" Receive detect task started (ID: %d)\n", UserGetTaskID());
    while (detect_task_run) {
        printf("\n=== Detect Measurement ===\n");
        printf("Data: existing_object_count: %d\n", existing_object_count);
        sensor_data.person_present = existing_object_count > 0 ? 1 : 0;
        UserTaskDelay(500);
    }
}

/**
 * @description: 创建并启动传感器任务
 * @return 成功: 0, 失败: -1
 */
int CreateAndStartTasks(char* mqtt_ipv4, char* mqtt_port)
{
    static MqttServerAddr mqtt_server_addr;
    strncpy(mqtt_server_addr.ipv4, mqtt_ipv4, sizeof(mqtt_server_addr.ipv4) - 1);
    strncpy(mqtt_server_addr.port, mqtt_port, sizeof(mqtt_server_addr.port) - 1);
    mqtt_server_addr.ipv4[sizeof(mqtt_server_addr.ipv4) - 1] = '\0';
    mqtt_server_addr.port[sizeof(mqtt_server_addr.port) - 1] = '\0';
    printf("MQTT server address: %s:%s\n", mqtt_server_addr.ipv4, mqtt_server_addr.port);

    UtaskType temp_task, humi_task, mqtt_task, detect_task, detect_receive_task, light_task;

    printf(" Initializing sensor tasks...\n");
    SensorMutexInit();
    /* 创建温度传感器任务 */
    strncpy(temp_task.name, "temp_task", NAME_NUM_MAX - 1);
    temp_task.func_entry = (void *)TemperatureTask;
    temp_task.func_param = (void *)&temperature_task_run;
    temp_task.stack_size = SENSOR_TASK_STACK_SIZE;
    temp_task.prio = TEMPERATURE_TASK_PRIORITY;
    
    temperature_task_id = UserTaskCreate(temp_task);
    if (temperature_task_id < 0) {
        printf(" Failed to create temperature task\n");
        return -1;
    }

    /* 创建光照传感器任务 */
    strncpy(light_task.name, "light_task", NAME_NUM_MAX - 1);
    light_task.func_entry = (void *)LightTask;
    light_task.func_param = (void *)&light_task_run;
    light_task.stack_size = SENSOR_TASK_STACK_SIZE;
    light_task.prio = LIGHT_TASK_PRIORITY;
    
    light_task_id = UserTaskCreate(light_task);
    if (light_task_id < 0) {
        printf(" Failed to create light task\n");
        return -1;
    }
    
    /* 创建湿度传感器任务 */
    strncpy(humi_task.name, "humi_task", NAME_NUM_MAX - 1);
    humi_task.func_entry = (void *)HumidityTask;
    humi_task.func_param = (void *)&humidity_task_run;
    humi_task.stack_size = SENSOR_TASK_STACK_SIZE;
    humi_task.prio = HUMIDITY_TASK_PRIORITY;
    
    humidity_task_id = UserTaskCreate(humi_task);
    if (humidity_task_id < 0) {
        printf(" Failed to create humidity task\n");
        // UserTaskDelete(humidity_task_id); // 创建任务失败时调用了 删除无效的任务 ID（负值），这会引入错误路径。
        return -1;
    }
    
	 /* 创建MQTT边缘设备任务 */
    strncpy(mqtt_task.name, "mqtt_edge_task", NAME_NUM_MAX - 1);
    mqtt_task.func_entry = (void *)MqttEdgeDeviceTask;
    mqtt_task.func_param = (void *)(&mqtt_server_addr);
    mqtt_task.stack_size = MQTT_TASK_STACK_SIZE;   // MQTT任务需要较大栈空间
    mqtt_task.prio = MQTT_TASK_PRIORITY;           // 设置合适的优先级
    
    mqtt_task_id = UserTaskCreate(mqtt_task);
    if (mqtt_task_id < 0) {
        printf("Failed to create MQTT edge device task\n");
		// UserTaskDelete(mqtt_task_id);
        return -1;
    } else {
        printf("MQTT edge device task created successfully, ID: %d\n", mqtt_task_id);
    }
	
    /* 创建k210人脸识别任务 */
    strncpy(detect_task.name, "detect_task", NAME_NUM_MAX - 1);
    detect_task.func_entry = (void *)DetectTask;
    detect_task.func_param = NULL;
    detect_task.stack_size = DETECT_TASK_STACK_SIZE;
    detect_task.prio = DETECT_TASK_PRIORITY;
    
    detect_task_id = UserTaskCreate(detect_task);
    if (detect_task_id < 0) {
        printf(" Failed to create detect task\n");
		// UserTaskDelete(detect_task_id);
        return -1;
    } else {
        printf("detect task created successfully, ID: %d\n", detect_task_id);
    }
	
    /* 创建接收Detect任务 */
    strncpy(detect_receive_task.name, "detect_receive_task", NAME_NUM_MAX - 1);
    detect_receive_task.func_entry = (void *)DetectReceiveTask;
    detect_receive_task.func_param = NULL;
    detect_receive_task.stack_size = DETECT_RECEIVE_TASK_STACK_SIZE;
    detect_receive_task.prio = DETECT_RECEIVE_TASK_PRIORITY;
    
    detect_receive_task_id = UserTaskCreate(detect_receive_task);
    if (detect_receive_task_id < 0) {
        printf(" Failed to create detect_receive task\n");
		// UserTaskDelete(detect_receive_task_id);
        return -1;
    } else {
        printf("detect_receive task created successfully, ID: %d\n", detect_receive_task_id);
    }

    // /* 启动任务 */


    // UserTaskDelay(100);
    if (UserTaskStartup(detect_task_id) != EOK) {
        printf(" Failed to start detect task\n");
        UserTaskDelete(detect_task_id);
        return -1;
    }

    if (UserTaskStartup(detect_receive_task_id) != EOK) {
        printf(" Failed to start detect_receive task\n");
        UserTaskDelete(detect_receive_task_id);
        return -1;
    }
    // UserTaskDelay(500);
    if (UserTaskStartup(temperature_task_id) != EOK) {
        printf(" Failed to start temperature task\n");
		   UserTaskDelete(temperature_task_id);
        return -1;
    }
    
    // UserTaskDelay(100);
    if (UserTaskStartup(humidity_task_id) != EOK) {
        printf(" Failed to start humidity task\n");
        UserTaskDelete(humidity_task_id);
        return -1;
    }

    if (UserTaskStartup(light_task_id) != EOK) {
        printf(" Failed to start light task\n");
        UserTaskDelete(light_task_id);
        return -1;
    }

    if (UserTaskStartup(mqtt_task_id) != EOK) {
        printf(" Failed to start mqtt task\n");
        UserTaskDelete(mqtt_task_id);
        return -1;
    }


    printf(" tasks created successfully:\n");
    printf("   - Temperature Task: ID=%d, Priority=%d\n", temperature_task_id, TEMPERATURE_TASK_PRIORITY);
    printf("   - Humidity Task: ID=%d, Priority=%d\n", humidity_task_id, HUMIDITY_TASK_PRIORITY);
	printf("   - MQTT Task: ID=%d, Priority=%d\n", mqtt_task_id, MQTT_TASK_PRIORITY);
    printf("   - Detect Task: ID=%d, Priority=%d\n", detect_task_id, DETECT_TASK_PRIORITY);
    printf("   - Detect_Receive Task: ID=%d, Priority=%d\n", detect_receive_task_id, DETECT_RECEIVE_TASK_PRIORITY);
    
    return 0;
}

/**
 * @description: 停止传感器任务
 */
void StopSensorTasks(void)
{
    printf(" Stopping tasks...\n");
    
    temperature_task_run = 0;
    humidity_task_run = 0;
	mqtt_task_run = 0;
    
    /* 给任务一些时间正常退出 */
    UserTaskDelay(200);
    
    /* 强制删除任务 */
    if (temperature_task_id >= 0) {
        UserTaskDelete(temperature_task_id);
        temperature_task_id = -1;
    }
    
    if (humidity_task_id >= 0) {
        UserTaskDelete(humidity_task_id);
        humidity_task_id = -1;
    }
    
	if (mqtt_task_id >= 0) {
		UserTaskDelete(mqtt_task_id);
		mqtt_task_id = -1;
    }
    printf("tasks stopped successfully\n");
}

/**
 * @description: 监控任务状态
 */
void MonitorSensorTasks(void)
{
    int monitor_count = 0;
    
    printf(" Starting task monitoring...\n");
    
    while (monitor_count < SENSOR_RUN_CYCLES * 2) {
        char temp_name[NAME_NUM_MAX], humi_name[NAME_NUM_MAX];
        uint8_t temp_stat, humi_stat;
        
        UserGetTaskName(temperature_task_id, temp_name);
        UserGetTaskName(humidity_task_id, humi_name);
        temp_stat = UserGetTaskStat(temperature_task_id);
        humi_stat = UserGetTaskStat(humidity_task_id);
        
        printf("\n--- Task Status Monitor (Cycle %d) ---\n", monitor_count + 1);
        printf("Temperature Task: ID=%d, Name=%s, State=%d\n", 
               temperature_task_id, temp_name, temp_stat);
        printf("Humidity Task: ID=%d, Name=%s, State=%d\n", 
               humidity_task_id, humi_name, humi_stat);   

        UserTaskDelay(3000);  /* 每3秒监控一次 */
        monitor_count++;
    }
}

void MqttEdgeDeviceTask(MqttServerAddr* mqtt_server_addr)
{
    int ret;
    DeviceState current_state = STATE_NO_PERSON;
    DeviceState previous_state = STATE_NO_PERSON;

    // 查找并初始化WiFi适配器，沿用 MqttTest 的连接方式
    g_mqtt_adapter = AdapterDeviceFindByName(ADAPTER_WIFI_NAME);
    if (!g_mqtt_adapter) {
        lw_print("Failed to find WiFi adapter\n");
        return;
    }
    g_mqtt_adapter->socket.protocal = SOCKET_PROTOCOL_TCP;

    const char *client_id = "xiuos_device_001";
    const char *subscribe_topic = "iot/devices/#";
    const char *status_topic = "iot/devices/status";
    const char *ping_topic = "iot/devices/ping";

MQTT_CONNECT:
    // 1) TCP连接到 MQTT 服务器
    lw_print("Connecting MQTT TCP: %s:%s ...\n", mqtt_server_addr->ipv4, mqtt_server_addr->port);
    ret = AdapterDeviceConnect(g_mqtt_adapter, CLIENT, mqtt_server_addr->ipv4, mqtt_server_addr->port, IPV4);
    if (ret < 0) {
        lw_print("TCP connect failed: %d\n", ret);
        PrivTaskDelay(3000);
        goto MQTT_CONNECT;
    }
    lw_print("TCP connected to %s:%s\n", mqtt_server_addr->ipv4, mqtt_server_addr->port);

    // 2) 发送 MQTT CONNECT（参考 MqttTest）
    {
        MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
        uint8_t conn_buf[256];
        int conn_len;

        data.clientID.cstring = (char *)client_id;
        data.keepAliveInterval = 60;
        data.username.cstring = NULL;
        data.password.cstring = NULL;
        data.MQTTVersion = 4;     // MQTT 3.1.1
        data.cleansession = 1;

        conn_len = MQTTSerialize_connect(conn_buf, sizeof(conn_buf), &data);
        if (conn_len <= 0) {
            lw_print("MQTT CONNECT serialize failed\n");
            AdapterDeviceClose(g_mqtt_adapter);
            PrivTaskDelay(3000);
            goto MQTT_CONNECT;
        }
        AdapterDeviceSend(g_mqtt_adapter, conn_buf, conn_len);
        lw_print("MQTT CONNECT sent (%d bytes)\n", conn_len);
        PrivTaskDelay(500);
    }

    // 3) 订阅主题（QOS0）
    {
        uint8_t sub_buf[200];
        MQTTString topic = {.cstring = (char *)subscribe_topic};
        int qos0 = 0;
        int sub_len = MQTTSerialize_subscribe(sub_buf, sizeof(sub_buf), 0, 1 /* packet id */, 1, &topic, &qos0);
        if (sub_len > 0) {
            AdapterDeviceSend(g_mqtt_adapter, sub_buf, sub_len);
            lw_print("Subscribe %s success (len=%d)\n", subscribe_topic, sub_len);
        } else {
            lw_print("Subscribe %s failed to serialize\n", subscribe_topic);
        }
    }

    // 主循环
    uint8_t no_mqtt_msg_exchange = 1;

    // 接收缓冲区
    uint8_t recv_buf[512];

    while (1) {
        UserTaskDelay(500);

        // 尝试接收消息并反序列化 PUBLISH 载荷
        ssize_t recv_len = AdapterDeviceRecv(g_mqtt_adapter, recv_buf, sizeof(recv_buf));
        if (recv_len > 0) {
            unsigned char dup, retained;
            int qos;
            unsigned short packetid;
            MQTTString topicName;
            unsigned char *payload = NULL;
            int payloadlen = 0;

            int ok = MQTTDeserialize_publish(&dup, &qos, &retained, &packetid,
                                             &topicName, &payload, &payloadlen,
                                             recv_buf, (int)recv_len);
            if (ok == 1 && payload && payloadlen > 0) {
                lw_print("Received MQTT publish: topic=%.*s, payload_len=%d\n",
                         topicName.lenstring.len, topicName.lenstring.data, payloadlen);
                ProcessMqttMessage(payload, payloadlen);
                no_mqtt_msg_exchange = 0;
            } else {
                // 非 PUBLISH 或解析失败，忽略
            }
        }

        // 从内部消息队列获取传感器数据
        SensorData sensor_data = GetSensorDataFromQueue();

        // 状态机逻辑
        previous_state = current_state;

        if (!sensor_data.person_present) {
            current_state = STATE_NO_PERSON;
        } else if (previous_state == STATE_NO_PERSON && sensor_data.person_present) {
            current_state = STATE_PERSON_ENTER;
        } else if (sensor_data.person_present && sensor_data.light_intensity < 50) {
            current_state = STATE_PERSON_DARK;
        } else if (sensor_data.person_present && sensor_data.light_intensity >= 50) {
            current_state = STATE_PERSON_BRIGHT;
        } else if (sensor_data.humidity > 80.0) {
            current_state = STATE_HIGH_HUMIDITY;
        } else if (sensor_data.temperature > 30.0) {
            current_state = STATE_HIGH_TEMPERATURE;
        } else {
            current_state = STATE_NORMAL;
        }


        // 发布设备状态（不再使用 AdapterDeviceMqttSend，改用 QOS0 发布）
        {
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "device_id", client_id);

            cJSON *sensors = cJSON_CreateObject();
            cJSON_AddBoolToObject(sensors, "person_present", sensor_data.person_present);
            cJSON_AddNumberToObject(sensors, "light_intensity", sensor_data.light_intensity);
            cJSON_AddNumberToObject(sensors, "temperature", sensor_data.temperature);
            cJSON_AddNumberToObject(sensors, "humidity", sensor_data.humidity);
            cJSON_AddItemToObject(root, "sensor_data", sensors);

            const char *state_str[] = {
                "no_person", "person_enter", "person_dark", "person_bright",
                "high_humidity", "high_temperature", "normal"
            };
            cJSON_AddStringToObject(root, "device_state", state_str[current_state]);

            char *json_str = cJSON_PrintUnformatted(root);
            lw_print("Publishing: %s\n", json_str);

            if (AdapterMQTTPublish_QOS0(g_mqtt_adapter, (char *)status_topic, (uint8_t*)json_str) == 0) {
                lw_print("Publish success\n");
            } else {
                lw_print("Publish failed\n");
            }

            cJSON_free(json_str);
            cJSON_Delete(root);
        }

    }

}

// 处理接收到的MQTT消息
void ProcessMqttMessage(uint8_t *data, size_t len)
{
    // 简单的消息处理示例
    if (len > 0) {
        lw_print("MQTT message: %.*s\n", len, data);
        
        // 这里可以添加具体的消息解析和处理逻辑
        // 例如：解析控制命令、配置更新等
    }
}

// 检查MQTT连接状态
int CheckMqttConnection(void)
{
    // 尝试发送一个小的测试消息来检查连接
    const char *test_topic = "iot/devices/ping";
    const char *test_payload = "ping";
    
    ssize_t ret = AdapterDeviceMqttSend(g_mqtt_adapter, test_topic, test_payload, strlen(test_payload));
    return (ret >= 0);
}

/**
 * @brief 从内部消息队列获取传感器数据
 * @return 传感器数据结构
 */
SensorData GetSensorDataFromQueue(void)
{
    
    sensor_data.person_present = CheckPersonPresence();    // 检查是否有人
    sensor_data.light_intensity = GetLightIntensity();     // 获取光照强度
    sensor_data.temperature = GetTemperature();            // 获取温度
    sensor_data.humidity = GetHumidity();                  // 获取湿度
    
    return sensor_data;
}

/**
 * 检测人员存在
 * 返回: 1-有人, 0-无人
 */
uint8_t CheckPersonPresence(void)
{
    return sensor_data.person_present;
}

/**
 * 获取光照强度
 * 返回: 光照强度值 (lux)
 */
float GetLightIntensity(void)
{
    return sensor_data.light_intensity;
}

/**
 * 获取温度值
 * 返回: 温度值 (°C)
 */
float GetTemperature(void)
{
    return sensor_data.temperature;
}

/**
 * 获取湿度值
 * 返回: 湿度值 (%RH)
 */
float GetHumidity(void)
{
    return sensor_data.humidity;
}

int MqttTest()
{
    struct Adapter* adapter = AdapterDeviceFindByName(ADAPTER_WIFI_NAME);
    if (!adapter) {
        printf("找不到WiFi适配器\n");
        return -1;
    }

    // 1. 使用适配器API连接MQTT服务器
    const char *ip = "192.168.5.96";
    const char *port = "1883";
    enum NetRoleType net_role = CLIENT;
    enum IpType ip_type = IPV4;
    adapter->socket.protocal = SOCKET_PROTOCOL_TCP;
    printf("连接MQTT服务器: %s:%s\n", ip, port);
    
    int ret = AdapterDeviceConnect(adapter, net_role, ip, port, ip_type);
    if (ret < 0) {
        printf("连接MQTT服务器失败: %d\n", ret);
        return -1;
    }
    printf("连接到MQTT服务器成功\n");
 	printf("组装MQTT连接参数...\n");
    
    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    uint8_t buf[200];
    int buflen = sizeof(buf);
    int len = 0;
    
    data.clientID.cstring = "test_client_001";      // 客户端ID
    data.keepAliveInterval = 60;                   // 保持活跃60秒
    data.username.cstring = NULL;                  // 用户名（可选）
    data.password.cstring = NULL;                  // 密码（可选）
    data.MQTTVersion = 4;                          // MQTT 3.1.1
    data.cleansession = 1;                         // 清理会话
    
    len = MQTTSerialize_connect(buf, buflen, &data);
    if (len <= 0) {
        printf(" MQTT连接包序列化失败\n");
        return -1;
    }
    printf(" MQTT连接包序列化成功，长度: %d字节\n", len);
    
    // 发送连接包
    printf("发送MQTT CONNECT包...\n");
	AdapterDeviceSend(adapter, buf, len);

    PrivTaskDelay(1000); // 等待连接建立

    // 3. 发送MQTT PUBLISH消息（模拟真实设备通信）
    printf("开始发送模拟设备消息...\n");

    // 测试消息类型数组
    const char* test_messages[] = {
    // 设备数据上报
    "{\"type\":\"device_data\",\"data\":{\"name\":\"temperature\",\"value\":25.5}}",
    "{\"type\":\"device_data\",\"data\":{\"name\":\"humidity\",\"value\":65.2}}",
    "{\"type\":\"device_data\",\"data\":{\"name\":\"light_intensity\",\"value\":780}}",
    
    // 控制命令（Yeelight灯泡）
    "{\"type\":\"control_command\",\"data\":{\"command\":\"turn_on\"}}",
    "{\"type\":\"control_command\",\"data\":{\"command\":\"set_brightness\",\"value\":80}}",
    "{\"type\":\"control_command\",\"data\":{\"command\":\"set_color\",\"r\":255,\"g\":100,\"b\":50}}",
    "{\"type\":\"control_command\",\"data\":{\"command\":\"turn_off\"}}",
    

    };

    int message_count = sizeof(test_messages) / sizeof(test_messages[0]);

    for(int i = 0; i < message_count; ++i) {
        const char* topic = "iot/devices/control";  // 使用Python代码中的主题
        
        printf("发送消息[%d/%d]:\n", i+1, message_count);
        printf("  主题: %s\n", topic);
        printf("  内容: %s\n", test_messages[i]);
        
        // 使用适配器发送
        int ret = AdapterMQTTPublish_QOS0(adapter, topic, (uint8_t*)test_messages[i]);
        
        if (ret == 0) {
            printf("   发送成功\n");
        } else {
            printf("   发送失败，错误码: %d\n", ret);
        }
        
        PrivTaskDelay(5000); // 2秒间隔，便于观察
    }
        
    return 0;
}

int AdapterMQTTPublish_QOS0(struct Adapter *adapter, char *topic, uint8_t* msg)
{
    if (!adapter || !topic || !msg) {
        return -1;
    }
    
    uint8_t buf[256];
    MQTTString topicString = {.cstring = topic};
    uint32_t msg_len = strlen((char *)msg);
    
    // 序列化PUBLISH包
    int len = MQTTSerialize_publish(buf, sizeof(buf), 0, 0, 0, 0, topicString, msg, msg_len);
    if (len <= 0) {
        return -1;
    }
    
    // 使用适配器发送
	AdapterDeviceSend(adapter, buf, len);
    
    return 0;
}