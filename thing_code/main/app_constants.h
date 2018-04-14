#ifndef APP_CONSTANTS_H
#define APP_CONSTANTS_H

// #define WIFI_SSID "Home"
// #define WIFI_PASS "davidchristianfleig"

// #define CONFIG_AWS_EXAMPLE_THING_NAME "YellowClockinator"
// #define CONFIG_AWS_EXAMPLE_CLIENT_ID "YellowClockinator"
// #define CONFIG_ESPTOOLPY_PORT COM19

// #define AWS_IOT_MQTT_HOSTNAME "a1p7fx1rx0lfgm.iot.us-east-1.amazonaws.com"

#define DEFAULT_NTP_SERVER "pool.ntp.org"
#define DEFAULT_TIMEZONE "EST5EDT,M3.2.0/2,M11.1.0"

#define CLOCK_TASKS_STACK_DEPTH 3000
#define CLOCK_TASKS_PRIORITY 3

#define SHADOW_TASK_STACK_DEPTH 8000
#define SHADOW_TASK_PRIORITY 5

#define SOURCE_ROUTER_TASKS_STACK_DEPTH 3000
#define SOURCE_ROUTER_TASKS_PRIORITY 3

#define TOPIC_TASK_STACK_DEPTH 5000
#define TOPIC_TASK_PRIORITY 3

#define MAX_TOPIC_NAME_LENGTH 30
#define TWDT_TIMEOUT_S 120
#define TASK_RESET_PERIOD_S 30

#define CHECK_ERROR_CODE(returned, expected) ({                        \
            if(returned != expected){                                  \
                printf("TWDT ERROR\n");                                \
                abort();                                               \
            } \
})

#endif