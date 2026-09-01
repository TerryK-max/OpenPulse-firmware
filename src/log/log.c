#include "log/log.h"

void log_init(void)
{
    USB_Log_Init();
}

uint8_t log_is_connected(void)
{
    return USB_Log_IsConnected();
}
