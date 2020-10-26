// slightly modified from libnetfilter_queue example

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>

#include <libmnl/libmnl.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nfnetlink.h>

#include <linux/types.h>
#include <linux/ip.h>
#include <linux/netfilter/nfnetlink_queue.h>

#include <libnetfilter_queue/libnetfilter_queue.h>
#include <libnetfilter_queue/libnetfilter_queue_ipv4.h>
#include <libnetfilter_queue/pktbuff.h>

#include "conntrack.h"

static struct mnl_socket *nl;

static void nfq_send_verdict(int queue_num, uint32_t id, uint32_t mark, struct pkt_buff *pktb)
{
    char buf[MNL_SOCKET_BUFFER_SIZE];
    struct nlmsghdr *nlh;

    nlh = nfq_nlmsg_put(buf, NFQNL_MSG_VERDICT, queue_num);
    nfq_nlmsg_verdict_put(nlh, id, NF_ACCEPT);
    nfq_nlmsg_verdict_put_mark(nlh, mark);
    if (pktb_mangled(pktb)) {
        nfq_nlmsg_verdict_put_pkt(nlh, pktb_data(pktb), pktb_len(pktb));
    }

    if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
        perror("nfq_send_verdict: mnl_socket_sendto");
        exit(EXIT_FAILURE);
    }
}

static int queue_cb(const struct nlmsghdr *nlh, void *fwmark_ptr)
{
    uint8_t *payload;
    struct nfqnl_msg_packet_hdr *ph = NULL;
    struct nlattr *attr[NFQA_MAX+1] = {};
    uint32_t id = 0;
    struct nfgenmsg *nfg;
    in_addr_t new_daddr;
    struct pkt_buff *pktb;
    uint16_t plen;
    uint32_t fwmark = *((uint32_t *) fwmark_ptr);

    if (nfq_nlmsg_parse(nlh, attr) < 0) {
        perror("nfq_nlmsg_parse");
        return MNL_CB_ERROR;
    }

    nfg = mnl_nlmsg_get_payload(nlh);

    if (attr[NFQA_PACKET_HDR] == NULL) {
        fputs("queue_cb: metaheader not set\n", stderr);
        return MNL_CB_ERROR;
    }

    plen = mnl_attr_get_payload_len(attr[NFQA_PAYLOAD]);
    payload = mnl_attr_get_payload(attr[NFQA_PAYLOAD]);

    new_daddr = nfct_add(payload);

    pktb = pktb_alloc(AF_INET, payload, plen, 0);

    if (new_daddr != (in_addr_t) -1) {
        nfq_ip_mangle(pktb, 0, offsetof(struct iphdr, daddr), sizeof(in_addr_t), (char *) &new_daddr, sizeof(in_addr_t));
    }

    ph = mnl_attr_get_payload(attr[NFQA_PACKET_HDR]);
    id = ntohl(ph->packet_id);

    nfq_send_verdict(ntohs(nfg->res_id), id, fwmark, pktb);

    free(pktb);

    return MNL_CB_OK;
}

void nfq_cleanup(void) {
    nfct_cleanup();
    mnl_socket_close(nl);
}

int nfq_loop(unsigned int queue_num, unsigned int fwmark)
{
    char *buf;
    /* largest possible packet payload, plus netlink data overhead: */
    size_t sizeof_buf = 0xffff + (MNL_SOCKET_BUFFER_SIZE/2);
    struct nlmsghdr *nlh;
    int ret;
    unsigned int portid;

    nl = mnl_socket_open(NETLINK_NETFILTER);
    if (nl == NULL) {
        perror("mnl_socket_open");
        exit(EXIT_FAILURE);
    }

    if (mnl_socket_bind(nl, 0, MNL_SOCKET_AUTOPID) < 0) {
        perror("mnl_socket_bind");
        exit(EXIT_FAILURE);
    }
    portid = mnl_socket_get_portid(nl);

    buf = malloc(sizeof_buf);
    if (!buf) {
        perror("nfq_loop: malloc");
        exit(EXIT_FAILURE);
    }

    nlh = nfq_nlmsg_put(buf, NFQNL_MSG_CONFIG, queue_num);
    nfq_nlmsg_cfg_put_cmd(nlh, AF_INET, NFQNL_CFG_CMD_BIND);

    if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
        perror("mnl_socket_sendto");
        exit(EXIT_FAILURE);
    }

    nlh = nfq_nlmsg_put(buf, NFQNL_MSG_CONFIG, queue_num);
    nfq_nlmsg_cfg_put_params(nlh, NFQNL_COPY_PACKET, 0xffff);

    mnl_attr_put_u32(nlh, NFQA_CFG_FLAGS, htonl(NFQA_CFG_F_GSO));
    mnl_attr_put_u32(nlh, NFQA_CFG_MASK, htonl(NFQA_CFG_F_GSO));

    if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
        perror("mnl_socket_sendto");
        exit(EXIT_FAILURE);
    }

    /* ENOBUFS is signalled to userspace when packets were lost
     * on kernel side.  In most cases, userspace isn't interested
     * in this information, so turn it off.
     */
    ret = 1;
    mnl_socket_setsockopt(nl, NETLINK_NO_ENOBUFS, &ret, sizeof(int));

    if (nfct_init() < 0) {
        perror("nfct_init");
        exit(EXIT_FAILURE);
    }

    for (;;) {
        ret = mnl_socket_recvfrom(nl, buf, sizeof_buf);
        if (ret == -1) {
            perror("mnl_socket_recvfrom");
            exit(EXIT_FAILURE);
        }

        ret = mnl_cb_run(buf, ret, 0, portid, queue_cb, &fwmark);
        if (ret < 0) {
            perror("mnl_cb_run");
            exit(EXIT_FAILURE);
        }
    }

    nfq_cleanup();

    return 0;
}
