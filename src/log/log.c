#include "log/log.h"
#include "usb/usb_device.h"

void log_init(void)
{
    usb_device_init();
}

uint8_t log_is_connected(void)
{
    return usb_device_configured();
}
