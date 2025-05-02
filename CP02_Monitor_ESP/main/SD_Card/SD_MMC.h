
#pragma once

#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

#include "esp_flash.h"  

#define CONFIG_EXAMPLE_PIN_CLK  14
#define CONFIG_EXAMPLE_PIN_CMD  15
#define CONFIG_EXAMPLE_PIN_D0   16
#define CONFIG_EXAMPLE_PIN_D1   18
#define CONFIG_EXAMPLE_PIN_D2   17
#define CONFIG_EXAMPLE_PIN_D3   21  



esp_err_t s_example_write_file(const char *path, char *data);
esp_err_t s_example_read_file(const char *path);

extern uint32_t SDCard_Size;
extern uint32_t Flash_Size;
void SD_Init(void);
void Flash_Searching(void);