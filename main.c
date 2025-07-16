#include <stdio.h>
#include "xparameters.h"
#include "netif/xadapter.h"
#include "platform.h"
#include "platform_config.h"
#include "xil_printf.h"
#include "sleep.h"
#include "xil_cache.h"

// lwIP Includes
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/inet.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/priv/tcp_priv.h"
#include "lwipopts.h"

// PTP Includes
#include "ptp/ptpd.h"

// Hardware Driver Includes for Timer and Interrupts
#include "xintc.h"      // AXI Interrupt Controller Header
#include "xtmrctr.h"    // AXI Timer Header

// --- Constant Definitions ---
#define DEFAULT_IP_ADDRESS  "192.168.1.10"
#define DEFAULT_IP_MASK     "255.255.255.0"
#define DEFAULT_GW_ADDRESS  "192.168.1.1"

// ** IMPORTANT: Update these IDs to match your Vivado Block Design **
#define INTC_DEVICE_ID      XPAR_INTC_0_DEVICE_ID
#define TMRCTR_DEVICE_ID    XPAR_TMRCTR_0_DEVICE_ID
#define TIMER_IRPT_INTR     XPAR_INTC_0_TMRCTR_0_VEC_ID

// PTP periodic tick rate (10 Hz = 100ms)
#define PTP_TICK_RATE_HZ    10
#define TIMER_RESET_VALUE   (XPAR_AXI_TIMER_0_CLOCK_FREQ_HZ / PTP_TICK_RATE_HZ)

// --- Global Variables ---
extern volatile int TcpFastTmrFlag;
extern volatile int TcpSlowTmrFlag;

struct netif server_netif;
static XIntc interrupt_controller;
static XTmrCtr timer_controller;

// Flag set by the timer ISR to trigger PTP processing
volatile int ptp_timer_flag = 0;

// PTP Globals
ptp_clock_t ptp_clock;
ptpd_opts ptp_opts;
foreign_master_record_t foreign_records[PTPD_DEFAULT_MAX_FOREIGN_RECORDS];
sys_mbox_t ptp_alert_queue;

// --- Function Prototypes ---
static int setup_interrupt_system();
static void Timer_ISR_Handler(void *CallBackRef, u8 TmrCtrNumber);

// --- IP Address Helper Functions ---
static void print_ip(char *msg, ip_addr_t *ip)
{
    xil_printf("%s: %d.%d.%d.%d\r\n", msg,
               ip4_addr1(ip), ip4_addr2(ip), ip4_addr3(ip), ip4_addr4(ip));
}

static void print_ip_settings(ip_addr_t *ip, ip_addr_t *mask, ip_addr_t *gw)
{
    print_ip("Board IP", ip);
    print_ip("Netmask", mask);
    print_ip("Gateway", gw);
}

static void assign_default_ip(ip_addr_t *ip, ip_addr_t *mask, ip_addr_t *gw)
{
    inet_aton(DEFAULT_IP_ADDRESS, ip);
    inet_aton(DEFAULT_IP_MASK, mask);
    inet_aton(DEFAULT_GW_ADDRESS, gw);
}

// --- PTP Initialization ---
void ptpd_opts_init()
{
    xil_printf("Initializing ptpd options...\r\n");

    memset(&ptp_opts, 0, sizeof(ptpd_opts));
    ptp_opts.slave_only = 0;
    ptp_opts.sync_interval = 1;
    ptp_opts.announce_interval = 1;
    ptp_opts.clock_quality.clock_class = 248;
    ptp_opts.clock_quality.clock_accuracy = 0xFE;
    ptp_opts.clock_quality.offset_scaled_log_variance = 0xFFFF;
    ptp_opts.priority1 = 128;
    ptp_opts.priority2 = 128;

    if (ptp_startup(&ptp_clock, &ptp_opts, foreign_records) != 0) {
        xil_printf("PTP startup failed!\r\n");
    }
}

// --- Main Application ---
int main()
{
    struct netif *netif = &server_netif;

    unsigned char mac_ethernet_address[] = {
        0x00, 0x0a, 0x35, 0x00, 0x01, 0x02 };

    init_platform();

    xil_printf("\r\n----- PTP + lwIP UDP Server (Bare-Metal) -----\r\n");

    lwip_init();

    if (!xemac_add(netif, NULL, NULL, NULL, mac_ethernet_address,
                   PLATFORM_EMAC_BASEADDR)) {
        xil_printf("Error adding network interface\r\n");
        return -1;
    }
    netif_set_default(netif);

    // This enables interrupts globally, including for the timer
    platform_enable_interrupts();
    setup_interrupt_system(); // Setup timer interrupt

    netif_set_up(netif);

    // Static IP configuration (DHCP disabled)
    assign_default_ip(&(netif->ip_addr), &(netif->netmask), &(netif->gw));
    print_ip_settings(&(netif->ip_addr), &(netif->netmask), &(netif->gw));

    // Create ptp_alert_queue
    ptp_alert_queue = sys_mbox_new();

    // Setup ptpd and register UDP handlers
    ptpd_opts_init();
    ptpd_net_init(&ptp_clock.net_path);

    xil_printf("PTP initialized. Starting main loop...\r\n");

    while (1) {
        // Handle lwIP's own timers (if TCP is used)
        if (TcpFastTmrFlag) {
            tcp_fasttmr();
            TcpFastTmrFlag = 0;
        }
        if (TcpSlowTmrFlag) {
            tcp_slowtmr();
            TcpSlowTmrFlag = 0;
        }

        // Poll for incoming network packets
        xemacif_input(netif);

        // Check if the periodic timer has fired
        if (ptp_timer_flag) {
            ptp_timer_flag = 0; // Reset the flag
            ptpd_periodic_handler(); // Run the PTP state machine
        }
    }

    cleanup_platform();
    return 0;
}

// --- Timer and Interrupt Setup Functions ---

void Timer_ISR_Handler(void *CallBackRef, u8 TmrCtrNumber)
{
    // Set the flag for the main loop to process
    ptp_timer_flag = 1;
}

static int setup_interrupt_system()
{
    int status;

    // Initialize the interrupt controller driver
    status = XIntc_Initialize(&interrupt_controller, INTC_DEVICE_ID);
    if (status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    // Initialize the timer driver
    status = XTmrCtr_Initialize(&timer_controller, TMRCTR_DEVICE_ID);
    if (status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    // Connect the timer ISR to the interrupt controller
    status = XIntc_Connect(&interrupt_controller, TIMER_IRPT_INTR,
                           (XInterruptHandler)XTmrCtr_InterruptHandler,
                           &timer_controller);
    if (status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    // Start the interrupt controller
    status = XIntc_Start(&interrupt_controller, XIN_REAL_MODE);
    if (status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    // Enable the timer interrupt in the interrupt controller
    XIntc_Enable(&interrupt_controller, TIMER_IRPT_INTR);

    // Set the timer handler that will be called from the driver's ISR
    XTmrCtr_SetHandler(&timer_controller, Timer_ISR_Handler, NULL);

    // Configure the timer for auto-reload (periodic) mode
    XTmrCtr_SetOptions(&timer_controller, 0, XTC_INT_MODE_OPTION | XTC_AUTO_RELOAD_OPTION);

    // Set the timer reset value for a 10 Hz tick rate
    XTmrCtr_SetResetValue(&timer_controller, 0, TIMER_RESET_VALUE);

    // Start the timer
    XTmrCtr_Start(&timer_controller, 0);

    xil_printf("Periodic timer for PTP started successfully.\r\n");

    return XST_SUCCESS;
}