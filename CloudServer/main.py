import json
import logging
import threading
import time
import socket
import re
from typing import Callable, Dict, Any, List, Optional, Tuple
import paho.mqtt.client as mqtt
import math

# 配置日志
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger('iot_server')

# ------------------------------ Yeelight 设备控制相关类 ------------------------------
class YeelightDiscoverer:
    """Yeelight设备发现器（基于UDP多播）"""
    MULTICAST_ADDR = "239.255.255.250"
    MULTICAST_PORT = 1982
    SEARCH_MSG = (
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1982\r\n"
        'MAN: "ssdp:discover"\r\n'
        "ST: wifi_bulb\r\n\r\n"
    ).encode("utf-8")

    @staticmethod
    def discover(timeout: int = 5) -> List[Dict]:
        """发现局域网内的Yeelight设备"""
        devices = []
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP) as udp_socket:
            udp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            udp_socket.bind(("", YeelightDiscoverer.MULTICAST_PORT))
            udp_socket.settimeout(timeout)

            udp_socket.sendto(
                YeelightDiscoverer.SEARCH_MSG,
                (YeelightDiscoverer.MULTICAST_ADDR, YeelightDiscoverer.MULTICAST_PORT)
            )
            logger.info(f"已发送设备发现请求，等待{timeout}秒...")

            while True:
                try:
                    data, _ = udp_socket.recvfrom(1024)
                    response = data.decode("utf-8", errors="ignore")
                    device_info = YeelightDiscoverer._parse_response(response)
                    if device_info and device_info not in devices:
                        devices.append(device_info)
                except socket.timeout:
                    logger.info("设备发现超时")
                    break
                except Exception as e:
                    logger.error(f"解析设备响应失败: {str(e)}")
        return devices

    @staticmethod
    def _parse_response(response: str) -> Optional[Dict]:
        """解析灯泡的UDP响应"""
        location_match = re.search(r"Location: yeelight://([\d.]+):(\d+)", response, re.IGNORECASE)
        if not location_match:
            return None

        id_match = re.search(r"id: (\S+)", response, re.IGNORECASE)
        model_match = re.search(r"model: (\S+)", response, re.IGNORECASE)
        fw_ver_match = re.search(r"fw_ver: (\S+)", response, re.IGNORECASE)
        support_match = re.search(r"support: ([\S\s]+?)\r\n", response, re.IGNORECASE)

        return {
            "ip": location_match.group(1),
            "port": int(location_match.group(2)),
            "device_id": id_match.group(1) if id_match else "",
            "model": model_match.group(1) if model_match else "",
            "fw_version": fw_ver_match.group(1) if fw_ver_match else "",
            "supported_methods": support_match.group(1).split() if support_match else []
        }


class YeelightClient:
    """Yeelight灯泡TCP客户端"""
    def __init__(self, device_ip: str, device_port: int = 55443):
        self.device_ip = device_ip
        self.device_port = device_port
        self.tcp_socket: Optional[socket.socket] = None
        self.is_connected = False
        self.msg_id = 1
        self.response_dict = {}
        self.response_lock = threading.Lock()
        self.notification_callback: Optional[Callable[[Dict], None]] = None
        self.receive_thread: Optional[threading.Thread] = None

    def connect(self, notification_callback: Optional[Callable[[Dict], None]] = None) -> bool:
        """建立TCP连接并启动接收线程"""
        try:
            self.tcp_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.tcp_socket.settimeout(10)
            self.tcp_socket.connect((self.device_ip, self.device_port))
            self.is_connected = True
            logger.info(f"已连接到Yeelight灯泡：{self.device_ip}:{self.device_port}")

            self.notification_callback = notification_callback
            self.receive_thread = threading.Thread(target=self._receive_loop, daemon=True)
            self.receive_thread.start()
            return True
        except Exception as e:
            logger.error(f"TCP连接失败: {str(e)}")
            self.close()
            return False

    def _receive_loop(self):
        """设置非阻塞模式接收"""
        buffer = b""
        
        if self.tcp_socket:
            self.tcp_socket.setblocking(False)
        
        while self.is_connected and self.tcp_socket:
            try:
                try:
                    data = self.tcp_socket.recv(1024)
                    if data:
                        buffer += data
                    else:
                        logger.warning("TCP连接已断开")
                        self.close()
                        break
                except BlockingIOError:
                    pass
                except Exception as e:
                    logger.error(f"接收数据异常: {str(e)}")
                    self.close()
                    break
                
                while b"\r\n" in buffer:
                    msg_bytes, buffer = buffer.split(b"\r\n", 1)
                    if msg_bytes:
                        try:
                            msg = json.loads(msg_bytes.decode("utf-8"))
                            self._process_message(msg)
                        except json.JSONDecodeError:
                            logger.error(f"解析消息失败: {msg_bytes}")
                
                time.sleep(0.1)
                
            except Exception as e:
                logger.error(f"接收循环异常: {str(e)}", exc_info=True)
                self.close()
                break

    def _process_message(self, msg: Dict):
        """处理接收到的消息"""
        if "id" in msg:
            msg_id = msg["id"]
            with self.response_lock:
                self.response_dict[msg_id] = msg
        elif "method" in msg and msg["method"] == "props":
            if self.notification_callback:
                self.notification_callback(msg["params"])

    def send_command(self, method: str, params: List) -> Optional[Dict]:
        """发送控制命令到灯泡"""
        if not self.is_connected or not self.tcp_socket:
            logger.error("TCP连接未建立，无法发送命令")
            return None

        with self.response_lock:
            current_id = self.msg_id
            self.msg_id += 1
            if current_id in self.response_dict:
                del self.response_dict[current_id]

        command = {
            "id": current_id,
            "method": method,
            "params": params
        }
        command_str = json.dumps(command) + "\r\n"

        try:
            self.tcp_socket.sendall(command_str.encode("utf-8"))

            start_time = time.time()
            while time.time() - start_time < 5:
                with self.response_lock:
                    if current_id in self.response_dict:
                        return self.response_dict.pop(current_id)
                time.sleep(0.1)
            logger.warning(f"命令（ID: {current_id}）超时未收到响应")
            return None
        except Exception as e:
            logger.error(f"发送命令失败: {str(e)}")
            self.close()
            return None

    def set_power(self, power: str, effect: str = "smooth", duration: int = 500, mode: int = 0) -> Optional[Dict]:
        if power not in ["on", "off"]:
            logger.error("power参数必须为'on'或'off'")
            return None
        return self.send_command("set_power", [power, effect, duration, mode])

    def set_brightness(self, brightness: int, effect: str = "smooth", duration: int = 500) -> Optional[Dict]:
        if not (1 <= brightness <= 100):
            logger.error("亮度值必须在1-100之间")
            return None
        return self.send_command("set_bright", [brightness, effect, duration])

    def set_rgb(self, red: int, green: int, blue: int, effect: str = "smooth", duration: int = 500) -> Optional[Dict]:
        rgb_value = (red << 16) | (green << 8) | blue
        if not (0 <= rgb_value <= 16777215):
            logger.error("RGB值超出范围（0-16777215）")
            return None
        return self.send_command("set_rgb", [rgb_value, effect, duration])

    def start_color_flow(self, flow_expression: List[Dict]) -> Optional[Dict]:
        """启动颜色流动效果"""
        # flow_expression格式: [duration, mode, value, brightness]
        flow_params = [len(flow_expression), 0]  # 0表示颜色流动完成后停止
        for flow in flow_expression:
            flow_params.extend([flow['duration'], flow['mode'], flow['value'], flow['brightness']])
        return self.send_command("start_cf", flow_params)

    def stop_color_flow(self) -> Optional[Dict]:
        """停止颜色流动效果"""
        return self.send_command("stop_cf", [])

    def get_property(self, props: List[str]) -> Optional[Dict]:
        valid_props = ["power", "bright", "ct", "rgb", "hue", "sat", "color_mode", "flowing", "delayoff"]
        for prop in props:
            if prop not in valid_props:
                logger.error(f"无效属性: {prop}，可选属性：{valid_props}")
                return None
        return self.send_command("get_prop", props)

    def close(self):
        if self.is_connected and self.tcp_socket:
            try:
                self.tcp_socket.close()
                logger.info(f"已断开与{self.device_ip}:{self.device_port}的连接")
            except Exception as e:
                logger.error(f"关闭连接失败: {str(e)}")
            finally:
                self.is_connected = False
                self.tcp_socket = None


class LightStateManager:
    """灯泡状态管理器，根据传感器数据控制灯泡"""
    
    def __init__(self, light_client: YeelightClient):
        self.light_client = light_client
        self.last_person_state = False
        self.last_update_time = 0
        self.current_mode = "idle"
        
        # 环境状态阈值
        self.TEMP_THRESHOLDS = {
            "very_cold": 26,     # 很冷
            "cold": 27,          # 冷
            "comfortable": 28,   # 舒适
            "warm": 29,          # 温暖
            "hot": 30            # 热
        }
        
        self.HUMIDITY_THRESHOLDS = {
            "dry": 55,          # 干燥
            "comfortable": 60,  # 舒适
            "humid": 65,        # 潮湿
            "very_humid": 70    # 很潮湿
        }
        
        self.LIGHT_THRESHOLDS = {
            "dark": 50,         # 暗
            "dim": 80,          # 昏暗
            "normal": 150,      # 正常
            "bright": 200       # 明亮
        }

    def temperature_to_color(self, temperature: float) -> Tuple[int, int, int]:
        """根据温度映射到颜色（冷色到暖色）"""
        if temperature < self.TEMP_THRESHOLDS["very_cold"]:
            # 很冷：深蓝色
            return (0, 100, 255)
        elif temperature < self.TEMP_THRESHOLDS["cold"]:
            # 冷：蓝色到青色渐变
            ratio = (temperature - self.TEMP_THRESHOLDS["very_cold"]) / (self.TEMP_THRESHOLDS["cold"] - self.TEMP_THRESHOLDS["very_cold"])
            return (0, int(100 + 155 * ratio), 255)
        elif temperature < self.TEMP_THRESHOLDS["comfortable"]:
            # 舒适：绿色到黄色渐变
            ratio = (temperature - self.TEMP_THRESHOLDS["cold"]) / (self.TEMP_THRESHOLDS["comfortable"] - self.TEMP_THRESHOLDS["cold"])
            return (int(255 * ratio), 255, 0)
        elif temperature < self.TEMP_THRESHOLDS["warm"]:
            # 温暖：黄色到橙色渐变
            ratio = (temperature - self.TEMP_THRESHOLDS["comfortable"]) / (self.TEMP_THRESHOLDS["warm"] - self.TEMP_THRESHOLDS["comfortable"])
            return (255, int(255 * (1 - ratio * 0.5)), 0)
        else:
            # 热：橙色到红色渐变
            ratio = min(1.0, (temperature - self.TEMP_THRESHOLDS["warm"]) / (self.TEMP_THRESHOLDS["hot"] - self.TEMP_THRESHOLDS["warm"]))
            return (255, int(128 * (1 - ratio)), 0)

    def humidity_to_effect(self, humidity: float) -> str:
        """根据湿度决定特效类型"""
        if humidity < self.HUMIDITY_THRESHOLDS["dry"]:
            return "pulse_slow"  # 干燥：缓慢脉冲
        elif humidity < self.HUMIDITY_THRESHOLDS["comfortable"]:
            return "breath"      # 舒适：呼吸效果
        elif humidity < self.HUMIDITY_THRESHOLDS["humid"]:
            return "wave"        # 潮湿：波浪效果
        else:
            return "flash_fast"  # 很潮湿：快速闪烁

    def calculate_brightness(self, light_intensity: float, person_present: bool) -> int:
        """根据光照强度计算灯泡亮度"""
        if not person_present:
            return 1  # 无人时关闭亮度
        
        # 有人时，根据环境光照自动调整亮度
        if light_intensity < self.LIGHT_THRESHOLDS["dark"]:
            return 40  # 很暗环境用较高亮度
        elif light_intensity < self.LIGHT_THRESHOLDS["dim"]:
            return 30
        elif light_intensity < self.LIGHT_THRESHOLDS["normal"]:
            return 20
        else:
            return 10  # 明亮环境用较低亮度

    def create_weather_flow(self, temperature: float, humidity: float) -> List[Dict]:
        """创建基于温湿度的动态流动效果"""
        base_color = self.temperature_to_color(temperature)
        effect_speed = 1000  # 基础速度
        
        # 根据湿度调整速度
        if humidity > self.HUMIDITY_THRESHOLDS["very_humid"]:
            effect_speed = 500  # 高湿度时快速变化
        elif humidity < self.HUMIDITY_THRESHOLDS["dry"]:
            effect_speed = 2000  # 干燥时缓慢变化
            
        # 创建流动序列
        flow_expression = []
        
        # 主色调
        flow_expression.append({
            'duration': effect_speed,
            'mode': 1,  # RGB模式
            'value': (base_color[0] << 16) | (base_color[1] << 8) | base_color[2],
            'brightness': 70
        })
        
        # 根据湿度添加辅助色调
        if humidity > self.HUMIDITY_THRESHOLDS["humid"]:
            # 高湿度：添加蓝色调波动
            for i in range(3):
                flow_expression.append({
                    'duration': effect_speed // 2,
                    'mode': 1,
                    'value': ((base_color[0]//2) << 16) | ((base_color[1]//2) << 8) | 255,
                    'brightness': 60 + i*5
                })
        elif humidity < self.HUMIDITY_THRESHOLDS["dry"]:
            # 低湿度：添加暖色调波动
            for i in range(2):
                flow_expression.append({
                    'duration': effect_speed,
                    'mode': 1,
                    'value': 0xFF4500,  # 橙红色
                    'brightness': 50 + i*10
                })
        
        return flow_expression

    def update_light_state(self, sensor_data: Dict) -> Dict:
        """根据传感器数据更新灯泡状态"""
        person_present = sensor_data.get('person_present', False)
        light_intensity = sensor_data.get('light_intensity', 0)
        temperature = sensor_data.get('temperature', 22.0)
        humidity = sensor_data.get('humidity', 50.0)
        
        brightness = self.calculate_brightness(light_intensity, person_present)
        response = {}
        
        try:
            # 控制开关
            if person_present and not self.last_person_state:
                # 有人进入：开灯
                self.light_client.set_power("on", "smooth", 300)
                self.current_mode = "active"
                response['action'] = "light_on"
                logger.info("检测到有人，开灯")
                
            elif not person_present and self.last_person_state:
                # 无人：关灯
                self.light_client.set_power("off", "smooth", 1000)
                self.current_mode = "idle"
                response['action'] = "light_off"
                logger.info("无人，关灯")
            
            # 如果有人，设置亮度和颜色效果
            if person_present:
                # 设置亮度
                self.light_client.set_brightness(brightness, "smooth", 500)
                response['brightness'] = brightness
                
                # 停止之前的流动效果
                # self.light_client.stop_color_flow()
                
                # 根据温湿度创建动态效果
                if humidity > self.HUMIDITY_THRESHOLDS["humid"] or temperature > self.TEMP_THRESHOLDS["warm"]:
                    # 高湿度或高温度：启动流动效果
                    # flow_expression = self.create_weather_flow(temperature, humidity)
                    # self.light_client.start_color_flow(flow_expression)
                    self.current_mode = "weather_flow"
                    response['effect'] = "weather_flow"
                    logger.info(f"温度: {temperature}°C, 湿度: {humidity}%")
                else:
                    # 正常情况：固定颜色
                    color = self.temperature_to_color(temperature)
                    self.light_client.set_rgb(color[0], color[1], color[2], "smooth", 800)
                    self.current_mode = "temperature_color"
                    response['color'] = color
                    response['effect'] = "temperature_based"
                    logger.info(f"设置温度颜色: RGB{color}, 温度: {temperature}°C")
            
            self.last_person_state = person_present
            self.last_update_time = time.time()
            response['status'] = 'success'
            response['mode'] = self.current_mode
            
        except Exception as e:
            logger.error(f"控制灯泡失败: {str(e)}")
            response['status'] = 'error'
            response['message'] = str(e)
        
        return response


# ------------------------------ MQTT 服务器相关类 ------------------------------
class MessageHandler:
    def __init__(self):
        self.handlers: Dict[str, Callable] = {}
    
    def register(self, message_type: str) -> Callable:
        def decorator(func: Callable) -> Callable:
            self.handlers[message_type] = func
            logger.info(f"Registered handler for message type: {message_type}")
            return func
        return decorator
    
    def handle(self, message_type: str, data: Any) -> Any:
        if message_type in self.handlers:
            try:
                return self.handlers[message_type](data)
            except Exception as e:
                logger.error(f"Error handling message type {message_type}: {str(e)}")
                return {"status": "error", "message": str(e)}
        else:
            logger.warning(f"No handler found for message type: {message_type}")
            return {"status": "error", "message": f"No handler for {message_type}"}


class IoTServer:
    def __init__(self, config: Dict[str, Any]):
        self.config = config
        self.client = mqtt.Client()
        self.message_handler = MessageHandler()
        self.light_client: Optional[YeelightClient] = None
        self.light_state_manager: Optional[LightStateManager] = None
        
        # 设置MQTT回调
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self.client.on_disconnect = self._on_disconnect
        
        if "username" in config and "password" in config:
            self.client.username_pw_set(config["username"], config["password"])

        self._init_light_connection()
    
    def _init_light_connection(self):
        """初始化灯泡连接"""
        devices = YeelightDiscoverer.discover(timeout=5)
        if not devices:
            logger.warning("未发现任何Yeelight设备")
            return
        
        target_device = devices[0]
        logger.info(f"选中设备：{target_device}")
        self.light_client = YeelightClient(
            device_ip=target_device["ip"],
            device_port=target_device["port"]
        )
        
        if self.light_client.connect():
            self.light_state_manager = LightStateManager(self.light_client)
            logger.info("灯泡状态管理器初始化完成")
    
    def _on_connect(self, client: mqtt.Client, userdata: Any, flags: Dict, rc: int):
        if rc == 0:
            logger.info("Connected to MQTT broker successfully")
            for topic in self.config["topics"]:
                client.subscribe(topic)
                logger.info(f"Subscribed to topic: {topic}")
        else:
            logger.error(f"Failed to connect, return code {rc}")
    
    def _on_message(self, client: mqtt.Client, userdata: Any, msg: mqtt.MQTTMessage):
        logger.info(f"Received message from topic {msg.topic}")
        
        try:
            payload = msg.payload.decode('utf-8')
            message_data = json.loads(payload)
            
            # 处理设备状态消息
            if 'sensor_data' in message_data:
                sensor_data = message_data['sensor_data']
                logger.info(f"处理传感器数据: 有人={sensor_data.get('person_present')}, "
                           f"光照={sensor_data.get('light_intensity')}lx, "
                           f"温度={sensor_data.get('temperature')}°C, "
                           f"湿度={sensor_data.get('humidity')}%")
                
                # 使用灯泡状态管理器处理传感器数据
                if self.light_state_manager:
                    result = self.light_state_manager.update_light_state(sensor_data)
                    logger.info(f"灯泡控制结果: {result}")
                
        except Exception as e:
            logger.error(f"处理MQTT消息失败: {str(e)}")
    
    def _on_disconnect(self, client: mqtt.Client, userdata: Any, rc: int):
        if rc != 0:
            logger.warning(f"Unexpected disconnection with rc {rc}")
        else:
            logger.info("Disconnected from MQTT broker")
    
    def start(self):
        logger.info("Starting IoT server...")
        self.client.connect(
            self.config["broker_host"],
            self.config["broker_port"],
            keepalive=60
        )
        self.client.loop_forever()
    
    def stop(self):
        logger.info("Stopping IoT server...")
        if self.light_client:
            self.light_client.close()
        self.client.disconnect()


# ------------------------------ 程序入口 ------------------------------
if __name__ == "__main__":
    config = {
        "broker_host": "localhost",
        "broker_port": 1883,
        "username": "",
        "password": "",
        "topics": ["iot/devices/#"]
    }
    
    server = IoTServer(config)
    
    # 注册其他消息处理函数
    @server.message_handler.register("control_command")
    def handle_control_command(data):
        """处理手动控制命令"""
        if not server.light_client or not server.light_client.is_connected:
            return {"status": "error", "message": "Light not connected"}
        
        command = data.get("command", "")
        try:
            if command == "turn_on":
                response = server.light_client.set_power("on")
            elif command == "turn_off":
                response = server.light_client.set_power("off")
            elif command == "get_state":
                if server.light_state_manager:
                    return {
                        "status": "success", 
                        "mode": server.light_state_manager.current_mode,
                        "last_update": server.light_state_manager.last_update_time
                    }
            else:
                return {"status": "error", "message": f"Unknown command: {command}"}
            
            return {"status": "success", "response": response}
        except Exception as e:
            return {"status": "error", "message": str(e)}
    
    try:
        server.start()
    except KeyboardInterrupt:
        server.stop()