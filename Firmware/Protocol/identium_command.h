#ifndef RFID_CONTROLLER_IDENTIUM_COMMAND_H_
#define RFID_CONTROLLER_IDENTIUM_COMMAND_H_

#include <stdint.h>

typedef uint8_t (*Identium_CommandReplyCallback_t)(
    const char *commandType,
    uint32_t errCode,
    const char *errString
);

#define IDENTIUM_COMMAND_ERR_OK               0U
#define IDENTIUM_COMMAND_ERR_INVALID_COMMAND  1U
#define IDENTIUM_COMMAND_ERR_NOT_SUPPORTED    2U
#define IDENTIUM_COMMAND_ERR_EXECUTION        3U

void Identium_Command_Init(void);

void Identium_Command_ProcessJson(
    const char *json,
    Identium_CommandReplyCallback_t replyCallback
);

void Identium_Command_Task(
    uint32_t nowMs
);

#endif /* RFID_CONTROLLER_IDENTIUM_COMMAND_H */
