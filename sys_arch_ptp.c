#include "../ptpd.h"
#include "xtmrctr.h" // AXI Timer driver header

// --- Hardware Timer Instance ---
// This driver instance will be used to access the timer hardware.
static XTmrCtr HwTimer;

// --- Software Clock Adjustment ---
// This variable stores the fine-grained offset calculated by the PTP servo.
// It is applied to the raw hardware time to "slew" the clock without
// causing abrupt jumps.
static int64_t time_offset_ns = 0;

/**
 * @brief Initialize the hardware timer for PTP.
 *
 * This function should be called once at startup from main.c. It initializes
 * the AXI Timer driver and configures the two timers in cascade mode to
 * act as a single 64-bit free-running counter.
 */
void ptpd_hw_timer_init(void)
{
    int status;
    u32_t timer_options;

    xil_printf("PTPd: Initializing hardware timestamping timer...\r\n");

    // Initialize the timer driver instance.
    // ** IMPORTANT: Update TMRCTR_DEVICE_ID to match your hardware design **
    status = XTmrCtr_Initialize(&HwTimer, XPAR_TMRCTR_0_DEVICE_ID);
    if (status != XST_SUCCESS) {
        xil_printf("PTPd: ERROR: Failed to initialize hardware timer!\r\n");
        return;
    }

    // --- Configure Timer 0 (Low 32 bits) ---
    // Disable the timer before changing settings
    XTmrCtr_Stop(&HwTimer, 0);
    // Get current options
    timer_options = XTmrCtr_GetOptions(&HwTimer, 0);
    // Set cascade mode and enable generation output
    timer_options |= XTC_CASCADE_MODE_OPTION | XTC_EXT_GENERATE_OPTION;
    XTmrCtr_SetOptions(&HwTimer, 0, timer_options);

    // --- Configure Timer 1 (High 32 bits) ---
    // Disable the timer before changing settings
    XTmrCtr_Stop(&HwTimer, 1);
    // Get current options
    timer_options = XTmrCtr_GetOptions(&HwTimer, 1);
    // Set cascade mode
    timer_options |= XTC_CASCADE_MODE_OPTION;
    XTmrCtr_SetOptions(&HwTimer, 1, timer_options);

    // Reset the timers to ensure they start from 0
    XTmrCtr_Reset(&HwTimer, 0);
    XTmrCtr_Reset(&HwTimer, 1);

    // Start both timers. They will now run as a single 64-bit counter.
    XTmrCtr_Start(&HwTimer, 0);
    XTmrCtr_Start(&HwTimer, 1);

    xil_printf("PTPd: 64-bit hardware timer started.\r\n");
}


/**
 * @brief Get the current time from the hardware clock.
 *
 * Reads the 64-bit value from the cascaded AXI timers and converts it
 * into the TimeInternal format (seconds and nanoseconds) required by PTPd.
 *
 * @param time A pointer to a TimeInternal structure to be filled.
 */
void getTime(TimeInternal *time)
{
    u32_t high1, high2, low;
    u64_t current_ticks;
    u64_t current_nanoseconds;

    // This loop ensures a consistent read of the 64-bit value from two
    // separate 32-bit registers, avoiding rollover issues.
    do {
        high1 = XTmrCtr_GetValue(&HwTimer, 1); // Read high bits
        low   = XTmrCtr_GetValue(&HwTimer, 0); // Read low bits
        high2 = XTmrCtr_GetValue(&HwTimer, 1); // Read high bits again
    } while (high1 != high2); // If high bits changed, a rollover occurred, so retry

    // Combine into a 64-bit tick value
    current_ticks = ((u64_t)high2 << 32) | low;

    // Convert hardware ticks to nanoseconds.
    // This assumes the AXI Timer is clocked at the processor frequency.
    // ** IMPORTANT: Verify XPAR_CPU_CORE_CLOCK_FREQ_HZ matches your timer's clock **
    current_nanoseconds = (current_ticks * 1000000000ULL) / XPAR_CPU_CORE_CLOCK_FREQ_HZ;

    // Apply the software clock adjustment from the servo
    current_nanoseconds += time_offset_ns;

    // Populate the TimeInternal structure
    time->seconds = current_nanoseconds / 1000000000ULL;
    time->nanoseconds = current_nanoseconds % 1000000000ULL;
}

/**
 * @brief Set the hardware clock time.
 *
 * This function performs a hard reset of the clock to a specific time.
 * It is typically only used once at initialization if required.
 *
 * @param time A pointer to a TimeInternal structure with the new time.
 */
void setTime(const TimeInternal *time)
{
    u64_t new_nanoseconds;
    u64_t new_ticks;

    // Convert the TimeInternal format to a single nanosecond value
    new_nanoseconds = time->seconds * 1000000000ULL + time->nanoseconds;

    // Convert nanoseconds to hardware ticks
    new_ticks = (new_nanoseconds * XPAR_CPU_CORE_CLOCK_FREQ_HZ) / 1000000000ULL;

    // Stop the timers
    XTmrCtr_Stop(&HwTimer, 0);
    XTmrCtr_Stop(&HwTimer, 1);

    // Set the new value in the timer registers
    XTmrCtr_SetResetValue(&HwTimer, 0, (u32_t)(new_ticks & 0xFFFFFFFF));
    XTmrCtr_SetResetValue(&HwTimer, 1, (u32_t)(new_ticks >> 32));

    // Reset and restart the timers
    XTmrCtr_Reset(&HwTimer, 0);
    XTmrCtr_Reset(&HwTimer, 1);
    XTmrCtr_Start(&HwTimer, 0);
    XTmrCtr_Start(&HwTimer, 1);

    // Reset the software offset since we just did a hard set
    time_offset_ns = 0;
}

/**
 * @brief Adjust the clock frequency (slewing).
 *
 * This function is called by the PTP clock servo to make fine-grained
 * adjustments to the local clock. Instead of jumping the time, it modifies
 * a software offset, which effectively "slews" the clock.
 *
 * @param adj The adjustment value in parts per billion (ppb).
 * (Note: The reference ptpd implementation passes nanoseconds here).
 * @return TRUE
 */
bool adjTime(int32_t adj_ns)
{
    // Add the adjustment from the servo to our software offset.
    // This avoids large jumps in time by applying a continuous correction
    // in the getTime() function.
    time_offset_ns += adj_ns;

    return TRUE;
}
