#include "stm32f4xx.h"

// -----------------------------------------------------------------------------
// Nucleo F446RE pin definitions
// LED LD2  = PA5
// Button B1 = PC13
// USART2 TX = PA2, RX = PA3 (AF7)
// PWM GPIO candidates:
//   PA8  = TIM1_CH1, AF1
//   PA9  = TIM1_CH2, AF1
//   PA10 = TIM1_CH3, AF1
//
// Clock: 16 MHz HSI
// -----------------------------------------------------------------------------
//
// Notes:
// - This file intentionally uses register-level programming through CMSIS device
//   definitions only. No HAL is used.
// - PA8/PA9/PA10 are configured as alternate-function outputs for TIM1 PWM.
// - TIM1 will be used for High/Low side PWM control
// - TIM2 is configured for encoder mode.

// PWM Pin Output Mapping:
// Phase A high: PA8  / TIM1_CH1
// Phase A low:  PA7  / TIM1_CH1N

// Phase B high: PA9  / TIM1_CH2
// Phase B low:  PB0  / TIM1_CH2N

// Phase C high: PA10 / TIM1_CH3
// Phase C low:  PB1  / TIM1_CH3N

static void clock_init(void);
static void gpio_init(void);
static void encoder_tim2_init(void);
static void pwm_gpio_init(void);
static void uart2_init(void);
static void debug_print(const char *msg);
static void delay(volatile uint32_t count);
static void error_handler(void);
static void int32_to_string(int32_t value, char *buffer, uint32_t buffer_size);

int main(void)
{
    clock_init();
    gpio_init();
    encoder_tim2_init();
    pwm_gpio_init();
    uart2_init();

    debug_print("Nucleo F446RE Board Test\r\n");
    debug_print("PA8/PA9/PA10 configured for TIM1 PWM alternate function\r\n");
    debug_print("TIM2 configured for encoder mode\r\n");
    debug_print("USART2 configured for serial communication\r\n");  
    debug_print("All peripherals initialized successfully\r\n");


    while (1)
    {
        debug_print("Reading Encoder\r\n");
        int32_t encoder_count = (int32_t)TIM2->CNT;
        char count_string[16];
        int32_to_string(encoder_count, count_string, sizeof(count_string));
        debug_print("Encoder count: ");
        debug_print(count_string);
        debug_print("\r\n");
        delay(1000000);
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
// PWM GPIO setup only:
//   PA8  = TIM1_CH1, AF1
//   PA9  = TIM1_CH2, AF1
//   PA10 = TIM1_CH3, AF1
//
// These three pins are a convenient 3-phase PWM group because they are all on
// TIM1, which is the STM32F446 advanced-control timer commonly used for motor
// control PWM.
//
// This function does not configure TIM1, duty cycle, frequency, complementary
// outputs, dead time, or enable PWM output. It only prepares PA8/PA9/PA10 so
// that TIM1 can drive them later.
// -----------------------------------------------------------------------------
static void pwm_gpio_init(void)
{
    // Enable GPIOA clock
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    // Small delay/readback after enabling peripheral clock
    volatile uint32_t dummy = RCC->AHB1ENR;
    (void)dummy;

    // PA8, PA9, PA10 to alternate function mode.
    // MODER:
    //   00 = input
    //   01 = general purpose output
    //   10 = alternate function
    //   11 = analog
    GPIOA->MODER &= ~((3U << (8U * 2U)) |
                      (3U << (9U * 2U)) |
                      (3U << (10U * 2U)));

    GPIOA->MODER |=  ((2U << (8U * 2U)) |
                      (2U << (9U * 2U)) |
                      (2U << (10U * 2U)));

    // Push-pull output type for PA8/PA9/PA10
    GPIOA->OTYPER &= ~((1U << 8U) |
                       (1U << 9U) |
                       (1U << 10U));

    // High speed output for cleaner PWM edges.
    // Use very high speed for now; this can be reduced later if EMI is a concern.
    GPIOA->OSPEEDR &= ~((3U << (8U * 2U)) |
                        (3U << (9U * 2U)) |
                        (3U << (10U * 2U)));

    GPIOA->OSPEEDR |=  ((3U << (8U * 2U)) |
                        (3U << (9U * 2U)) |
                        (3U << (10U * 2U)));

    // No internal pull-up or pull-down on PWM outputs.
    GPIOA->PUPDR &= ~((3U << (8U * 2U)) |
                      (3U << (9U * 2U)) |
                      (3U << (10U * 2U)));

    // Select AF1 for PA8/PA9/PA10.
    // PA8/PA9/PA10 are in AFR[1], because AFR[0] is pins 0-7 and AFR[1] is pins 8-15.
    // In AFR[1], PA8 is field 0, PA9 is field 1, PA10 is field 2.
    GPIOA->AFR[1] &= ~((0xFU << ((8U - 8U) * 4U)) |
                       (0xFU << ((9U - 8U) * 4U)) |
                       (0xFU << ((10U - 8U) * 4U)));

    GPIOA->AFR[1] |=  ((1U << ((8U - 8U) * 4U)) |
                       (1U << ((9U - 8U) * 4U)) |
                       (1U << ((10U - 8U) * 4U)));
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