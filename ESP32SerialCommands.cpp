#include "ESP32SerialCommands.h"
#include "DekiLogSystem.h"
#include "SDCardComponent.h"
#include "IDekiSDCard.h"  // from deki-sdcard

#ifdef ESP32
#include "driver/uart.h"
#include <cstring>
#include <string>

#define SERIAL_UART_NUM UART_NUM_0
#define SERIAL_BUF_SIZE 256
#endif

bool ESP32SerialCommands::s_Initialized = false;
bool ESP32SerialCommands::s_InStorageMode = false;

#ifdef ESP32
static void serial_send(const char* str)
{
    uart_write_bytes(SERIAL_UART_NUM, str, strlen(str));
    uart_write_bytes(SERIAL_UART_NUM, "\r\n", 2);
}
#endif

void ESP32SerialCommands::Initialize(unsigned long baudRate)
{
#ifdef ESP32
    if (s_Initialized)
    {
        return;
    }

    uart_config_t uart_config = {};
    uart_config.baud_rate = static_cast<int>(baudRate);
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_DEFAULT;

    uart_param_config(SERIAL_UART_NUM, &uart_config);
    uart_driver_install(SERIAL_UART_NUM, SERIAL_BUF_SIZE * 2, 0, 0, nullptr, 0);

    s_Initialized = true;
    DEKI_LOG_INTERNAL("ESP32SerialCommands: Initialized at %lu baud", baudRate);
#else
    (void)baudRate;
#endif
}

void ESP32SerialCommands::ProcessCommands()
{
#ifdef ESP32
    size_t available = 0;
    uart_get_buffered_data_len(SERIAL_UART_NUM, &available);
    if (available == 0)
    {
        return;
    }

    // Read until newline
    char buf[SERIAL_BUF_SIZE];
    int len = 0;
    while (len < (int)sizeof(buf) - 1)
    {
        uint8_t c;
        int read = uart_read_bytes(SERIAL_UART_NUM, &c, 1, pdMS_TO_TICKS(10));
        if (read <= 0 || c == '\n')
            break;
        if (c != '\r')
            buf[len++] = static_cast<char>(c);
    }
    buf[len] = '\0';

    std::string cmd(buf);
    if (cmd.empty())
    {
        return;
    }

    DEKI_LOG_INTERNAL("ESP32SerialCommands: Received command: %s", cmd.c_str());

    if (cmd == "STORAGE_MODE")
    {
        auto* sdCard = SDCardComponent::GetSDCardModule();
        if (sdCard && sdCard->SupportsStorageMode())
        {
            if (sdCard->SetStorageMode(true))
            {
                s_InStorageMode = true;
                serial_send("OK:STORAGE_MODE");
                DEKI_LOG_INTERNAL("ESP32SerialCommands: Entered storage mode");
            }
            else
            {
                serial_send("ERROR:STORAGE_MODE_FAILED");
                DEKI_LOG_ERROR("ESP32SerialCommands: Failed to enter storage mode");
            }
        }
        else
        {
            serial_send("ERROR:STORAGE_MODE_NOT_SUPPORTED");
            DEKI_LOG_WARNING("ESP32SerialCommands: Storage mode not supported (no SD card module)");
        }
    }
    else if (cmd == "EXIT_STORAGE")
    {
        auto* sdCard = SDCardComponent::GetSDCardModule();
        if (sdCard && sdCard->SetStorageMode(false))
        {
            s_InStorageMode = false;
            serial_send("OK:EXIT_STORAGE");
            DEKI_LOG_INTERNAL("ESP32SerialCommands: Exited storage mode");
        }
        else
        {
            serial_send("ERROR:EXIT_STORAGE_FAILED");
            DEKI_LOG_ERROR("ESP32SerialCommands: Failed to exit storage mode");
        }
    }
    else if (cmd == "STATUS")
    {
        if (s_InStorageMode)
        {
            serial_send("STATUS:STORAGE_MODE");
        }
        else
        {
            serial_send("STATUS:RUNNING");
        }
    }
    else if (cmd == "PING")
    {
        serial_send("PONG");
    }
    else
    {
        std::string err = "ERROR:UNKNOWN_COMMAND:" + cmd;
        serial_send(err.c_str());
        DEKI_LOG_WARNING("ESP32SerialCommands: Unknown command: %s", cmd.c_str());
    }
#endif
}

bool ESP32SerialCommands::IsInStorageMode()
{
    return s_InStorageMode;
}
