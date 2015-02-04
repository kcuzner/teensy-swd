/**
 * Shared header file defining the communication proctocol between host and
 * device.
 */

#ifndef _USB_TYPES_H_

/**
 * How this thing is going to work:
 * This USB code is based on mine from previously...we haven't yet used it with
 * endpoints and stuff and I want to keep this simple, so this only uses the
 * control endpoint.
 *
 * There are three control requests:
 * 0x2000 - Begin write request
 * 0x2100 - Begin read request
 * 0x2280 - Read request status
 *
 * Each request uses the wIndex field to send an 8-bit command index which will
 * be used to track the command. Commands are queued in the order received. The
 * indexes are shared between read and write. An attempt to begin a request
 * using an index whose swd_request_t.done is FALSE will result in a STALL since
 * the swd module still has control over that request. wIndex values greater
 * than 255 will result in a STALL.
 *
 * Any request can be read by issuing the read request status command with the
 * wIndex set to the index to be read. An index greater than 255 results in a
 * STALL.
 */

#define USB_SWD_BEGIN_READ 0x2000
#define USB_SWD_BEGIN_WRITE 0x2100
#define USB_SWD_READ_STATUS 0x2280

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct {
    uint8_t request;
} read_req_t;

typedef struct {
    uint8_t request;
    uint32_t data;
} write_req_t;

#ifdef __cplusplus
}
#endif

#endif // _USB_TYPES_H_
