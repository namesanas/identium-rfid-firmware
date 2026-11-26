#include "http_upload.h"

#include "../../Config/product_config.h"
#include "../Time/time_service.h"
#include "../configuration/reader_settings.h"
#include "../upload/active_upload_transport.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

extern uint32_t Configuration_Service_GetRecvTimeoutSeconds(void);

/*
 * ------------------------------------------------------------
 * Sizes
 * ------------------------------------------------------------
 *
 * A tag batch is at most s_buf_size (<= 8192) bytes. The body grows by
 * the "/a.b.c.d" suffix on reader_name; the header is below 512 bytes.
 */
#define HTTP_BODY_MAX            (8192U + 32U)
#define HTTP_REQUEST_SIZE        (HTTP_BODY_MAX + 512U)
#define HTTP_QUEUE_SIZE          12288U
#define HTTP_RESPONSE_BODY_MAX   2000U  /* < IDENTIUM_PROTOCOL_RX_BUFFER_SIZE */
#define HTTP_LINE_MAX            128U
#define HTTP_RETRY_MAX_MS        30000U
#define HTTP_DEFAULT_TIMEOUT_S   10U
#define HTTP_MIN_TIMEOUT_S       2U
#define HTTP_MAX_TIMEOUT_S       120U

typedef enum
{
    HTTP_STATE_IDLE = 0,
    HTTP_STATE_SENDING,     /* request (re)transmission in progress */
    HTTP_STATE_WAITING,     /* request sent, response not complete */
    HTTP_STATE_RETRY        /* retained request, bounded retry delay */
} HttpUpload_State_t;

typedef enum
{
    HTTP_PARSE_STATUS = 0,
    HTTP_PARSE_HEADER,
    HTTP_PARSE_BODY_LENGTH,
    HTTP_PARSE_BODY_CLOSE,
    HTTP_PARSE_CHUNK_SIZE,
    HTTP_PARSE_CHUNK_DATA,
    HTTP_PARSE_CHUNK_END,
    HTTP_PARSE_TRAILER
} HttpUpload_Parse_t;

static Ethernet_RxByteCallback_t g_rxCallback = NULL;

static HttpUpload_Url_t g_url;
static uint8_t g_urlValid = 0U;

/* Pending events: [length hi][length lo][body] ... */
static uint8_t g_queue[HTTP_QUEUE_SIZE];
static uint16_t g_queueHead = 0U;
static uint16_t g_queueTail = 0U;
static uint16_t g_queueUsed = 0U;

static char g_request[HTTP_REQUEST_SIZE];
static uint16_t g_requestLength = 0U;
static uint16_t g_requestSent = 0U;
static uint32_t g_retryDelayMs = 0U;
static uint32_t g_retryAtMs = 0U;
static uint32_t g_sentAtMs = 0U;
static HttpUpload_State_t g_state = HTTP_STATE_IDLE;

static HttpUpload_Parse_t g_parse = HTTP_PARSE_STATUS;
static char g_line[HTTP_LINE_MAX];
static uint16_t g_lineLength = 0U;
static uint16_t g_status = 0U;
static int32_t g_contentLength = -1;
static uint8_t g_chunked = 0U;
static uint8_t g_connectionClose = 0U;
static uint32_t g_bodyRemaining = 0U;
static char g_body[HTTP_RESPONSE_BODY_MAX];
static uint16_t g_bodyLength = 0U;
static uint8_t g_bodyTruncated = 0U;
static uint16_t g_lastStatus = 0U;

/* ------------------------------------------------------------
 * URL
 * ------------------------------------------------------------ */

static uint8_t HttpUpload_ParseDecimal(const char **p, const char *end, uint32_t max, uint32_t *out)
{
    uint32_t value = 0U;
    uint8_t digits = 0U;

    while((*p < end) && (**p >= '0') && (**p <= '9'))
    {
        value = (value * 10U) + (uint32_t)(**p - '0');
        if(value > max)   /* max <= 1000000: no overflow */
        {
            return 0U;
        }
        digits = 1U;
        (*p)++;
    }

    *out = value;
    return (digits != 0U) ? 1U : 0U;
}

uint8_t HttpUpload_ParseUrl(const char *url, HttpUpload_Url_t *out)
{
    static const char scheme[] = "http://";
    const char *p;
    const char *hostStart;
    const char *hostEnd;
    const char *authorityEnd;
    uint32_t value;
    uint8_t i;
    size_t length;

    if((url == NULL) || (out == NULL))
    {
        return 0U;
    }

    memset(out, 0, sizeof(*out));

    for(i = 0U; i < (sizeof(scheme) - 1U); i++)
    {
        char c = url[i];

        if((c >= 'A') && (c <= 'Z'))
        {
            c = (char)(c - 'A' + 'a');
        }
        if(c != scheme[i])
        {
            return 0U;   /* also rejects https:// */
        }
    }

    hostStart = url + (sizeof(scheme) - 1U);
    authorityEnd = hostStart;
    while((*authorityEnd != '\0') && (*authorityEnd != '/') && (*authorityEnd != '?'))
    {
        authorityEnd++;
    }

    hostEnd = hostStart;
    while((hostEnd < authorityEnd) && (*hostEnd != ':'))
    {
        hostEnd++;
    }

    /* Host: dotted IPv4 only. */
    p = hostStart;
    for(i = 0U; i < 4U; i++)
    {
        if((HttpUpload_ParseDecimal(&p, hostEnd, 255U, &value) == 0U))
        {
            return 0U;
        }
        out->ip[i] = (uint8_t)value;

        if(i < 3U)
        {
            if((p >= hostEnd) || (*p != '.'))
            {
                return 0U;
            }
            p++;
        }
    }
    if(p != hostEnd)
    {
        return 0U;
    }

    out->port = 80U;
    if(hostEnd < authorityEnd)
    {
        p = hostEnd + 1;
        if(
            (HttpUpload_ParseDecimal(&p, authorityEnd, 65535U, &value) == 0U) ||
            (p != authorityEnd) ||
            (value == 0U)
        )
        {
            return 0U;
        }
        out->port = (uint16_t)value;
    }

    length = (size_t)(authorityEnd - hostStart);
    if(length > HTTP_UPLOAD_HOST_MAX)
    {
        return 0U;
    }
    memcpy(out->host, hostStart, length);
    out->host[length] = '\0';

    if(*authorityEnd == '\0')
    {
        out->path[0] = '/';
        out->path[1] = '\0';
    }
    else
    {
        length = strlen(authorityEnd);
        if(length > HTTP_UPLOAD_PATH_MAX)
        {
            return 0U;
        }
        if(*authorityEnd == '?')
        {
            out->path[0] = '/';
            memcpy(&out->path[1], authorityEnd, (length < HTTP_UPLOAD_PATH_MAX) ? length : (HTTP_UPLOAD_PATH_MAX - 1U));
        }
        else
        {
            memcpy(out->path, authorityEnd, length);
        }
    }

    /* Characters that would break the request line. */
    for(p = out->path; *p != '\0'; p++)
    {
        if((*p <= ' ') || (*p == 0x7F))
        {
            return 0U;
        }
    }

    return 1U;
}

/* ------------------------------------------------------------
 * Queue
 * ------------------------------------------------------------ */

static void HttpUpload_QueueWrite(const uint8_t *data, uint16_t length)
{
    uint16_t i;

    for(i = 0U; i < length; i++)
    {
        g_queue[g_queueHead] = data[i];
        g_queueHead = (uint16_t)((g_queueHead + 1U) % HTTP_QUEUE_SIZE);
    }
    g_queueUsed = (uint16_t)(g_queueUsed + length);
}

static void HttpUpload_QueueRead(uint8_t *data, uint16_t length)
{
    uint16_t i;

    for(i = 0U; i < length; i++)
    {
        data[i] = g_queue[g_queueTail];
        g_queueTail = (uint16_t)((g_queueTail + 1U) % HTTP_QUEUE_SIZE);
    }
    g_queueUsed = (uint16_t)(g_queueUsed - length);
}

uint16_t HttpUpload_Submit(const uint8_t *json, uint16_t length)
{
    static const char nameKey[] = "{\"reader_name\":\"";
    const uint16_t keyLength = (uint16_t)(sizeof(nameKey) - 1U);
    const uint16_t accepted = length;
    char suffix[20];
    uint16_t suffixLength = 0U;
    uint16_t insertAt = 0U;
    uint16_t total;
    uint8_t header[2];

    if((json == NULL) || (g_urlValid == 0U))
    {
        return 0U;
    }

    while((length > 0U) && ((json[length - 1U] == '\n') || (json[length - 1U] == '\r')))
    {
        length--;
    }
    if(length == 0U)
    {
        return 0U;
    }

    /* Part B.1: over HTTP reader_name is "name/reader-ip"; the server
     * learns the reader's address from it. */
    if((length > keyLength) && (memcmp(json, nameKey, keyLength) == 0))
    {
        uint16_t i = keyLength;
        const uint8_t *ip = ReaderSettings_Get()->ip;

        while((i < length) && (json[i] != '"'))
        {
            i = (uint16_t)(i + ((json[i] == '\\') ? 2U : 1U));
        }

        if(i < length)
        {
            insertAt = i;
            suffixLength = (uint16_t)snprintf(
                suffix, sizeof(suffix), "/%u.%u.%u.%u",
                (unsigned int)ip[0], (unsigned int)ip[1],
                (unsigned int)ip[2], (unsigned int)ip[3]
            );
        }
    }

    total = (uint16_t)(length + suffixLength);
    if(
        (total > HTTP_BODY_MAX) ||
        ((uint32_t)g_queueUsed + 2U + total > HTTP_QUEUE_SIZE)
    )
    {
        return 0U;
    }

    header[0] = (uint8_t)(total >> 8U);
    header[1] = (uint8_t)total;
    HttpUpload_QueueWrite(header, 2U);

    if(suffixLength != 0U)
    {
        HttpUpload_QueueWrite(json, insertAt);
        HttpUpload_QueueWrite((const uint8_t *)suffix, suffixLength);
        HttpUpload_QueueWrite(&json[insertAt], (uint16_t)(length - insertAt));
    }
    else
    {
        HttpUpload_QueueWrite(json, length);
    }

    return accepted;
}

/* ------------------------------------------------------------
 * Response parser
 * ------------------------------------------------------------ */

static void HttpUpload_ResetParser(void)
{
    g_parse = HTTP_PARSE_STATUS;
    g_lineLength = 0U;
    g_status = 0U;
    g_contentLength = -1;
    g_chunked = 0U;
    g_connectionClose = 0U;
    g_bodyRemaining = 0U;
    g_bodyLength = 0U;
    g_bodyTruncated = 0U;
}

static char HttpUpload_Lower(char c)
{
    return ((c >= 'A') && (c <= 'Z')) ? (char)(c - 'A' + 'a') : c;
}

/* Case-insensitive "line starts with name". */
static uint8_t HttpUpload_HeaderIs(const char *line, const char *name)
{
    while(*name != '\0')
    {
        if(HttpUpload_Lower(*line) != *name)
        {
            return 0U;
        }
        line++;
        name++;
    }
    return 1U;
}

static uint8_t HttpUpload_Contains(const char *text, const char *word)
{
    size_t wordLength = strlen(word);

    for(; *text != '\0'; text++)
    {
        size_t i;

        for(i = 0U; i < wordLength; i++)
        {
            if(HttpUpload_Lower(text[i]) != word[i])
            {
                break;
            }
        }
        if(i == wordLength)
        {
            return 1U;
        }
    }
    return 0U;
}

static void HttpUpload_StoreBodyByte(uint8_t byte)
{
    if(g_bodyLength < sizeof(g_body))
    {
        g_body[g_bodyLength++] = (char)byte;
    }
    else
    {
        g_bodyTruncated = 1U;
    }
}

/* Returns 1 when the line completes the response. */
static uint8_t HttpUpload_ProcessLine(void)
{
    const char *p;
    uint32_t value;

    g_line[g_lineLength] = '\0';

    switch(g_parse)
    {
        case HTTP_PARSE_STATUS:
            if(g_lineLength == 0U)
            {
                return 0U;   /* stray CRLF before the status line */
            }
            g_status = 0U;
            p = strchr(g_line, ' ');
            if((p != NULL) && HttpUpload_HeaderIs(g_line, "http/"))
            {
                p++;
                if(HttpUpload_ParseDecimal(&p, p + 3, 999U, &value) != 0U)
                {
                    g_status = (uint16_t)value;
                }
            }
            g_parse = HTTP_PARSE_HEADER;
            return 0U;

        case HTTP_PARSE_HEADER:
            if(g_lineLength != 0U)
            {
                if(HttpUpload_HeaderIs(g_line, "content-length:"))
                {
                    p = g_line + 15;
                    while(*p == ' ')
                    {
                        p++;
                    }
                    if(HttpUpload_ParseDecimal(&p, p + 8, 1000000UL, &value) != 0U)
                    {
                        g_contentLength = (int32_t)value;
                    }
                }
                else if(HttpUpload_HeaderIs(g_line, "transfer-encoding:"))
                {
                    g_chunked = HttpUpload_Contains(g_line + 18, "chunked");
                }
                else if(HttpUpload_HeaderIs(g_line, "connection:"))
                {
                    g_connectionClose = HttpUpload_Contains(g_line + 11, "close");
                }
                return 0U;
            }

            /* End of headers. */
            if((g_status >= 100U) && (g_status < 200U))
            {
                uint16_t keepClose = g_connectionClose;

                HttpUpload_ResetParser();   /* interim response, e.g. 100 */
                g_connectionClose = (uint8_t)keepClose;
                return 0U;
            }
            if((g_status == 204U) || (g_status == 304U))
            {
                return 1U;
            }
            if(g_chunked != 0U)
            {
                g_parse = HTTP_PARSE_CHUNK_SIZE;
                return 0U;
            }
            if(g_contentLength >= 0)
            {
                if(g_contentLength == 0)
                {
                    return 1U;
                }
                g_bodyRemaining = (uint32_t)g_contentLength;
                g_parse = HTTP_PARSE_BODY_LENGTH;
                return 0U;
            }
            g_connectionClose = 1U;
            g_parse = HTTP_PARSE_BODY_CLOSE;
            return 0U;

        case HTTP_PARSE_CHUNK_SIZE:
            value = 0U;
            for(p = g_line; *p != '\0'; p++)
            {
                char c = HttpUpload_Lower(*p);

                if((c >= '0') && (c <= '9'))
                {
                    value = (value << 4U) | (uint32_t)(c - '0');
                }
                else if((c >= 'a') && (c <= 'f'))
                {
                    value = (value << 4U) | (uint32_t)(c - 'a' + 10);
                }
                else
                {
                    break;   /* ';' chunk extension or whitespace */
                }
                if(value > 1000000UL)
                {
                    break;
                }
            }
            if(value == 0U)
            {
                g_parse = HTTP_PARSE_TRAILER;
                return 0U;
            }
            g_bodyRemaining = value;
            g_parse = HTTP_PARSE_CHUNK_DATA;
            return 0U;

        case HTTP_PARSE_CHUNK_END:
            g_parse = HTTP_PARSE_CHUNK_SIZE;
            return 0U;

        case HTTP_PARSE_TRAILER:
            return (g_lineLength == 0U) ? 1U : 0U;

        default:
            return 0U;
    }
}

/* Returns 1 when the byte completes the response. */
static uint8_t HttpUpload_ParseByte(uint8_t byte)
{
    switch(g_parse)
    {
        case HTTP_PARSE_BODY_LENGTH:
        case HTTP_PARSE_CHUNK_DATA:
            HttpUpload_StoreBodyByte(byte);
            g_bodyRemaining--;
            if(g_bodyRemaining == 0U)
            {
                if(g_parse == HTTP_PARSE_BODY_LENGTH)
                {
                    return 1U;
                }
                g_parse = HTTP_PARSE_CHUNK_END;
            }
            return 0U;

        case HTTP_PARSE_BODY_CLOSE:
            HttpUpload_StoreBodyByte(byte);
            return 0U;

        default:
            break;
    }

    if(byte == '\r')
    {
        return 0U;
    }

    if(byte == '\n')
    {
        uint8_t complete = HttpUpload_ProcessLine();

        g_lineLength = 0U;
        return complete;
    }

    if(g_lineLength < (HTTP_LINE_MAX - 1U))
    {
        g_line[g_lineLength++] = (char)byte;
    }
    return 0U;
}

/* Keep the request until a 2xx acknowledgement. Retry delay caps at 30 s.
 * RAM-only: resets and deliberate URL changes can still discard data. */
static void HttpUpload_Retry(void)
{
    g_retryDelayMs = (g_retryDelayMs == 0U) ? 1000U : g_retryDelayMs * 2U;
    if(g_retryDelayMs > HTTP_RETRY_MAX_MS) g_retryDelayMs = HTTP_RETRY_MAX_MS;
    g_retryAtMs = Time_GetTickMs();
    g_state = HTTP_STATE_RETRY;
    g_requestSent = 0U;
    HttpUpload_ResetParser();
    (void)ActiveUpload_Transport_Close();
}

/* The response is complete: free the slot, then hand a command (if any)
 * to the Identium parser. A command reply produced by the parser is
 * queued and goes out as the next request. */
static void HttpUpload_FinishResponse(uint8_t closeSocket)
{
    uint16_t i;
    uint8_t deliver;

    g_lastStatus = g_status;
    if((g_status < 200U) || (g_status >= 300U))
    {
        HttpUpload_Retry();
        return;
    }
    g_state = HTTP_STATE_IDLE;
    g_retryDelayMs = 0U;

    /* Only a 2xx body is a command. 404 {"error":...} would otherwise
     * produce an error reply, which would be answered with 404 again. */
    deliver = (uint8_t)(
        (g_status >= 200U) && (g_status < 300U) &&
        (g_bodyLength != 0U) && (g_bodyTruncated == 0U) &&
        (g_rxCallback != NULL)
    );

    /* Only a body naming a command is one. Servers commonly acknowledge
     * every POST with a status object ({"status":"success",...}); passing
     * that to the command parser produced an "invalid command_type" reply,
     * which the server acknowledged again: an endless POST loop that kept
     * the link busy and starved the tag uploads. */
    if(deliver != 0U)
    {
        static const char key[] = "\"command_type\"";
        const uint16_t keyLength = (uint16_t)(sizeof(key) - 1U);

        deliver = 0U;
        for(i = 0U; (uint32_t)i + keyLength <= g_bodyLength; i++)
        {
            if(memcmp(&g_body[i], key, keyLength) == 0)
            {
                deliver = 1U;
                break;
            }
        }
    }

    if((closeSocket != 0U) || (g_connectionClose != 0U))
    {
        (void)ActiveUpload_Transport_Close();
    }

    if(deliver != 0U)
    {
        /* One command per response; the line parser dispatches on LF, so
         * line breaks inside a pretty-printed body become spaces. */
        for(i = 0U; i < g_bodyLength; i++)
        {
            uint8_t byte = (uint8_t)g_body[i];

            g_rxCallback(((byte == '\r') || (byte == '\n')) ? (uint8_t)' ' : byte);
        }
        g_rxCallback((uint8_t)'\n');
    }
}

/* ------------------------------------------------------------
 * Requests
 * ------------------------------------------------------------ */

static uint32_t HttpUpload_TimeoutMs(void)
{
    uint32_t seconds = ReaderSettings_Get()->httpTimeout;

    if(seconds == 0U)
    {
        seconds = Configuration_Service_GetRecvTimeoutSeconds();
    }
    if(seconds == 0U)
    {
        seconds = HTTP_DEFAULT_TIMEOUT_S;
    }
    if(seconds < HTTP_MIN_TIMEOUT_S)
    {
        seconds = HTTP_MIN_TIMEOUT_S;
    }
    if(seconds > HTTP_MAX_TIMEOUT_S)
    {
        seconds = HTTP_MAX_TIMEOUT_S;
    }
    return seconds * 1000U;
}

static uint8_t HttpUpload_StartNext(void)
{
    uint8_t header[2];
    uint16_t bodyLength;
    int written;

    if(g_queueUsed < 2U)
    {
        return 0U;
    }

    HttpUpload_QueueRead(header, 2U);
    bodyLength = (uint16_t)(((uint16_t)header[0] << 8U) | header[1]);

    written = snprintf(
        g_request,
        sizeof(g_request),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: IdentiumReader/%u.%u.%u.%u\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %u\r\n"
        "Connection: keep-alive\r\n"
        "\r\n",
        g_url.path,
        g_url.host,
        (unsigned int)PRODUCT_MAINBOARD_VERSION_0,
        (unsigned int)PRODUCT_MAINBOARD_VERSION_1,
        (unsigned int)PRODUCT_MAINBOARD_VERSION_2,
        (unsigned int)PRODUCT_MAINBOARD_VERSION_3,
        (unsigned int)bodyLength
    );

    if(
        (written <= 0) ||
        ((uint32_t)written + bodyLength > sizeof(g_request))
    )
    {
        /* Cannot happen with the limits above; drop the event. */
        uint8_t discard;

        while(bodyLength-- > 0U)
        {
            HttpUpload_QueueRead(&discard, 1U);
        }
        return 0U;
    }

    HttpUpload_QueueRead((uint8_t *)&g_request[written], bodyLength);

    g_requestLength = (uint16_t)(written + bodyLength);
    g_requestSent = 0U;
    g_retryDelayMs = 0U;
    g_state = HTTP_STATE_SENDING;
    HttpUpload_ResetParser();
    return 1U;
}

void HttpUpload_Init(Ethernet_RxByteCallback_t rxCallback)
{
    g_rxCallback = rxCallback;
    g_queueHead = 0U;
    g_queueTail = 0U;
    g_queueUsed = 0U;
    g_state = HTTP_STATE_IDLE;
    g_lastStatus = 0U;
    HttpUpload_ResetParser();
    (void)HttpUpload_Configure();
}

uint8_t HttpUpload_Configure(void)
{
    g_urlValid = HttpUpload_ParseUrl(ReaderSettings_Get()->httpUrl, &g_url);

    /* A request built for the previous URL is not sent to the new one. */
    g_state = HTTP_STATE_IDLE;
    HttpUpload_ResetParser();

    return g_urlValid;
}

uint8_t HttpUpload_GetTarget(uint8_t ip[4], uint16_t *port)
{
    if((g_urlValid == 0U) || (ip == NULL) || (port == NULL))
    {
        return 0U;
    }

    memcpy(ip, g_url.ip, 4U);
    *port = g_url.port;
    return 1U;
}

void HttpUpload_OnDisconnected(void)
{
    if(g_state == HTTP_STATE_IDLE)
    {
        return;
    }

    /* "Read until close" body: the close ends the response. */
    if((g_state == HTTP_STATE_WAITING) && (g_parse == HTTP_PARSE_BODY_CLOSE))
    {
        HttpUpload_FinishResponse(0U);
        return;
    }

    if(g_state != HTTP_STATE_RETRY) HttpUpload_Retry();
}

uint8_t HttpUpload_IsBusy(void)
{
    return ((g_state != HTTP_STATE_IDLE) || (g_queueUsed != 0U)) ? 1U : 0U;
}

void HttpUpload_Task(void)
{
    static uint8_t rx[512];
    uint16_t received;
    uint16_t i;
    uint32_t now = Time_GetTickMs();

    received = ActiveUpload_Transport_Receive(rx, sizeof(rx));
    for(i = 0U; i < received; i++)
    {
        if((g_state == HTTP_STATE_IDLE) || (g_state == HTTP_STATE_RETRY))
        {
            break;   /* nothing outstanding: unsolicited bytes are ignored */
        }

        if(HttpUpload_ParseByte(rx[i]) != 0U)
        {
            HttpUpload_FinishResponse(0U);
            return;   /* the socket may be closed now */
        }
    }

    if(g_state == HTTP_STATE_IDLE)
    {
        (void)HttpUpload_StartNext();
    }

    if(g_state == HTTP_STATE_RETRY)
    {
        if((now - g_retryAtMs) < g_retryDelayMs) return;
        g_state = HTTP_STATE_SENDING;
    }

    if(g_state == HTTP_STATE_SENDING)
    {
        uint16_t remaining = (uint16_t)(g_requestLength - g_requestSent);
        uint16_t space = ActiveUpload_Transport_GetTxFreeSize();

        if(space > remaining)
        {
            space = remaining;
        }
        if(space != 0U)
        {
            g_requestSent = (uint16_t)(
                g_requestSent +
                ActiveUpload_Transport_Send((const uint8_t *)&g_request[g_requestSent], space)
            );
        }
        if(g_requestSent >= g_requestLength)
        {
            g_state = HTTP_STATE_WAITING;
            g_sentAtMs = now;
        }
        return;
    }

    if(
        (g_state == HTTP_STATE_WAITING) &&
        ((now - g_sentAtMs) >= HttpUpload_TimeoutMs())
    )
    {
        HttpUpload_Retry();
    }
}

uint16_t HttpUpload_GetLastStatus(void)
{
    return g_lastStatus;
}
