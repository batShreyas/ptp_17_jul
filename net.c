#include "../ptpd.h"

// --- Global PTP Data Structures ---
// These are defined in main.c and used here.
extern ptp_clock_t ptp_clock;

// --- lwIP UDP Protocol Control Blocks (PCBs) ---
static struct udp_pcb *ptp_event_pcb;
static struct udp_pcb *ptp_general_pcb;

// --- PTP Multicast Addresses ---
static const ip_addr_t ptp_primary_multicast   = PTP_PRIMARY_MULTICAST_IP;
static const ip_addr_t ptp_peer_multicast      = PTP_PEER_MULTICAST_IP;

// --- Function Prototypes ---
static void ptp_event_recv_callback(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port);
static void ptp_general_recv_callback(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port);


/**
 * @brief Initializes the PTP network layer.
 *
 * This function creates UDP PCBs for PTP event and general messages,
 * binds them to the correct ports, and joins the PTP multicast groups.
 * It must be called after lwIP has been initialized and a network
 * interface is up.
 *
 * @param net_path A pointer to the NetPath data structure.
 * @return TRUE on success, FALSE on failure.
 */
bool ptpd_net_init(net_path_t *net_path)
{
    err_t err;

    xil_printf("PTPd: Initializing network layer...\r\n");

    // 1. Initialize Event Message PCB (Port 319)
    ptp_event_pcb = udp_new();
    if (ptp_event_pcb == NULL) {
        xil_printf("PTPd: ERROR: Failed to create Event PCB\r\n");
        return FALSE;
    }

    // Bind to the PTP event port
    err = udp_bind(ptp_event_pcb, IP_ADDR_ANY, PTP_EVENT_PORT);
    if (err != ERR_OK) {
        xil_printf("PTPd: ERROR: Failed to bind Event PCB (err: %d)\r\n");
        udp_remove(ptp_event_pcb);
        return FALSE;
    }

    // 2. Initialize General Message PCB (Port 320)
    ptp_general_pcb = udp_new();
    if (ptp_general_pcb == NULL) {
        xil_printf("PTPd: ERROR: Failed to create General PCB\r\n");
        udp_remove(ptp_event_pcb); // Clean up previous PCB
        return FALSE;
    }

    // Bind to the PTP general port
    err = udp_bind(ptp_general_pcb, IP_ADDR_ANY, PTP_GENERAL_PORT);
    if (err != ERR_OK) {
        xil_printf("PTPd: ERROR: Failed to bind General PCB (err: %d)\r\n");
        udp_remove(ptp_event_pcb);
        udp_remove(ptp_general_pcb);
        return FALSE;
    }

    // 3. Join PTP Multicast Groups
    // Use the default network interface for multicast
    struct netif* default_netif = netif_default;

    err = igmp_joingroup(&default_netif->ip_addr, &ptp_primary_multicast);
    if (err != ERR_OK) {
        xil_printf("PTPd: ERROR: Failed to join primary multicast group (err: %d)\r\n");
    }

    err = igmp_joingroup(&default_netif->ip_addr, &ptp_peer_multicast);
    if (err != ERR_OK) {
        xil_printf("PTPd: ERROR: Failed to join peer multicast group (err: %d)\r\n");
    }

    // 4. Register UDP Receive Callbacks
    udp_recv(ptp_event_pcb, ptp_event_recv_callback, &ptp_clock);
    udp_recv(ptp_general_pcb, ptp_general_recv_callback, &ptp_clock);

    xil_printf("PTPd: Network layer initialized successfully.\r\n");
    return TRUE;
}

/**
 * @brief Shuts down the PTP network layer.
 *
 * @param net_path A pointer to the NetPath data structure.
 */
void ptpd_net_shutdown(net_path_t *net_path)
{
    if (ptp_event_pcb) {
        udp_remove(ptp_event_pcb);
    }
    if (ptp_general_pcb) {
        udp_remove(ptp_general_pcb);
    }
}

/**
 * @brief Sends a PTP network packet.
 *
 * This function allocates a pbuf, copies the provided data into it,
 * and sends it over the specified UDP PCB.
 *
 * @param data Pointer to the data to send.
 * @param len Length of the data.
 * @param dst_addr Destination IP address.
 * @param pcb The UDP PCB to send the packet on.
 * @return The number of bytes sent, or a negative value on error.
 */
static int net_send_packet(const void *data, int len, const ip_addr_t *dst_addr, struct udp_pcb *pcb, u16_t port)
{
    err_t err;
    struct pbuf *p;

    // Allocate a pbuf for the packet
    p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
    if (p == NULL) {
        xil_printf("PTPd: ERROR: Failed to allocate pbuf for sending\r\n");
        return -1;
    }

    // Copy the application data into the pbuf
    memcpy(p->payload, data, len);

    // Send the UDP packet
    err = udp_sendto(pcb, p, dst_addr, port);
    pbuf_free(p); // Free the pbuf

    if (err != ERR_OK) {
        xil_printf("PTPd: ERROR: Failed to send UDP packet (err: %d)\r\n");
        return -1;
    }

    return len;
}

/**
 * @brief Send a PTP event message.
 * @param data Pointer to the data to send.
 * @param len Length of the data.
 * @return Number of bytes sent or negative on error.
 */
int net_send_event(const void *data, int len)
{
    return net_send_packet(data, len, &ptp_primary_multicast, ptp_event_pcb, PTP_EVENT_PORT);
}

/**
 * @brief Send a PTP general message.
 * @param data Pointer to the data to send.
 * @param len Length of the data.
 * @return Number of bytes sent or negative on error.
 */
int net_send_general(const void *data, int len)
{
    return net_send_packet(data, len, &ptp_primary_multicast, ptp_general_pcb, PTP_GENERAL_PORT);
}


/**
 * @brief lwIP callback for receiving PTP event messages.
 */
static void ptp_event_recv_callback(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    if (p != NULL) {
        // Pass the received data to the main PTP message handler
        handle_msg(p->payload, p->len);
        pbuf_free(p);
    }
}

/**
 * @brief lwIP callback for receiving PTP general messages.
 */
static void ptp_general_recv_callback(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    if (p != NULL) {
        // Pass the received data to the main PTP message handler
        handle_msg(p->payload, p->len);
        pbuf_free(p);
    }
}
