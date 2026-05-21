#include "stm32f4xx_hal.h"
#include <cstring>

// Nucleo F446RE onboard LED LD2
#define LED_GPIO_PORT GPIOA
#define LED_PIN       GPIO_PIN_5

// Nucleo F446RE blue user button B1
#define BUTTON_GPIO_PORT GPIOC
#define BUTTON_PIN       GPIO_PIN_13

// UART Handler
UART_HandleTypeDef huart2;

void SystemClock_Config(void);
static void GPIO_Init(void);
static void UART2_Init(void);
static void Debug_Print(const char *msg);
void Error_Handler(void);

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    SystemCoreClockUpdate();
    GPIO_Init();
    UART2_Init();

    const char msg[] = "Before delay\r\n";
    Debug_Print(msg);
    for (volatile uint32_t i = 0; i < 1000000; i++)
    {
    }
    Debug_Print("After delay\r\n");


    Debug_Print("Nucleo F446RE Board Test\r\n");

    GPIO_PinState lastButtonState = GPIO_PIN_RESET;

    while (1)
    {
        Debug_Print("Loop alive\r\n");
        GPIO_PinState buttonState = HAL_GPIO_ReadPin(BUTTON_GPIO_PORT, BUTTON_PIN);

        // User button is active-low
        if (buttonState == GPIO_PIN_RESET)
        {
            // Log only when the button first transitions from released to pressed
            if (lastButtonState == GPIO_PIN_SET)
            {
                Debug_Print("Button pressed\r\n");
            }

            HAL_GPIO_TogglePin(LED_GPIO_PORT, LED_PIN);
            HAL_Delay(150);
        }
        else
        {
            HAL_GPIO_WritePin(LED_GPIO_PORT, LED_PIN, GPIO_PIN_RESET);
        }

        lastButtonState = buttonState;
    }
}

static void GPIO_Init(void)
{
    // Enable GPIO peripheral clocks
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // Configure PA5 as output for onboard LED
    GPIO_InitStruct.Pin = LED_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_GPIO_PORT, &GPIO_InitStruct);

    // Configure PC13 as input for user button
    GPIO_InitStruct.Pin = BUTTON_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(BUTTON_GPIO_PORT, &GPIO_InitStruct);
}

void SystemClock_Config(void)
{
    // Basic clock setup using internal HSI oscillator.
    // This is enough for a simple board test.

    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        while (1)
        {
        }
    }

    RCC_ClkInitStruct.ClockType =
        RCC_CLOCKTYPE_HCLK |
        RCC_CLOCKTYPE_SYSCLK |
        RCC_CLOCKTYPE_PCLK1 |
        RCC_CLOCKTYPE_PCLK2;

    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
    {
        while (1)
        {
        }
    }
}

static void UART2_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();

    // Force USART2 into a clean reset state first
    __HAL_RCC_USART2_FORCE_RESET();
    __HAL_RCC_USART2_RELEASE_RESET();

    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // USART2 TX = PA2
    // USART2 RX = PA3
    GPIO_InitStruct.Pin = GPIO_PIN_2 | GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart2) != HAL_OK)
    {
        Error_Handler();
    }
}

static void Debug_Print(const char *msg)
{
    if (msg == nullptr)
    {
        return;
    }

    //uint16_t length = static_cast<uint16_t>(std::strlen(message));

    // Wait for UART to become ready before starting a new transmit
    uint32_t startTick = HAL_GetTick();
    while (HAL_UART_GetState(&huart2) != HAL_UART_STATE_READY)
    {
        if ((HAL_GetTick() - startTick) > 1000)
        {
            Error_Handler();
        }
    }

    HAL_StatusTypeDef status = HAL_BUSY;

    // Retry if UART is busy
    startTick = HAL_GetTick();
    while (status == HAL_BUSY)
    {
        status = HAL_UART_Transmit(
            &huart2,
            reinterpret_cast<uint8_t *>(const_cast<char *>(msg)),
            std::strlen(msg),
            HAL_MAX_DELAY
        );

        if ((HAL_GetTick() - startTick) > 1000)
        {
            Error_Handler();
        }
    }

    if (status != HAL_OK)
    {
        Error_Handler();
    }

    // Wait until the final byte has fully left the UART shift register
    startTick = HAL_GetTick();
    while (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_TC) == RESET)
    {
        if ((HAL_GetTick() - startTick) > 1000)
        {
            Error_Handler();
        }
    }
}

void Error_Handler(void)
{
    // If you are debugging with ST-LINK, this will break here.
    __BKPT(0);

    // Make sure the LED GPIO clock is enabled.
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // LD2 on the Nucleo F446RE is PA5.
    GPIO_InitStruct.Pin = GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // Blink rapidly forever to indicate an error.
    while (1)
    {
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);

        for (volatile uint32_t i = 0; i < 500000; i++)
        {
            // crude delay, avoids depending on HAL_Delay()
        }
    }
}
