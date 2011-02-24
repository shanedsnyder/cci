/*
 * Copyright (c) 2010-2011 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2010-2011 UT-Battelle, LLC.  All rights reserved.
 * $COPYRIGHT$
 */

#include "cci/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "cci.h"
#include "plugins/core/core.h"
#include "core_sock.h"

sock_globals_t *sglobals = NULL;

/*
 * Local functions
 */
static int sock_init(uint32_t abi_ver, uint32_t flags, uint32_t *caps);
static const char *sock_strerror(enum cci_status status);
static int sock_get_devices(cci_device_t const ***devices);
static int sock_free_devices(cci_device_t const **devices);
static int sock_create_endpoint(cci_device_t *device, 
                                    int flags, 
                                    cci_endpoint_t **endpoint, 
                                    cci_os_handle_t *fd);
static int sock_destroy_endpoint(cci_endpoint_t *endpoint);
static int sock_bind(cci_device_t *device, int backlog, uint32_t *port, 
                         cci_service_t **service, cci_os_handle_t *fd);
static int sock_unbind(cci_service_t *service, cci_device_t *device);
static int sock_get_conn_req(cci_service_t *service, 
                                 cci_conn_req_t **conn_req);
static int sock_accept(cci_conn_req_t *conn_req, 
                           cci_endpoint_t *endpoint, 
                           cci_connection_t **connection);
static int sock_reject(cci_conn_req_t *conn_req);
static int sock_connect(cci_endpoint_t *endpoint, char *server_uri, 
                            uint32_t port,
                            void *data_ptr, uint32_t data_len, 
                            cci_conn_attribute_t attribute,
                            void *context, int flags, 
                            struct timeval *timeout);
static int sock_disconnect(cci_connection_t *connection);
static int sock_set_opt(cci_opt_handle_t *handle, 
                            cci_opt_level_t level, 
                            cci_opt_name_t name, const void* val, int len);
static int sock_get_opt(cci_opt_handle_t *handle, 
                            cci_opt_level_t level, 
                            cci_opt_name_t name, void** val, int *len);
static int sock_arm_os_handle(cci_endpoint_t *endpoint, int flags);
static int sock_get_event(cci_endpoint_t *endpoint, 
                              cci_event_t ** const event,
                              uint32_t flags);
static int sock_return_event(cci_endpoint_t *endpoint, 
                                 cci_event_t *event);
static int sock_send(cci_connection_t *connection, 
                         void *header_ptr, uint32_t header_len, 
                         void *data_ptr, uint32_t data_len, 
                         void *context, int flags);
static int sock_sendv(cci_connection_t *connection, 
                          void *header_ptr, uint32_t header_len, 
                          char **data_ptrs, int *data_lens,
                          uint8_t segment_cnt, void *context, int flags);
static int sock_rma_register(cci_endpoint_t *endpoint, void *start, 
                                 uint64_t length, uint64_t *rma_handle);
static int sock_rma_register_phys(cci_endpoint_t *endpoint, 
                                      cci_sg_t *sg_list, uint32_t sg_cnt, 
                                      uint64_t *rma_handle);
static int sock_rma_deregister(uint64_t rma_handle);
static int sock_rma(cci_connection_t *connection, 
                        void *header_ptr, uint32_t header_len, 
                        uint64_t local_handle, uint64_t local_offset, 
                        uint64_t remote_handle, uint64_t remote_offset,
                        uint64_t data_len, void *context, int flags);


static void sock_progress_sends(sock_dev_t *sdev);

/*
 * Public plugin structure.
 *
 * The name of this structure must be of the following form:
 *
 *    cci_core_<your_plugin_name>_plugin
 *
 * This allows the symbol to be found after the plugin is dynamically
 * opened.
 *
 * Note that your_plugin_name should match the direct name where the
 * plugin resides.
 */
cci_plugin_core_t cci_core_sock_plugin = {
    {
        /* Logistics */
        CCI_ABI_VERSION,
        CCI_CORE_API_VERSION,
        "sock",
        CCI_MAJOR_VERSION, CCI_MINOR_VERSION, CCI_RELEASE_VERSION,
        5,
        
        /* Bootstrap function pointers */
        cci_core_sock_post_load,
        cci_core_sock_pre_unload,
    },

    /* API function pointers */
    sock_init,
    sock_strerror,
    sock_get_devices,
    sock_free_devices,
    sock_create_endpoint,
    sock_destroy_endpoint,
    sock_bind,
    sock_unbind,
    sock_get_conn_req,
    sock_accept,
    sock_reject,
    sock_connect,
    sock_disconnect,
    sock_set_opt,
    sock_get_opt,
    sock_arm_os_handle,
    sock_get_event,
    sock_return_event,
    sock_send,
    sock_sendv,
    sock_rma_register,
    sock_rma_register_phys,
    sock_rma_deregister,
    sock_rma
};


static int sock_init(uint32_t abi_ver, uint32_t flags, uint32_t *caps)
{
    cci__dev_t *dev;

    fprintf(stderr, "In sock_init\n");

    /* init sock globals */
    sglobals = calloc(1, sizeof(*sglobals));
    if (!sglobals)
        return CCI_ENOMEM;

    /* FIXME magic number */
    sglobals->devices = calloc(32, sizeof(*sglobals->devices));

    /* find devices that we own */

    TAILQ_FOREACH(dev, &globals->devs, entry) {
        if (0 == strcmp("sock", dev->driver)) {
            int i;
            const char *arg;
            cci_device_t *device;
            sock_dev_t *sdev;

            device = &dev->device;
            device->max_send_size = SOCK_AM_SIZE;

            /* TODO determine link rate
             *
             * linux->driver->get ethtool settings->speed
             * bsd/darwin->ioctl(SIOCGIFMEDIA)->ifm_active
             * windows ?
             */
            device->rate = 10000000000;

            device->pci.domain = -1;    /* per CCI spec */
            device->pci.bus = -1;       /* per CCI spec */
            device->pci.dev = -1;       /* per CCI spec */
            device->pci.func = -1;      /* per CCI spec */

            dev->priv = calloc(1, sizeof(*dev->priv));
            if (!dev->priv)
                return CCI_ENOMEM;

            sdev = dev->priv;
            TAILQ_INIT(&sdev->queued);
            TAILQ_INIT(&sdev->pending);
            pthread_mutex_init(&sdev->lock, NULL);

            /* parse conf_argv */
            for (i = 0, arg = device->conf_argv[i];
                 arg != NULL; 
                 i++, arg = device->conf_argv[i]) {
                if (0 == strncmp("ip=", arg, 3)) {
                    const char *ip = &arg[3];

                    sdev->ip= inet_addr(ip); /* network order */
                }
            }
            if (sdev->ip!= 0) {
                sglobals->devices[sglobals->count] = device;
                sglobals->count++;
                dev->is_up = 1;
            }

            /* TODO determine if IP is available and up */
        }
    }

    sglobals->devices = realloc(sglobals->devices,
                                (sglobals->count + 1) * sizeof(cci_device_t *));

    return CCI_SUCCESS;
}


static const char *sock_strerror(enum cci_status status)
{
    printf("In sock_sterrror\n");
    return NULL;
}


static int sock_get_devices(cci_device_t const ***devices)
{
    printf("In sock_get_devices\n");

    if (!sglobals)
        return CCI_ENODEV;

    *devices = sglobals->devices;

    return CCI_SUCCESS;
}


static int sock_free_devices(cci_device_t const **devices)
{
    printf("In sock_free_devices\n");

    if (!sglobals)
        return CCI_ENODEV;

    /* tear everything down */

    /* for each device
     *     for each endpoint
     *         for each connection
     *             close conn
     *         for each tx/rx
     *             free it
     *         close socket
     *     for each listening endpoint
     *         remove from service
     *         for each conn_req
     *             free it
     *         close socket
     */

    return CCI_ERR_NOT_IMPLEMENTED;
}


static int sock_create_endpoint(cci_device_t *device, 
                                    int flags, 
                                    cci_endpoint_t **endpoint, 
                                    cci_os_handle_t *fd)
{
    int i, ret;
    cci__dev_t *dev = NULL;
    cci__ep_t *ep = NULL;
    sock_ep_t *sep = NULL;

    printf("In sock_create_endpoint\n");

    if (!sglobals)
        return CCI_ENODEV;

    dev = container_of(device, cci__dev_t, device);
    if (0 != strcmp("sock", dev->driver)) {
        ret = CCI_EINVAL;
        goto out;
    }

    ep = container_of(*endpoint, cci__ep_t, endpoint);
    ep->priv = calloc(1, sizeof(*sep));
    if (!ep->priv) {
        ret = CCI_ENOMEM;
        goto out;
    }

    (*endpoint)->max_recv_buffer_count = SOCK_EP_RX_CNT;
    ep->max_hdr_size = SOCK_EP_MAX_HDR_SIZE;
    ep->rx_buf_cnt = SOCK_EP_RX_CNT;
    ep->tx_buf_cnt = SOCK_EP_TX_CNT;
    ep->buffer_len = SOCK_EP_BUF_LEN;
    ep->tx_timeout = SOCK_EP_TX_TIMEOUT;

    sep = ep->priv;
    sep->ids = calloc(SOCK_NUM_BLOCKS, sizeof(*sep->ids));
    if (!sep->ids) {
        ret = CCI_ENOMEM;
        goto out;
    }

    sep->sock = socket(PF_INET, SOCK_DGRAM, 0);
    if (sep->sock == -1) {
        ret = errno;
        goto out;
    }

    for (i = 0; i < SOCK_EP_HASH_SIZE; i++)
        TAILQ_INIT(&sep->conn_hash[i]);

    TAILQ_INIT(&sep->txs);
    TAILQ_INIT(&sep->idle_txs);
    TAILQ_INIT(&sep->rxs);
    TAILQ_INIT(&sep->idle_rxs);
    pthread_mutex_init(&sep->lock, NULL);

    return CCI_SUCCESS;

out:
    pthread_mutex_lock(&dev->lock);
    TAILQ_REMOVE(&dev->eps, ep, entry);
    pthread_mutex_unlock(&dev->lock);
    if (sep) {
        if (sep->ids)
            free(sep->ids);
        free(sep);
    }
    if (ep)
        free(ep);
    *endpoint = NULL;
    return ret;
}


static int sock_destroy_endpoint(cci_endpoint_t *endpoint)
{
    printf("In sock_destroy_endpoint\n");

    if (!sglobals)
        return CCI_ENODEV;

    return CCI_ERR_NOT_IMPLEMENTED;
}

/*! sock_bind()
 *
 * device, port, service are always set
 */
static int sock_bind(cci_device_t *device, int backlog, uint32_t *port, 
                         cci_service_t **service, cci_os_handle_t *fd)
{
    int ret;
    cci__dev_t *dev;
    cci__svc_t *svc;
    cci__lep_t *lep;
    sock_dev_t *sdev;
    sock_lep_t *slep;
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);

    printf("In sock_bind\n");

    if (!sglobals)
        return CCI_ENODEV;

    dev = container_of(device, cci__dev_t, device);
    if (0 != strcmp("sock", dev->driver)) {
        ret = CCI_EINVAL;
        goto out;
    }

    if (*port > (64 * 1024))
        return CCI_ERANGE;

    svc = container_of(*service, cci__svc_t, service);
    TAILQ_FOREACH(lep, &svc->leps, sentry) {
        if (lep->dev == dev) {
            break;
        }
    }

    /* allocate sock listening endpoint */
    slep = calloc(1, sizeof(*slep));
    if (!slep)
        return CCI_ENOMEM;

    /* open socket */
    slep->sock = socket(PF_INET, SOCK_DGRAM, 0);
    if (slep->sock == -1) {
        ret = errno;
        goto out;
    }

    /* bind socket to device and port */
    sdev = dev->priv;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((uint16_t) *port);
    sin.sin_addr.s_addr = sdev->ip;

    ret = bind(slep->sock, (const struct sockaddr *) &sin, len);
    if (ret) {
        ret = errno;
        goto out;
    }

    /* create OS handle */
    /* TODO */

    lep->priv = slep;

    return CCI_SUCCESS;

out:
    if (slep)
        free(slep);
    return ret;
}


static int sock_unbind(cci_service_t *service, cci_device_t *device)
{
    printf("In sock_unbind\n");

    if (!sglobals)
        return CCI_ENODEV;

    return CCI_ERR_NOT_IMPLEMENTED;
}


static int sock_get_conn_req(cci_service_t *service, 
                                 cci_conn_req_t **conn_req)
{
    printf("In sock_get_conn_req\n");

    if (!sglobals)
        return CCI_ENODEV;

    return CCI_ERR_NOT_IMPLEMENTED;
}


static int sock_accept(cci_conn_req_t *conn_req, 
                           cci_endpoint_t *endpoint, 
                           cci_connection_t **connection)
{
    printf("In sock_accept\n");

    if (!sglobals)
        return CCI_ENODEV;

    return CCI_ERR_NOT_IMPLEMENTED;
}


static int sock_reject(cci_conn_req_t *conn_req)
{
    printf("In sock_reject\n");

    if (!sglobals)
        return CCI_ENODEV;

    return CCI_ERR_NOT_IMPLEMENTED;
}

static int sock_getaddrinfo(const char *uri, in_addr_t *in)
{
    int ret;
    char *hostname, *colon;
    struct addrinfo *ai, hints;

    if (0 == strncmp("ip://", uri, 5))
        hostname = strdup(&uri[5]);
    else
        return CCI_EINVAL;

    colon = strchr(hostname, ':');
    if (colon)
        colon = '\0';

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    ret = getaddrinfo(hostname, NULL, &hints, &ai);
    free(hostname);

    if (ret) {
        freeaddrinfo(ai);
        return ret;
    }

    *in = ((struct sockaddr_in *)ai->ai_addr)->sin_addr.s_addr;
    freeaddrinfo(ai);

    return CCI_SUCCESS;
}

static void
sock_get_id(sock_ep_t *ep, uint32_t *id)
{
    uint32_t n, block, offset;
    uint64_t *b;

    while (1) {
        n = random();
        block = n / SOCK_BLOCK_SIZE;
        offset = n % SOCK_BLOCK_SIZE;
        b = &ep->ids[block];

        if ((*b & (1 << offset)) == 0) {
            *b |= (1 << offset);
            *id = (block * SOCK_BLOCK_SIZE) + offset;
            break;
        }
    }
    return;
}

static void
sock_put_id(sock_ep_t *ep, uint32_t id)
{
    uint32_t block, offset;
    uint64_t *b;

    block = id / SOCK_BLOCK_SIZE;
    offset = id % SOCK_BLOCK_SIZE;
    b = &ep->ids[block];

    assert((*b & (1 << offset)) == 1);
    *b &= ~(1 << offset);

    return;
}

static int sock_connect(cci_endpoint_t *endpoint, char *server_uri, 
                            uint32_t port,
                            void *data_ptr, uint32_t data_len, 
                            cci_conn_attribute_t attribute,
                            void *context, int flags, 
                            struct timeval *timeout)
{
    int ret;
    cci__ep_t *ep;
    cci__dev_t *dev;
    cci__conn_t *conn;
    sock_ep_t *sep;
    sock_dev_t *sdev;
    sock_conn_t *sconn;
    sock_tx_t *tx;
    sock_header_t *hdr;
    cci__evt_t *evt;
    cci_event_t *event;
    cci_event_other_t *other;
    cci_connection_t *connection;
    struct sockaddr_in *sin;
    void *ptr;
    in_addr_t in;
    sock_seq_ack_t *sa;
    uint64_t ack;


    printf("In sock_connect\n");

    if (!sglobals)
        return CCI_ENODEV;

    /* allocate a new connection */
    conn = calloc(1, sizeof(*conn));
    if (!conn)
            return CCI_ENOMEM;

    conn->priv = calloc(1, sizeof(*sconn));
    if (!conn->priv) {
        ret = CCI_ENOMEM;
        goto out;
    }

    /* set up the connection */
    conn->uri = strdup(server_uri);
    if (!conn->uri) {
        ret = CCI_ENOMEM;
        goto out;
    }

    /* conn->tx_timeout = 0  by default */

    connection = &conn->connection;
    connection->attribute = attribute;
    connection->endpoint = endpoint;
    connection->max_send_size = SOCK_AM_SIZE;

    /* set up sock specific info */

    sconn->status = SOCK_CONN_ACTIVE;
    sin = (struct sockaddr_in *) &sconn->sin;
    memset(sin, 0, sizeof(*sin));
    sin->sin_family = AF_INET;
    sin->sin_port = htons(port);

    ret = sock_getaddrinfo(server_uri, &in);
    if (ret)
        goto out;
    sin->sin_addr.s_addr = in;  /* already in network order */

    /* peer will assign id */

    pthread_mutex_init(&sconn->lock, NULL);

    /* get our endpoint and device */
    ep = container_of(endpoint, cci__ep_t, endpoint);
    sep = ep->priv;
    dev = ep->dev;
    sdev = dev->priv;

    /* get a tx */
    pthread_mutex_lock(&sep->lock);
    if (!TAILQ_EMPTY(&sep->idle_txs)) {
        tx = TAILQ_FIRST(&sep->idle_txs);
        TAILQ_REMOVE(&sep->idle_txs, tx, dentry);
    }
    pthread_mutex_unlock(&sep->lock);

    if (!tx)
        return CCI_ENOBUFS;

    /* prep the tx */
    tx->msg_type = SOCK_MSG_CONN_REQUEST;

    evt = &tx->evt;
    event = &evt->event;
    event->type = CCI_EVENT_CONNECT_SUCCESS; /* for now */

    other = &event->info.other;
    other->context = context;
    other->u.connect.connection = connection;

    /* pack the msg */

    hdr = (sock_header_t *) tx->buffer;
    sock_get_id(sep, &sconn->id);
    /* FIXME silence -Wall -Werror until it is used */
    if (0) sock_put_id(sep, 0);
    sock_pack_conn_request(hdr, attribute, (uint16_t) data_len, sconn->id);
    ptr = hdr++;
    tx->len = sizeof(*hdr);

    /* add seq and ack */

    tx->seq = 0;
    tx->seq = random() << 16; /* fill bits 16-47 */
    tx->seq |= random() & 0xFFFFULL; /* fill bits 0-15 */
    ack = sconn->ack;

    sa = (sock_seq_ack_t *) ptr;
    sock_pack_seq_ack(sa, tx->seq, ack);
    ptr = sa++;
    tx->len += sizeof(*sa);

    /* zero even if unreliable */

    tx->cycles = 0;
    tx->resends = 0;

    if (data_len)
            memcpy(ptr, data_ptr, data_len);

    tx->len += data_len;
    assert(tx->len <= ep->buffer_len);

    /* insert at tail of device's queued list */

    dev = ep->dev;
    sdev = dev->priv;

    tx->state = SOCK_TX_QUEUED;
    pthread_mutex_lock(&sdev->lock);
    TAILQ_INSERT_TAIL(&sdev->queued, tx, dentry);
    pthread_mutex_unlock(&sdev->lock);

    /* try to progress txs */

    sock_progress_sends(sdev);

    return CCI_SUCCESS;

out:
    if (conn) {
        if (conn->uri)
            free((char *) conn->uri);
        if (conn->priv)
            free(conn->priv);
        free(conn);
    }
    return ret;
}


static int sock_disconnect(cci_connection_t *connection)
{
    printf("In sock_disconnect\n");

    if (!sglobals)
        return CCI_ENODEV;

    return CCI_ERR_NOT_IMPLEMENTED;
}


static int sock_set_opt(cci_opt_handle_t *handle, 
                            cci_opt_level_t level, 
                            cci_opt_name_t name, const void* val, int len)
{
    printf("In sock_set_opt\n");

    if (!sglobals)
        return CCI_ENODEV;

    return CCI_ERR_NOT_IMPLEMENTED;
}


static int sock_get_opt(cci_opt_handle_t *handle, 
                            cci_opt_level_t level, 
                            cci_opt_name_t name, void** val, int *len)
{
    printf("In sock_get_opt\n");

    if (!sglobals)
        return CCI_ENODEV;

    return CCI_ERR_NOT_IMPLEMENTED;
}


static int sock_arm_os_handle(cci_endpoint_t *endpoint, int flags)
{
    printf("In sock_arm_os_handle\n");

    if (!sglobals)
        return CCI_ENODEV;

    return CCI_ERR_NOT_IMPLEMENTED;
}


static int sock_get_event(cci_endpoint_t *endpoint, 
                              cci_event_t ** const event,
                              uint32_t flags)
{
    printf("In sock_get_event\n");

    if (!sglobals)
        return CCI_ENODEV;

    return CCI_ERR_NOT_IMPLEMENTED;
}


static int sock_return_event(cci_endpoint_t *endpoint, 
                                 cci_event_t *event)
{
    printf("In sock_return_event\n");

    if (!sglobals)
        return CCI_ENODEV;

    return CCI_ERR_NOT_IMPLEMENTED;
}


static int sock_sendto(cci_os_handle_t sock, void *buf, int len,
                       const struct sockaddr_in sin)
{
    int ret = 0;
    int left = len;
    const struct sockaddr *s = (const struct sockaddr *)&sin;
    socklen_t slen = sizeof(sin);

    while (left) {
        int offset = len - left;
        ret = sendto(sock, buf + offset, left, 0, s, slen);
        if (ret == -1) {
            if (errno == EINTR)
                continue;
            else {
                ret = errno;
            goto out;
            }
        }
        offset += ret;
        left -= ret;
    }
out:
        return ret;
}

static void
sock_progress_pending(sock_dev_t *sdev)
{
    int ret, left;
    void *ptr;
    sock_tx_t           *tx, *tmp;
    cci__evt_t          *evt;
    cci_event_t         *event;         /* generic CCI event */
    cci_event_send_t    *send;          /* generic CCI send event */
    cci_connection_t    *connection;    /* generic CCI connection */
    cci__conn_t         *conn;
    sock_conn_t         *sconn;
    cci__ep_t           *ep;
    sock_ep_t           *sep;

    /* This is only for reliable messages.
     * Do not dequeue txs, just walk the list.
     */

    pthread_mutex_lock(&sdev->lock);
    TAILQ_FOREACH_SAFE(tx, &sdev->pending, dentry, tmp) {

        evt = &(tx->evt);
        event = &evt->event;
        connection = event->info.send.connection;
        conn = container_of(connection, cci__conn_t, connection);
        sconn = conn->priv;

        ep = container_of(connection->endpoint, cci__ep_t, endpoint);
        sep = ep->priv;

        /* try to send it */

        tx->cycles++;

        /* TODO cycles % cycles_per_resend == 0 */
        tx->resends++;

        if (tx->resends >= ep->tx_timeout ||
            tx->resends >= conn->tx_timeout) {

            /* dequeue */

            TAILQ_REMOVE(&sdev->pending, tx, dentry);

            /* set status and add to completed events */

            send = &(event->info.send);

            send->status = CCI_ETIMEDOUT;
            pthread_mutex_lock(&ep->lock);
            TAILQ_INSERT_TAIL(&ep->evts, evt, entry);
            pthread_mutex_unlock(&ep->lock);

            continue;
        }


        left = tx->len;
        ptr = tx->buffer;

        ret = sock_sendto(sep->sock, ptr, left, sconn->sin);
        if (ret == -1) {
            switch (errno) {
            case EINTR:
                break;
            default:
                fprintf(stderr, "sendto() failed with %s\n", strerror(errno));
                /* fall through */
            case EAGAIN:
            case ENOMEM:
            case ENOBUFS:
                /* give up for now */
                pthread_mutex_unlock(&sdev->lock);
                return;
            }
            assert(ret == left);
        }
        /* msg sent */
    }
    pthread_mutex_unlock(&sdev->lock);

    return;
}

static void
sock_progress_queued(sock_dev_t *sdev)
{
    int                 ret, left, timeout;
    void                *ptr;
    sock_tx_t           *tx, *tmp;
    cci__ep_t           *ep;
    cci__evt_t          *evt;
    cci__conn_t         *conn;
    sock_ep_t           *sep;
    sock_conn_t         *sconn;
    cci_event_t         *event;         /* generic CCI event */
    cci_connection_t    *connection;    /* generic CCI connection */
    cci_endpoint_t      *endpoint;      /* generic CCI endpoint */

    pthread_mutex_lock(&sdev->lock);
    TAILQ_FOREACH_SAFE(tx, &sdev->queued, dentry, tmp) {

        /* FIXME */
        //TAILQ_REMOVE(&sdev->queued, tx, dentry);
        //pthread_mutex_unlock(&sdev->lock);

        evt = &(tx->evt);
        event = &(evt->event);
        if (event->type == CCI_EVENT_SEND)
            connection = event->info.send.connection;
        else
            /* FIXME is this true of CONN_REPLY and CONN_ACK? */
            connection = event->info.other.u.connect.connection;
        conn = container_of(connection, cci__conn_t, connection);
        sconn = conn->priv;

        endpoint = connection->endpoint;
        ep = container_of(endpoint, cci__ep_t, endpoint);
        sep = ep->priv;

        /* try to send it */

        tx->cycles++;

        /* cycles % cycles_per_resend == 0 */
        if (tx->cycles % SOCK_PROG_FREQ != 0)
            continue;

        tx->resends++;

        timeout = conn->tx_timeout ? conn->tx_timeout : ep->tx_timeout;

        /* FIXME still holding sdev->lock */
        if (tx->resends * SOCK_RESEND_TIME >= timeout) {

            /* set status and add to completed events */

            switch (tx->msg_type) {
            case SOCK_MSG_SEND:
                event->info.send.status = CCI_ETIMEDOUT;
                break;
            case SOCK_MSG_CONN_REQUEST:
            case SOCK_MSG_CONN_REPLY:
            case SOCK_MSG_CONN_ACK:
                event->type = CCI_EVENT_CONNECT_TIMEOUT;
                break;
            default:
                /* TODO */
                return;
            }
            /* if SILENT, put idle tx */
            if (tx->msg_type == SOCK_MSG_SEND &&
                tx->flags & CCI_FLAG_SILENT) {

                tx->state = SOCK_TX_IDLE;
                pthread_mutex_lock(&sep->lock);
                TAILQ_INSERT_HEAD(&sep->idle_txs, tx, dentry);
                pthread_mutex_unlock(&sep->lock);
            } else {
                tx->state = SOCK_TX_COMPLETED;
                pthread_mutex_lock(&ep->lock);
                TAILQ_INSERT_TAIL(&ep->evts, evt, entry);
                pthread_mutex_unlock(&ep->lock);
            }
        }

        left = tx->len;
        ptr = tx->buffer;
again:
        ret = sock_sendto(sep->sock, ptr, left, sconn->sin);
        if (ret == -1) {
            switch (errno) {
            case EINTR:
                break;
            default:
                fprintf(stderr, "sendto() failed with %s\n", strerror(errno));
                /* fall through */
            case EAGAIN:
            case ENOMEM:
            case ENOBUFS:
                /* requeue */
                pthread_mutex_lock(&sdev->lock);
                TAILQ_INSERT_HEAD(&sdev->queued, tx, dentry);
                pthread_mutex_unlock(&sdev->lock);
                return;
            }
        } else {
            /* (partial?) success */

            left -= ret;
            ptr += ret;
            if (left) {
                    goto again;
            }
        }
        /* msg sent */

        /* if reliable, add to pending
         * else add to completed events */

        if (connection->attribute & CCI_CONN_ATTR_RO ||
            connection->attribute & CCI_CONN_ATTR_RU) {
            tx->state = SOCK_TX_PENDING;
            pthread_mutex_lock(&sdev->lock);
            TAILQ_INSERT_TAIL(&sdev->pending, tx, dentry);
            pthread_mutex_unlock(&sdev->lock);
        } else {
            /* TODO handle SILENT flag */
            tx->state = SOCK_TX_COMPLETED;
            pthread_mutex_lock(&ep->lock);
            TAILQ_INSERT_TAIL(&ep->evts, evt, entry);
            pthread_mutex_unlock(&ep->lock);
        }
    } while (!TAILQ_EMPTY(&sdev->queued));

    return;
}

static void
sock_progress_sends(sock_dev_t *sdev)
{
    sock_progress_pending(sdev);
    sock_progress_queued(sdev);

    return;
}

static int sock_send(cci_connection_t *connection, 
                         void *header_ptr, uint32_t header_len, 
                         void *data_ptr, uint32_t data_len, 
                         void *context, int flags)
{
    uint8_t segment_cnt = 0;

    if (data_ptr && data_len)
        segment_cnt = 1;

    return sock_sendv(connection, header_ptr, header_len,
                      (char **) &data_ptr, (int *) &data_len,
                      segment_cnt, context, flags);
}


static int sock_sendv(cci_connection_t *connection, 
                          void *header_ptr, uint32_t header_len, 
                          char **data_ptrs, int *data_lens,
                          uint8_t segment_cnt, void *context, int flags)
{
    int i, ret, is_reliable = 0, data_len = 0;
    cci_endpoint_t *endpoint = connection->endpoint;
    cci__ep_t *ep;
    cci__dev_t *dev;
    cci__conn_t *conn;
    sock_ep_t *sep;
    sock_conn_t *sconn;
    sock_dev_t *sdev;
    sock_tx_t *tx;
    sock_header_t *hdr;
    void *ptr;
    cci__evt_t *evt;
    cci_event_t *event;     /* generic CCI event */
    cci_event_send_t *send; /* generic CCI send event */

    if (segment_cnt < 2)
        printf("In sock_send\n");
    else
        printf("In sock_sendv\n");

    if (!sglobals)
        return CCI_ENODEV;

    for (i = 0; i < segment_cnt; i++) {
        if (!data_ptrs[i] && data_lens[i])
            return CCI_EINVAL;
        data_len += data_lens[i];
    }

    if (header_len + data_len > connection->max_send_size)
        return CCI_EMSGSIZE;

    ep = container_of(endpoint, cci__ep_t, endpoint);
    sep = ep->priv;
    conn = container_of(connection, cci__conn_t, connection);
    sconn = conn->priv;
    dev = ep->dev;
    sdev = dev->priv;

    is_reliable = connection->attribute & CCI_CONN_ATTR_RO ||
                  connection->attribute & CCI_CONN_ATTR_RU;

    /* if unreliable, try to send */
    if (!is_reliable) {
        int len;
        char *buffer;

        len = sizeof(sock_header_t) + header_len + data_len;
        buffer = calloc(len, sizeof(char));
        if (!buffer)
            return CCI_ENOMEM;

        /* pack buffer */
        hdr = (sock_header_t *) tx->buffer;
        sock_pack_send(hdr, header_len, data_len, sconn->peer_id);
        ptr = hdr++;
        tx->len = len;

        if (header_len) {
            memcpy(ptr, header_ptr, header_len);
            ptr += header_len;
        }
        for (i = 0; i < segment_cnt; i++) {
            if (data_lens[i]) {
                memcpy(ptr, data_ptrs[i], data_lens[i]);
                ptr += data_lens[i];
            }
        }

        /* try to send */
        ret = sock_sendto(sep->sock, buffer, len, sconn->sin);
        free(buffer);
        if (ret == 0)
            return CCI_SUCCESS;

        /* if error, fall through */
    }

    /* get a tx */
    pthread_mutex_lock(&sep->lock);
    if (!TAILQ_EMPTY(&sep->idle_txs)) {
        tx = TAILQ_FIRST(&sep->idle_txs);
        TAILQ_REMOVE(&sep->idle_txs, tx, dentry);
    }
    pthread_mutex_unlock(&sep->lock);

    if (!tx)
        return CCI_ENOBUFS;

    /* tx bookkeeping */
    tx->msg_type = SOCK_MSG_SEND;
    tx->flags = flags;

    /* setup generic CCI event */
    evt = &tx->evt;
    event = &evt->event;
    event->type = CCI_EVENT_SEND;

    send = &(event->info.send);
    send->connection = connection;
    send->context = context;
    send->status = CCI_SUCCESS; /* for now */

    /* pack send header */

    hdr = (sock_header_t *) tx->buffer;
    sock_pack_send(hdr, header_len, data_len, sconn->peer_id);
    ptr = hdr++;
    tx->len = sizeof(*hdr);

    /* if reliable, add seq and ack */

    if (is_reliable) {
        sock_seq_ack_t *sa;
        uint64_t ack;

        pthread_mutex_lock(&sconn->lock);
        tx->seq = sconn->seq++;
        ack = sconn->ack;
        pthread_mutex_unlock(&sconn->lock);

        sa = (sock_seq_ack_t *) ptr;
        sock_pack_seq_ack(sa, tx->seq, ack);
        ptr = sa++;
        tx->len += sizeof(*sa);
    }

    /* zero even if unreliable */

    tx->cycles = 0;
    tx->resends = 0;

    /* copy user header and data to buffer
     * NOTE: ignore CCI_FLAG_NO_COPY because we need to
             send the entire packet in one shot. We could
             use sendmsg() with an iovec. */

    if (header_len) {
        memcpy(ptr, header_ptr, header_len);
        ptr += header_len;
    }
    for (i = 0; i < segment_cnt; i++) {
        if (data_lens[i]) {
            memcpy(ptr, data_ptrs[i], data_lens[i]);
            ptr += data_lens[i];
        }
    }

    tx->len += header_len + data_len;
    assert(tx->len <= ep->buffer_len);

    /* insert at tail of sock device's queued list */

    tx->state = SOCK_TX_QUEUED;
    pthread_mutex_lock(&sdev->lock);
    TAILQ_INSERT_TAIL(&sdev->queued, tx, dentry);
    pthread_mutex_unlock(&sdev->lock);

    /* try to progress txs */

    sock_progress_sends(sdev);

    /* if unreliable, we are done */
    if (!is_reliable)
        return CCI_SUCCESS;

    /* if blocking, wait for completion */

    if (tx->flags & CCI_FLAG_BLOCKING) {
        int ret;

        while (tx->state != SOCK_TX_COMPLETED)
            usleep(SOCK_PROG_TIME / 2);

        /* get status and cleanup */
        ret = send->status;

        /* FIXME race with get_event()
         *       get_event() must ignore sends with 
         *       flags & CCI_FLAG_BLOCKING */

        pthread_mutex_lock(&ep->lock);
        TAILQ_REMOVE(&ep->evts, evt, entry);
        pthread_mutex_unlock(&ep->lock);

        pthread_mutex_lock(&sep->lock);
        TAILQ_INSERT_HEAD(&sep->idle_txs, tx, dentry);
        pthread_mutex_unlock(&sep->lock);

        return ret;
    }

    return CCI_SUCCESS;
}


static int sock_rma_register(cci_endpoint_t *endpoint, void *start, 
                                 uint64_t length, uint64_t *rma_handle)
{
    printf("In sock_rma_register\n");

    if (!sglobals)
        return CCI_ENODEV;

    return CCI_ERR_NOT_IMPLEMENTED;
}


static int sock_rma_register_phys(cci_endpoint_t *endpoint, 
                                      cci_sg_t *sg_list, uint32_t sg_cnt, 
                                      uint64_t *rma_handle)
{
    printf("In sock_rma_register_phys\n");

    if (!sglobals)
        return CCI_ENODEV;

    return CCI_ERR_NOT_IMPLEMENTED;
}


static int sock_rma_deregister(uint64_t rma_handle)
{
    printf("In sock_rma_deregister\n");

    if (!sglobals)
        return CCI_ENODEV;

    return CCI_ERR_NOT_IMPLEMENTED;
}


static int sock_rma(cci_connection_t *connection, 
                        void *header_ptr, uint32_t header_len, 
                        uint64_t local_handle, uint64_t local_offset, 
                        uint64_t remote_handle, uint64_t remote_offset,
                        uint64_t data_len, void *context, int flags)
{
    printf("In sock_rma\n");

    if (!sglobals)
        return CCI_ENODEV;

    return CCI_ERR_NOT_IMPLEMENTED;
}
