Project Wavetrace
Project Wavetrace is a modular, extensible wireless analysis and hacking suite for the ESP32 platform. It provides real-time spectrum analysis, wireless manipulation tools, and a touchscreen interface, making it ideal for hardware enthusiasts, security researchers, and makers.

Features
Multi-Protocol Spectrum Analysis

Real-time RF spectrum visualization using CC1101 (sub-GHz) and NRF24L01+ (2.4GHz) modules.

FFT-based waterfall and area graph displays for detailed signal inspection.

Channel hopping and frequency selection for broad band coverage.

Touchscreen User Interface

Intuitive, icon-driven GUI on a TFT display with full touchscreen support.

Interactive controls for frequency, mode, and tool selection.

On-screen notifications, status bars, and real-time feedback.

Wireless Hacking Suite

WiFi packet monitoring and promiscuous mode analysis.

Bluetooth and BLE device scanning (planned/extendable).

Tools for replay attacks, jamming, and signal spoofing.

Modular codebase for easy addition of new wireless tools.

Hardware Integration & Conflict Management

Optimized SPI bus sharing for TFT, touch controller, CC1101, and NRF24L01+.

I2C and UART peripherals supported for extended functionality.

Careful pin assignment and bus arbitration for reliable multi-module operation.

Profile Management

Save, load, and manage custom signal profiles (frequency, protocol, bit length, value).

On-device keyboard for naming and organizing profiles.

EEPROM-backed storage for persistent configuration.

Robust Testing & Debugging

Automated test scripts for SPI and GUI validation.

Serial debug logging and on-screen error reporting.

Visual battery, WiFi, and SD card status indicators.

Example Use Cases
Wireless spectrum analysis for security research and device debugging.

Signal replay and jamming for penetration testing (with legal/ethical use).

Educational demonstrations of RF protocols and wireless attacks.

Custom tool development for new wireless standards.

Hardware Requirements
ESP32 microcontroller

CC1101 sub-GHz RF transceiver

NRF24L01+ 2.4GHz RF transceiver

TFT display with XPT2046 touchscreen controller

PCF8574 I2C GPIO expander (for button inputs)

Optional: SD card, additional I2C/UART sensors
