#include "RGB.h"
#include "esp_log.h"

static const char *TAG = "RGB_LAMP";
static uint8_t RGB_Data[192][3] ={
    {64, 1, 0}, {63, 2, 0}, {62, 3, 0}, {61, 4, 0}, {60, 5, 0}, {59, 6, 0}, {58, 7, 0}, {57, 8, 0},
    {56, 9, 0}, {55, 10, 0}, {54, 11, 0}, {53, 12, 0}, {52, 13, 0}, {51, 14, 0}, {50, 15, 0}, {49, 16, 0},
    {48, 17, 0}, {47, 18, 0}, {46, 19, 0}, {45, 20, 0}, {44, 21, 0}, {43, 22, 0}, {42, 23, 0}, {41, 24, 0},
    {40, 25, 0}, {39, 26, 0}, {38, 27, 0}, {37, 28, 0}, {36, 29, 0}, {35, 30, 0}, {34, 31, 0}, {33, 32, 0},
    {32, 33, 0}, {31, 34, 0}, {30, 35, 0}, {29, 36, 0}, {28, 37, 0}, {27, 38, 0}, {26, 39, 0}, {25, 40, 0},
    {24, 41, 0}, {23, 42, 0}, {22, 43, 0}, {21, 44, 0}, {20, 45, 0}, {19, 46, 0}, {18, 47, 0}, {17, 48, 0},
    {16, 49, 0}, {15, 50, 0}, {14, 51, 0}, {13, 52, 0}, {12, 53, 0}, {11, 54, 0}, {10, 55, 0}, {9, 56, 0},
    {8, 57, 0}, {7, 58, 0}, {6, 59, 0}, {5, 60, 0}, {4, 61, 0}, {3, 62, 0}, {2, 63, 0}, {1, 64, 0},

    {0, 64, 1}, {0, 63, 2}, {0, 62, 3}, {0, 61, 4}, {0, 60, 5}, {0, 59, 6}, {0, 58, 7}, {0, 57, 8},
    {0, 56, 9}, {0, 55, 10}, {0, 54, 11}, {0, 53, 12}, {0, 52, 13}, {0, 51, 14}, {0, 50, 15}, {0, 49, 16},
    {0, 48, 17}, {0, 47, 18}, {0, 46, 19}, {0, 45, 20}, {0, 44, 21}, {0, 43, 22}, {0, 42, 23}, {0, 41, 24},
    {0, 40, 25}, {0, 39, 26}, {0, 38, 27}, {0, 37, 28}, {0, 36, 29}, {0, 35, 30}, {0, 34, 31}, {0, 33, 32},
    {0, 32, 33}, {0, 31, 34}, {0, 30, 35}, {0, 29, 36}, {0, 28, 37}, {0, 27, 38}, {0, 26, 39}, {0, 25, 40},
    {0, 24, 41}, {0, 23, 42}, {0, 22, 43}, {0, 21, 44}, {0, 20, 45}, {0, 19, 46}, {0, 18, 47}, {0, 17, 48},
    {0, 16, 49}, {0, 15, 50}, {0, 14, 51}, {0, 13, 52}, {0, 12, 53}, {0, 11, 54}, {0, 10, 55}, {0, 9, 56},
    {0, 8, 57}, {0, 7, 58}, {0, 6, 59}, {0, 5, 60}, {0, 4, 61}, {0, 3, 62}, {0, 2, 63}, {0, 1, 64},

    {1, 0, 64}, {2, 0, 63}, {3, 0, 62}, {4, 0, 61}, {5, 0, 60}, {6, 0, 59}, {7, 0, 58}, {8, 0, 57},
    {9, 0, 56}, {10, 0, 55}, {11, 0, 54}, {12, 0, 53}, {13, 0, 52}, {14, 0, 51}, {15, 0, 50}, {16, 0, 49},
    {17, 0, 48}, {18, 0, 47}, {19, 0, 46}, {20, 0, 45}, {21, 0, 44}, {22, 0, 43}, {23, 0, 42}, {24, 0, 41},
    {25, 0, 40}, {26, 0, 39}, {27, 0, 38}, {28, 0, 37}, {29, 0, 36}, {30, 0, 35}, {31, 0, 34}, {32, 0, 33},
    {33, 0, 32}, {34, 0, 31}, {35, 0, 30}, {36, 0, 29}, {37, 0, 28}, {38, 0, 27}, {39, 0, 26}, {40, 0, 25},
    {41, 0, 24}, {42, 0, 23}, {43, 0, 22}, {44, 0, 21}, {45, 0, 20}, {46, 0, 19}, {47, 0, 18}, {48, 0, 17},
    {49, 0, 16}, {50, 0, 15}, {51, 0, 14}, {52, 0, 13}, {53, 0, 12}, {54, 0, 11}, {55, 0, 10}, {56, 0, 9},
    {57, 0, 8}, {58, 0, 7}, {59, 0, 6}, {60, 0, 5}, {61, 0, 4}, {62, 0, 3}, {63, 0, 2}, {64, 0, 1}
};

static led_strip_handle_t led_strip;


void RGB_Init(void)
{
    ESP_LOGI(TAG, "Initializing RGB LED strip");
    /* LED strip initialization with the GPIO and pixels number*/
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1, // at least one LED on board
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    ESP_LOGI(TAG, "RGB LED strip initialized on GPIO %d", BLINK_GPIO);

    /* Set all LED off to clear all pixels */
    led_strip_clear(led_strip);
    ESP_LOGI(TAG, "RGB LED strip cleared");
}

void Set_RGB( uint8_t red_val, uint8_t green_val, uint8_t blue_val)
{
    ESP_LOGD(TAG, "Setting RGB values: R=%d, G=%d, B=%d", red_val, green_val, blue_val);
    /* Set the LED pixel using RGB from 0 (0%) to 255 (100%) for each color */
    led_strip_set_pixel(led_strip, 0, red_val, green_val, blue_val);
    /* Refresh the strip to send data */
    led_strip_refresh(led_strip);
}

void _RGB_Example(void *arg)
{
    ESP_LOGI(TAG, "RGB example demo task started");
    static uint8_t i = 0;
    while(1)
    {
        ESP_LOGD(TAG, "RGB cycle index: %d", i);
        Set_RGB(RGB_Data[i][0]*3,RGB_Data[i][1]*3,RGB_Data[i][2]*3);
        i++;
        if(i >= 192) {
            i = 0;
            ESP_LOGI(TAG, "RGB cycle completed, restarting");
        }
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

void RGB_Example(void)
{
    ESP_LOGI(TAG, "Starting RGB example demo task");
    // RGB
    xTaskCreatePinnedToCore(
        _RGB_Example, 
        "RGB Demo",
        4096, 
        NULL, 
        4, 
        NULL, 
        0);
    ESP_LOGI(TAG, "RGB example demo task created successfully");
}