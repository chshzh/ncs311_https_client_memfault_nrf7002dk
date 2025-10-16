/*
 * Memfault service helpers shared by the HTTPS client sample.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize Memfault services (logging, buttons, firmware confirmation). */
#ifdef CONFIG_MEMFAULT
int mflt_backend_services_init(void);

/** Notify Memfault services that the network connection is available. */
void mflt_backend_services_handle_network_connected(void);

/** Notify Memfault services that the network connection has been lost. */
void mflt_backend_services_handle_network_disconnected(void);
#else
static inline int mflt_backend_services_init(void)
{
	return 0;
}

static inline void mflt_backend_services_handle_network_connected(void)
{
}

static inline void mflt_backend_services_handle_network_disconnected(void)
{
}
#endif /* CONFIG_MEMFAULT */

#ifdef __cplusplus
}
#endif
