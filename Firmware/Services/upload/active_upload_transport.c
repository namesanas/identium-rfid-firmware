#include "active_upload_transport.h"

#include "../configuration/reader_settings.h"
#include "../ethernet/ethernet_service.h"
#include "../ethernet/http_upload.h"
#include "../wifi/wifi_service.h"
#include "../../Devices/w5500/w5500.h"

#include <stddef.h>

uint8_t ActiveUpload_Transport_GetSelected(void)
{
    uint8_t selection = ReaderSettings_GetUploadInterface();

    if (selection == READER_SETTINGS_UPLOAD_IF_WIFI)
        return ACTIVE_UPLOAD_INTERFACE_WIFI;
    if (selection == READER_SETTINGS_UPLOAD_IF_ETH)
        return ACTIVE_UPLOAD_INTERFACE_ETH;
    return (W5500_IsLinkUp() != 0U) ?
        ACTIVE_UPLOAD_INTERFACE_ETH : ACTIVE_UPLOAD_INTERFACE_WIFI;
}

uint8_t ActiveUpload_Transport_IsConnected(void)
{
    if (ActiveUpload_Transport_GetSelected() == ACTIVE_UPLOAD_INTERFACE_WIFI)
        return WiFi_Service_IsUploadConnected();
    return Ethernet_Service_IsConnected();
}

uint16_t ActiveUpload_Transport_GetTxFreeSize(void)
{
    if (ActiveUpload_Transport_GetSelected() == ACTIVE_UPLOAD_INTERFACE_WIFI)
    {
        if (WiFi_Service_IsUploadConnected() == 0U) return 0U;
        return WiFi_Service_UploadGetFreeSpace();
    }
    return W5500_Socket0_GetTxFreeSize();
}

uint16_t ActiveUpload_Transport_Send(const uint8_t *data, uint16_t length)
{
    if ((data == NULL) || (length == 0U)) return 0U;
    if (ActiveUpload_Transport_GetSelected() == ACTIVE_UPLOAD_INTERFACE_WIFI)
        return WiFi_Service_UploadSend(data, length);
    return W5500_Socket0_Send(data, length);
}

uint16_t ActiveUpload_Transport_Receive(uint8_t *data, uint16_t capacity)
{
    if ((data == NULL) || (capacity == 0U)) return 0U;
    if (ActiveUpload_Transport_GetSelected() == ACTIVE_UPLOAD_INTERFACE_WIFI)
        return WiFi_Service_UploadReceive(data, capacity);
    return W5500_Socket0_Receive(data, capacity);
}

uint8_t ActiveUpload_Transport_Close(void)
{
    if (ActiveUpload_Transport_GetSelected() == ACTIVE_UPLOAD_INTERFACE_WIFI)
        return WiFi_Service_CloseUploadSocket();
    return W5500_Socket0_Close();
}

uint16_t ActiveUpload_Transport_SendComplete(const uint8_t *data, uint16_t length)
{
    uint16_t accepted;
    if ((data == NULL) || (length == 0U)) return 0U;

    if (ActiveUpload_Transport_GetSelected() == ACTIVE_UPLOAD_INTERFACE_WIFI)
    {
        if (WiFi_Service_IsUploadConnected() == 0U) return 0U;
        if (ReaderSettings_GetUploadMode() == READER_SETTINGS_UPLOAD_HTTP)
            return HttpUpload_Submit(data, length);
        if (WiFi_Service_UploadGetFreeSpace() < length) return 0U;
        accepted = WiFi_Service_UploadSend(data, length);
        return (accepted == length) ? accepted : 0U;
    }
    return Ethernet_Service_SendComplete(data, length);
}
