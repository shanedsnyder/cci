/*
 * Copyright (c) 2010-2011 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2010-2011 Myricom, Inc.  All rights reserved.
 * Copyright (c) 2010-2011 Qlogic Corporation.  All rights reserved.
 * Copyright (c) 2010-2012 UT-Battelle, LLC.  All rights reserved.
 * Copyright (c) 2010-2012 Oak Ridge National Labs.  All rights reserved.
 * Copyright © 2012 inria.  All rights reserved.
 *
 * See COPYING in top-level directory
 *
 * $COPYRIGHT$
 *
 * Main header file for the Common Communications Interface (CCI).
 */

/*! @file
 * Open Questions:
 *
 * \todo Windows os handle: file handle, iocompletionport ???.
 --> Wait and talk to a Windows expert on this (e.g., Fab T.).

 * Goals of CCI:
 * - simplicity: small API (e.g., smaller/easier than verbs)
 * - portability: support multiple different underlying transports
 * - performance: preferably faster than TCP sockets, but definitely
     no slower than TCP sockets

 * \todo How do we return errors for non-zero-copy sends?  (e.g., RNR
   errors that take a while to occur -- may be long after the send has
   locally completed).  We can't necessarily return a pointer to the
   message that failed because the app may have overwritten it by
   then.  Possible: we could return an asynch event send error with a
   pointer to our internal buffer, with the condition that the
   internal buffer will be released when the event is returned...?

 * \todo Explain object allocation: CCI allocates everything; it may
   allocate some hidden state with it.  CCI has to clean up all
   structs, too.
*/

#ifndef CCI_H
#define CCI_H

#include "cci/public_config.h"

#include <stdio.h> /* for NULL */
#include <errno.h>
#include <stdint.h> /* may need to be fixed for windows */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/uio.h>

#ifndef CCI_DECLSPEC
#define CCI_DECLSPEC
#endif

BEGIN_C_DECLS

/* ================================================================== */
/*                                                                    */
/*                               INIT                                 */
/*                                                                    */
/* ================================================================== */

/*! \defgroup env Initialization / Environment */

/*!
  This constant is passed in via the cci_init() function and is used
  for internal consistency checks.

  \ingroup env
 */
#define CCI_ABI_VERSION 2

/*!
  This is the first CCI function that must called; no other CCI
  functions can be invoked before this function returns successfully.

   \param[in] abi_ver: A constant describing the ABI version that this
   application requires (one of the CCI_ABI_* values).

   \param[in] flags: A constant describing behaviors that this application
   requires.  Currently, 0 is the only valid value.

   \param[out] caps: Capabilities of the underlying library:
   * THREAD_SAFETY

   \return CCI_SUCCESS  CCI is available for use.
   \return CCI_EINVAL   Caps is NULL or incorrect ABI version.
   \return CCI_ENOMEM   Not enough memory to complete.
   \return CCI_ERR_NOT_FOUND    No transports or CCI_CONFIG.
   \return CCI_ERROR    Unable to parse CCI_CONFIG.
   \return Errno if fopen() fails.
   \return Each transport may have additional error codes.

   If cci_init() completes successfully, then CCI is loaded and
   available to be used in this application.  The application must
   call cci_finalize() to free all CCI resources at the end of its
   execution.

   If cci_init() fails, an appropriate error code is returned.

   If cci_init() is invoked again with the same parameters after it
   has already returned successfully, it's a no-op.  If invoked again
   with different parameters, if the CCI implementation can change its
   behavior to *also* accommodate the new behaviors indicated by the
   new parameter values, it can return successfully.  Otherwise, it
   can return a failure and continue as if cci_init() had not been
   invoked again.

  \ingroup env
*/
CCI_DECLSPEC int cci_init(uint32_t abi_ver, uint32_t flags, uint32_t * caps);

/*!
  This is the last CCI function that must be called; no other
  CCI functions can be invoked after this function.

  \return CCI_SUCCESS  CCI has been properly finalized.
  \return CCI_ERROR    CCI was not initialized when cci_finalize()
                       was called.

  If cci_init was invoked multiple times, cci_finalize() should be
  called as many times, and only the last one will not be a no-op.
 */
CCI_DECLSPEC int cci_finalize(void);

/*! \example init.c
 *  This is an example of using init and strerror.
 */

/* ================================================================== */
/*                                                                    */
/*                             STATUS                                 */
/*                                                                    */
/* ================================================================== */

/*! Status codes that are returned from CCI functions.

  Note that status code names that are derived from <errno.h>
  generally follow the same naming convention (e.g., EINVAL ->
  CCI_EINVAL).  Error status codes that are unique to CCI are of the
  form CCI_ERR_<foo>.

  These status codes may be stringified with cci_strerror().

  \ingroup env

  IF YOU ADD TO THESE ENUM CODES, ALSO EXTEND src/api/strerror.c!!
 */
typedef enum cci_status {

	/*! Returned from most functions when they succeed. */
	CCI_SUCCESS = 0,

	/* -------------------------------------------------------------
	   General error status codes
	   ------------------------------------------------------------- */

	/*! Generic error */
	CCI_ERROR,

	/* -------------------------------------------------------------
	   Send completion status codes
	   ------------------------------------------------------------- */

	/*! For both reliable and unreliable sends, this error code means
	   that cci_disconnect() has been invoked on the send side (in
	   which case this is an application error), or the receiver
	   replied that the receiver invoked cci_disconnect(). */
	CCI_ERR_DISCONNECTED,

	/*! For a reliable send, this error code means that a receiver
	   is reachable, the connection is connected but the receiver
	   could not receive the incoming message during the timeout
	   period. If a receiver cannot receive an incoming message for
	   transient reasons (most likely out of resources), it returns
	   an Receiver-Not-Ready NACK and drops the message.  The sender
	   keeps retrying to send the message until the timeout expires,

	   If the timeout expires and the last control message received
	   from the receiver was an RNR NACK, then this message is
	   completed with the RNR status.  If the connection is both
	   reliable and ordered, then all successive sends are also
	   completed in the order in which they were issued with the RNR
	   status.

	   \todo We need a discussion somewhere in the docs of exactly what
	   happens for reliables with RNR, drops, ordering, ... etc.

	   This error code will not be returned for unreliable sends.
	 */
	CCI_ERR_RNR,

	/*! The local device is gone, not coming back */
	CCI_ERR_DEVICE_DEAD,

	/*! Error returned from remote peer indicating that the address was
	   either invalid or unable to be used for access / permissions
	   reasons. */
	CCI_ERR_RMA_HANDLE,

	/*! Error returned from remote peer indicating that it does not support
	   the operation that was requested. */
	CCI_ERR_RMA_OP,

	/*! Not yet implemented */
	CCI_ERR_NOT_IMPLEMENTED,

	/*! Not found */
	CCI_ERR_NOT_FOUND,

	/* -------------------------------------------------------------
	   Errno.h error codes
	   ------------------------------------------------------------- */

	/*! Invalid parameter passed to CCI function call */
	CCI_EINVAL = EINVAL,

	/*! For a reliable send, this error code means that the sender did
	   not get anything back from the receiver within a timeout (no
	   ACK, no NACK, etc.).  It is unknown whether the receiver
	   actually received the message or not.

	   This error code won't occur for unreliable sends.

	   For a connection request, this error code means that the initiator
	   did not get anything back from the target within a timeout.
	   It is unknown whether the target received the request and ignored
	   it, did not receive it at all, or receive it too late.
	 */
	CCI_ETIMEDOUT = ETIMEDOUT,

	/*! No more memory */
	CCI_ENOMEM = ENOMEM,

	/*! No device available */
	CCI_ENODEV = ENODEV,

	/*! The requested device is down */
	CCI_ENETDOWN = ENETDOWN,

	/*! Resource busy (e.g. port in use) */
	CCI_EBUSY = EBUSY,

	/*! Value out of range (e.g. no port available) */
	CCI_ERANGE = ERANGE,

	/*! Resource temporarily unavailable */
	CCI_EAGAIN = EAGAIN,

	/*! The output queue for a network interface is full */
	CCI_ENOBUFS = ENOBUFS,

	/*! Message too long */
	CCI_EMSGSIZE = EMSGSIZE,

	/*! No message of desired type */
	CCI_ENOMSG = ENOMSG,

	/*! Address not available */
	CCI_EADDRNOTAVAIL = EADDRNOTAVAIL,

	/*! Connection request rejected */
	CCI_ECONNREFUSED = ECONNREFUSED
	    /* ...more here, inspired from errno.h... */
} cci_status_t;

/* ================================================================== */
/*                                                                    */
/*                             DEVICES                                */
/*                                                                    */
/* ================================================================== */

/*! \defgroup devices Devices */

/*! \section devices Devices
  Device types and functions.

  \ingroup devices

  Before launching into detail, let's first describe the CCI system
  configuration file.  On POSIX systems, it is likely a simple
  INI-style text file; on Windows systems, it may be registry entries.
  The key thing is to support trivial namespaces and key=value pairs.

  Here is an example text config file:

\verbatim
# Comments are anything after the # symbols.

# Sections in this file are denoted by [section name].  Each section
# denotes a single CCI device.

[bob0]
# The only mandated field in each section is "transport".  It indicates
# which CCI transport should be applied to this device.
transport = psm

# The priority field determines the ordering of devices returned by
# cci_get_devices().  100 is the highest priority; 0 is the lowest priority.
# If not specified, the priority value is 50.
priority = 10

# The last field understood by the CCI core is the "default" field.
# Only one device is allowed to have a "true" value for default.  All
# others must be set to 0 (or unset, which is assumed to be 0).  If
# one device is marked as the default, then this device will be used
# when NULL is passed as the device when creating an endpoint.  If no
# device is marked as the default, it is undefined as to which device
# will be used when NULL is passed as the device when creating an
# endpoint.
default = 1

# All other fields are uninterpreted by the CCI core; they're just
# passed to the transport.  The transport can do whatever it wants with
# these values (e.g., system admins can set values to configure the
# transport).  transport documentation should specify what parameters are
# available, what each parameter is/does, and what its legal values
# are.

# This example shows a bonded PSM device that uses both the ipath0 and
# ipath1 devices.  Some other parameters are also passed to the PSM
# transport; it assumedly knows how to handle them.

device = ipath0,ipath1
capabilities = bonded,failover,age_of_captain:52
qos_stuff = fast

# bob2 is another PSM device, but it only uses the ipath0 device.
[bob2]
transport = psm
device = ipath0

# bob3 is another PSM device, but it only uses the ipath1 device.
[bob3]
transport = psm
device = ipath1
sl = 3 # IB service level (if applicable)

# storage is a device that uses the UDP transport.  Note that this transport
# allows specifying which device to use by specifying its IP address
# and MAC address -- assumedly it's an error if there is no single
# device that matches both the specified IP address and MAC
# (vs. specifying a specific device name).
[storage]
transport = udp
priority = 5
ip = 172.31.194.1
mac = 01:12:23:34:45
\endverbatim

The config file forms the basis for the device discussion, below.
*/

/*!
  Structure representing one CCI device. A CCI device is a [section]
  from the config file, above.

  \ingroup devices
*/
typedef const struct cci_device {
	/*! Name of the device from the config file, e.g., "bob0" */
	const char *name;

	/*! Name of the device driver, e.g., "sock" or "verbs" */
	const char *transport;

	/*! Is this device actually up and running? */
	unsigned up;

	/*! Human readable description string (to include newlines); should
	   contain debugging info, probably the network address of the
	   device at a bare minimum. */
	const char *info;

	/*! Array of "key=value" strings from the config file for this
	   device; the last pointer in the array is NULL. */
	const char * const *conf_argv;

	/*! Maximum send size supported by the device */
	uint32_t max_send_size;

	/*! Data rate per specification: data bits per second (not the
	   signaling rate). 0 if unknown. */
	uint64_t rate;

	/*! The PCI ID of this device as reported by the OS/hardware.  All
	   values will be ((uint32_t) -1) for non-PCI devices (e.g.,
	   shared memory) */
	struct {
		uint32_t domain, bus, dev, func;
	} pci;
} cci_device_t;

/*!
  Get an array of devices.

  Returns a NULL-terminated array of (struct cci_device *)'s.
  The pointers can be copied, but the actual cci_device
  instances may not.  The array of devices is allocated by the CCI
  library; there may be hidden state that the application does not
  see.

  \param[out] devices	Array of pointers to be filled by the function.
			Previous value in the pointer will be overwritten.

  \return CCI_SUCCESS   The array of devices is available.
  \return CCI_EINVAL    Devices is NULL.
  \return Each transport may have additional error codes.

  If cci_get_devices() succeeds, the entire returned set of data (to
  include the data pointed to by the individual cci_device
  instances) should be treated as const.

  If cci_get_devices() is invoked again later, it may return a larger
  array if some new devices appeared in the system.  All previously
  returned devices are guaranteed to be in the new array, but their
  status may have changed.  For instance, if the corresponding physical
  devices is not available anymore, the CCI device will have its
  "up" field unset.

  The order of devices returned corresponds to the priority fields in
  the devices.  If two devices share the same priority, their
  ordering in the return array is arbitrary.

  If cci_get_devices() fails, the value returned in devices is
  undefined.

  \ingroup devices
*/
CCI_DECLSPEC int cci_get_devices(cci_device_t * const ** devices);

/*! \example devices.c
 *  This is an example of using get_devices.
 *  It also iterates over the conf_argv array.
 */

/*====================================================================*/
/*                                                                    */
/*                            ENDPOINTS                               */
/*                                                                    */
/*====================================================================*/

/*! \defgroup endpoints Endpoints */

/*!
  And endpoint is a set of resources associated with a single NUMA
  locality.  Buffers should be pinned by the CCI implementation to the
  NUMA locality where the thread is located who calls
  create_endpoint().

  Advice to users: bind a thread to a locality before calling
  create_endpoint().

  Sidenote: if we want to someday make endpoints span multiple NUMA
  localities, we can add a function to say "add this locality (or
  thread?) to this endpoint.

  \todo Flesh this out section better.

  Endpoints are "thread safe" by default...  Meaning multiple threads
  can call functions on endpoints simultaneously and it's "safe".  No
  guarantees are made about serialization or concurrency.

  \ingroup endpoints
 */

/*! A set of flags that describe how the endpoint should be created.

  \ingroup endpoints
 */
typedef enum cci_endpoint_flags {
	/*! For future expansion */
	bogus_must_have_something_here
} cci_endpoint_flags_t;

/*! Endpoint.

  \ingroup endpoints
*/
typedef const struct cci_endpoint {
	/*! Device that runs this endpoint. */
	cci_device_t *device;
} cci_endpoint_t;

/*! OS-native handles

  \ingroup endpoints
 */
#ifdef _WIN32
typedef HANDLE cci_os_handle_t;
#else
typedef int cci_os_handle_t;
#endif

/*!
  Create an endpoint.

  \param[in] device: A pointer to a device that was returned via
  cci_get_devices() or NULL.

  \param[in] flags: Flags specifying behavior of this endpoint.

  \param[out] endpoint: A handle to the endpoint that was created.

  \param[out] fd: Operating system handle that can be used to block for
  progress on this endpoint.

  \return CCI_SUCCESS   The endpoint is ready for use.
  \return CCI_EINVAL    Endpoint or fd is NULL.
  \return CCI_ENODEV    Device is not "up".
  \return CCI_ENODEV    Device is NULL and no CCI device is available.
  \return CCI_ENOMEM    Unable to allocate enough memory.
  \return Each transport may have additional error codes.

  This function creates a CCI endpoint.  A CCI endpoint represents a
  collection of local resources (such as buffers and a completion
  queue).  An endpoint is associated with a device that performs the
  actual communication (see the description of cci_get_devices(),
  above).

  The device argument can be a pointer that was returned by
  cci_get_devices() to indicate that a specific device should be used
  for this endpoint, or NULL, indicating that the system default
  device should be used.

  If successful, cci_create_endpoint() creates an endpoint and
  returns a pointer to it in the endpoint parameter.

  cci_create_endpoint() is a local operation (i.e., it occurs on
  local hardware).  There is no need to talk to name services, etc.
  To be clear, the intent is that this function can be invoked many
  times locally without affecting any remote resources.

  If it is desirable to bind the CCI endpoint to a specific set of
  resources (e.g., a NUMA node), the application should bind the calling
  thread before calling cci_create_endpoint().

  Advice to users: to set the send/receive buffer count on the endpoint,
  call cci_set|get_opt() after creating the endpoint with the applicable
  options.

  \ingroup endpoints
*/
CCI_DECLSPEC int cci_create_endpoint(cci_device_t * device,
				     int flags,
				     cci_endpoint_t ** endpoint,
				     cci_os_handle_t * fd);

/*!
  Create an endpoint bound to a specified service.

  \param[in] device: A pointer to a device that was returned via
                     cci_get_devices() or NULL.

  \param[in] service: Device-specific service hint.

  \param[in] flags: Flags specifying behavior of this endpoint.

  \param[out] endpoint: A handle to the endpoint that was created.

  \param[out] fd: Operating system handle that can be used to block for
                  progress on this endpoint.

  \return CCI_SUCCESS   The endpoint is ready for use.
  \return CCI_EINVAL    Device, endpoint, or fd is NULL.
  \return CCI_ENODEV    Device is not "up".
  \return CCI_ENOMEM    Unable to allocate enough memory.
  \return Each transport may have additional error codes.

  This function creates a CCI endpoint bound to a specified service.
  A CCI endpoint represents a collection of local resources (such as
  buffers and a completion queue).  An endpoint is associated with a
  device that performs the actual communication (see the description
  of cci_get_devices(), above).

  The device argument must be a pointer that was returned by
  cci_get_devices() to indicate that a specific device should be used
  for this endpoint.

  The service argument is a string that provides a device-specific hint
  about how to bind the endpoint. Most transports use AF_INET sockets,
  which use a port for the service. For these transports, pass the
  requested port as a string. The shared memory (SM) transport, however,
  uses AF_UNIX, which uses a path. See README.ctp.sm for instructions
  on how to create a service string.

  If successful, cci_create_endpoint_at() creates an endpoint and
  returns a pointer to it in the endpoint parameter.

  cci_create_endpoint_at() is a local operation (i.e., it occurs on
  local hardware).  There is no need to talk to name services, etc.
  To be clear, the intent is that this function can be invoked many
  times locally without affecting any remote resources.

  If it is desirable to bind the CCI endpoint to a specific set of
  resources (e.g., a NUMA node), the application should bind the calling
  thread before calling cci_create_endpoint_at().

  Advice to users: to set the send/receive buffer count on the endpoint,
  call cci_set|get_opt() after creating the endpoint with the applicable
  options.

  \ingroup endpoints
*/
CCI_DECLSPEC int cci_create_endpoint_at(cci_device_t * device,
				        const char *service,
				        int flags,
				        cci_endpoint_t ** endpoint,
				        cci_os_handle_t * fd);

/*! Destroy an endpoint.

   \param[in] endpoint: Handle previously returned from a successful call to
   cci_create_endpoint().

   \return CCI_SUCCESS  The endpoint's resources have been released.
   \return CCI_EINVAL   Endpoint is NULL.
   \return Each transport may have additional error codes.

   Successful completion of this function makes all data structures
   and state associated with the endpoint stale (including the OS
   handle, connections, events, event buffers, and RMA registrations).
   All open connections are closed immediately -- it is exactly as if
   cci_disconnect() was invoked on every open connection on this
   endpoint.

  \ingroup endpoints
 */
CCI_DECLSPEC int cci_destroy_endpoint(cci_endpoint_t * endpoint);

/*!
  Returns a string corresponding to a CCI status enum.

  \param[in] endpoint: The CCI endpoint that returned this status,
                       NULL if none applicable.
  \param[in] status:   A CCI status enum.

  \return A string when the status is valid.
  \return NULL if not valid.

  \ingroup env
*/
CCI_DECLSPEC const char *cci_strerror(cci_endpoint_t *endpoint,
				      enum cci_status status);

/*====================================================================*/
/*                                                                    */
/*                            CONNECTIONS                             */
/*                                                                    */
/*====================================================================*/

/*! \defgroup connection Connections */

/********************/
/*                  */
/*      SERVER      */
/*                  */
/********************/

/*!
  Connection request attributes.

  CCI provides optional reliability and ordering to meet the varying
  needs of applications.

  Unreliable connections are always unordered (Unreliable/Unordered or
  UU). UU connections may be unicast or multicast. UU connections offer
  no delivery guarantees; messages may arrive once, multiple times or
  never. UU connections have no timeout.

  UU multicast connections are always unidirectional, send *or* receive.
  If an endpoint wants to join a multicast group to both send and
  receive, it needs to establish two distinct connections, one for
  sending and one for receiving.

  UU connections (unicast or multicast) only support messages (see
  Communications below).

  Reliable connections may be ordered (Reliable/Ordered or RO) or
  unordered (Reliable/Unordered or RU). Reliable connections are unicast
  only. Reliable connections deliver messages once. If the packet cannot
  be delivered after a specific amount of time, the connection is
  broken; there is no guarantee regarding which messages have been
  received successfully before the connection was broken.

  For reliable connections, RU connections allow the most aggressive
  optimization of the underlying network(s) to provide better
  performance. RO connections will reduce performance on most networks.

  Reliable connections support both messages and remote memory access
  (see Communications below).

  \ingroup connection
*/
typedef enum cci_conn_attribute {
	CCI_CONN_ATTR_RO,	/*!< Reliable ordered.  Means that
				   both completions and delivery are
				   in the same order that they were
				   issued. */
	CCI_CONN_ATTR_RU,	/*!< Reliable unordered.  Means that
				   delivery is guaranteed, but both
				   delivery and completion may be in a
				   different order than they were
				   issued. */
	CCI_CONN_ATTR_UU,	/*!< Unreliable unordered (RMA
				   forbidden).  Delivery is not
				   guaranteed, and both delivery and
				   completions may be in a different
				   order than they were issued. */
	CCI_CONN_ATTR_UU_MC_TX,	/*!< Multicast send (RMA forbidden) */
	CCI_CONN_ATTR_UU_MC_RX	/*!< Multicast recv (RMA forbidden) */
} cci_conn_attribute_t;

/*!
  Connection handle.

  \ingroup connection
*/
typedef const struct cci_connection {
	/*! Maximum send size for the connection */
	uint32_t max_send_size;
	/*! Local endpoint associated to the connection */
	cci_endpoint_t *endpoint;
	/*! Attributes of the connection */
	cci_conn_attribute_t attribute;
	/*! Application-provided, private context. */
	void *context;
} cci_connection_t;

union cci_event;
typedef const union cci_event cci_event_t;

/*!
  Accept a connection request.

  \param[in] conn_req		A connection request event previously returned by
				cci_get_event().
  \param[in] context		Cookie to be used to identify the connection in
				incoming events.

  \return CCI_SUCCESS   CCI has started completing the connection handshake.
  \return CCI_EINVAL    The event is not a connection request or it has
                        already been accepted or rejected.
  \return Each transport may have additional error codes.

  Upon success, CCI will attempt to complete the connection handshake.
  Once completed, CCI will return a CCI_EVENT_ACCEPT event. If successful,
  the event will contain a pointer to the new connection. It will always
  contain the context passed in here.

  The connection request event must still be returned to CCI via
  cci_return_event().

  \ingroup connection
*/
CCI_DECLSPEC int cci_accept(cci_event_t *conn_req, const void *context);

/*!
  Reject a connection request.

  \param[in] conn_req	Connection request event to reject.

  \return CCI_SUCCESS	Connection request has been rejected.
  \return CCI_EINVAL    The event is not a connection request or it has
                        already been accepted or rejected.
  \return Each transport may have additional error codes.

   Rejects an incoming connection request.  The connection request
   event must still be returned to CCI via cci_return_event().

   \ingroup connection
 */
CCI_DECLSPEC int cci_reject(cci_event_t *conn_req);

/*! \example server.c
 *  This application demonstrates opening an endpoint, getting connection
 *  requests, accepting connections, polling for events, and echoing received
 *  messages back to the client.
 */

/********************/
/*                  */
/*      CLIENT      */
/*                  */
/********************/

/*!
  Initiate a connection request (client side).

  Request a connection from a specific endpoint. The server endpoint's address
  is described by a Uniform Resource Identifier. The use of an URI allows for
  flexible description (IP address, hostname, etc).

  The connection request can carry limited amount of data to be passed to the
  server for application-specific usage (identification, authentication, etc).

  The connect call is always non-blocking, reliable and requires a decision
  by the server (accept or reject), even for an unreliable connection, except
  for multicast.

  Multicast connections don't necessarily involve a discrete connection
  server, they may be handled by IGMP or other distributed framework.

  Upon completion, an ...

  \param[in] endpoint	Local endpoint to use for requested connection.
  \param[in] server_uri	Uniform Resource Identifier of the server and is
                        generated by the server's endpoint when it is created.
  \param[in] data_ptr	Pointer to connection data to be sent in the
                        connection request (for authentication, etc).
  \param[in] data_len	Length of connection data.  Implementations must
                        support data_len values <= 1,024 bytes.
  \param[in] attribute	Attributes of the requested connection (reliability,
                        ordering, multicast, etc).
  \param[in] context	Cookie to be used to identify the completion through
                        a connect accepted, rejected, or failed event, and
                        used to identify the connection in incoming events.
  \param[in] flags      Currently unused.
  \param[in] timeout	NULL means forever.

  \return CCI_SUCCESS   The request is buffered and ready to be sent or
                        has been sent.
  \return CCI_EINVAL    data_len is strictly larger than CCI_CONN_REQ_LEN.
  \return Each transport may have additional error codes.

  \ingroup connection
*/
/* QUESTION: data is cached or not ? */
CCI_DECLSPEC int cci_connect(cci_endpoint_t * endpoint, const char *server_uri,
			     const void *data_ptr, uint32_t data_len,
			     cci_conn_attribute_t attribute,
			     const void *context, int flags, const struct timeval *timeout);

/*!
  This constant is the maximum value of data_len passed to cci_connect().

  \ingroup connection
 */
#define CCI_CONN_REQ_LEN    (1024)	/* see above */

/*!
  Tear down an existing connection.

  Operation is local, remote side is not notified. Any future attempt
  to use the connection will result in undefined behavior.

  \param[in] connection	Connection to server.

  \return CCI_SUCCESS   The connection's resources have been released.
  \return CCI_EINVAL    Connection is NULL.
  \return Each transport may have additional error codes.

  \ingroup connection
 */
CCI_DECLSPEC int cci_disconnect(cci_connection_t * connection);

/*! \example client.c
 *  This application demonstrates opening an endpoint, connecting to a
 *  server, sending messages, and polling for events.
 */

/* ================================================================== */
/*                                                                    */
/*                           EVENTS                                   */
/*                                                                    */
/* ================================================================== */

/*! \defgroup events Events */

/*!
  Event types.

  Each event has a unique type and the first element is always the event type.
  A detailed description of each event is provided with the event structure.

  The CCI_EVENT_NONE event type is never passed to the application and is for
  internal CCI use only.

  \ingroup events
 */
typedef enum cci_event_type {

	/*! Never use - for internal CCI use only. */
	CCI_EVENT_NONE,

	/*! A send or RMA has completed. */
	CCI_EVENT_SEND,

	/*! A message has been received. */
	CCI_EVENT_RECV,

	/*! An outgoing connection request has completed. */
	CCI_EVENT_CONNECT,

	/*! An incoming connection request from a client. */
	CCI_EVENT_CONNECT_REQUEST,

	/*! An incoming connection accept has completed. */
	CCI_EVENT_ACCEPT,

	/*! This event occurs when the keepalive timeout has expired (see
	   CCI_OPT_ENDPT_KEEPALIVE_TIMEOUT for more details). */
	CCI_EVENT_KEEPALIVE_TIMEDOUT,

	/*! A device on this endpoint has failed.

	   \todo JMS What exactly do we do here?  Do all handles
	   (connections, etc.) on the endpoint become stale?  What about
	   sends that are in-flight -- do we complete them all with an
	   error?  And so on. */
	CCI_EVENT_ENDPOINT_DEVICE_FAILED
} cci_event_type_t;

static inline const char *
cci_event_type_str(cci_event_type_t type)
{
	switch (type) {
	case CCI_EVENT_NONE:
		return "CCI_EVENT_NONE";
	case CCI_EVENT_SEND:
		return "CCI_EVENT_SEND";
	case CCI_EVENT_RECV:
		return "CCI_EVENT_RECV";
	case CCI_EVENT_CONNECT:
		return "CCI_EVENT_CONNECT";
	case CCI_EVENT_CONNECT_REQUEST:
		return "CCI_EVENT_CONNECT_REQUEST";
	case CCI_EVENT_ACCEPT:
		return "CCI_EVENT_ACCEPT";
	case CCI_EVENT_KEEPALIVE_TIMEDOUT:
		return "CCI_EVENT_KEEPALIVE_TIMEDOUT";
	case CCI_EVENT_ENDPOINT_DEVICE_FAILED:
		return "CCI_EVENT_ENDPOINT_DEVICE_FAILED";
	default:
		return "Unknown event";
	}
	/* Never get here */
	return NULL;
}

/*!
  Send event.

  A completion struct instance is returned for each cci_send() that
  requested a completion notification.

  On a reliable connection, a sender will generally complete a send
  when the receiver replies for that message.  Additionally, an error
  status may be returned (UNREACHABLE, DISCONNECTED, RNR).

  On an unreliable connection, a sender will return CCI_SUCCESS upon
  local completion (i.e., the message has been queued up to some lower
  layer -- there is no guarantee that it is "on the wire", etc.).
  Other send statuses will only be returned for local errors.

  The number of fields in this struct is intentionally limited in
  order to reduce costs associated with state storage, caching,
  updating, copying.  For example, there is no field pointing to the
  endpoint used for the send because it can be obtained from the
  cci_connection, or through the endpoint passed to the
  cci_get_event() call.

  If it is desirable to match send completions with specific sends
  (it usually is), it is the responsibility of the caller to pass a
  meaningful context value to cci_send().

  The ordering of fields in this struct is intended to reduce memory
  holes between fields.

  \ingroup events
*/
typedef struct cci_event_send {
	/*! Type of event - should equal CCI_EVENT_SEND */
	cci_event_type_t type;

	/*! Result of the send. */
	cci_status_t status;

	/*! Connection that the send was initiated on. */
	cci_connection_t *connection;

	/*! Context value that was passed to cci_send() */
	void *context;
} cci_event_send_t;

/*!
  Receive event.

  A completion struct instance is returned for each message received.

  The number of fields in this struct is intentionally limited in
  order to reduce costs associated with state storage, caching,
  updating, copying.  For example, there is no field pointing to the
  endpoint because it can be obtained from the cci_connection or
  through the endpoint passed to the cci_get_event() call.

  The ordering of fields in this struct is intended to reduce memory
  holes between fields.

  \ingroup events
*/
typedef struct cci_event_recv {
	/*! Type of event - should equal CCI_EVENT_RECV */
	cci_event_type_t type;

	/*! The length of the data (in bytes).  This value may be 0. */
	uint32_t len;

	/*! Pointer to the data.  The pointer always points to an address that is
	   8-byte aligned, unless (len == 0), in which case the value is undefined. */
	const void * ptr;

	/*! Connection that this message was received on. */
	cci_connection_t *connection;
} cci_event_recv_t;

/*!
  Connect request completion event.

  The status field may contain the following values:

  CCI_SUCCESS	The connection was accepted by the server and
		successfully established. The corresponding
		connection structure is available in the event
		connection field.

  CCI_ECONNREFUSED	The server rejected the connection request.

  CCI_ETIMEDOUT    The connection could not be established before
                   the timeout expired.

  Some transports may also return specific return codes.

  The connection field is only valid for use if status is
  CCI_SUCCESS. It is set to NULL in any other case.

  The context field is always set to what was passed to
  cci_connect(). On success, the connection structure context
  field is also set accordingly.

  The number of fields in this struct is intentionally limited in
  order to reduce costs associated with state storage, caching,
  updating, copying.  For example, there is no field pointing to the
  endpoint because it can be obtained from the cci_connection or
  through the endpoint passed to the cci_get_event() call.

  The ordering of fields in this struct is intended to reduce memory
  holes between fields.

  \ingroup events
*/
typedef struct cci_event_connect {
	/*! Type of event - should equal CCI_EVENT_CONNECT. */
	cci_event_type_t type;

	/*! Result of the connect request. */
	cci_status_t status;

	/*! Context value that was passed to cci_connect() */
	void *context;

	/*! The new connection, if the request successfully completed. */
	cci_connection_t *connection;
} cci_event_connect_t;

/*!
  Connection request event.

  An incoming connection request from a client. It includes the
  requested connection attributes (reliability and ordering) and
  an optional payload.

  The number of fields in this struct is intentionally limited in
  order to reduce costs associated with state storage, caching,
  updating, copying.

  The ordering of fields in this struct is intended to reduce memory
  holes between fields.

  This event should be passed to either cci_accept() or cci_reject()
  before being returned with cci_return_event().

  \ingroup events
*/
typedef struct cci_event_connect_request {
	/*! Type of event - should equal CCI_EVENT_CONNECT_REQUEST. */
	cci_event_type_t type;

	/*! Length of connection data */
	uint32_t data_len;

	/*! Pointer to connection data received with the connection request */
	const void *data_ptr;

	/*! Attribute of requested connection */
	cci_conn_attribute_t attribute;
} cci_event_connect_request_t;

/*!
  Accept completion event.

  The status field may contain the following values:

  CCI_SUCCESS	The accepted connection was successfully established.
		The corresponding connection structure is available
		in the event connection field.

  Some transports may also return specific return codes.

  The connection field is only valid for use if status is
  CCI_SUCCESS. It is set to NULL in any other case.

  The context field is always set to what was passed to
  cci_accept(). On success, the connection structure context
  field is also set accordingly.

  The number of fields in this struct is intentionally limited in
  order to reduce costs associated with state storage, caching,
  updating, copying.  For example, there is no field pointing to the
  endpoint because it can be obtained from the cci_connection or
  through the endpoint passed to the cci_get_event() call.

  The ordering of fields in this struct is intended to reduce memory
  holes between fields.

  \ingroup events
*/
typedef struct cci_event_accept {
	/*! Type of event - should equal CCI_EVENT_ACCEPT. */
	cci_event_type_t type;

	/*! Result of the accept. */
	cci_status_t status;

	/*! The context that was passed to cci_accept() */
	void *context;

	/*! The new connection, if the request successfully completed. */
	cci_connection_t *connection;
} cci_event_accept_t;

/*!
  Keepalive timeout event.

  We were unable to send a periodic message to the peer. The application
  can attempt communication or disconnect. The connection will continue
  to consume resources until the application calls cci_disconnect().

  The number of fields in this struct is intentionally limited in
  order to reduce costs associated with state storage, caching,
  updating, copying.  For example, there is no field pointing to the
  endpoint because it can be obtained from the cci_connection or
  through the endpoint passed to the cci_get_event() call.

  The ordering of fields in this struct is intended to reduce memory
  holes between fields.

  \ingroup events
*/
typedef struct cci_event_keepalive_timedout {
	/*! Type of event - should equal CCI_EVENT_KEEPALIVE_TIMEDOUT. */
	cci_event_type_t type;

	/*! The connection that timed out. */
	cci_connection_t *connection;
} cci_event_keepalive_timedout_t;

/*!
  Endpoint device failed event.

  The endpoint's device has failed.

  The number of fields in this struct is intentionally limited in
  order to reduce costs associated with state storage, caching,
  updating, copying.  For example, there is no field pointing to the
  endpoint because it can be obtained from the cci_connection or
  through the endpoint passed to the cci_get_event() call.

  The ordering of fields in this struct is intended to reduce memory
  holes between fields.

  \ingroup events
*/
typedef struct cci_event_endpoint_device_failed {
	/*! Type of event - should equal CCI_EVENT_ENDPOINT_DEVICE_FAILED. */
	cci_event_type_t type;

	/*! The endpoint on the device that failed. */
	cci_endpoint_t *endpoint;
} cci_event_endpoint_device_failed_t;

/*!
  Generic event

  This is union of all events and the event type. Each event must start
  with the type as well. The application can simply look at the event
  as a type to determine how to handle it.

  \ingroup events
*/
union cci_event {
	cci_event_type_t type;
	cci_event_send_t send;
	cci_event_recv_t recv;
	cci_event_connect_t connect;
	cci_event_connect_request_t request;
	cci_event_accept_t accept;
	cci_event_keepalive_timedout_t keepalive;
	cci_event_endpoint_device_failed_t dev_failed;
};

/********************/
/*                  */
/*  Event handling  */
/*                  */
/********************/

/*!

  \todo From Patrick: This function is for windows. The default way to
   do Object synchronization in Windows is to have the kernel
   continuously notify the Object in user-space. On Unixes, we can
   catch the call to poll to arm the interrupt, but we can't on
   Windows, so we have a function for that.  Since we are not Windows
   gurus, we decided to freeze it until we ask a pro about it.

  \ingroup events
*/
CCI_DECLSPEC int cci_arm_os_handle(cci_endpoint_t * endpoint, int flags);

/*!
  Get the next available CCI event.

  This function never blocks; it polls instantly to see if there is
  any pending event of any type (send completion, receive, connection
  request, etc.). If the application wants to block, it should pass the
  OS handle to the OS's native blocking mechanism (e.g., select/poll on
  the POSIX fd).  This also allows the app to busy poll for a while and
  then OS block if nothing interesting is happening.  The behavior of
  the OS handle when used with the OS blocking mechanism is to return
  the equivalent of a POLLIN which indicates that the application should
  call cci_get_event(). It does not, however, guarantee that
  cci_get_event() will return something other than CCI_EAGAIN. For
  example, the application has two threads: one blocking on the OS
  handle and another calling cci_get_event(). By the time the blocking
  thread wakes and calls cci_get_event(), the other thread may have
  already reaped all the queued events. Note, the application must never
  directly read from or write to the OS handle. The results are
  undefined.

  This function borrows the buffer associated with the event; it must
  be explicitly returned later via cci_return_event().

  \param[in] endpoint   Endpoint to poll for a new event.
  \param[in] event      New event, if any.

  \return CCI_SUCCESS   An event was retrieved.
  \return CCI_EAGAIN    No event is available.
  \return CCI_ENOBUFS	No event is available and there are no available
                        receive buffers. The application must return events
			before any more messages can be received.
  \return Each transport may have additional error codes.

   To discuss:

   - it may be convenient to optionally get multiple OS handles; one
     each for send completions, receives, and "other" (errors,
     incoming connection requests, etc.).  Should that be part of
     endpoint creation?  If we allow this concept, do we need a way to
     pass in a different CQ here to get just those types of events?

   - How do we have CCI-implementation private space in the event --
     bound by size?  I.e., how/who determines the max inline data
     size?

  \ingroup events
*/
CCI_DECLSPEC int cci_get_event(cci_endpoint_t * endpoint,
			       cci_event_t ** event);

/*!
  This function returns the buffer associated with an event that was
  previously obtained via cci_get_event().  The data buffer associated
  with the event will immediately become stale to the application.

  Events may be returned in any order; they do not need to be returned
  in the same order that cci_poll_event() issued them.  All events
  must be returned, even send completions and "other" events -- not
  just receive events.  However, it is possible (likely) that
  returning send completion and "other" events will be no-ops.

  \param[in] event	    Event to return.

  \return CCI_SUCCESS  The event was returned to CCI.
  \return CCI_EINVAL   The event is a connection request and it has
                       not been passed to cci_accept() or cci_reject().
  \return Each transport may have additional error codes.

  \todo What to do about hardware that cannot return buffers out of
     order?  Is the overhead of software queued returns (to effect
     in-order hardware returns) acceptable?

  \ingroup events
*/
CCI_DECLSPEC int cci_return_event(cci_event_t * event);

/*====================================================================*/
/*                                                                    */
/*                 ENDPOINTS / CONNECTIONS OPTIONS                    */
/*                                                                    */
/*====================================================================*/

/*! \defgroup opts Endpoint / Connection Options */

/*!
  Name of options

  \ingroup opts
*/
typedef enum cci_opt_name {
	/*! Default send timeout for all new connections.

	   cci_get_opt() and cci_set_opt().

	   The parameter must point to a uint32_t.
	 */
	CCI_OPT_ENDPT_SEND_TIMEOUT,

	/*! How many receiver buffers on the endpoint.  It is the max
	   number of messages the CCI layer can receive without dropping.

	   cci_get_opt() and cci_set_opt().

	   The parameter must point to a uint32_t.
	 */
	CCI_OPT_ENDPT_RECV_BUF_COUNT,

	/*! How many send buffers on the endpoint.  It is the max number of
	   pending messages the CCI layer can buffer before failing or
	   blocking (depending on reliability mode).

	   cci_get_opt() and cci_set_opt().

	   The parameter must point to a uint32_t.
	 */
	CCI_OPT_ENDPT_SEND_BUF_COUNT,

	/*! Send a periodic message over each reliable connection on the
	   endpoint.

	   Sending keepalive messages can determine if a peer has silently
	   disconnected. The CCI transport will periodically send a message over
	   each connection. If the transport determines that the message was
	   successfully received, it will repeat at the next period.  If the
	   transport determines that the message was not successfully delivered,
	   it will raise the CCI_EVENT_KEEPALIVE_TIMEDOUT event on the
	   connection and the keepalive timeout for that connection is set to 0
	   (i.e. disabled).

	   If a keepalive event is raised, the connection is *not* disconnected.
	   Recovery decisions are up to the application; it may choose to
	   disconnect the connection, re-arm the keepalive timeout, send a MSG
	   or RMA, etc. The application may "re-arm" the keepalive timeout for
	   the connection individually using CCI_OPT_CONN_KEEPALIVE_TIMEOUT or
	   re-arm all connections with this option.

	   The keepalive timeout is expressed in microseconds. The default is 0
	   (i.e. disabled). Using this option enables the same timeout on all
	   connections, currently opened and those opened in the future.

	   The messages are sent internally within CCI and are never visible to
	   the application either locally or at the peer. Using keepalives may
	   cause spurious wake ups when using the OS handle for blocking.

	   cci_get_opt() and cci_set_opt().

	   The parameter must point to a uint32_t.
	 */
	CCI_OPT_ENDPT_KEEPALIVE_TIMEOUT,

	/*! Retrieve the endpoint's URI used for listening for connection
	   requests. The application should never need to parse this URI.

	   cci_get_opt() only.

	   The parameter must point to a char *.
	   The application is responsible for freeing the pointer that is
	   stored in this char *.
	 */
	CCI_OPT_ENDPT_URI,

	/*! RMA registration alignment requirements, if any, for this endpoint.
	   This option needs the address of a cci_alignment_t pointer passed in.
	   The CTP will allocate and fill in the struct with the minimal
	   alignment needed for each member of the struct. A value of 0
	   indicates that there are no alignment requirements for that member. A
	   value of 4, for example, indicates that that member must be 4-byte
	   aligned.

	   If the CTP requires RMA alignment and the application passes in an
	   un-aligned parameter, the CTP may need to allocate a temporary
	   buffer, register it, and use it instead. This will also require a
	   copy of the data to the correct location. This will decrease
	   performance for these cases.

           cci_get_opt() only.

	   The parameter must point to a cci_alignment_t.
        */
	CCI_OPT_ENDPT_RMA_ALIGN,

	/*! Reliable send timeout in microseconds.

	   cci_get_opt() and cci_set_opt().

	   The parameter must point to a uint32_t.
	 */
	CCI_OPT_CONN_SEND_TIMEOUT,

	/*! Send a periodic message over this reliable connection.

	   This option is similar to CCI_OPT_ENDPT_KEEPALIVE_TIMEOUT except that
	   it modifies the keepalive timeout on a single connection only. The
	   application may use it to re-arm a connection that has raised a
	   CCI_EVENT_KEEPALIVE_TIMEDOUT, to selectively arm only some
	   connections, or to set a timeout different from the endpoint's
	   keepalive timeout period.

	   cci_get_opt() and cci_set_opt().

	   The parameter must point to a uint32_t.
	 */
	CCI_OPT_CONN_KEEPALIVE_TIMEOUT
} cci_opt_name_t;

typedef struct cci_alignment {
	uint32_t rma_write_local_addr;	/*!< WRITE local_handle->start + offset */
	uint32_t rma_write_remote_addr;	/*!< WRITE remote_handle->start + offset */
	uint32_t rma_write_length;	/*!< WRITE length */
	uint32_t rma_read_local_addr;	/*!< READ local_handle->start + offset */
	uint32_t rma_read_remote_addr;	/*!< READ remote_handle->start + offset */
	uint32_t rma_read_length;	/*!< READ length */
} cci_alignment_t;

typedef const void cci_opt_handle_t;

/*!
  Set an endpoint or connection option value.

  \param[in] handle Endpoint or connection handle.
  \param[in] name   Which option to set the value of.
  \param[in] val    Pointer to the input value. The type of the value
                    must match the option name.

  Depending on the value of name, handle must be a cci_endpoint_t*
  or a cci_connection_t*.

  \return CCI_SUCCESS   Value successfully set.
  \return CCI_EINVAL    Handle or val is NULL.
  \return CCI_EINVAL    Trying to set a get-only option.
  \return CCI_ERR_NOT_IMPLEMENTED   Not supported by this transport.
  \return Each transport may have additional error codes.

  Note that the set may fail if the CCI implementation cannot
  actually set the value.

  \ingroup opts
*/
CCI_DECLSPEC int cci_set_opt(cci_opt_handle_t * handle,
			     cci_opt_name_t name, const void *val);

/*!
  Get an endpoint or connection option value.

  \param[in] handle Endpoint or connection handle.
  \param[in] name   Which option to get the value of.
  \param[in] val    Pointer to the output value. The type of the value
                    must match the option name.

  Depending on the value of name, handle must be a cci_endpoint_t*
  or a cci_connection_t*.

  \return CCI_SUCCESS   Value successfully retrieved.
  \return CCI_EINVAL    Handle or val is NULL.
  \return CCI_ERR_NOT_IMPLEMENTED   Not supported by this transport.
  \return Each transport may have additional error codes.

  \ingroup opts
*/
CCI_DECLSPEC int cci_get_opt(cci_opt_handle_t * handle,
			     cci_opt_name_t name, void *val);

/* ================================================================== */
/*                                                                    */
/*                        COMMUNICATIONS                              */
/*                                                                    */
/* ================================================================== */

/*! \defgroup communications Communications */

/*!
  Send a short message.

  A short message limited to the size of cci_connection::max_send_size,
  which may be lower than the cci_device::max_send_size.

  If the application needs to send a message larger than
  cci_connection::max_send_size, the application is responsible for
  segmenting and reassembly or it should use cci_rma().

  When cci_send() returns, the application buffer is reusable. By
  default, CCI will buffer the data internally.

  \param[in] connection	Connection (destination/reliability/ordering).
  \param[in] msg_ptr    Pointer to local segment.
  \param[in] msg_len    Length of local segment (limited to max send size).
  \param[in] context	Cookie to identify the completion through a Send event
			when non-blocking.
  \param[in] flags      Optional flags: CCI_FLAG_BLOCKING,
                        CCI_FLAG_NO_COPY, CCI_FLAG_SILENT.  These flags
                        are explained below.

  \return CCI_SUCCESS   The message has been queued to send.
  \return CCI_EINVAL    Connection is NULL.
  \return Each transport may have additional error codes.

  \todo When someone implements: it would be nice to have a way for an
  MPI implementation to have a progress thread for long messages.
  This progress thread would only "wake up" for the rendezvous
  messages that preceed RMA operations -- short/eager messages go the
  normal processing path (that don't force a wakeup of the progression
  thread).  Patrick proposes two ways: 1. use a distinct connection
  (to a different endpoint) and block on the OS handle from second
  endpoint in the progression thread.  2. define a sleep function that
  returns only when a message with CCI_FLAG_WAKE is received.

  \ingroup communications

  The send will complete differently in reliable and unreliable
  connections:

  - Reliable: only when remote side ACKs complete delivery -- but not
    necessary consumption (i.e., remote completion).
  - Unreliable: when the buffer is re-usable (i.e., local completion).

  When cci_send() returns, the buffer is re-usable by the application.

  \anchor CCI_FLAG_BLOCKING
  If the CCI_FLAG_BLOCKING flag is specified, cci_send() will also
  block until the send completion has occurred.  In this case, there
  is no event returned for this send via cci_get_event(); the send
  completion status is returned via cci_send().

  \anchor CCI_FLAG_NO_COPY
  If the CCI_FLAG_NO_COPY is specified, the application is
  indicating that it does not need the buffer back until the send
  completion occurs (which is most useful when CCI_FLAG_BLOCKING is
  not specified).  The CCI implementation is therefore free to use
  "zero copy" types of transmission with the buffer -- if it wants to.

  \anchor CCI_FLAG_SILENT
  CCI_FLAG_SILENT means that no completion will be generated for
  non-CCI_FLAG_BLOCKING sends.  For reliable ordered connections,
  since completions are issued in order, the completion of any
  non-SILENT send directly implies the completion of any previous
  SILENT sends.  For unordered connections, completion ordering is not
  guaranteed -- it is \b not safe to assume that application protocol
  semantics imply specific unordered SILENT send completions.  The
  only ways to know when unordered SILENT sends have completed (and
  that the local send buffer is "owned" by the application again) is
  to close the connection.

  Note, using both CCI_FLAG_NO_COPY and CCI_FLAG_SILENT is only allowed
  on RO connections.
*/
CCI_DECLSPEC int cci_send(cci_connection_t * connection,
			  const void *msg_ptr, uint32_t msg_len,
			  const void *context, int flags);

#define CCI_FLAG_BLOCKING   (1 << 0)
#define CCI_FLAG_NO_COPY    (1 << 1)
#define CCI_FLAG_SILENT     (1 << 3)
#define CCI_FLAG_READ       (1 << 4)	/* for RMA only */
#define CCI_FLAG_WRITE      (1 << 5)	/* for RMA only */
#define CCI_FLAG_FENCE      (1 << 6)	/* for RMA only */

/*!

  Send a short vectored (gather) message.

  Like cci_send(), cci_sendv() sends a short message bound by
  cci_connection::max_send_size. Instead of a single data buffer,
  cci_sendv() allows the application to gather an array of iovcnt
  buffers pointed to by struct iovec *data.

  \param[in] connection	Connection (destination/reliability).
  \param[in] data	    Array of local data buffers.
  \param[in] iovcnt	    Count of local data array.
  \param[in] context	Cookie to identify the completion through a Send event
				    when non-blocking.
  \param[in] flags      Optional flags: \ref CCI_FLAG_BLOCKING,
                        \ref CCI_FLAG_NO_COPY, \ref CCI_FLAG_SILENT.
                        See cci_send().

  \return CCI_SUCCESS   The message has been queued to send.
  \return CCI_EINVAL    Connection is NULL.
  \return Each transport may have additional error codes.

  \ingroup communications

 */
CCI_DECLSPEC int cci_sendv(cci_connection_t * connection,
			   const struct iovec *data, uint32_t iovcnt,
			   const void *context, int flags);

/* RMA Area operations */

/*!
  Opaque RMA handle for use with cci_rma().

  The RMA handle contains all information that a transport will need to
  initiate a remote RMA operation. The contents should not be inspected
  or modified by the application. The contents are serialized and ready
  for sending to peers.
*/
typedef const struct cci_rma_handle {
	uint64_t stuff[4];
} cci_rma_handle_t;

/*!
  Register memory for RMA operations.

  Prior to accessing memory using RMA, the application must register
  the memory with an endpoint. Memory registered with one endpoint may
  not be accessed via another endpoint, unless also registered with
  that endpoint (i.e. an endpoint serves as a protection domain).

  Registration may take awhile depending on the underlying device and
  should not be in the critical path.

  It is allowable to have overlapping registrations.

  \param[in]  endpoint      Local endpoint to use for RMA.
  \param[in]  start         Pointer to local memory.
  \param[in]  length        Length of local memory.
  \param[in]  flags         Optional flags:
    - CCI_FLAG_READ:        Local memory may be read from other endpoints.
    - CCI_FLAG_WRITE:       Local memory may be written by other endpoints.
  \param[out] rma_handle    Handle for use with cci_rma().

  flags may be 0 if this handle will never be accessed by any other
  endpoint.

  \return CCI_SUCCESS   The memory is ready for RMA.
  \return CCI_EINVAL    endpoint, start, or rma_handle is NULL.
  \return CCI_EINVAL    length is 0.
  \return Each transport may have additional error codes.

  \ingroup communications
*/
CCI_DECLSPEC int cci_rma_register(cci_endpoint_t * endpoint,
				  void *start, uint64_t length,
				  int flags,
				  cci_rma_handle_t ** rma_handle);

/*!
  Deregister memory.

  If an RMA is in progress that uses this handle, the RMA may abort or
  the deregistration may fail.

  Once deregistered, the handle is stale.

  \param[in] endpoint   Local endpoint to use for RMA.
  \param[in] rma_handle Handle for use with cci_rma().

  \return CCI_SUCCESS   The memory is deregistered.
  \return Each transport may have additional error codes.

  \ingroup communications
 */
CCI_DECLSPEC int cci_rma_deregister(cci_endpoint_t * endpoint,
				    cci_rma_handle_t * rma_handle);

/*!
  Perform a RMA operation between local and remote memory on a valid connection.

  Initiate a remote memory WRITE access (move local memory to remote
  memory) or READ (move remote memory to local memory). Adding the FENCE
  flag ensures all previous operations on the same connection are guaranteed
  to complete remotely prior to this operation and all subsequent operations.
  Remote completion does not imply a remote completion event, merely a
  successful RMA operation.

  Optionally, send a remote completion event to the target. If msg_ptr
  and msg_len are provided, send a completion event to the target after
  the RMA has completed. It is guaranteed to arrive after the RMA operation
  has finished.

  In an ordered connection, RMA completion events are ordered according
  to the ordering of the cci_send() or cci_rma() calls in the local peer.
  For instance a cci_rma() with completion message posted between two
  cci_send() will generate a completion event on the target between the
  receive events of these sends.

  CCI makes no guarantees about the data delivery within the RMA operation
  (e.g., no last-byte-written-last).

  A local completion will be generated. If a completion message is provided,
  then a remote completion will be generated as well.

  remote_handle must have been created with protection flags that match
  the flags passed in cci_rma() here. local_handles does not need any
  protection flag since it is only accessed locally here.

  RMA requires a valid connection (i.e. open on both sides). If the remote
  peer has called disconnect(), any attempt to RMA to that peer using the half
  closed connection should fail.

  \param[in] connection     Connection (destination).
  \param[in] msg_ptr         Pointer to data for the remote completion.
  \param[in] msg_len         Length of data for the remote completion.
  \param[in] local_handle   Handle of the local RMA area.
  \param[in] local_offset   Offset in the local RMA area.
  \param[in] remote_handle  Handle of the remote RMA area.
  \param[in] remote_offset  Offset in the remote RMA area.
  \param[in] data_len       Length of data segment.
  \param[in] context        Cookie to identify the completion through a Send event
                            when non-blocking.
  \param[in] flags          Optional flags:
    - CCI_FLAG_BLOCKING:    Blocking call (see cci_send() for details).
    - CCI_FLAG_READ:        Move data from remote to local memory.
    - CCI_FLAG_WRITE:       Move data from local to remote memory
    - CCI_FLAG_FENCE:       All previous operations on the same connection
                            are guaranteed to complete remotely prior to
                            this operation and all subsequent operations.
    - CCI_FLAG_SILENT:      Generates no local completion event (see cci_send()
                            for details).

  \return CCI_SUCCESS   The RMA operation has been initiated.
  \return CCI_EINVAL    connection is NULL.
  \return CCI_EINVAL    connection is unreliable.
  \return CCI_EINVAL    data_len is 0.
  \return CCI_EINVAL    Both READ and WRITE flags are set.
  \return CCI_EINVAL    Neither the READ or WRITE flag is set.
  \return CCI_ERR_DISCONNECTED  The remote peer has closed the connection.
  \return Each transport may have additional error codes.

  \note CCI_FLAG_FENCE only applies to RMA operations for this connection. It does
  not apply to sends on this connection.

  \ingroup communications

  \note READ may not be performance efficient.
*/
CCI_DECLSPEC int cci_rma(cci_connection_t * connection,
			 const void *msg_ptr, uint32_t msg_len,
			 cci_rma_handle_t * local_handle, uint64_t local_offset,
			 cci_rma_handle_t * remote_handle, uint64_t remote_offset,
			 uint64_t data_len, const void *context, int flags);

END_C_DECLS

#endif				/* CCI_H */
