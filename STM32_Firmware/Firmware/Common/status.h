/*
 * status.h
 *
 *  Created on: 26-Sept-2026
 *      Author: Identium
 */

#ifndef STATUS_H_
#define STATUS_H_


#include <stdint.h>

typedef enum
{
    STATUS_OK = 0U,
    STATUS_ERROR,
    STATUS_TIMEOUT,
    STATUS_BUSY,
    STATUS_INVALID_PARAM,
    STATUS_NOT_READY,
    STATUS_NO_DATA,
    STATUS_OVERFLOW,
    STATUS_UNSUPPORTED
} Status_t;




#endif /* STATUS_H_ */
