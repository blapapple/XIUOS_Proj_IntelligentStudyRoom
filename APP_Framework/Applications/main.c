/*
* Copyright (c) 2020 AIIT XUOS Lab
* XiUOS is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*        http://license.coscl.org.cn/MulanPSL2
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
* See the Mulan PSL v2 for more details.
*/
#include <stdio.h>
#include <string.h>
#include <transform.h>

extern int FrameworkInit();
extern void ApplicationOtaTaskInit(void);

int main(void)
{
	printf("Intellij_Study_Room\n Running on edu-riscv\n");
	FrameworkInit();
#ifdef APPLICATION_OTA
	ApplicationOtaTaskInit();
#endif

#ifdef OTA_BY_PLATFORM
    OtaTask();
#endif

#ifdef APPLICATION_WEBSERVER
    webserver();
#endif
    // MqttTest("192.168.76.149", "1883");
    // MonitorSensorTasks();
    // StopSensorTasks();
    // k210_detect("face.json");

    WifiInitAndConnect("Factory", "00000000");
    // MqttTest();

    printf("Creating parallel tasks for all...\n");
    if (CreateAndStartTasks("10.220.116.149", "1883") < 0) {
        printf("Failed to create tasks for all...\n");
        return -1;
    }
    printf("Parallel tasks created and started\n");
    
    return 0;
}