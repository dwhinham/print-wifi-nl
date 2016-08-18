// vim:ts=4:sw=4:expandtab
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <errno.h>
#include <ifaddrs.h>
#include <linux/if_ether.h>
#include <linux/nl80211.h>
#include <net/if.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/genl.h>
#include <netlink/netlink.h>

#define IW_ESSID_MAX_SIZE 32

typedef struct {
    int flags;
    char essid[IW_ESSID_MAX_SIZE + 1];
    uint8_t bssid[ETH_ALEN];
    int quality;
    int signal_level;
    int noise_level;
    int bitrate;
    double frequency;
} wireless_info_t;

/*
 * Return the IP address for the given interface or "no IP" if the
 * interface is up and running but hasn't got an IP address yet
 *
 */
const char *get_ip_addr(const char *interface) {
    static char part[512];
    socklen_t len = sizeof(struct sockaddr_in);
    memset(part, 0, sizeof(part));

    struct ifaddrs *ifaddr, *addrp;
    bool found = false;

    getifaddrs(&ifaddr);

    if (ifaddr == NULL)
        return NULL;

    /* Skip until we are at the AF_INET address of interface */
    for (addrp = ifaddr;

         (addrp != NULL &&
          (strcmp(addrp->ifa_name, interface) != 0 ||
           addrp->ifa_addr == NULL ||
           addrp->ifa_addr->sa_family != AF_INET));

         addrp = addrp->ifa_next) {
        /* Check if the interface is down */
        if (strcmp(addrp->ifa_name, interface) != 0)
            continue;
        found = true;
        if ((addrp->ifa_flags & IFF_RUNNING) == 0) {
            freeifaddrs(ifaddr);
            return NULL;
        }
    }

    if (addrp == NULL) {
        freeifaddrs(ifaddr);
        return (found ? "no IP" : NULL);
    }

    int ret;
    if ((ret = getnameinfo(addrp->ifa_addr, len, part, sizeof(part), NULL, 0, NI_NUMERICHOST)) != 0) {
        fprintf(stderr, "i3status: getnameinfo(): %s\n", gai_strerror(ret));
        freeifaddrs(ifaddr);
        return "no IP";
    }

    freeifaddrs(ifaddr);
    return part;
}

// Like iw_print_bitrate, but without the dependency on libiw.
static void print_bitrate(char *buffer, int buflen, int bitrate) {
    const int kilo = 1e3;
    const int mega = 1e6;
    const int giga = 1e9;

    const double rate = bitrate;
    char scale;
    int divisor;

    if (rate >= giga) {
        scale = 'G';
        divisor = giga;
    } else if (rate >= mega) {
        scale = 'M';
        divisor = mega;
    } else {
        scale = 'k';
        divisor = kilo;
    }
    snprintf(buffer, buflen, "%g %cb/s", rate / divisor, scale);
}

// Based on NetworkManager/src/platform/wifi/wifi-utils-nl80211.c
static uint32_t nl80211_xbm_to_percent(int32_t xbm, int32_t divisor) {
#define NOISE_FLOOR_DBM -90
#define SIGNAL_MAX_DBM -20

    xbm /= divisor;
    if (xbm < NOISE_FLOOR_DBM)
        xbm = NOISE_FLOOR_DBM;
    if (xbm > SIGNAL_MAX_DBM)
        xbm = SIGNAL_MAX_DBM;

    return 100 - 70 * (((float)SIGNAL_MAX_DBM - (float)xbm) / ((float)SIGNAL_MAX_DBM - (float)NOISE_FLOOR_DBM));
}

// Based on NetworkManager/src/platform/wifi/wifi-utils-nl80211.c
static void find_ssid(uint8_t *ies, uint32_t ies_len, uint8_t **ssid, uint32_t *ssid_len) {
#define WLAN_EID_SSID 0
    *ssid = NULL;
    *ssid_len = 0;

    while (ies_len > 2 && ies[0] != WLAN_EID_SSID) {
        ies_len -= ies[1] + 2;
        ies += ies[1] + 2;
    }
    if (ies_len < 2)
        return;
    if (ies_len < (uint32_t)(2 + ies[1]))
        return;

    *ssid_len = ies[1];
    *ssid = ies + 2;
}

static int gwi_sta_cb(struct nl_msg *msg, void *data) {
    wireless_info_t *info = data;

    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
    struct nlattr *sinfo[NL80211_STA_INFO_MAX + 1];
    struct nlattr *rinfo[NL80211_RATE_INFO_MAX + 1];
    static struct nla_policy stats_policy[NL80211_STA_INFO_MAX + 1] = {
            [NL80211_STA_INFO_RX_BITRATE] = {.type = NLA_NESTED},
    };

    static struct nla_policy rate_policy[NL80211_RATE_INFO_MAX + 1] = {
            [NL80211_RATE_INFO_BITRATE] = {.type = NLA_U16},
    };

    if (nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0), genlmsg_attrlen(gnlh, 0), NULL) < 0)
        return NL_SKIP;

    if (tb[NL80211_ATTR_STA_INFO] == NULL)
        return NL_SKIP;

    if (nla_parse_nested(sinfo, NL80211_STA_INFO_MAX, tb[NL80211_ATTR_STA_INFO], stats_policy))
        return NL_SKIP;

    if (sinfo[NL80211_STA_INFO_RX_BITRATE] == NULL)
        return NL_SKIP;

    if (nla_parse_nested(rinfo, NL80211_RATE_INFO_MAX, sinfo[NL80211_STA_INFO_RX_BITRATE], rate_policy))
        return NL_SKIP;

    if (rinfo[NL80211_RATE_INFO_BITRATE] == NULL)
        return NL_SKIP;

    // NL80211_RATE_INFO_BITRATE is specified in units of 100 kbit/s, but iw
    // used to specify bit/s, so we convert to use the same code path.
    info->bitrate = (int)nla_get_u16(rinfo[NL80211_RATE_INFO_BITRATE]) * 100 * 1000;

    return NL_SKIP;
}

static int gwi_scan_cb(struct nl_msg *msg, void *data) {
    wireless_info_t *info = data;
    struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    struct nlattr *bss[NL80211_BSS_MAX + 1];
    struct nla_policy bss_policy[NL80211_BSS_MAX + 1] = {
            [NL80211_BSS_FREQUENCY] = {.type = NLA_U32},
            [NL80211_BSS_BSSID] = {.type = NLA_UNSPEC},
            [NL80211_BSS_INFORMATION_ELEMENTS] = {.type = NLA_UNSPEC},
            [NL80211_BSS_SIGNAL_MBM] = {.type = NLA_U32},
            [NL80211_BSS_SIGNAL_UNSPEC] = {.type = NLA_U8},
            [NL80211_BSS_STATUS] = {.type = NLA_U32},
    };

    if (nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0), genlmsg_attrlen(gnlh, 0), NULL) < 0)
        return NL_SKIP;

    if (tb[NL80211_ATTR_BSS] == NULL)
        return NL_SKIP;

    if (nla_parse_nested(bss, NL80211_BSS_MAX, tb[NL80211_ATTR_BSS], bss_policy))
        return NL_SKIP;

    if (bss[NL80211_BSS_STATUS] == NULL)
        return NL_SKIP;

    const uint32_t status = nla_get_u32(bss[NL80211_BSS_STATUS]);

    if (status != NL80211_BSS_STATUS_ASSOCIATED &&
        status != NL80211_BSS_STATUS_IBSS_JOINED)
        return NL_SKIP;

    if (bss[NL80211_BSS_BSSID] == NULL)
        return NL_SKIP;

    memcpy(info->bssid, nla_data(bss[NL80211_BSS_BSSID]), ETH_ALEN);

    if (bss[NL80211_BSS_FREQUENCY]) {
        info->frequency = (double)nla_get_u32(bss[NL80211_BSS_FREQUENCY]) * 1e6;
    }

    if (bss[NL80211_BSS_SIGNAL_UNSPEC]) {
        info->signal_level = nla_get_u8(bss[NL80211_BSS_SIGNAL_UNSPEC]);
        info->quality = info->signal_level;
    }

    if (bss[NL80211_BSS_SIGNAL_MBM]) {
        info->signal_level = (int)nla_get_u32(bss[NL80211_BSS_SIGNAL_MBM]) / 100;
        info->quality = nl80211_xbm_to_percent(nla_get_u32(bss[NL80211_BSS_SIGNAL_MBM]), 100);
    }

    if (bss[NL80211_BSS_INFORMATION_ELEMENTS]) {
        uint8_t *ssid;
        uint32_t ssid_len;

        find_ssid(nla_data(bss[NL80211_BSS_INFORMATION_ELEMENTS]),
                  nla_len(bss[NL80211_BSS_INFORMATION_ELEMENTS]),
                  &ssid, &ssid_len);
        if (ssid && ssid_len) {
            snprintf(info->essid, sizeof(info->essid), "%.*s", ssid_len, ssid);
        }
    }

    return NL_SKIP;
}

static int get_wireless_info(const char *interface, wireless_info_t *info) {
    memset(info, 0, sizeof(wireless_info_t));

    struct nl_sock *sk = nl_socket_alloc();
    if (genl_connect(sk) != 0)
        goto error1;

    if (nl_socket_modify_cb(sk, NL_CB_VALID, NL_CB_CUSTOM, gwi_scan_cb, info) < 0)
        goto error1;

    const int nl80211_id = genl_ctrl_resolve(sk, "nl80211");
    if (nl80211_id < 0)
        goto error1;

    const unsigned int ifidx = if_nametoindex(interface);
    if (ifidx == 0)
        goto error1;

    struct nl_msg *msg = NULL;
    if ((msg = nlmsg_alloc()) == NULL)
        goto error1;

    if (!genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, nl80211_id, 0, NLM_F_DUMP, NL80211_CMD_GET_SCAN, 0) ||
        nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifidx) < 0)
        goto error2;

    if (nl_send_sync(sk, msg) < 0)
        // nl_send_sync calls nlmsg_free()
        goto error1;
    msg = NULL;

    if (nl_socket_modify_cb(sk, NL_CB_VALID, NL_CB_CUSTOM, gwi_sta_cb, info) < 0)
        goto error1;

    if ((msg = nlmsg_alloc()) == NULL)
        goto error1;

    if (!genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, nl80211_id, 0, NLM_F_DUMP, NL80211_CMD_GET_STATION, 0) || nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifidx) < 0 || nla_put(msg, NL80211_ATTR_MAC, 6, info->bssid) < 0)
        goto error2;

    if (nl_send_sync(sk, msg) < 0)
        // nl_send_sync calls nlmsg_free()
        goto error1;
    msg = NULL;

    nl_socket_free(sk);
    return 1;

error2:
    nlmsg_free(msg);
error1:
    nl_socket_free(sk);
    return 0;
}

int main(int argc, char** argv) {
    const char *interface;
    const char *ip_addr;
    wireless_info_t wireless_info;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <interface>\n", argv[0]);
        return EXIT_FAILURE;
    }

    interface = argv[1];

    ip_addr = get_ip_addr(interface);
    if (ip_addr == NULL)
        return EXIT_FAILURE;

    if (!get_wireless_info(interface, &wireless_info))
        return EXIT_FAILURE;

    printf("%s\t%s\t%d%\n", ip_addr, wireless_info.essid, wireless_info.quality);
    return EXIT_SUCCESS;
}
