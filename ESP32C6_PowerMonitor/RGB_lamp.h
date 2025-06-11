#pragma once
#include "Arduino.h"

#define PIN_NEOPIXEL 8

void RGB_Lamp_Init();                                                    // Initialize RGB lamp data
void Set_Color(uint8_t Red,uint8_t Green,uint8_t Blue);                 // Set RGB bead color
void RGB_Lamp_Loop(uint16_t Waiting);                                   // The lamp beads change color in cycles
void RGB_Lamp_Off();                                                    // Turn off RGB lamp
void RGB_Lamp_SetRunning(bool running);                                 // Set RGB lamp running state
