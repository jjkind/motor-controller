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
// - TIM1 itself is not configured or started yet. This only prepares the GPIOs.
//

static void clock_init(void);
static void gpio_init(void);
static void pwm_gpio_init(void);
static void uart2_init(void);
static void debug_print(const char *msg);
static void delay(volatile uint32_t count);
static void error_handler(void);

int main(void)
{
    clock_init();
    gpio_init();
    pwm_gpio_init();
    uart2_init();

    debug_print("Nucleo F446RE Board Test\r\n");
    debug_print("PA8/PA9/PA10 configured for TIM1 PWM alternate function\r\n");

    while (1)
    {
        debug_print("In application space\r\n");
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