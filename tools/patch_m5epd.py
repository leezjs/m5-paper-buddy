from pathlib import Path

Import("env")


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text()
    if new in text:
        return
    if old not in text:
        raise RuntimeError(f"expected snippet not found in {path}")
    path.write_text(text.replace(old, new, 1))


lib_root = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") / "M5EPD" / "src"
if not lib_root.exists():
    print(f"[patch_m5epd] skip: {lib_root} does not exist yet")
else:
    replace_once(
        lib_root / "M5EPD_Driver.h",
        '    M5EPD_Driver(int8_t spi_index = VSPI);\n',
        """#if !defined(M5EPD_DEFAULT_SPI_HOST)
#if defined(VSPI)
#define M5EPD_DEFAULT_SPI_HOST VSPI
#else
#define M5EPD_DEFAULT_SPI_HOST FSPI
#endif
#endif
    M5EPD_Driver(int8_t spi_index = M5EPD_DEFAULT_SPI_HOST);
""",
    )

    replace_once(
        lib_root / "M5EPD_Driver.cpp",
        """    } else {
        _epd_spi = new SPIClass(VSPI);
    }
""",
        """    } else {
        _epd_spi = new SPIClass(M5EPD_DEFAULT_SPI_HOST);
    }
""",
    )

    replace_once(
        lib_root / "M5EPD.h",
        """#define M5EPD_MAIN_PWR_PIN   2
#define M5EPD_CS_PIN         15
#define M5EPD_SCK_PIN        14
#define M5EPD_MOSI_PIN       12
#define M5EPD_BUSY_PIN       27
#define M5EPD_MISO_PIN       13
#define M5EPD_EXT_PWR_EN_PIN 5
#define M5EPD_EPD_PWR_EN_PIN 23
#define M5EPD_KEY_RIGHT_PIN  39
#define M5EPD_KEY_PUSH_PIN   38
#define M5EPD_KEY_LEFT_PIN   37
#define M5EPD_BAT_VOL_PIN    35
#define M5EPD_PORTC_W_PIN    19
#define M5EPD_PORTC_Y_PIN    18
#define M5EPD_PORTB_W_PIN    33
#define M5EPD_PORTB_Y_PIN    26
#define M5EPD_PORTA_W_PIN    32
#define M5EPD_PORTA_Y_PIN    25
""",
        """#ifndef M5EPD_MAIN_PWR_PIN
#define M5EPD_MAIN_PWR_PIN   2
#endif
#ifndef M5EPD_CS_PIN
#define M5EPD_CS_PIN         15
#endif
#ifndef M5EPD_SCK_PIN
#define M5EPD_SCK_PIN        14
#endif
#ifndef M5EPD_MOSI_PIN
#define M5EPD_MOSI_PIN       12
#endif
#ifndef M5EPD_BUSY_PIN
#define M5EPD_BUSY_PIN       27
#endif
#ifndef M5EPD_MISO_PIN
#define M5EPD_MISO_PIN       13
#endif
#ifndef M5EPD_EXT_PWR_EN_PIN
#define M5EPD_EXT_PWR_EN_PIN 5
#endif
#ifndef M5EPD_EPD_PWR_EN_PIN
#define M5EPD_EPD_PWR_EN_PIN 23
#endif
#ifndef M5EPD_KEY_RIGHT_PIN
#define M5EPD_KEY_RIGHT_PIN  39
#endif
#ifndef M5EPD_KEY_PUSH_PIN
#define M5EPD_KEY_PUSH_PIN   38
#endif
#ifndef M5EPD_KEY_LEFT_PIN
#define M5EPD_KEY_LEFT_PIN   37
#endif
#ifndef M5EPD_BAT_VOL_PIN
#define M5EPD_BAT_VOL_PIN    35
#endif
#ifndef M5EPD_TOUCH_SDA_PIN
#define M5EPD_TOUCH_SDA_PIN  21
#endif
#ifndef M5EPD_TOUCH_SCL_PIN
#define M5EPD_TOUCH_SCL_PIN  22
#endif
#ifndef M5EPD_TOUCH_INT_PIN
#define M5EPD_TOUCH_INT_PIN  36
#endif
#ifndef M5EPD_SD_SCK_PIN
#define M5EPD_SD_SCK_PIN     14
#endif
#ifndef M5EPD_SD_MISO_PIN
#define M5EPD_SD_MISO_PIN    13
#endif
#ifndef M5EPD_SD_MOSI_PIN
#define M5EPD_SD_MOSI_PIN    12
#endif
#ifndef M5EPD_SD_CS_PIN
#define M5EPD_SD_CS_PIN      4
#endif
#ifndef M5EPD_PORTC_W_PIN
#define M5EPD_PORTC_W_PIN    19
#endif
#ifndef M5EPD_PORTC_Y_PIN
#define M5EPD_PORTC_Y_PIN    18
#endif
#ifndef M5EPD_PORTB_W_PIN
#define M5EPD_PORTB_W_PIN    33
#endif
#ifndef M5EPD_PORTB_Y_PIN
#define M5EPD_PORTB_Y_PIN    26
#endif
#ifndef M5EPD_PORTA_W_PIN
#define M5EPD_PORTA_W_PIN    32
#endif
#ifndef M5EPD_PORTA_Y_PIN
#define M5EPD_PORTA_Y_PIN    25
#endif
""",
    )

    replace_once(
        lib_root / "M5EPD.cpp",
        """#include "M5EPD.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "soc/adc_channel.h"

#define BAT_ADC_CHANNEL   ADC1_GPIO35_CHANNEL
#define BASE_VOLATAGE     3600
#define SCALE             0.5  // 0.78571429
#define ADC_FILTER_SAMPLE 8
""",
        """#include "M5EPD.h"
#include "driver/gpio.h"

#ifndef M5EPD_BATTERY_SCALE_NUM
#define M5EPD_BATTERY_SCALE_NUM 2
#endif
#ifndef M5EPD_BATTERY_SCALE_DEN
#define M5EPD_BATTERY_SCALE_DEN 1
#endif
#ifndef M5EPD_BATTERY_SAMPLES
#define M5EPD_BATTERY_SAMPLES 8
#endif
""",
    )

    replace_once(
        lib_root / "M5EPD.cpp",
        """        SPI.begin(14, 13, 12, 4);
        SD.begin(4, SPI, 20000000);
""",
        """        SPI.begin(M5EPD_SD_SCK_PIN, M5EPD_SD_MISO_PIN, M5EPD_SD_MOSI_PIN,
                  M5EPD_SD_CS_PIN);
        SD.begin(M5EPD_SD_CS_PIN, SPI, 20000000);
""",
    )

    replace_once(
        lib_root / "M5EPD.cpp",
        """        if (TP.begin(21, 22, 36) != ESP_OK) {
""",
        """        if (TP.begin(M5EPD_TOUCH_SDA_PIN, M5EPD_TOUCH_SCL_PIN,
                     M5EPD_TOUCH_INT_PIN) != ESP_OK) {
""",
    )

    replace_once(
        lib_root / "M5EPD.cpp",
        """        Wire.begin(21, 22, (uint32_t)400000U);
""",
        """        Wire.begin(M5EPD_TOUCH_SDA_PIN, M5EPD_TOUCH_SCL_PIN,
                   (uint32_t)400000U);
""",
    )

    replace_once(
        lib_root / "M5EPD.cpp",
        """void M5EPD::BatteryADCBegin() {
    if (_is_adc_start) {
        return;
    }
    _is_adc_start = true;
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(BAT_ADC_CHANNEL, ADC_ATTEN_DB_11);
    _adc_chars = (esp_adc_cal_characteristics_t *)calloc(
        1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12,
                             BASE_VOLATAGE, _adc_chars);
}

/** @brief Read raw data of ADC
 * @retval ADC Raw data
 */
uint32_t M5EPD::getBatteryRaw() {
    return adc1_get_raw(BAT_ADC_CHANNEL);
}

/** @brief Read battery voltage
 * @retval voltage in mV
 */
uint32_t M5EPD::getBatteryVoltage() {
    uint32_t adc_raw_value = 0;
    for (uint16_t i = 0; i < ADC_FILTER_SAMPLE; i++) {
        adc_raw_value += adc1_get_raw(BAT_ADC_CHANNEL);
    }

    adc_raw_value = adc_raw_value / ADC_FILTER_SAMPLE;
    uint32_t voltage =
        (uint32_t)(esp_adc_cal_raw_to_voltage(adc_raw_value, _adc_chars) /
                   SCALE);
    return voltage;
}
""",
        """void M5EPD::BatteryADCBegin() {
    if (_is_adc_start) {
        return;
    }
    _is_adc_start = true;
    analogReadResolution(12);
    analogSetPinAttenuation(M5EPD_BAT_VOL_PIN, ADC_11db);
}

/** @brief Read raw data of ADC
 * @retval ADC Raw data
 */
uint32_t M5EPD::getBatteryRaw() {
    return analogRead(M5EPD_BAT_VOL_PIN);
}

/** @brief Read battery voltage
 * @retval voltage in mV
 */
uint32_t M5EPD::getBatteryVoltage() {
    uint32_t total_millivolts = 0;
    for (uint16_t i = 0; i < M5EPD_BATTERY_SAMPLES; i++) {
        total_millivolts += analogReadMilliVolts(M5EPD_BAT_VOL_PIN);
    }

    total_millivolts /= M5EPD_BATTERY_SAMPLES;
    return (total_millivolts * M5EPD_BATTERY_SCALE_NUM) /
           M5EPD_BATTERY_SCALE_DEN;
}
""",
    )

    replace_once(
        lib_root / "utility" / "In_eSPI.h",
        """#ifdef ESP32
#include "soc/spi_reg.h"
""",
        """#ifdef ESP32
#include "soc/spi_reg.h"
#if CONFIG_IDF_TARGET_ESP32S3
#ifndef SPI_MOSI_DLEN_REG
#define SPI_MOSI_DLEN_REG(i) SPI_MS_DLEN_REG(i)
#endif
#ifndef SPI_USR_MOSI_DBITLEN
#define SPI_USR_MOSI_DBITLEN SPI_MS_DATA_BITLEN
#endif
#ifndef SPI_USR_MOSI_DBITLEN_S
#define SPI_USR_MOSI_DBITLEN_S SPI_MS_DATA_BITLEN_S
#endif
#ifndef VSPIQ_IN_IDX
#define VSPIQ_IN_IDX FSPIQ_IN_IDX
#endif
#ifndef VSPID_OUT_IDX
#define VSPID_OUT_IDX FSPID_OUT_IDX
#endif
#endif
""",
    )

    replace_once(
        lib_root / "utility" / "In_eSPI.h",
        """#ifdef USE_HSPI_PORT
#define SPI_PORT HSPI
#else
#define SPI_PORT VSPI
#endif
""",
        """#ifdef USE_HSPI_PORT
#define SPI_PORT HSPI
#elif defined(VSPI)
#define SPI_PORT VSPI
#else
#define SPI_PORT FSPI
#endif
""",
    )

    replace_once(
        lib_root / "utility" / "In_eSPI.h",
        """#if defined(USE_HSPI_PORT)
    uint8_t port = HSPI;
#else
    uint8_t port = VSPI;
#endif
""",
        """#if defined(USE_HSPI_PORT)
    uint8_t port = HSPI;
#elif defined(VSPI)
    uint8_t port = VSPI;
#else
    uint8_t port = FSPI;
#endif
""",
    )

    print(f"[patch_m5epd] patched {lib_root}")
