/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include "usb/usb_host.h"
#include "usb/usb_types_uvc.h"
#include "esp_err.h"

// Use this macros for opening a UVC stream with any VID or PID
#define UVC_HOST_ANY_VID (0)
#define UVC_HOST_ANY_PID (0)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct uvc_host_stream_s *uvc_host_stream_hdl_t;

/**
 * @brief Configuration structure of USB Host UVC driver
 */
typedef struct {
    size_t driver_task_stack_size; /**< Stack size of the driver's task */
    unsigned driver_task_priority; /**< Priority of the driver's task */
    int xCoreID;                   /**< Core affinity of the driver's task */
    bool create_background_task;   /**< When set to true, background task handling usb events is created.
                                        Otherwise user has to periodically call uvc_host_handle_events function */
} uvc_host_driver_config_t;

/**
 * @brief Event types of this driver
 */
enum uvc_host_dev_event {
    UVC_HOST_TRANSFER_ERROR,         /**< USB transfer error */
    UVC_HOST_DEVICE_DISCONNECTED,    /**< Device was suddenly disconnected. The stream is stopped. */
    UVC_HOST_FRAME_BUFFER_OVERFLOW,  /**< The received frame was discarded because it exceeded the available frame buffer space.
                                          To resolve this, increase the `frame_size` parameter in `uvc_host_stream_config_t.advanced` to allocate a larger buffer. */
    UVC_HOST_FRAME_BUFFER_UNDERFLOW, /**< The received frame was discarded because no available buffer was free for storage.
                                          To address this, either optimize your processing speed or increase the `number_of_frame_buffers` parameter in
                                          `uvc_host_stream_config_t.advanced` to allocate additional buffers. */
};

/**
 * @brief UVC Device Event data structure
 */
typedef struct {
    enum uvc_host_dev_event type;
    union {
        struct {
            esp_err_t error;               //!< Error code from USB Host
        } transfer_error;                  // UVC_HOST_TRANSFER_ERROR
        struct {
            uvc_host_stream_hdl_t stream_hdl;  //!< Disconnection event
        } device_disconnected;                 // UVC_HOST_DEVICE_DISCONNECTED
        struct {
        } frame_overflow; // UVC_HOST_FRAME_BUFFER_OVERFLOW
        struct {
        } frame_underflow; // UVC_HOST_FRAME_BUFFER_UNDERFLOW
    };
} uvc_host_stream_event_data_t;

/**
 * @brief Formats supported by this driver
 */
enum uvc_host_stream_format {
    UVC_VS_FORMAT_UNDEFINED = 0, // Invalid format. Do not request this format from the camera.
    UVC_VS_FORMAT_MJPEG,
    UVC_VS_FORMAT_YUY2,
    UVC_VS_FORMAT_H264,
    UVC_VS_FORMAT_H265,
};

typedef struct {
    unsigned h_res;                     /**< Horizontal resolution */
    unsigned v_res;                     /**< Vertical resolution */
    float fps;                          /**< Frames per second */
    enum uvc_host_stream_format format; /**< Frame coding format */
} uvc_host_stream_format_t;

/**
 * @brief Video Stream frame
 *
 * This type is returned from frame callback upon receiving new frame
 */
typedef struct {
    const uvc_host_stream_format_t vs_format; /**< Format of this frame buffer */
    size_t data_buffer_len;                   /**< Max data length supported by this frame buffer */
    size_t data_len;                          /**< Data length of currently store frame */
    uint8_t *data;                            /**< Frame data */
} uvc_host_frame_t;

/**
 * @brief Stream event callback type
 *
 * @param[in] event    Event structure
 * @param[in] user_ctx User's argument passed to open function
 */
typedef void (*uvc_host_stream_callback_t)(const uvc_host_stream_event_data_t *event, void *user_ctx);

/**
 * @brief Frame callback type
 *
 * @param[in] frame    Received frame
 * @param[in] user_ctx User's argument passed to open function
 * @return
 *     - true:  The frame was processed by user. The UVC driver owns the frame now.
 *     - false: The frame was not yet processed by user.
 *              The user must call uvc_host_frame_return() to return it to UVC driver.
 */
typedef bool (*uvc_host_frame_callback_t)(const uvc_host_frame_t *frame, void *user_ctx);

/**
 * @brief Configuration structure of UVC device
 */
typedef struct {
    uvc_host_stream_callback_t event_cb;  /**< Stream's event callback function. Can be NULL */
    uvc_host_frame_callback_t frame_cb;   /**< Stream's frame callback function */
    void *user_ctx;                       /**< User's argument that will be passed to the callbacks */
    struct {
        uint16_t vid;                     /**< Device's Vendor ID. Set to 0 for any */
        uint16_t pid;                     /**< Device's Product ID. Set to 0 for any */
        uint8_t uvc_stream_index;         /**< Index of UVC function you want to use. Set to 0 to use first available UVC function */
    } usb;
    uvc_host_stream_format_t vs_format;   /**< Video Stream format. Resolution, FPS and encoding */
    struct {
        int number_of_frame_buffers; /**< Number of frame buffers. These can be very large as they must hold the full frame.*/
        size_t frame_size;           /**< 0: Use dwMaxVideoFrameSize from format negotiation result (might be too large).
                                          (0; SIZE_MAX>: Use user provide frame size. */
        uint32_t frame_heap_caps;    /**< Memory capabilities for frame buffers. Directly passed to heap_caps_malloc() */
        int number_of_urbs;          /**< Number of URBs for this stream. Triple buffering scheme is recommended */
        size_t urb_size;             /**< Size in bytes of 1 URB, 10kB should be enough for start.
                                          Larger value results in less frequent interrupts at the cost of memory consumption */
    } advanced;
} uvc_host_stream_config_t;

/**
 * @brief Install UVC driver
 *
 * - USB Host Library must already be installed before calling this function (via usb_host_install())
 * - This function should be called before calling any other UVC driver functions
 *
 * @param[in] driver_config Driver configuration structure. If set to NULL, a default configuration will be used.
 * @return
 *     - ESP_OK: Success - driver installed
 *     - ESP_ERR_INVALID_STATE: Driver already installed or USB Host Library is not installed
 *     - ESP_ERR_NO_MEM: Not enough free memory for the driver
 */
esp_err_t uvc_host_install(const uvc_host_driver_config_t *driver_config);

/**
 * @brief Uninstall UVC driver
 *
 * - User must ensure that all UVC devices must be closed via uvc_host_stream_close() before calling this function
 *
 * @return
 *     - ESP_OK: Success - driver uninstalled
 *     - ESP_ERR_INVALID_STATE: Driver was not installed before or not all UVC devices are closed
 */
esp_err_t uvc_host_uninstall(void);

/**
 * @brief Handle UVC HOST events
 *
 * If UVC Host install was called with create_background_task=false configuration, application needs to handle USB Host events.
 * Do not call this function if UVC host install was called with create_background_task=true configuration
 *
 * @param[in]  timeout  Timeout in FreeRTOS tick
 * @return
 *     - ESP_OK: All events handled
 *     - ESP_ERR_INVALID_STATE: UVC driver not installed
 *     - ESP_ERR_TIMEOUT: No events handled within the timeout
 *     - ESP_FAIL: Event handling finished, driver uninstalled. You do not have to call this function further
 */
esp_err_t uvc_host_handle_events(unsigned long timeout);

/**
 * @brief Open UVC compliant device
 *
 * @param[in]  stream_config  Configuration structure of the device
 * @param[in]  timeout        Timeout in FreeRTOS ticks
 * @param[out] stream_hdl_ret UVC stream handle
 * @return
 *     - ESP_OK: Success - stream opened
 *     - ESP_ERR_INVALID_STATE: UVC driver is not installed
 *     - ESP_ERR_INVALID_ARG: stream_config or stream_hdl_ret is NULL
 *     - ESP_ERR_NO_MEM: Not enough free memory for the stream
 *     - ESP_ERR_NOT_FOUND: UVC stream with requested configuration was not found
 */
esp_err_t uvc_host_stream_open(const uvc_host_stream_config_t *stream_config, int timeout, uvc_host_stream_hdl_t *stream_hdl_ret);

/**
 * @brief Start UVC stream
 *
 * After this call, the user will be informed about new frames by frame callback
 *
 * @param[in] stream_hdl UVC handle obtained from uvc_host_stream_open()
 * @return
 *     - ESP_OK: Success - streaming started
 *     - ESP_ERR_INVALID_ARG: stream_hdl is NULL
 *     - ESP_ERR_INVALID_STATE: Stream already streaming
 *     - ESP_ERR_NOT_FOUND: Format negotiation error
 *     - Else: USB lib error
 */
esp_err_t uvc_host_stream_start(uvc_host_stream_hdl_t stream_hdl);

/**
 * @brief Stop UVC stream
 *
 * @param[in] stream_hdl UVC handle obtained from uvc_host_stream_open()
 * @return
 *     - ESP_OK: Success - streaming stopped
 *     - ESP_ERR_INVALID_ARG: stream_hdl is NULL
 *     - Else: USB lib error
 */
esp_err_t uvc_host_stream_stop(uvc_host_stream_hdl_t stream_hdl);

/**
 * @brief Close UVC device and release its resources
 *
 * @param[in] stream_hdl UVC handle obtained from uvc_host_stream_open()
 * @return
 *   - ESP_OK: Success - device closed
 *   - ESP_ERR_INVALID_ARG: stream_hdl is NULL
 *   - ESP_ERR_INVALID_STATE: UVC driver is not installed or some frames were not returned
 */
esp_err_t uvc_host_stream_close(uvc_host_stream_hdl_t stream_hdl);

/**
 * @brief Return processed frame back to the driver
 *
 * Must not call this function if the frame callback returns true.
 * Must call this function after the frame is processed if the frame callback returns false.
 *
 * @param[in] stream_hdl UVC handle obtained from uvc_host_stream_open()
 * @param[in] frame      Frame obtained from frame callback
 * @return
 *     - ESP_OK: Success - The frame was returned to the driver
 *     - ESP_INVALID_ARG: frame or stream_hdl is NULL
 *     - ESP_FAIL: The frame was not returned to the driver
 */
esp_err_t uvc_host_frame_return(uvc_host_stream_hdl_t stream_hdl, uvc_host_frame_t *frame);

/**
 * @brief Print device's descriptors
 *
 * Device and full Configuration descriptors are printed in human readable format to stdout.
 *
 * @param stream_hdl UVC handle obtained from uvc_host_stream_open()
 */
void uvc_host_desc_print(uvc_host_stream_hdl_t stream_hdl);

#ifdef __cplusplus
}
#endif
