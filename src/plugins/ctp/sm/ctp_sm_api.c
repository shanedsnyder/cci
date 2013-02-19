/*
 * Copyright (c) 2013 UT-Battelle, LLC.  All rights reserved.
 * $COPYRIGHT$
 */

#include "cci/private_config.h"

#include <stdio.h>

#include "cci.h"
#include "plugins/ctp/ctp.h"
#include "ctp_sm.h"

/*
 * Local functions
 */
static int ctp_sm_init(cci_plugin_ctp_t * plugin, uint32_t abi_ver, uint32_t flags, uint32_t * caps);
static int ctp_sm_finalize(cci_plugin_ctp_t * plugin);
static const char *ctp_sm_strerror(cci_endpoint_t * endpoint, enum cci_status status);
static int ctp_sm_create_endpoint(cci_device_t * device,
				    int flags,
				    cci_endpoint_t ** endpoint,
				    cci_os_handle_t * fd);
static int ctp_sm_destroy_endpoint(cci_endpoint_t * endpoint);
static int ctp_sm_accept(cci_event_t *event, const void *context);
static int ctp_sm_reject(cci_event_t *event);
static int ctp_sm_connect(cci_endpoint_t * endpoint, const char *server_uri,
			    const void *data_ptr, uint32_t data_len,
			    cci_conn_attribute_t attribute,
			    const void *context, int flags, const struct timeval *timeout);
static int ctp_sm_disconnect(cci_connection_t * connection);
static int ctp_sm_set_opt(cci_opt_handle_t * handle,
			    cci_opt_name_t name, const void *val);
static int ctp_sm_get_opt(cci_opt_handle_t * handle,
			    cci_opt_name_t name, void *val);
static int ctp_sm_arm_os_handle(cci_endpoint_t * endpoint, int flags);
static int ctp_sm_get_event(cci_endpoint_t * endpoint,
			      cci_event_t ** event);
static int ctp_sm_return_event(cci_event_t * event);
static int ctp_sm_send(cci_connection_t * connection,
			 const void *msg_ptr, uint32_t msg_len,
			 const void *context, int flags);
static int ctp_sm_sendv(cci_connection_t * connection,
			  const struct iovec *data, uint32_t iovcnt,
			  const void *context, int flags);
static int ctp_sm_rma_register(cci_endpoint_t * endpoint,
				 void *start, uint64_t length,
				 int flags, cci_rma_handle_t ** rma_handle);
static int ctp_sm_rma_deregister(cci_endpoint_t * endpoint, cci_rma_handle_t * rma_handle);
static int ctp_sm_rma(cci_connection_t * connection,
			const void *msg_ptr, uint32_t msg_len,
			cci_rma_handle_t * local_handle, uint64_t local_offset,
			cci_rma_handle_t * remote_handle, uint64_t remote_offset,
			uint64_t data_len, const void *context, int flags);

/*
 * Public plugin structure.
 *
 * The name of this structure must be of the following form:
 *
 *    cci_ctp_<your_plugin_name>_plugin
 *
 * This allows the symbol to be found after the plugin is dynamically
 * opened.
 *
 * Note that your_plugin_name should match the direct name where the
 * plugin resides.
 */
cci_plugin_ctp_t cci_ctp_sm_plugin = {
	{
	 /* Logistics */
	 CCI_ABI_VERSION,
	 CCI_CTP_API_VERSION,
	 "sm",
	 CCI_MAJOR_VERSION, CCI_MINOR_VERSION, CCI_RELEASE_VERSION,
	 30, /* same as sock and tcp */

	 /* Bootstrap function pointers */
	 cci_ctp_sm_post_load,
	 cci_ctp_sm_pre_unload,
	 },

	/* API function pointers */
	ctp_sm_init,
	ctp_sm_finalize,
	ctp_sm_strerror,
	ctp_sm_create_endpoint,
	ctp_sm_destroy_endpoint,
	ctp_sm_accept,
	ctp_sm_reject,
	ctp_sm_connect,
	ctp_sm_disconnect,
	ctp_sm_set_opt,
	ctp_sm_get_opt,
	ctp_sm_arm_os_handle,
	ctp_sm_get_event,
	ctp_sm_return_event,
	ctp_sm_send,
	ctp_sm_sendv,
	ctp_sm_rma_register,
	ctp_sm_rma_deregister,
	ctp_sm_rma
};

static int ctp_sm_init(cci_plugin_ctp_t *plugin, uint32_t abi_ver, uint32_t flags, uint32_t * caps)
{
	CCI_ENTER;

	debug(CCI_DB_INFO, "%s", "In sm_init\n");

	CCI_EXIT;
	return CCI_SUCCESS;
}

static int ctp_sm_finalize(cci_plugin_ctp_t * plugin)
{
	CCI_ENTER;

	debug(CCI_DB_INFO, "%s", "In sm_free_devices\n");

	CCI_EXIT;
	return CCI_ERR_NOT_IMPLEMENTED;
}

static const char *ctp_sm_strerror(cci_endpoint_t * endpoint, enum cci_status status)
{
	CCI_ENTER;

	debug(CCI_DB_INFO, "%s", "In sm_sterrror\n");

	CCI_EXIT;
	return NULL;
}

static int ctp_sm_create_endpoint(cci_device_t * device,
				    int flags,
				    cci_endpoint_t ** endpoint,
				    cci_os_handle_t * fd)
{
	CCI_ENTER;

	debug(CCI_DB_INFO, "%s", "In sm_create_endpoint\n");

	CCI_EXIT;
	return CCI_ERR_NOT_IMPLEMENTED;
}

static int ctp_sm_destroy_endpoint(cci_endpoint_t * endpoint)
{
	CCI_ENTER;

	debug(CCI_DB_INFO, "%s", "In sm_destroy_endpoint\n");

	CCI_EXIT;
	return CCI_ERR_NOT_IMPLEMENTED;
}

static int ctp_sm_accept(cci_event_t *event, const void *context)
{
	CCI_ENTER;

	debug(CCI_DB_INFO, "%s", "In sm_accept\n");

	CCI_EXIT;
	return CCI_ERR_NOT_IMPLEMENTED;
}

static int ctp_sm_reject(cci_event_t *event)
{
	CCI_ENTER;

	debug(CCI_DB_INFO, "%s", "In sm_reject\n");

	CCI_EXIT;
	return CCI_ERR_NOT_IMPLEMENTED;
}

static int ctp_sm_connect(cci_endpoint_t * endpoint, const char *server_uri,
			    const void *data_ptr, uint32_t data_len,
			    cci_conn_attribute_t attribute,
			    const void *context, int flags, const struct timeval *timeout)
{
	CCI_ENTER;

	debug(CCI_DB_INFO, "%s", "In sm_connect\n");

	CCI_EXIT;
	return CCI_ERR_NOT_IMPLEMENTED;
}

static int ctp_sm_disconnect(cci_connection_t * connection)
{
	CCI_ENTER;

	debug(CCI_DB_INFO, "%s", "In sm_disconnect\n");

	CCI_EXIT;
	return CCI_ERR_NOT_IMPLEMENTED;
}

static int ctp_sm_set_opt(cci_opt_handle_t * handle,
			    cci_opt_name_t name, const void *val)
{
	CCI_ENTER;

	debug(CCI_DB_INFO, "%s", "In sm_set_opt\n");

	CCI_EXIT;
	return CCI_ERR_NOT_IMPLEMENTED;
}

static int ctp_sm_get_opt(cci_opt_handle_t * handle,
			    cci_opt_name_t name, void *val)
{
	CCI_ENTER;

	debug(CCI_DB_INFO, "%s", "In sm_get_opt\n");

	CCI_EXIT;
	return CCI_ERR_NOT_IMPLEMENTED;
}

static int ctp_sm_arm_os_handle(cci_endpoint_t * endpoint, int flags)
{
	CCI_ENTER;

	debug(CCI_DB_INFO, "%s", "In sm_arm_os_handle\n");

	CCI_EXIT;
	return CCI_ERR_NOT_IMPLEMENTED;
}

static int ctp_sm_get_event(cci_endpoint_t * endpoint,
			      cci_event_t ** event)
{
	CCI_ENTER;

	debug(CCI_DB_INFO, "%s", "In sm_get_event\n");

	CCI_EXIT;
	return CCI_ERR_NOT_IMPLEMENTED;
}

static int ctp_sm_return_event(cci_event_t * event)
{
	CCI_ENTER;

	debug(CCI_DB_INFO, "%s", "In sm_return_event\n");

	CCI_EXIT;
	return CCI_ERR_NOT_IMPLEMENTED;
}

static int ctp_sm_send(cci_connection_t * connection,
			 const void *msg_ptr, uint32_t msg_len,
			 const void *context, int flags)
{
	CCI_ENTER;

	debug(CCI_DB_INFO, "%s", "In sm_send\n");

	CCI_EXIT;
	return CCI_ERR_NOT_IMPLEMENTED;
}

static int ctp_sm_sendv(cci_connection_t * connection,
			  const struct iovec *data, uint32_t iovcnt,
			  const void *context, int flags)
{
	CCI_ENTER;

	debug(CCI_DB_INFO, "%s", "In sm_sendv\n");

	CCI_EXIT;
	return CCI_ERR_NOT_IMPLEMENTED;
}

static int ctp_sm_rma_register(cci_endpoint_t * endpoint,
				 void *start, uint64_t length,
				 int flags, cci_rma_handle_t ** rma_handle)
{
	CCI_ENTER;

	debug(CCI_DB_INFO, "%s", "In sm_rma_register\n");

	CCI_EXIT;
	return CCI_ERR_NOT_IMPLEMENTED;
}

static int ctp_sm_rma_deregister(cci_endpoint_t * endpoint, cci_rma_handle_t * rma_handle)
{
	CCI_ENTER;

	debug(CCI_DB_INFO, "%s", "In sm_rma_deregister\n");

	CCI_EXIT;
	return CCI_ERR_NOT_IMPLEMENTED;
}

static int ctp_sm_rma(cci_connection_t * connection,
			const void *msg_ptr, uint32_t msg_len,
			cci_rma_handle_t * local_handle, uint64_t local_offset,
			cci_rma_handle_t * remote_handle, uint64_t remote_offset,
			uint64_t data_len, const void *context, int flags)
{
	CCI_ENTER;

	debug(CCI_DB_INFO, "%s", "In sm_rma\n");

	CCI_EXIT;
	return CCI_ERR_NOT_IMPLEMENTED;
}