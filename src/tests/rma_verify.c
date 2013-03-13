/*
 * Copyright (c) 2011-2013 UT-Battelle, LLC.  All rights reserved.
 * Copyright (c) 2011-2013 Oak Ridge National Labs.  All rights reserved.
 *
 * See COPYING in top-level directory
 *
 * Copyright © 2012 Inria.  All rights reserved.
 * $COPYRIGHT$
 *
 */

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/select.h>

#include "cci.h"

#define ITERS		(1)
#define RMA_REG_LEN	(4 * 1024 * 1024)

/* Globals */
int connect_done = 0, done = 0;
int ready = 0;
int is_server = 0;
int is_client = 0;
int count = 0;
int iters = ITERS;
char *name = NULL;
char *server_uri = NULL;
char *buffer = NULL;
uint32_t current_size = 0;
uint64_t local_offset = 0;
uint64_t remote_offset = 0;
uint64_t length = 0;
cci_device_t **devices = NULL;
cci_endpoint_t *endpoint = NULL;
cci_connection_t *connection = NULL;
cci_conn_attribute_t attr = CCI_CONN_ATTR_RU;
cci_rma_handle_t *local_rma_handle = NULL;
cci_rma_handle_t remote_rma_handle;
cci_os_handle_t fd = 0;
int ignore_os_handle = 0;
int blocking = 0;
int nfds = 0;
fd_set rfds;

typedef struct options {
	uint64_t reg_len;
#define RMA_WRITE 1
#define RMA_READ  2
	uint32_t method;
	int flags;
} options_t;

options_t opts;

typedef enum msg_type {
	MSG_CONN_REQ,
	MSG_CONN_REPLY,
	MSG_RMA_CHK,
	MSG_RMA_STATUS
} msg_type_t;

typedef union hdr {
	struct generic_hdr {
		msg_type_t type;
	} generic;

	struct conn_req_hdr {
		msg_type_t type;
		options_t opts;
	} request;

	struct conn_reply_hdr {
		msg_type_t type;
		struct cci_rma_handle handle;
	} reply;

	struct rma_chk_hdr {
		msg_type_t type;
		uint64_t offset;
		uint64_t len;
		uint32_t crc;
		int pad;
	} check;

	struct status_hdr {
		msg_type_t type;
		uint32_t crc;
	} status;
} hdr_t;

hdr_t msg;
uint32_t msg_len = 0;

extern uint32_t
crc32(uint32_t crc, const void *buf, size_t size);

static void print_usage(void)
{
	fprintf(stderr, "usage: %s -h <server_uri> [-s] [-i <iters>] "
		"[-c <type>] [-B|-I] [-o <local_offset>] [-O <remote_offset>"
		"[[-w | -r] [-R <reg_len>] [-l <max_len>]]\n", name);
	fprintf(stderr, "where:\n");
	fprintf(stderr, "\t-h\tServer's URI\n");
	fprintf(stderr, "\t-s\tSet to run as the server\n");
	fprintf(stderr, "\t-i\tRun this number of iterations\n");
	fprintf(stderr, "\t-c\tConnection type (RU or RO) set by client only\n");
	fprintf(stderr, "\t-w\tUse RMA WRITE (default)\n");
	fprintf(stderr, "\t-r\tUse RMA READ instead of RMA WRITE\n");
	fprintf(stderr, "\t-l\tTest RMA up to length\n");
	fprintf(stderr, "\t-R\tRegister RMA length (default max_len))\n");
	fprintf(stderr, "\t-o\tRMA local offset (default 0)\n");
	fprintf(stderr, "\t-O\tRMA remote offset (default 0)\n");
	fprintf(stderr, "\t-B\tBlock using the OS handle instead of polling\n");
	fprintf(stderr, "\t-I\tGet OS handle but ignore it\n\n");
	fprintf(stderr, "Example:\n");
	fprintf(stderr, "server$ %s -h sock://foo -p 2211 -s\n", name);
	fprintf(stderr, "client$ %s -h sock://foo -p 2211\n", name);
	exit(EXIT_FAILURE);
}

static void check_return(cci_endpoint_t * endpoint, char *func, int ret, int need_exit)
{
	if (ret) {
		fprintf(stderr, "%s() returned %s\n", func, cci_strerror(endpoint, ret));
		if (need_exit)
			exit(EXIT_FAILURE);
	}
	return;
}

static void poll_events(void)
{
	int ret;
	cci_event_t *event;

	if (blocking) {
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);

		ret = select(nfds, &rfds, NULL, NULL, NULL);
		if (!ret)
			return;
	}

	ret = cci_get_event(endpoint, &event);
	if (ret == CCI_SUCCESS) {
		assert(event);
		switch (event->type) {
		case CCI_EVENT_SEND:
			if (event->send.status != CCI_SUCCESS) {
				fprintf(stderr, "RMA failed with %s.\n",
					cci_strerror(endpoint, event->send.status));
				cci_disconnect(connection);
				connection = NULL;
				done = 1;
			}
			if (is_server)
				break;
			/* Client */
			if (event->send.context == (void *)0xdeadbeef) {
				done = 1;
				break;
			}
			/* RMA completed */
			count++;
			if (count < iters) {
				ret = cci_rma(connection,
						&msg,
						msg_len,
						local_rma_handle,
						local_offset,
						&remote_rma_handle,
						remote_offset,
						current_size,
						NULL,
						opts.flags);
				check_return(endpoint, "cci_rma", ret, 1);
			}
			break;
		case CCI_EVENT_RECV:
			if (is_client) {
				hdr_t *h = (void*)event->recv.ptr;

				if (!ready) {
					ready = 1;
					memcpy((void*)&remote_rma_handle,
						&h->reply.handle,
						sizeof(remote_rma_handle));
				} else {
					/* RMA status msg */
					if (h->status.crc != msg.check.crc)
						fprintf(stderr,
							"Server reported CRC failed.\n"
							"Local CRC %u != remote CRC %u.\n"
							"count=%d current_size=%u\n",
							msg.check.crc, h->status.crc,
							count, current_size);
				}
			} else {
				hdr_t *h = (void*)event->recv.ptr;

				/* is_server */
				if (event->recv.len == 3) {
					done = 1;
				} else {
					uint32_t crc = 0;

					/* RMA check request */
					crc = crc32(0,
						(void*)((uintptr_t)buffer + h->check.offset),
						h->check.len);
					msg.status.type = MSG_RMA_STATUS;
					msg.status.crc = crc;
				}
			}
			break;
		case CCI_EVENT_CONNECT:
			connect_done = 1;
			connection = event->connect.connection;
			break;
		default:
			fprintf(stderr, "ignoring event type %d\n",
				event->type);
		}
		cci_return_event(event);
	}
	return;
}

static void do_client(void)
{
	int ret, rlen = 0, i = 0;
	uint32_t min = 1;
	hdr_t msg;
	long *r = NULL;

	msg.request.type = MSG_CONN_REQ;
	msg.request.opts = opts;

	/* initiate connect */
	ret =
	    cci_connect(endpoint, server_uri, &msg, sizeof(msg.request), attr, NULL,
			0, NULL);
	check_return(endpoint, "cci_connect", ret, 1);

	/* poll for connect completion */
	while (!connect_done)
		poll_events();

	if (!connection) {
		fprintf(stderr, "no connection\n");
		return;
	}

	while (!ready)
		poll_events();

	ret = posix_memalign((void **)&buffer, 4096, opts.reg_len);
	check_return(endpoint, "memalign buffer", ret, 1);

	rlen = sizeof(*r);
	for (i = 0; (((i + 1) * rlen)) < (int) opts.reg_len; i++) {
		r = (void*)((uintptr_t)buffer + (i * rlen));
		*r = random();
	}

	/* for the client, we do not need remote access flags */

	ret = cci_rma_register(endpoint, buffer, opts.reg_len, 0, &local_rma_handle);
	check_return(endpoint, "cci_rma_register", ret, 1);

	if (opts.method == RMA_WRITE)
		opts.flags = CCI_FLAG_WRITE;
	else
		opts.flags = CCI_FLAG_READ;

	/* begin communication with server */
	for (current_size = min; current_size <= length;) {
		msg.check.type = MSG_RMA_CHK;
		msg.check.offset = remote_offset;
		msg.check.len = current_size;
		msg.check.crc = crc32(0,
				(void*)((uintptr_t)buffer + local_offset),
				current_size);
		msg_len = sizeof(msg.check);

		fprintf(stderr, "Testing length %9u ... ", current_size);

		ret = cci_rma(connection, &msg, msg_len,
			      local_rma_handle, local_offset,
			      &remote_rma_handle, remote_offset,
			      current_size, NULL, opts.flags);
		check_return(endpoint, "cci_rma", ret, 1);

		while (count < iters)
			poll_events();

		if (connection)
			fprintf(stderr, "success.\n");
		else
			goto out;

		count = 0;
		current_size *= 2;

		if (current_size >= 64 * 1024) {
			if (iters >= 32)
				iters /= 2;
		}
	}

	ret = cci_send(connection, "bye", 3, (void *)0xdeadbeef, 0);
	check_return(endpoint, "cci_send", ret, 0);

	while (!done)
		poll_events();
out:
	ret = cci_rma_deregister(endpoint, local_rma_handle);
	check_return(endpoint, "cci_rma_deregister", ret, 1);

	printf("client done\n");
	sleep(1);

	return;
}

static void do_server(void)
{
	int ret = 0;
	hdr_t *h = NULL;

	while (!ready) {
		cci_event_t *event;

		if (blocking) {
			FD_ZERO(&rfds);
			FD_SET(fd, &rfds);

			ret = select(nfds, &rfds, NULL, NULL, NULL);
			if (!ret)
				return;
		}

		ret = cci_get_event(endpoint, &event);
		if (ret == CCI_SUCCESS) {
			switch (event->type) {
			case CCI_EVENT_CONNECT_REQUEST:
				h = (void*)event->request.data_ptr;
				opts = h->request.opts;
				ret = cci_accept(event, NULL);
				check_return(endpoint, "cci_accept", ret, 1);
				break;
			case CCI_EVENT_ACCEPT:
				{
					int len, rlen, i;
					hdr_t msg;
					long *r = NULL;

					ready = 1;
					connection = event->accept.connection;

					len = opts.reg_len;

					ret =
					    posix_memalign((void **)&buffer,
							   4096, len);
					check_return(endpoint, "memalign buffer", ret, 1);

					rlen = sizeof(*r);
					for (i = 0; ((i + 1) * rlen) < (int) opts.reg_len; i++) {
						r = (void*)((uintptr_t)buffer + (i * rlen));
						*r = random();
					}

					ret = cci_rma_register(endpoint,
							     buffer,
							     opts.reg_len,
							     opts.method == RMA_WRITE ? CCI_FLAG_WRITE : CCI_FLAG_READ,
							     &local_rma_handle);
					check_return(endpoint, "cci_rma_register",
							     ret, 1);

					msg.reply.type = MSG_CONN_REPLY;
					msg.reply.handle = *local_rma_handle;

					ret = cci_send(connection, &msg,
						     sizeof(msg.reply), NULL, 0);
					check_return(endpoint, "cci_send", ret, 1);
					break;
				}
			default:
				fprintf(stderr,
					"%s: ignoring unexpected event %d\n",
					__func__, event->type);
				break;
			}
			ret = cci_return_event(event);
			if (ret)
				fprintf(stderr, "cci_return_event() failed with %s\n",
						cci_strerror(endpoint, ret));
		}
	}

	while (!done)
		poll_events();

	ret = cci_rma_deregister(endpoint, local_rma_handle);
	check_return(endpoint, "cci_rma_deregister", ret, 1);

	printf("server done\n");
	sleep(1);

	return;
}

int main(int argc, char *argv[])
{
	int ret, c;
	uint32_t caps = 0;
	cci_os_handle_t *os_handle = NULL;
	char *uri = NULL;
	pid_t pid = 0;

	pid = getpid();
	srandom(pid);

	name = argv[0];

	while ((c = getopt(argc, argv, "h:si:c:wrl:o:O:R:BI")) != -1) {
		switch (c) {
		case 'h':
			server_uri = strdup(optarg);
			is_client = 1;
			break;
		case 's':
			is_server = 1;
			break;
		case 'i':
			iters = strtoul(optarg, NULL, 0);
			break;
		case 'c':
			if (strncasecmp("ru", optarg, 2) == 0)
				attr = CCI_CONN_ATTR_RU;
			else if (strncasecmp("ro", optarg, 2) == 0)
				attr = CCI_CONN_ATTR_RO;
			else
				print_usage();
			printf("Using %s connection\n",
			       attr == CCI_CONN_ATTR_RU ? "RU" : "RO");
			break;
		case 'w':
			opts.method = RMA_WRITE;
			break;
		case 'r':
			opts.method = RMA_READ;
			break;
		case 'l':
			length = strtoul(optarg, NULL, 0);
			break;
		case 'R':
			opts.reg_len = strtoul(optarg, NULL, 0);
			break;
		case 'o':
			local_offset = strtoul(optarg, NULL, 0);
			break;
		case 'O':
			remote_offset = strtoul(optarg, NULL, 0);
			break;
		case 'B':
			blocking = 1;
			os_handle = &fd;
			break;
		case 'I':
			ignore_os_handle = 1;
			os_handle = &fd;
			break;
		default:
			print_usage();
		}
	}

	if (!is_server && !server_uri) {
		fprintf(stderr, "Must select -h or -s\n");
		print_usage();
	}

	if (is_server && is_client) {
		fprintf(stderr, "Must select -h or -s, not both\n");
		print_usage();
	}

	if (blocking && ignore_os_handle) {
		fprintf(stderr, "-B and -I are not compatible.\n");
		fprintf(stderr, "-B will block using select() using the OS handle.\n");
		fprintf(stderr, "-I will obtain the OS handle, but not use it to wait.\n");
		print_usage();
	}

	if (!opts.reg_len) {
		if (!length) {
			opts.reg_len = RMA_REG_LEN;
		} else {
			opts.reg_len = length;
		}
	}

	if (!length) {
		if (!opts.reg_len)
			length = RMA_REG_LEN;
		else
			length = opts.reg_len;
	}

	if (opts.reg_len == length) {
		if (local_offset || remote_offset) {
			fprintf(stderr, "*** RMA registration length == RMA length "
					"and an offset was requested. ***\n"
					"*** This should cause an error. ***\n");
		}
	}

	if (is_client)
		fprintf(stderr, "Testing with local_offset %"PRIu64" "
				"remote_offset %"PRIu64" "
				"reg_len %"PRIu64" length %"PRIu64"\n",
				local_offset, remote_offset, opts.reg_len, length);

	ret = cci_init(CCI_ABI_VERSION, 0, &caps);
	if (ret) {
		fprintf(stderr, "cci_init() failed with %s\n",
			cci_strerror(NULL, ret));
		exit(EXIT_FAILURE);
	}

	/* create an endpoint */
	ret = cci_create_endpoint(NULL, 0, &endpoint, os_handle);
	if (ret) {
		fprintf(stderr, "cci_create_endpoint() failed with %s\n",
			cci_strerror(NULL, ret));
		exit(EXIT_FAILURE);
	}

	ret = cci_get_opt(endpoint,
			  CCI_OPT_ENDPT_URI, &uri);
	if (ret) {
		fprintf(stderr, "cci_get_opt() failed with %s\n", cci_strerror(NULL, ret));
		exit(EXIT_FAILURE);
	}
	printf("Opened %s\n", uri);

	if (blocking) {
		nfds = fd + 1;
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
	}

	if (is_server)
		do_server();
	else
		do_client();

	/* clean up */
	ret = cci_destroy_endpoint(endpoint);
	if (ret) {
		fprintf(stderr, "cci_destroy_endpoint() failed with %s\n",
			cci_strerror(NULL, ret));
		exit(EXIT_FAILURE);
	}
	free(buffer);
	free(uri);
	free(server_uri);

	ret = cci_finalize();
	if (ret) {
		fprintf(stderr, "cci_finalize() failed with %s\n",
			cci_strerror(NULL, ret));
		exit(EXIT_FAILURE);
	}

	return 0;
}
