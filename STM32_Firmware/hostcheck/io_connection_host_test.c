#include <assert.h>
#include <stdio.h>
#include "../Firmware/Services/GPIO/gpio_service.c"
#include "../Firmware/Services/ethernet/host_tcp_server.c"

static uint32_t tick;
static unsigned writes, releases, receivedByte;
static uint8_t outputCount = 3U, socketState, owner = HOST_TRANSPORT_NONE;
uint32_t Time_GetTickMs(void) { return tick; }
uint8_t Industrial_IO_GetOutputCount(void) { return outputCount; }
uint8_t Industrial_IO_SetOutput(uint8_t n, uint8_t state)
{ (void)state; assert(n <= outputCount); writes++; return 1U; }
void Industrial_IO_GetAllInputs(uint8_t states[4]) { memset(states, 0, 4); }
uint8_t Industrial_IO_HasBuzzer(void) { return 0U; }
void Industrial_IO_SetBuzzer(uint8_t on) { (void)on; }
uint8_t W5500_Socket3_Listen(uint16_t port)
{ assert(port == 5000U); socketState = W5500_Sn_SR_LISTEN; return 1U; }
uint8_t W5500_Socket3_GetStatus(void) { return socketState; }
uint8_t W5500_Socket3_Close(void) { socketState = W5500_Sn_SR_CLOSED; return 1U; }
uint16_t W5500_Socket3_Receive(uint8_t *data, uint16_t size)
{ assert(size); if(!receivedByte) return 0U; receivedByte = 0U; *data = 'X'; return 1U; }
uint16_t W5500_Socket3_Send(const uint8_t *data, uint16_t length)
{ (void)data; return length; }
uint8_t HOST_Transport_GetActive(void) { return owner; }
void HOST_Transport_Release(void) { owner = HOST_TRANSPORT_NONE; releases++; }
void HOST_Transport_PushByte(uint8_t source, uint8_t byte)
{ (void)byte; owner = source; }
int main(void)
{
    const uint8_t outputs[] = {1, 4}, states[] = {1, 1};
    const uint32_t durations[] = {0, 0};
    uint32_t error;
    const char *message;
    GPIO_Service_Init(NULL);
    assert(GPIO_Service_SetOutputs(outputs, states, durations, 2, &error, &message) == GPIO_SERVICE_ERR_NOT_SUPPORTED);
    assert(writes == 0U && error == 2U && gpioOutputStates[0] == 0U);
    outputCount = 4U;
    assert(GPIO_Service_SetOutputs(outputs, states, durations, 2, &error, &message) == GPIO_SERVICE_OK);
    assert(writes == 2U);

    Host_Tcp_Server_Init();
    tick = 0xFFFFFF00U;
    socketState = W5500_Sn_SR_ESTABLISHED;
    receivedByte = 1U;
    Host_Tcp_Server_Task();
    tick += 59999U;
    Host_Tcp_Server_Task();
    assert(releases == 0U);
    tick++;
    Host_Tcp_Server_Task();
    assert(releases == 1U && socketState == W5500_Sn_SR_LISTEN);
    socketState = W5500_Sn_SR_ESTABLISHED;
    Host_Tcp_Server_Task();
    tick += 59000U;
    Host_Tcp_Server_Send((const uint8_t *)"tag\n", 4);
    tick += 2000U;
    Host_Tcp_Server_Task();
    assert(socketState == W5500_Sn_SR_ESTABLISHED);
    puts("GPIO prevalidation, TCP idle release, tick wrap and active output: PASS");
    return 0;
}
