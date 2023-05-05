#ifndef LOG_H
#define LOG_H

typedef enum
{
    LOG_TYPE_PID,
    LOG_TYPE_BATTERY
} log_type_t;

typedef struct {
    float rollTarget;
    float rollError;
    float rollAdjust;
} log_block_pid_t;

typedef struct {
    float voltage;
} log_block_battery_t;

typedef struct
{
    log_type_t type;
    union
    {
        log_block_pid_t pid;
        log_block_battery_t bat;
    } data;
} log_block_t;




#endif /* LOG_H */
