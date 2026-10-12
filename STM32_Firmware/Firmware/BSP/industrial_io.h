#ifndef RFID_CONTROLLER_BSP_INDUSTRIAL_IO_H_
#define RFID_CONTROLLER_BSP_INDUSTRIAL_IO_H_

#include <stdint.h>

#define INDUSTRIAL_IO_COUNT 4U

uint8_t Industrial_IO_Init(void);
uint8_t Industrial_IO_SetOutput(uint8_t outputNumber, uint8_t state);
uint8_t Industrial_IO_GetOutput(uint8_t outputNumber, uint8_t *state);
uint8_t Industrial_IO_GetInput(uint8_t inputNumber);
void Industrial_IO_GetAllInputs(uint8_t states[INDUSTRIAL_IO_COUNT]);

#endif /* RFID_CONTROLLER_BSP_INDUSTRIAL_IO_H_ */
