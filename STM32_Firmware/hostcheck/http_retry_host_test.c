#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../Firmware/Services/ethernet/http_upload.c"
#include "../Firmware/Common/retry_timer.h"

static ReaderSettings_t settings;
static uint32_t tick;
static char sent[10000];
static uint16_t sentLength;
static const char *response;
static unsigned closes;
const ReaderSettings_t *ReaderSettings_Get(void) { return &settings; }
uint32_t Time_GetTickMs(void) { return tick; }
uint32_t Configuration_Service_GetRecvTimeoutSeconds(void) { return 2U; }
uint8_t ActiveUpload_Transport_Close(void) { closes++; return 1U; }
uint16_t ActiveUpload_Transport_GetTxFreeSize(void) { return 2048U; }
uint16_t ActiveUpload_Transport_Send(const uint8_t *data, uint16_t length)
{
    assert(sentLength + length < sizeof(sent));
    memcpy(sent + sentLength, data, length);
    sentLength += length;
    return length;
}
uint16_t ActiveUpload_Transport_Receive(uint8_t *data, uint16_t capacity)
{
    if(!response) return 0U;
    size_t length = strlen(response);
    assert(length <= capacity);
    memcpy(data, response, length);
    response = NULL;
    return (uint16_t)length;
}

int main(void)
{
    RetryTimer_t timer = {0};
    assert(RetryTimer_Ready(&timer, 0x80000000U));
    RetryTimer_Arm(&timer, 0xFFFFFFF0U, 32U);
    assert(!RetryTimer_Ready(&timer, 0x0FU));
    assert(RetryTimer_Ready(&timer, 0x10U));
    assert(RetryTimer_Ready(&timer, 0x80000010U));
    strcpy(settings.httpUrl, "http://192.168.1.1/tags");
    settings.httpTimeout = 2U;
    HttpUpload_Init(NULL);
    const char body[] = "{\"event_type\":\"tag_read\"}";
    assert(HttpUpload_Submit((const uint8_t *)body, sizeof(body)-1) == sizeof(body)-1);
    HttpUpload_Task();
    assert(g_state == HTTP_STATE_WAITING);
    char first[10000];
    memcpy(first, sent, sentLength);
    uint16_t firstLength = sentLength;
    const char *errors[] = {
        "HTTP/1.1 302 Found\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 503 Unavailable\r\nContent-Length: 0\r\n\r\n"
    };
    for(unsigned i = 0; i < 8; ++i)
    {
        if(i < 3) { response = errors[i]; HttpUpload_Task(); }
        else if(i == 3) { tick += 2000U; HttpUpload_Task(); }
        else HttpUpload_OnDisconnected();
        assert(g_state == HTTP_STATE_RETRY && HttpUpload_IsBusy());
        assert(g_retryDelayMs <= 30000U);
        uint32_t at = g_retryAtMs;
        HttpUpload_OnDisconnected(); /* callback must not restart the delay */
        assert(g_retryAtMs == at);
        sentLength = 0U;
        tick += g_retryDelayMs - 1U;
        HttpUpload_Task();
        assert(sentLength == 0U);
        tick++;
        HttpUpload_Task();
        assert(g_state == HTTP_STATE_WAITING);
        assert(sentLength == firstLength && !memcmp(first, sent, firstLength));
    }
    response = "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n";
    HttpUpload_Task();
    assert(!HttpUpload_IsBusy());
    assert(closes >= 8U);
    puts("HTTP retries, error responses, timeout, acknowledgement and timer wrap: PASS");
    return 0;
}
