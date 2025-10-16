/*
 * Memfault service helpers shared by the HTTPS client sample.
 */

#include "mflt_backend_services.h"

#if defined(CONFIG_MEMFAULT)

#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>

#include <memfault/core/data_packetizer.h>
#include <memfault/core/log.h>
#include <memfault/core/trace_event.h>
#include <memfault/metrics/metrics.h>
#include <memfault/panics/coredump.h>
#include <memfault/ports/zephyr/http.h>
#include <memfault_ncs.h>

#if !defined(CONFIG_DK_LIBRARY)
#error "CONFIG_DK_LIBRARY must be enabled when Memfault services are compiled"
#endif

#include <dk_buttons_and_leds.h>

#include "mflt_ota_triggers.h"
#include "mflt_wifi_metrics.h"
#if CONFIG_MEMFAULT_NCS_STACK_METRICS
#include "mflt_stack_metrics.h"
#endif

LOG_MODULE_REGISTER(memfault_backend, CONFIG_MEMFAULT_BACKEND_LOG_LEVEL);

#define LONG_PRESS_THRESHOLD_MS 3000

static bool wifi_connected;
static bool stack_metrics_initialized;

static int64_t button_press_ts_btn1;
static int64_t button_press_ts_btn2;

/* Recursive Fibonacci calculation used to trigger stack overflow. */
static int fib(int n)
{
	if (n <= 1) {
		return n;
	}

	return fib(n - 1) + fib(n - 2);
}

void memfault_metrics_heartbeat_collect_data(void)
{
	/* Maintain default NCS metrics collection (stack, connectivity, etc.) */
	memfault_ncs_metrics_collect_data();

	/* Append custom Wi-Fi metrics */
	mflt_wifi_metrics_collect();
}

static void on_connect(void)
{
#if IS_ENABLED(MEMFAULT_NCS_LTE_METRICS)
	uint32_t time_to_lte_connection;

	/* Retrieve the LTE time to connect metric. */
	memfault_metrics_heartbeat_timer_read(MEMFAULT_METRICS_KEY(ncs_lte_time_to_connect_ms),
					      &time_to_lte_connection);

	LOG_INF("Time to connect: %d ms", time_to_lte_connection);
#endif /* IS_ENABLED(MEMFAULT_NCS_LTE_METRICS) */

	if (IS_ENABLED(CONFIG_MEMFAULT_NCS_POST_COREDUMP_ON_NETWORK_CONNECTED) &&
	    memfault_coredump_has_valid_coredump(NULL)) {
		/* Coredump sending handled internally */
		return;
	}

	LOG_INF("Sending captured Memfault data");

	/* Trigger collection of heartbeat data. */
	memfault_metrics_heartbeat_debug_trigger();

	/* Check if there is any data available to be sent. */
	if (!memfault_packetizer_data_available()) {
		LOG_DBG("There was no data to be sent");
		return;
	}

	LOG_DBG("Sending stored data...");

	/* Send the data that has been captured to the Memfault cloud. */
	memfault_zephyr_port_post_data();
}

static void button_handler(uint32_t button_states, uint32_t has_changed)
{
	uint32_t buttons_pressed = has_changed & button_states;
	uint32_t buttons_released = has_changed & ~button_states;

	if (buttons_pressed & DK_BTN1_MSK) {
		button_press_ts_btn1 = k_uptime_get();
	}

	if (buttons_released & DK_BTN1_MSK) {
		int64_t duration = k_uptime_get() - button_press_ts_btn1;
		if (duration >= LONG_PRESS_THRESHOLD_MS) {
			LOG_WRN("Stack overflow will now be triggered");
			fib(10000);
		} else {
			LOG_INF("Button 1 short press detected, triggering Memfault heartbeat");
			if (wifi_connected) {
				memfault_metrics_heartbeat_debug_trigger();
				memfault_zephyr_port_post_data();
			} else {
				LOG_WRN("WiFi not connected, cannot collect metrics");
			}
		}
	}

	if (buttons_pressed & DK_BTN2_MSK) {
		button_press_ts_btn2 = k_uptime_get();
	}

	if (buttons_released & DK_BTN2_MSK) {
		int64_t duration = k_uptime_get() - button_press_ts_btn2;
		if (duration >= LONG_PRESS_THRESHOLD_MS) {
			volatile uint32_t i;

			LOG_WRN("Division by zero will now be triggered");
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdiv-by-zero"
			i = 1 / 0;
#pragma GCC diagnostic pop
			ARG_UNUSED(i);
		} else {
			LOG_INF("Button 2 short press detected, scheduling Memfault OTA check");
			mflt_ota_triggers_notify_button();
		}
	}

	if (buttons_pressed & DK_BTN3_MSK) {
		/* DK_BTN3_MSK is Switch 1 on nRF9160 DK. */
		int err = MEMFAULT_METRIC_ADD(switch_1_toggle_count, 1);
		if (err) {
			LOG_ERR("Failed to increment switch_1_toggle_count");
		} else {
			LOG_INF("switch_1_toggle_count incremented");
		}
	}

	if (buttons_pressed & DK_BTN4_MSK) {
		/* DK_BTN4_MSK is Switch 2 on nRF9160 DK. */
		MEMFAULT_TRACE_EVENT_WITH_LOG(switch_2_toggled, "Switch state: %d",
					      buttons_pressed & DK_BTN4_MSK ? 1 : 0);
		LOG_INF("switch_2_toggled event has been traced, button state: %d",
			buttons_pressed & DK_BTN4_MSK ? 1 : 0);
	}
}

int mflt_backend_services_init(void)
{
	int err;

	LOG_INF("Initializing Memfault services");

#if defined(CONFIG_MCUBOOT)
	if (!boot_is_img_confirmed()) {
		err = boot_write_img_confirmed();
		if (err) {
			LOG_ERR("New OTA FW confirm failed: %d", err);
		} else {
			LOG_INF("New OTA FW confirmed!");
		}
	}
#endif

	/* Lower the Memfault log capture threshold so debug logs are stored & uploaded. */
	memfault_log_set_min_save_level(kMemfaultPlatformLogLevel_Debug);

	err = dk_buttons_init(button_handler);
	if (err) {
		LOG_ERR("dk_buttons_init, error: %d", err);
		return err;
	}

	return 0;
}

void mflt_backend_services_handle_network_connected(void)
{
	wifi_connected = true;

#if CONFIG_MEMFAULT_NCS_STACK_METRICS
	if (!stack_metrics_initialized) {
		mflt_stack_metrics_init();
		stack_metrics_initialized = true;
		LOG_INF("Stack metrics monitoring initialized");
	}
#endif

	mflt_ota_triggers_notify_connected();
	on_connect();
}

void mflt_backend_services_handle_network_disconnected(void)
{
	wifi_connected = false;
}

#endif /* defined(CONFIG_MEMFAULT) */
