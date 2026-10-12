#ifndef RFID_CONTROLLER_HTTP_UPLOAD_H
#define RFID_CONTROLLER_HTTP_UPLOAD_H

#include <stdint.h>

#include "ethernet_service.h"

/*
 * HTTP upload transport (protocol specification Part D).
 *
 * Every JSON event is sent as one POST to upload.http.url over a
 * keep-alive connection on W5500 socket 0. One request is in flight at a
 * time; events produced meanwhile wait in a queue. A non-empty 2xx
 * response body is one command and is fed to the Identium command
 * parser exactly like a JSON line received over TCP.
 */

#define HTTP_UPLOAD_HOST_MAX   63U
#define HTTP_UPLOAD_PATH_MAX   127U

typedef struct
{
    uint8_t ip[4];
    uint16_t port;
    char host[HTTP_UPLOAD_HOST_MAX + 1U];   /* as written in the URL, for Host: */
    char path[HTTP_UPLOAD_PATH_MAX + 1U];
} HttpUpload_Url_t;

/* Parse "http://a.b.c.d[:port][/path]". The host must be an IPv4 address:
 * the reader has no DNS resolver. Returns 1 when valid. */
uint8_t HttpUpload_ParseUrl(const char *url, HttpUpload_Url_t *out);

void HttpUpload_Init(Ethernet_RxByteCallback_t rxCallback);

/* Re-read the URL from the reader settings. Returns 1 when usable. */
uint8_t HttpUpload_Configure(void);

/* Server address from the configured URL. Returns 0 when no valid URL. */
uint8_t HttpUpload_GetTarget(uint8_t ip[4], uint16_t *port);

/* Socket 0 is ESTABLISHED: send / receive. */
void HttpUpload_Task(void);

/* The connection was lost: an unanswered request is sent again on the
 * next connection. */
void HttpUpload_OnDisconnected(void);

/* Queue one JSON event (a trailing newline is removed). Returns length
 * when queued, 0 when the queue is full (caller retries). */
uint16_t HttpUpload_Submit(const uint8_t *json, uint16_t length);

/* Last HTTP status received (0 = none yet). For diagnostics. */
uint16_t HttpUpload_GetLastStatus(void);

#endif /* RFID_CONTROLLER_HTTP_UPLOAD_H */
