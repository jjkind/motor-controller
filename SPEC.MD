# Motor Controller Firmware Spec

## Goal

This project is firmware for a Nucleo F446RE-based motor-controller bring-up.
The current firmware establishes the low-level hardware foundation for:

- Three-phase complementary PWM generation.
- Three-phase ACS712 current sensing.
- PWM-synchronized ADC sampling.
- Quadrature encoder counting.
- UART logging of filtered phase-current values.

This is a bench bring-up project, not yet a complete closed-loop motor-control
application.

## Platform

- Board: ST Nucleo-F446RE.
- MCU: STM32F446RE.
- Build system: PlatformIO.
- PlatformIO environment: `nucleo_f446re`.
- Framework package: `stm32cube`.
- Upload/debug: ST-Link.
- Serial monitor baud rate: `115200`.
- Firmware style: register-level programming through CMSIS device definitions.

Do not introduce HAL application code unless the project intentionally changes
direction. The current firmware directly configures RCC, GPIO, TIM, ADC, USART,
SysTick, and NVIC registers.

## Pin Map

### Board I/O

| Signal | MCU pin | Notes |
| --- | --- | --- |
| LD2 LED | PA5 | Basic board output. |
| B1 button | PC13 | Active low, currently configured as input. |
| USART2 TX | PA2 | AF7, serial logging. |
| USART2 RX | PA3 | AF7, configured though RX is not currently used. |

### Three-Phase PWM

TIM1 generates center-aligned complementary PWM.

| Phase | High-side signal | Low-side signal |
| --- | --- | --- |
| A | PA8 / TIM1_CH1 / AF1 | PA7 / TIM1_CH1N / AF1 |
| B | PA9 / TIM1_CH2 / AF1 | PB0 / TIM1_CH2N / AF1 |
| C | PA10 / TIM1_CH3 / AF1 | PB1 / TIM1_CH3N / AF1 |

### Current Sense

| Phase | MCU pin | ADC channel |
| --- | --- | --- |
| A | PA4 | ADC1_IN4 |
| B | PC0 | ADC1_IN10 |
| C | PC1 | ADC1_IN11 |

PA0 and PA1 are reserved for the encoder and must not be reused as current
inputs in this hardware mapping.

### Encoder

| Encoder signal | MCU pin | Timer channel |
| --- | --- | --- |
| A | PA0 | TIM2_CH1 |
| B | PA1 | TIM2_CH2 |

TIM2 is configured in encoder mode 3, counting both TI1 and TI2 edges. The
counter is a 32-bit position count using the full TIM2 range.

## Clocking

- Clock source: HSI through PLL.
- HSI: 16 MHz.
- SYSCLK / HCLK: 84 MHz.
- APB1 / PCLK1: 42 MHz.
- APB2 / PCLK2: 84 MHz.
- SysTick: 1 kHz polling tick used for startup delays.

## PWM Requirements

- Timer: TIM1.
- Timer clock: 84 MHz.
- Prescaler: 0.
- Auto-reload value: `2100`.
- PWM mode: center-aligned PWM mode 1.
- PWM frequency target: 20 kHz.
- Initial duty cycle: 50 percent on phases A, B, and C.
- Duty registers:
  - Phase A: `TIM1->CCR1`.
  - Phase B: `TIM1->CCR2`.
  - Phase C: `TIM1->CCR3`.
- Valid duty range: `0 <= CCRx <= TIM1_PWM_ARR`.
- Main and complementary outputs are enabled.
- Output polarity is currently active high for main and complementary outputs.
- Dead time target: about 1 us.
- TIM1 main output enable (`MOE`) must be set before PWM appears on pins.

Before connecting a real power stage, verify PWM polarity, complementary output
behavior, dead time, gate-driver input polarity, and shutdown/fault behavior on
an oscilloscope.

## Current-Sense Pipeline

Current sensing uses three ACS712 current sensors read by ADC1.

- ADC reference voltage: 3.3 V.
- ADC resolution assumption: 12-bit, `0..4095`.
- Current conversion: `(measured_voltage - zero_voltage) / sensitivity`.
- Configured sensitivity: `0.1183 V/A`.
- Zero-current calibration samples: `20000`.
- Calibration must run with zero motor current.
- ADC1 is configured in scan mode for three regular conversions:
  - SQ1: ADC channel 4 / PA4.
  - SQ2: ADC channel 10 / PC0.
  - SQ3: ADC channel 11 / PC1.
- ADC common prescaler: PCLK2 / 4, giving a 21 MHz ADC clock.
- ADC sample time: currently configured with sample-time code `6`, intended as
  a slower/more stable current reading.

TIM1 channel 4 is used as an internal sampling timing point. It is not connected
to a GPIO pin. The TIM1 capture/compare interrupt calls the current-processing
path at a consistent point in the PWM cycle.

Current processing steps:

1. Read IA, IB, and IC ADC samples.
2. Convert each raw ADC value to amps using the calibrated zero-current offset.
3. Accumulate a block of raw current samples.
4. Average the block.
5. Apply a 5th-order Butterworth low-pass IIR filter.
6. Publish filtered current values for UART logging.

The intended filter design is:

- Filter order: 5th-order Butterworth IIR.
- Designed sample rate: 1000 Hz.
- Cutoff frequency: 20 Hz.

## UART Log Format

UART2 prints filtered phase-current values as integer milliamps.

Each normal data line is semicolon-delimited:

```text
IA_mA;IB_mA;IC_mA
```

Example:

```text
-13;61;-950
```

The plotting scripts in `test/` assume this three-column semicolon-delimited
format. If the firmware log format changes, update the scripts in the same
change.

## Existing Analysis Tools

- `test/plot_current_columns.py` plots the firmware's semicolon-delimited
  current log output.
- `test/plot_current_log.py` performs additional offline averaging/filtering of
  current logs.

These scripts are analysis helpers, not embedded unit tests.

## Safety Rules

- Do not connect the PWM outputs to a live gate driver or power stage until the
  PWM signals have been scoped at the MCU pins and gate-driver inputs.
- Confirm high-side/low-side polarity before enabling any bridge hardware.
- Confirm dead time at the gate-driver input pins, not only in code.
- Confirm behavior during reset, startup, debug halt, and firmware fault.
- Add explicit overcurrent/fault handling before using this with a motor under
  meaningful load.
- Current calibration must occur with zero motor current.

## Current Limitations

- There is no closed-loop speed, position, torque, or field-oriented control
  yet.
- PWM duty is initialized to 50 percent and is not currently controlled by a
  motor-control loop.
- Encoder count is configured but is not emitted in the normal UART data line.
- Fault input handling is not specified or implemented.
- Gate-driver enable/disable behavior is not specified.
- Overcurrent shutdown is not implemented.
- The project currently keeps most firmware in one source file.
- The default `README.MD` still appears to contain generic PlatformIO starter
  content and should eventually be replaced with a project quickstart.

## Known Inconsistencies To Resolve

The following items should be cleaned up before treating this spec as final:

- `CURRENT_RAW_SAMPLE_RATE_HZ` is set to `80000U`, but nearby comments describe
  a 20 kHz PWM-synchronized raw sample stream.
- `CURRENT_FILTERED_RATE_HZ` is derived from `CURRENT_RAW_SAMPLE_RATE_HZ`, so it
  does not match comments that describe a 1 kHz filtered-current stream.
- `CURRENT_PRINT_DECIMATION` is set to `20U`; comments should state the actual
  print period that results from the real filtered sample rate.
- Some TIM1 dead-time comments still refer to older 16 MHz / DTG=16 assumptions,
  while the current macro uses 84 ticks at an 84 MHz timer clock.
- Some ADC sample-time comments say 84 cycles, while the configured sample-time
  code and nearby comments refer to 144 cycles.
- `ACS712_ZERO_CURRENT_VOLTAGE` is defined but runtime calibration uses measured
  per-phase zero offsets; clarify whether the constant is obsolete, fallback
  documentation, or intended for a future mode.

## Near-Term Spec Additions

These details should be added once the hardware decisions are settled:

- Exact ACS712 part number and current range.
- Current-sense voltage-divider schematic values and expected zero-current ADC
  voltage.
- Gate-driver part number and input truth table.
- Fault pin mapping and required fault response.
- Motor type, pole count, encoder counts per revolution, and expected supply
  voltage/current limits.
- Intended control mode and loop rates.
- Expected UART capture workflow for producing log files consumed by `test/`.
