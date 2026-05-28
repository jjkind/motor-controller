#include "stm32f4xx.h"

// -----------------------------------------------------------------------------
// Nucleo F446RE pin definitions
// LED LD2  = PA5
// Button B1 = PC13 (active low)
// USART2 TX = PA2, RX = PA3 (AF7)
// Clock: 16 MHz HSI
//
// TIM1 complementary PWM pin mapping:
//   Phase A high: PA8  = TIM1_CH1,  AF1
//   Phase A low:  PA7  = TIM1_CH1N, AF1
//
//   Phase B high: PA9  = TIM1_CH2,  AF1
//   Phase B low:  PB0  = TIM1_CH2N, AF1
//
//   Phase C high: PA10 = TIM1_CH3,  AF1
//   Phase C low:  PB1  = TIM1_CH3N, AF1
//
// Current sense ADC pin mapping:
//   Current A: PA4 = ADC1_IN4
//   Current B: PC0 = ADC1_IN10
//   Current C: PC1 = ADC1_IN11
//
// Encoder pin mapping:
//   Encoder A: PA0 = TIM2_CH1
//   Encoder B: PA1 = TIM2_CH2
//
// TIM1 setup:
//   Timer clock:    16 MHz
//   PWM mode:       center-aligned PWM mode 1
//   PWM frequency:  20 kHz
//   ARR:            400
//   Initial duty:   50% on all three phases
//   Dead time:      ~1 us using DTG = 16 at 16 MHz timer clock
//
// Notes:
// - This file intentionally uses register-level programming through CMSIS device
//   definitions only. No HAL is used.
// - Do not connect this to a real power stage until you have scoped the gate
//   driver inputs and confirmed polarity, dead time, and fault behavior.
// -----------------------------------------------------------------------------

#define TIM1_PWM_ARR                400U
#define TIM1_PWM_50_PERCENT         (TIM1_PWM_ARR / 2U) 
#define TIM1_DEADTIME_TICKS         16U  // ~1 us at 16 MHz timer clock

#define ADC_MAX_COUNTS              4095U
#define ADC_REFERENCE_VOLTAGE       3.3f
#define ACS712_ZERO_CURRENT_VOLTAGE 2.5f
#define ACS712_SENSITIVITY          0.185f  // 185mV/A for ACS712ELC-5A

#define CURRENT_ZERO_CAL_SAMPLES    1000U
#define CURRENT_PRINT_DECIMATION    1000U // Changed from 20U

#define CURRENT_FILTER_ALPHA        0.239f  // Exponential moving average filter alpha

#define SYSTICK_1KHZ_RELOAD         16000U  // 16 MHz / 1000 = 16000 ticks for 1 kHz


static void clock_init(void);
static void gpio_init(void);

static void tim1_pwm_gpio_init(void);
static void tim1_pwm_init(void);

static void encoder_tim2_init(void);

static void adc_current_init(void);
static void adc_current_calibrate_zero(void);
static void adc_current_read_all(void);
static uint16_t adc_read_sequence_value(void);
static float adc_raw_to_voltage(uint16_t raw);
static float low_pass_filter(float previous, float input);
static float adc_raw_to_current_amps_with_offset(uint16_t raw, float zero_voltage);
static void print_adc_raw_and_offsets(void);

static void systick_1khz_init(void);
static uint8_t systick_1ms_elapsed(void);

static void print_current_values_ma(float current_a, float current_b, float current_c);

static void uart2_init(void);
static void debug_print(const char *msg);

static void delay(volatile uint32_t count);
static void error_handler(void);
static void int32_to_string(int32_t value, char *buffer, uint32_t buffer_size);


static volatile uint16_t adc_current_a_raw = 0;
static volatile uint16_t adc_current_b_raw = 0;
static volatile uint16_t adc_current_c_raw = 0;

static float current_a_zero_voltage = 0.0f;
static float current_b_zero_voltage = 0.0f;
static float current_c_zero_voltage = 0.0f;

static float current_a_filtered = 0.0f;
static float current_b_filtered = 0.0f;
static float current_c_filtered = 0.0f;

int main(void)
{
    clock_init();
    gpio_init();

    uart2_init();

    tim1_pwm_gpio_init();
    tim1_pwm_init();

    adc_current_init();
    adc_current_calibrate_zero();

    encoder_tim2_init();

    adc_current_init();


    debug_print("Nucleo F446RE Board Test\r\n");
    debug_print("TIM1 complementary PWM configured\r\n");
    debug_print("High-side: PA8/PA9/PA10\r\n");
    debug_print("Low-side:  PA7/PB0/PB1\r\n");
    debug_print("PWM: 20 kHz center-aligned, 50 percent duty, dead time enabled\r\n");
    debug_print("Encoder inputs: PA0=TIM2_CH1, PA1=TIM2_CH2\r\n");
    debug_print("ADC current inputs: PA4=IA, PC0=IB, PC1=IC\r\n");

    debug_print("Calibrating current zero offsets. Keep motor current at zero...\r\n");
    adc_current_calibrate_zero();
    debug_print("Current zero calibration complete\r\n");

    adc_current_read_all();

    // Avoids filtered values starting from zero and slowly ramping to the real offset
    current_a_filtered = adc_raw_to_current_amps_with_offset(adc_current_a_raw, current_a_zero_voltage);
    current_b_filtered = adc_raw_to_current_amps_with_offset(adc_current_b_raw, current_b_zero_voltage);
    current_c_filtered = adc_raw_to_current_amps_with_offset(adc_current_c_raw, current_c_zero_voltage);


    print_adc_raw_and_offsets();

    systick_1khz_init();

    uint32_t current_sample_count = 0;

    int32_t previous_encoder_count = (int32_t)TIM2->CNT;

    while (1)
    {
        int32_t encoder_count = (int32_t)TIM2->CNT;

        // Check if the encoder count has changed since the last print and print if it has
        if (encoder_count != previous_encoder_count)
        {
            char count_string[16];

            int32_to_string(encoder_count, count_string, sizeof(count_string));

            debug_print("Encoder count: ");
            debug_print(count_string);
            debug_print("\r\n");

            previous_encoder_count = encoder_count;
        }

        // Read current values every 1ms and print every 20ms
        if (systick_1ms_elapsed())
        {
            adc_current_read_all();

            float current_a = adc_raw_to_current_amps_with_offset(adc_current_a_raw, current_a_zero_voltage);
            float current_b = adc_raw_to_current_amps_with_offset(adc_current_b_raw, current_b_zero_voltage);
            float current_c = adc_raw_to_current_amps_with_offset(adc_current_c_raw, current_c_zero_voltage);

            current_a_filtered = low_pass_filter(current_a_filtered, current_a);
            current_b_filtered = low_pass_filter(current_b_filtered, current_b);
            current_c_filtered = low_pass_filter(current_c_filtered, current_c);

            current_sample_count++;

            if ((current_sample_count % CURRENT_PRINT_DECIMATION) == 0U)
            {
                print_current_values_ma(current_a_filtered, current_b_filtered, current_c_filtered);
            }
        }

        //delay(1000000);
    }
}

// -----------------------------------------------------------------------------
// Clock: select HSI (16 MHz) as SYSCLK, no PLL
// -----------------------------------------------------------------------------
static void clock_init(void)
{
    // Enable HSI
    RCC->CR |= RCC_CR_HSION;

    // Wait for HSI to be ready
    while (!(RCC->CR & RCC_CR_HSIRDY)) {}

    // Select HSI as SYSCLK
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSI;

    // Wait until HSI is used as SYSCLK
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI) {}

    // AHB, APB1, APB2 all at full speed (no dividers)
    RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2);
}

// -----------------------------------------------------------------------------
// GPIO:
//   PA5  = output push-pull (LED LD2)
//   PC13 = input floating (button B1)
//
// This function is for basic board GPIO only. PWM GPIO setup is intentionally
// separated into pwm_gpio_init().
// -----------------------------------------------------------------------------
static void gpio_init(void)
{
    // Enable GPIOA and GPIOC clocks
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOCEN;

    // Small delay to let clocks stabilize
    volatile uint32_t dummy = RCC->AHB1ENR;
    (void)dummy;

    // PA5 as output (MODER bits 11:10 = 01)
    GPIOA->MODER &= ~(3U << 10);
    GPIOA->MODER |=  (1U << 10);

    // PA5 push-pull (OTYPER bit 5 = 0)
    GPIOA->OTYPER &= ~(1U << 5);

    // PA5 low speed (OSPEEDR bits 11:10 = 00)
    GPIOA->OSPEEDR &= ~(3U << 10);

    // PA5 no pull (PUPDR bits 11:10 = 00)
    GPIOA->PUPDR &= ~(3U << 10);

    // LED off initially
    GPIOA->BSRR = (1U << (5 + 16));

    // PC13 as input (MODER bits 27:26 = 00)
    GPIOC->MODER &= ~(3U << 26);

    // PC13 no pull (PUPDR bits 27:26 = 00)
    GPIOC->PUPDR &= ~(3U << 26);
}

// -----------------------------------------------------------------------------
// TIM1 complementary PWM GPIO setup:
//   PA8  = TIM1_CH1,  AF1, Phase A high side
//   PA7  = TIM1_CH1N, AF1, Phase A low side
//   PA9  = TIM1_CH2,  AF1, Phase B high side
//   PB0  = TIM1_CH2N, AF1, Phase B low side
//   PA10 = TIM1_CH3,  AF1, Phase C high side
//   PB1  = TIM1_CH3N, AF1, Phase C low side
// -----------------------------------------------------------------------------
static void tim1_pwm_gpio_init(void)
{
    // Enable GPIOA and GPIOB clocks
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;

    // Readback after enabling clocks
    volatile uint32_t dummy = RCC->AHB1ENR;
    (void)dummy;

    // -------------------------------------------------------------------------
    // GPIOA pins: PA7, PA8, PA9, PA10 -> AF1
    // -------------------------------------------------------------------------

    // Alternate function mode
    GPIOA->MODER &= ~((3U << (7U  * 2U)) |
                      (3U << (8U  * 2U)) |
                      (3U << (9U  * 2U)) |
                      (3U << (10U * 2U)));

    GPIOA->MODER |=  ((2U << (7U  * 2U)) |
                      (2U << (8U  * 2U)) |
                      (2U << (9U  * 2U)) |
                      (2U << (10U * 2U)));

    // Push-pull
    GPIOA->OTYPER &= ~((1U << 7U) |
                       (1U << 8U) |
                       (1U << 9U) |
                       (1U << 10U));

    // Very high speed for PWM edges
    GPIOA->OSPEEDR &= ~((3U << (7U  * 2U)) |
                        (3U << (8U  * 2U)) |
                        (3U << (9U  * 2U)) |
                        (3U << (10U * 2U)));

    GPIOA->OSPEEDR |=  ((3U << (7U  * 2U)) |
                        (3U << (8U  * 2U)) |
                        (3U << (9U  * 2U)) |
                        (3U << (10U * 2U)));

    // No pull-up/pull-down
    GPIOA->PUPDR &= ~((3U << (7U  * 2U)) |
                      (3U << (8U  * 2U)) |
                      (3U << (9U  * 2U)) |
                      (3U << (10U * 2U)));

    // PA7 is in AFR[0], field 7. AF1 = TIM1_CH1N.
    GPIOA->AFR[0] &= ~(0xFU << (7U * 4U));
    GPIOA->AFR[0] |=  (1U   << (7U * 4U));

    // PA8/PA9/PA10 are in AFR[1], fields 0/1/2. AF1 = TIM1_CH1/2/3.
    GPIOA->AFR[1] &= ~((0xFU << ((8U  - 8U) * 4U)) |
                       (0xFU << ((9U  - 8U) * 4U)) |
                       (0xFU << ((10U - 8U) * 4U)));

    GPIOA->AFR[1] |=  ((1U << ((8U  - 8U) * 4U)) |
                       (1U << ((9U  - 8U) * 4U)) |
                       (1U << ((10U - 8U) * 4U)));

    // -------------------------------------------------------------------------
    // GPIOB pins: PB0, PB1 -> AF1
    // -------------------------------------------------------------------------

    // Alternate function mode
    GPIOB->MODER &= ~((3U << (0U * 2U)) |
                      (3U << (1U * 2U)));

    GPIOB->MODER |=  ((2U << (0U * 2U)) |
                      (2U << (1U * 2U)));

    // Push-pull
    GPIOB->OTYPER &= ~((1U << 0U) |
                       (1U << 1U));

    // Very high speed
    GPIOB->OSPEEDR &= ~((3U << (0U * 2U)) |
                        (3U << (1U * 2U)));

    GPIOB->OSPEEDR |=  ((3U << (0U * 2U)) |
                        (3U << (1U * 2U)));

    // No pull-up/pull-down
    GPIOB->PUPDR &= ~((3U << (0U * 2U)) |
                      (3U << (1U * 2U)));

    // PB0/PB1 are in AFR[0], fields 0/1. AF1 = TIM1_CH2N/TIM1_CH3N.
    GPIOB->AFR[0] &= ~((0xFU << (0U * 4U)) |
                       (0xFU << (1U * 4U)));

    GPIOB->AFR[0] |=  ((1U << (0U * 4U)) |
                       (1U << (1U * 4U)));
}


// -----------------------------------------------------------------------------
// TIM1 complementary PWM setup
//
// Generates three center-aligned PWM channels plus complementary outputs:
//   CH1/CH1N, CH2/CH2N, CH3/CH3N
//
// At 16 MHz timer clock and ARR=400 in center-aligned mode:
//   f_pwm = 16 MHz / (2 * ARR) = 20 kHz
//
// Duty is controlled by:
//   TIM1->CCR1 = Phase A duty ticks
//   TIM1->CCR2 = Phase B duty ticks
//   TIM1->CCR3 = Phase C duty ticks
//
// Range:
//   0 <= CCRx <= TIM1_PWM_ARR
// -----------------------------------------------------------------------------
static void tim1_pwm_init(void)
{
    // Enable TIM1 clock. TIM1 is on APB2.
    RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;

    // Readback after enabling clock
    volatile uint32_t dummy = RCC->APB2ENR;
    (void)dummy;

    // Reset TIM1 to a known state
    RCC->APB2RSTR |=  RCC_APB2RSTR_TIM1RST;
    RCC->APB2RSTR &= ~RCC_APB2RSTR_TIM1RST;

    // Disable counter during configuration
    TIM1->CR1 &= ~TIM_CR1_CEN;

    // Prescaler = 0: timer clock = 16 MHz
    TIM1->PSC = 0U;

    // Auto-reload value for 20 kHz center-aligned PWM
    TIM1->ARR = TIM1_PWM_ARR;

    // Initial duty cycle: 50% on all three phases
    TIM1->CCR1 = TIM1_PWM_50_PERCENT;
    TIM1->CCR2 = TIM1_PWM_50_PERCENT;
    TIM1->CCR3 = TIM1_PWM_50_PERCENT;

    // -------------------------------------------------------------------------
    // Configure PWM mode 1 on CH1, CH2, CH3 with preload enabled
    // -------------------------------------------------------------------------

    // CH1:
    // OC1M bits = 110: PWM mode 1
    // OC1PE bit = 1: preload enable
    TIM1->CCMR1 &= ~(TIM_CCMR1_OC1M | TIM_CCMR1_OC1PE);
    TIM1->CCMR1 |=  ((6U << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE);

    // CH2:
    // OC2M bits = 110: PWM mode 1
    // OC2PE bit = 1: preload enable
    TIM1->CCMR1 &= ~(TIM_CCMR1_OC2M | TIM_CCMR1_OC2PE);
    TIM1->CCMR1 |=  ((6U << TIM_CCMR1_OC2M_Pos) | TIM_CCMR1_OC2PE);

    // CH3:
    // OC3M bits = 110: PWM mode 1
    // OC3PE bit = 1: preload enable
    TIM1->CCMR2 &= ~(TIM_CCMR2_OC3M | TIM_CCMR2_OC3PE);
    TIM1->CCMR2 |=  ((6U << TIM_CCMR2_OC3M_Pos) | TIM_CCMR2_OC3PE);

    // -------------------------------------------------------------------------
    // Enable main and complementary channel outputs
    // -------------------------------------------------------------------------

    // Clear polarity bits first:
    // CCxP  = 0: active high main output
    // CCxNP = 0: active high complementary output
    TIM1->CCER &= ~(TIM_CCER_CC1P  | TIM_CCER_CC1NP |
                    TIM_CCER_CC2P  | TIM_CCER_CC2NP |
                    TIM_CCER_CC3P  | TIM_CCER_CC3NP);

    // Enable CH1/CH1N, CH2/CH2N, CH3/CH3N
    TIM1->CCER |= (TIM_CCER_CC1E  | TIM_CCER_CC1NE |
                   TIM_CCER_CC2E  | TIM_CCER_CC2NE |
                   TIM_CCER_CC3E  | TIM_CCER_CC3NE);

    // -------------------------------------------------------------------------
    // Dead time and main output enable
    // -------------------------------------------------------------------------

    // BDTR:
    // DTG = dead-time generator value.
    // With CKD=0 and 16 MHz timer clock, 1 timer tick = 62.5 ns.
    // DTG=16 gives approximately 1 us dead time.
    //
    // MOE = main output enable. Without MOE, TIM1 outputs do not drive pins.
    TIM1->BDTR = ((TIM1_DEADTIME_TICKS & 0xFFU) << TIM_BDTR_DTG_Pos) |
                 TIM_BDTR_MOE;

    // -------------------------------------------------------------------------
    // Center-aligned PWM and preload
    // -------------------------------------------------------------------------

    // CR1:
    // ARPE = auto-reload preload enable
    // CMS  = 01: center-aligned mode 1
    // DIR  = 0: starts counting up
    TIM1->CR1 &= ~(TIM_CR1_CMS | TIM_CR1_DIR);
    TIM1->CR1 |=  TIM_CR1_ARPE | (1U << TIM_CR1_CMS_Pos);

    // Generate update event to load PSC/ARR/CCR preload values
    TIM1->EGR = TIM_EGR_UG;

    // Clear update flag after forced update
    TIM1->SR &= ~TIM_SR_UIF;

    // Enable counter
    TIM1->CR1 |= TIM_CR1_CEN;
}


// -----------------------------------------------------------------------------
// Encoder TIM2 setup:
//   PA0 = TIM2_CH1, AF1 = Encoder Channel A
//   PA1 = TIM2_CH2, AF1 = Encoder Channel B
//
// TIM2 is configured in encoder mode 3:
//   - Counts on both TI1 and TI2 edges
//   - Direction is determined by the phase relationship between A and B
//   - TIM2->CNT becomes the encoder position count
//
// Encoder wiring:
//   Encoder Channel A -> voltage divider -> PA0
//   Encoder Channel B -> voltage divider -> PA1
// -----------------------------------------------------------------------------
static void encoder_tim2_init(void)
{
    // Enable GPIOA and TIM2 clocks
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    // Readback after enabling clocks
    volatile uint32_t dummy;
    dummy = RCC->AHB1ENR;
    dummy = RCC->APB1ENR;
    (void)dummy;

    // -------------------------------------------------------------------------
    // Configure PA0 and PA1 for alternate function AF1 = TIM2
    // -------------------------------------------------------------------------

    // PA0 and PA1 to alternate function mode
    GPIOA->MODER &= ~((3U << (0U * 2U)) |
                      (3U << (1U * 2U)));

    GPIOA->MODER |=  ((2U << (0U * 2U)) |
                      (2U << (1U * 2U)));

    // Push-pull/default output type. Not critical for AF input.
    GPIOA->OTYPER &= ~((1U << 0U) |
                       (1U << 1U));

    // Low speed is fine for input pins.
    GPIOA->OSPEEDR &= ~((3U << (0U * 2U)) |
                        (3U << (1U * 2U)));

    // No internal pull-up/pull-down.
    // Your external divider provides a defined path to ground.
    GPIOA->PUPDR &= ~((3U << (0U * 2U)) |
                      (3U << (1U * 2U)));

    // AF1 for PA0/PA1
    GPIOA->AFR[0] &= ~((0xFU << (0U * 4U)) |
                       (0xFU << (1U * 4U)));

    GPIOA->AFR[0] |=  ((1U << (0U * 4U)) |
                       (1U << (1U * 4U)));

    // -------------------------------------------------------------------------
    // Configure TIM2 in encoder mode
    // -------------------------------------------------------------------------

    // Disable timer before configuration
    TIM2->CR1 &= ~TIM_CR1_CEN;

    // Reset relevant TIM2 registers
    TIM2->CR1   = 0;
    TIM2->CR2   = 0;
    TIM2->SMCR  = 0;
    TIM2->DIER  = 0;
    TIM2->CCER  = 0;
    TIM2->CCMR1 = 0;
    TIM2->CCMR2 = 0;
    TIM2->CNT   = 0;

    // No prescaler. Count every valid encoder edge.
    TIM2->PSC = 0;

    // TIM2 is 32-bit. Use full range.
    TIM2->ARR = 0xFFFFFFFFU;

    // Configure CH1 and CH2 as inputs.
    //
    // CC1S = 01: CC1 channel is input, mapped to TI1
    // CC2S = 01: CC2 channel is input, mapped to TI2
    TIM2->CCMR1 &= ~(TIM_CCMR1_CC1S | TIM_CCMR1_CC2S);

    TIM2->CCMR1 |=  (1U << TIM_CCMR1_CC1S_Pos);
    TIM2->CCMR1 |=  (1U << TIM_CCMR1_CC2S_Pos);

    // Optional digital input filter.
    //
    // IC1F and IC2F are the input capture filters.
    // 0 = no filter
    // 3 = light filtering, useful for noisy bench wiring
    TIM2->CCMR1 &= ~(TIM_CCMR1_IC1F | TIM_CCMR1_IC2F);

    TIM2->CCMR1 |=  (3U << TIM_CCMR1_IC1F_Pos);
    TIM2->CCMR1 |=  (3U << TIM_CCMR1_IC2F_Pos);

    // Capture on non-inverted polarity.
    // If direction is backwards, swap A/B wires or invert one channel.
    TIM2->CCER &= ~(TIM_CCER_CC1P  |
                    TIM_CCER_CC1NP |
                    TIM_CCER_CC2P  |
                    TIM_CCER_CC2NP);

    // Enable channel 1 and channel 2 captures.
    TIM2->CCER |= (TIM_CCER_CC1E | TIM_CCER_CC2E);

    // Encoder mode 3:
    // SMS = 011, counter counts on both TI1 and TI2 edges.
    TIM2->SMCR &= ~TIM_SMCR_SMS;
    TIM2->SMCR |=  (3U << TIM_SMCR_SMS_Pos);

    // Generate an update event to load prescaler/ARR
    TIM2->EGR = TIM_EGR_UG;

    // Clear counter after update event
    TIM2->CNT = 0;

    // Enable TIM2 counter
    TIM2->CR1 |= TIM_CR1_CEN;
}


// -----------------------------------------------------------------------------
// ADC setup for current sensing using ACS712:
//   Current A -> PA4 = ADC1_IN4
//   Current B -> PC0 = ADC1_IN10
//   Current C -> PC1 = ADC1_IN11
//
// Important:
//   PA0 and PA1 are already used by TIM2 encoder input, so they are not used
//   as ADC current inputs in this version.
// -----------------------------------------------------------------------------
static void adc_current_init(void)
{
    // Enable GPIOA, GPIOC, and ADC1 clocks
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOCEN;
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;

    volatile uint32_t dummy;
    dummy = RCC->AHB1ENR;
    dummy = RCC->APB2ENR;
    (void)dummy;

    // PA4 analog mode, no pull
    GPIOA->MODER &= ~(3U << (4U * 2U));
    GPIOA->MODER |=  (3U << (4U * 2U));
    GPIOA->PUPDR &= ~(3U << (4U * 2U));

    // PC0 and PC1 analog mode, no pull
    GPIOC->MODER &= ~((3U << (0U * 2U)) |
                      (3U << (1U * 2U)));

    GPIOC->MODER |=  ((3U << (0U * 2U)) |
                      (3U << (1U * 2U)));

    GPIOC->PUPDR &= ~((3U << (0U * 2U)) |
                      (3U << (1U * 2U)));

    // ADC common prescaler: ADCPRE = 01 means PCLK2 / 4.
    // With current 16 MHz HSI/PCLK2 setup, ADC clock = 4 MHz.
    ADC->CCR &= ~(3U << 16);
    ADC->CCR |=  (1U << 16);

    // Reset ADC1 control/configuration registers used here
    ADC1->CR1 = 0;
    ADC1->CR2 = 0;

    // Scan mode: one software trigger converts all three regular channels
    ADC1->CR1 |= ADC_CR1_SCAN;

    // EOCS = EOC flag after each channel conversion
    // This lets adc_current_read_all() read DR once per channel.
    ADC1->CR2 |= ADC_CR2_EOCS;

    // Regular sequence length = 3 conversions, encoded as N - 1
    ADC1->SQR1 &= ~(0xFU << 20);
    ADC1->SQR1 |=  (2U << 20);

    // Regular conversion sequence:
    //   SQ1 = ADC channel 4  / PA4
    //   SQ2 = ADC channel 10 / PC0
    //   SQ3 = ADC channel 11 / PC1
    ADC1->SQR3 = 0;
    ADC1->SQR3 |= (4U  << 0);
    ADC1->SQR3 |= (10U << 5);
    ADC1->SQR3 |= (11U << 10);

    // Sample time = 84 cycles for channels 4, 10, and 11.
    // Channel 4 is in SMPR2. Channels 10 and 11 are in SMPR1.

    ADC1->SMPR2 &= ~(7U << (4U * 3U));
    ADC1->SMPR2 |=  (4U << (4U * 3U));

    ADC1->SMPR1 &= ~((7U << ((10U - 10U) * 3U)) |
                     (7U << ((11U - 10U) * 3U)));

    ADC1->SMPR1 |=  ((4U << ((10U - 10U) * 3U)) |
                     (4U << ((11U - 10U) * 3U)));

    // Enable ADC1
    ADC1->CR2 |= ADC_CR2_ADON;

    // Short stabilization delay after ADC enable
    delay(1000);
}


// -----------------------------------------------------------------------------
// Calibrate ACS712 zero-current offsets.
//
// Run this while the motor current is zero. The ACS712 output should be near
// VCC/2, but the exact value varies sensor-to-sensor, so calibration is better
// than hard-coding 2.5 V.
// -----------------------------------------------------------------------------
static void adc_current_calibrate_zero(void)
{
    uint64_t sum_a = 0;
    uint64_t sum_b = 0;
    uint64_t sum_c = 0;

    /*
     * Throw away a few initial reads in case the ADC/sensor output
     * has not fully settled yet.
     */
    for (uint32_t i = 0; i < 20U; i++)
    {
        adc_current_read_all();
    }

    for (uint32_t i = 0; i < CURRENT_ZERO_CAL_SAMPLES; i++)
    {
        adc_current_read_all();

        sum_a += adc_current_a_raw;
        sum_b += adc_current_b_raw;
        sum_c += adc_current_c_raw;
    }

    uint16_t zero_a_raw = (uint16_t)(sum_a / CURRENT_ZERO_CAL_SAMPLES);
    uint16_t zero_b_raw = (uint16_t)(sum_b / CURRENT_ZERO_CAL_SAMPLES);
    uint16_t zero_c_raw = (uint16_t)(sum_c / CURRENT_ZERO_CAL_SAMPLES);

    current_a_zero_voltage = adc_raw_to_voltage(zero_a_raw);
    current_b_zero_voltage = adc_raw_to_voltage(zero_b_raw);
    current_c_zero_voltage = adc_raw_to_voltage(zero_c_raw);
}


// -----------------------------------------------------------------------------
// Read one ADC value from the active conversion sequence.
// -----------------------------------------------------------------------------
static uint16_t adc_read_sequence_value(void)
{
    while (!(ADC1->SR & ADC_SR_EOC))
    {
    }

    return (uint16_t)ADC1->DR;
}


// -----------------------------------------------------------------------------
// Trigger one 3-channel ADC sequence and store raw current readings.
// -----------------------------------------------------------------------------
static void adc_current_read_all(void)
{
    // Clear stale status flags before a fresh software-triggered read
    ADC1->SR = 0;

    // Start regular conversion sequence
    ADC1->CR2 |= ADC_CR2_SWSTART;

    adc_current_a_raw = adc_read_sequence_value();
    adc_current_b_raw = adc_read_sequence_value();
    adc_current_c_raw = adc_read_sequence_value();
}


static float adc_raw_to_voltage(uint16_t raw)
{
    return ((float)raw * ADC_REFERENCE_VOLTAGE) / (float)ADC_MAX_COUNTS;
}

static float low_pass_filter(float previous, float input)
{
    return (previous + CURRENT_FILTER_ALPHA * (input - previous));
}


static float adc_raw_to_current_amps_with_offset(uint16_t raw, float zero_voltage)
{
    float voltage = adc_raw_to_voltage(raw);
    return (voltage - zero_voltage) / ACS712_SENSITIVITY;
}


// -----------------------------------------------------------------------------
// SysTick setup:
//   Core clock = 16 MHz
//   Reload = 16000 - 1
//   Tick rate = 1 kHz
// -----------------------------------------------------------------------------
static void systick_1khz_init(void)
{
    SysTick->LOAD = SYSTICK_1KHZ_RELOAD - 1U;
    SysTick->VAL  = 0U;

    // Use core clock, enable SysTick, no interrupt.
    // COUNTFLAG is polled in systick_1ms_elapsed().
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
}


static uint8_t systick_1ms_elapsed(void)
{
    // COUNTFLAG is set when SysTick counts from 1 to 0.
    // Reading CTRL clears COUNTFLAG.
    return ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) != 0U) ? 1U : 0U;
}


// -----------------------------------------------------------------------------
// Print current values in milliamps.
//
// Using integer milliamps avoids requiring printf floating-point support.
// Printed every 20th 1 kHz sample, so output rate is 50 Hz.
// -----------------------------------------------------------------------------
static void print_current_values_ma(float current_a, float current_b, float current_c)
{
    char value_string[16];

    int32_t current_a_ma = (int32_t)(current_a * 1000.0f);
    int32_t current_b_ma = (int32_t)(current_b * 1000.0f);
    int32_t current_c_ma = (int32_t)(current_c * 1000.0f);

    debug_print("IA=");
    int32_to_string(current_a_ma, value_string, sizeof(value_string));
    debug_print(value_string);
    debug_print(" mA, IB=");

    int32_to_string(current_b_ma, value_string, sizeof(value_string));
    debug_print(value_string);
    debug_print(" mA, IC=");

    int32_to_string(current_c_ma, value_string, sizeof(value_string));
    debug_print(value_string);
    debug_print(" mA, ENC=");

    int32_to_string((int32_t)TIM2->CNT, value_string, sizeof(value_string));
    debug_print(value_string);
    debug_print("\r\n");
}


static void print_adc_raw_and_offsets(void)
{
    char value_string[16];

    debug_print("RAW IA=");
    int32_to_string((int32_t)adc_current_a_raw, value_string, sizeof(value_string));
    debug_print(value_string);

    debug_print(", IB=");
    int32_to_string((int32_t)adc_current_b_raw, value_string, sizeof(value_string));
    debug_print(value_string);

    debug_print(", IC=");
    int32_to_string((int32_t)adc_current_c_raw, value_string, sizeof(value_string));
    debug_print(value_string);

    debug_print(" | ZERO mV IA=");
    int32_to_string((int32_t)(current_a_zero_voltage * 1000.0f), value_string, sizeof(value_string));
    debug_print(value_string);

    debug_print(", IB=");
    int32_to_string((int32_t)(current_b_zero_voltage * 1000.0f), value_string, sizeof(value_string));
    debug_print(value_string);

    debug_print(", IC=");
    int32_to_string((int32_t)(current_c_zero_voltage * 1000.0f), value_string, sizeof(value_string));
    debug_print(value_string);

    debug_print("\r\n");
}


// -----------------------------------------------------------------------------
// UART2: PA2=TX, PA3=RX, AF7, 115200 8N1, 16MHz clock
// BRR = 16000000 / 115200 = 138
// -----------------------------------------------------------------------------
static void uart2_init(void)
{
    // Enable GPIOA and USART2 clocks
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

    // Reset USART2
    RCC->APB1RSTR |=  RCC_APB1RSTR_USART2RST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_USART2RST;

    // PA2 and PA3 to alternate function mode (MODER = 10)
    GPIOA->MODER &= ~((3U << 4) | (3U << 6));
    GPIOA->MODER |=  ((2U << 4) | (2U << 6));

    // PA2 and PA3 push-pull output type
    GPIOA->OTYPER &= ~((1U << 2) | (1U << 3));

    // PA2 and PA3 very high speed
    GPIOA->OSPEEDR |= ((3U << 4) | (3U << 6));

    // PA2 and PA3 pull-up
    GPIOA->PUPDR &= ~((3U << 4) | (3U << 6));
    GPIOA->PUPDR |=  ((1U << 4) | (1U << 6));

    // PA2 and PA3 alternate function = AF7 (USART2)
    // AFR[0] covers pins 0-7
    GPIOA->AFR[0] &= ~((0xFU << 8) | (0xFU << 12));
    GPIOA->AFR[0] |=  ((7U  << 8) | (7U  << 12));

    // Baud rate: 16MHz / 115200 = 138
    USART2->BRR = 16000000 / 115200;

    // 8 data bits, no parity, 1 stop bit
    USART2->CR2 &= ~USART_CR2_STOP;   // 1 stop bit
    USART2->CR1  = USART_CR1_UE       // enable USART
                 | USART_CR1_TE       // enable TX
                 | USART_CR1_RE;      // enable RX
}


// -----------------------------------------------------------------------------
// Print a null-terminated string over UART2
// -----------------------------------------------------------------------------
static void debug_print(const char *msg)
{
    if (msg == 0) return;

    while (*msg)
    {
        // Wait for TX register empty
        while (!(USART2->SR & USART_SR_TXE)) {}
        USART2->DR = (uint8_t)*msg++;
    }

    // Wait for transmission complete
    while (!(USART2->SR & USART_SR_TC)) {}
}


static void int32_to_string(int32_t value, char *buffer, uint32_t buffer_size)
{
    if (buffer == nullptr || buffer_size == 0)
    {
        return;
    }

    uint32_t index = 0;

    // Handle zero directly
    if (value == 0)
    {
        if (buffer_size >= 2)
        {
            buffer[0] = '0';
            buffer[1] = '\0';
        }
        return;
    }

    // Handle negative numbers
    uint32_t unsigned_value;

    if (value < 0)
    {
        if (index < buffer_size - 1)
        {
            buffer[index++] = '-';
        }

        // Avoid overflow if value is INT32_MIN
        unsigned_value = (uint32_t)(-(value + 1)) + 1U;
    }
    else
    {
        unsigned_value = (uint32_t)value;
    }

    // Convert digits into temporary buffer in reverse order
    char temp[11]; // max uint32_t is 10 digits
    uint32_t temp_index = 0;

    while (unsigned_value > 0 && temp_index < sizeof(temp))
    {
        temp[temp_index++] = (char)('0' + (unsigned_value % 10U));
        unsigned_value /= 10U;
    }

    // Copy digits back in correct order
    while (temp_index > 0 && index < buffer_size - 1)
    {
        buffer[index++] = temp[--temp_index];
    }

    buffer[index] = '\0';
}

// -----------------------------------------------------------------------------
// Software delay (count is CPU cycles, roughly)
// -----------------------------------------------------------------------------
static void delay(volatile uint32_t count)
{
    while (count--) {}
}

// -----------------------------------------------------------------------------
// Error handler: blink LD2 LED rapidly 
// -----------------------------------------------------------------------------
static void error_handler(void)
{
    // Ensure GPIOA clock is on
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    // PA5 as output
    GPIOA->MODER &= ~(3U << 10);
    GPIOA->MODER |=  (1U << 10);

    while (1)
    {
        GPIOA->ODR ^= (1U << 5);
        delay(500000);
    }
}