/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "driver_mfrc522_basic.h"
#include "driver_mfrc522_interface.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define MSG_MAX 256
/*
 * Optional readers 1..3: same SPI1, separate CS. Define before build, e.g. in flags:
 *   -DRC522_READER1_CS_PORT=GPIOB -DRC522_READER1_CS_PIN=GPIO_PIN_0
 * Or add #define lines below. Port NULL = slot unused.
 */

//  #define RC522_READER1_CS_PORT GPIOA
//  #define RC522_READER1_CS_PIN GPIO_PIN_3

#ifndef RC522_READER0_CS_PORT
#define RC522_READER0_CS_PORT GPIOA
#endif
#ifndef RC522_READER0_CS_PIN
#define RC522_READER0_CS_PIN GPIO_PIN_4
#endif

#ifndef RC522_READER1_CS_PORT
#define RC522_READER1_CS_PORT NULL
#endif
#ifndef RC522_READER1_CS_PIN
#define RC522_READER1_CS_PIN 0U
#endif

#ifndef RC522_READER2_CS_PORT
#define RC522_READER2_CS_PORT NULL
#endif
#ifndef RC522_READER2_CS_PIN
#define RC522_READER2_CS_PIN 0U
#endif

#ifndef RC522_READER3_CS_PORT
#define RC522_READER3_CS_PORT NULL
#endif
#ifndef RC522_READER3_CS_PIN
#define RC522_READER3_CS_PIN 0U
#endif

/* User TLV area (page 4+); MF READ returns 16 bytes (4 pages) per command */
#define NTAG_USER_READ_MAX_BYTES 256U
#define NTAG_TEXT_OUT_MAX        240U
/* Last page address that can start a 4-page read (252..255) */
#define NTAG_LAST_READ_START_PAGE 252U
/* Valhalla scan result populated from NDEF content */
typedef struct
{
  char type;         /* single character for object type, from CSV first field */
  char camp[3];      /* up to 2 chars + NUL */
  char color[3];     /* up to 2 chars + NUL */
  char rune[3];      /* up to 2 chars + NUL */
} valhallaTag;

typedef struct
{
  uint8_t index;
  GPIO_TypeDef *cs_port;
  uint16_t cs_pin;
  uint8_t present;
  uint32_t scan_count;
  uint32_t scan_ok_count;
} rc522_device_t;
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

char msg[MSG_MAX];
static uint8_t s_rc522_log_reader = 0xFFU;
static rc522_device_t s_rc522_devices[MFRC522_INTERFACE_MAX_DEVICES];

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */

uint8_t readATQA_then_halt(void);

uint8_t readNTAGPage(uint8_t page, uint8_t *buf, uint8_t *out_len);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static uint8_t rc522_boards_register_all(void);
static rc522_device_t *rc522_find_first_present_device(void);
static uint8_t rc522_select(rc522_device_t *dev);
static void rc522_log_reader_clear(void);
static uint8_t rc522_scan(rc522_device_t *dev, valhallaTag *out_tag);
static uint8_t scanValhallaTag(valhallaTag *out_tag);
static void main_debug_vprint_with_device_id(int showDeviceId, const char *const fmt, va_list args);

void main_debug_print(const char *const fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  main_debug_vprint_with_device_id(1, fmt, args);
  va_end(args);
}

void main_debug_print_with_device_id(int showDeviceId, const char *const fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  main_debug_vprint_with_device_id(showDeviceId, fmt, args);
  va_end(args);
}

static void main_debug_vprint_with_device_id(int showDeviceId, const char *const fmt, va_list args)
{
#ifndef NO_DEBUG_MAIN
    char str[256];
    uint16_t len;
    int off = 0;

    memset((char *)str, 0, sizeof(char) * 256);
    if (s_rc522_log_reader != 0xFFU && showDeviceId == 1)
    {
        off = snprintf(str, sizeof(str), "[R%u] ", (unsigned int)s_rc522_log_reader);
        if (off < 0)
        {
            off = 0;
        }
        if (off >= (int)sizeof(str))
        {
            off = (int)sizeof(str) - 1;
        }
    }

    (void)vsnprintf((char *)str + off, sizeof(str) - (size_t)off, (char const *)fmt, args);

    len = (uint16_t)strlen((char *)str);

    HAL_UART_Transmit(&huart2, (uint8_t*)str, len, HAL_MAX_DELAY);
#endif

}

void main_debug_print_hex(const uint8_t *buf, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++)
    {
      main_debug_print_with_device_id(0, "0x%02X ", buf[i]);
    }
}

static uint8_t rc522_boards_register_all(void)
{
    static const struct
    {
        GPIO_TypeDef *cs_port;
        uint16_t cs_pin;
    } cs_table[MFRC522_INTERFACE_MAX_DEVICES] = {
        { RC522_READER0_CS_PORT, RC522_READER0_CS_PIN },
        { RC522_READER1_CS_PORT, RC522_READER1_CS_PIN },
        { RC522_READER2_CS_PORT, RC522_READER2_CS_PIN },
        { RC522_READER3_CS_PORT, RC522_READER3_CS_PIN },
    };
    uint8_t i;

    memset(s_rc522_devices, 0, sizeof(s_rc522_devices));

    for (i = 0U; i < MFRC522_INTERFACE_MAX_DEVICES; i++)
    {
        s_rc522_devices[i].index = i;
        s_rc522_devices[i].cs_port = cs_table[i].cs_port;
        s_rc522_devices[i].cs_pin = cs_table[i].cs_pin;

        if (cs_table[i].cs_port == NULL)
        {
            continue;
        }
        if (mfrc522_interface_spi_register_device(i, cs_table[i].cs_port, cs_table[i].cs_pin) != 0)
        {
            main_debug_print("main: rc522_boards_register_all: register reader %u failed.\r\n", i);
            return 1;
        }
        s_rc522_devices[i].present = 1U;
        main_debug_print("main: rc522_boards_register_all: reader %u CS registered.\r\n", i);
    }

    /* After registration, force all configured CS pins high (all readers deselected). */
    for (i = 0U; i < MFRC522_INTERFACE_MAX_DEVICES; i++)
    {
        if (s_rc522_devices[i].present == 0U)
        {
            continue;
        }
        HAL_GPIO_WritePin(cs_table[i].cs_port, cs_table[i].cs_pin, GPIO_PIN_SET);
    }
    main_debug_print("main: rc522_boards_register_all: all configured CS pins set high.\r\n");

    return 0;
}

static rc522_device_t *rc522_find_first_present_device(void)
{
    uint8_t i;

    for (i = 0U; i < MFRC522_INTERFACE_MAX_DEVICES; i++)
    {
        if (s_rc522_devices[i].present != 0U)
        {
            return &s_rc522_devices[i];
        }
    }

    return NULL;
}

static uint8_t rc522_select(rc522_device_t *dev)
{
    if (dev == NULL || dev->index >= MFRC522_INTERFACE_MAX_DEVICES || dev->present == 0U)
    {
        return 1;
    }
    if (mfrc522_interface_spi_select_device(dev->index) != 0)
    {
        return 1;
    }
    s_rc522_log_reader = dev->index;
    return 0;
}

static void rc522_log_reader_clear(void)
{
    s_rc522_log_reader = 0xFFU;
}

static uint8_t rc522_scan(rc522_device_t *dev, valhallaTag *out_tag)
{
    if (dev == NULL || out_tag == NULL)
    {
        return 1;
    }
    if (rc522_select(dev) != 0)
    {
        return 1;
    }

    dev->scan_count++;
    if (scanValhallaTag(out_tag) != 0)
    {
        return 1;
    }
    dev->scan_ok_count++;

    return 0;
}


uint16_t calcCRC(uint8_t* buf, uint16_t len)
{
  // Calc CRC bytes (note this can be done on the rc522)
  uint16_t crc = 0x6363; // Preset value for ISO 14443-3
  for (uint8_t i = 0; i < len; i++) {
      uint8_t ch = buf[i] ^ (uint8_t)(crc & 0x00FF);
      ch = ch ^ (ch << 4);
      crc = (crc >> 8) ^ ((uint16_t)ch << 8) ^ ((uint16_t)ch << 3) ^ (ch >> 4);
  }

  buf[len] = (uint8_t)(crc & 0xFF);        // CRC Low byte
  buf[len+1] = (uint8_t)((crc >> 8) & 0xFF); // CRC High byte

  return crc;
}

static void a_callback(uint16_t type)
{
    switch (type)
    {
        case MFRC522_INTERRUPT_MFIN_ACT :
        {
            main_debug_print("mfrc522: irq mfin act.\n");

            break;
        }
        case MFRC522_INTERRUPT_CRC :
        {
            break;
        }
        case MFRC522_INTERRUPT_TX :
        {
            break;
        }
        case MFRC522_INTERRUPT_RX :
        {
            break;
        }
        case MFRC522_INTERRUPT_IDLE :
        {
            break;
        }
        case MFRC522_INTERRUPT_HI_ALERT :
        {
            break;
        }
        case MFRC522_INTERRUPT_LO_ALERT :
        {
            break;
        }
        case MFRC522_INTERRUPT_ERR :
        {
            main_debug_print("mfrc522: irq err.\n");

            break;
        }
        case MFRC522_INTERRUPT_TIMER :
        {
            main_debug_print("mfrc522: irq timer.\n");

            break;
        }
        default :
        {
            main_debug_print("mfrc522: irq unknown code.\n");

            break;
        }
    }
}

void getInfo(void)
{
  mfrc522_info_t info;

  /* print mfrc522 info */
  mfrc522_info(&info);
  main_debug_print("mfrc522: chip is %s.\n", info.chip_name);
  main_debug_print("mfrc522: manufacturer is %s.\n", info.manufacturer_name);
  main_debug_print("mfrc522: interface is %s.\n", info.interface);
  main_debug_print("mfrc522: driver version is %d.%d.\n", info.driver_version / 1000, (info.driver_version % 1000) / 100);
  main_debug_print("mfrc522: min supply voltage is %0.1fV.\n", info.supply_voltage_min_v);
  main_debug_print("mfrc522: max supply voltage is %0.1fV.\n", info.supply_voltage_max_v);
  main_debug_print("mfrc522: max current is %0.2fmA.\n", info.max_current_ma);
  main_debug_print("mfrc522: max temperature is %0.1fC.\n", info.temperature_max);
  main_debug_print("mfrc522: min temperature is %0.1fC.\n", info.temperature_min);
}


uint8_t initMfrc522()
{
  uint8_t addr = 0x00;
  rc522_device_t *first;

  main_debug_print("main: Initializing mfrc522 as SPI...\r\n");

  /* Phase 1: register every configured CS, then select first reader for basic_init. */
  if (rc522_boards_register_all() != 0)
  {
      return 1;
  }
  first = rc522_find_first_present_device();
  if (first == NULL)
  {
      main_debug_print("main: no RC522 readers configured (all CS ports NULL).\r\n");
      return 1;
  }
  if (rc522_select(first) != 0)
  {
      main_debug_print("main: rc522_select(first) failed.\r\n");
      return 1;
  }

  /* basic int */
  uint8_t res = mfrc522_basic_init(MFRC522_INTERFACE_SPI, addr, a_callback);
  if (res != 0)
  {
      main_debug_print("main: mfrc522_basic_init failed.\r\n");
      return 1;
  }

  return 0;
}

uint8_t getRandom(void)
{
  uint8_t res;
  uint8_t buf[25];

  main_debug_print("main: Getting random generated id...\r\n");

  /* get the random */
  memset(buf, 0, sizeof(buf));
  res = mfrc522_basic_generate_random(buf);
  if (res != 0)
  {
      main_debug_print("main: mfrc522_basic_generate_random failed.\r\n");
      return 1;
  }

  /* output */
  main_debug_print("main: Received random ID:\r\n\t");
  main_debug_print_hex(buf, 25);
  main_debug_print_with_device_id(0, "\r\n");

  return 0;
}

void getVersion(void)
{
	uint8_t res;
	uint8_t id = 0;
	uint8_t version = 0;

  main_debug_print("main: Getting version..\r\n");

  res = mfrc522_get_vesion(&id, &version);
  if (res != 0)
  {
      main_debug_print("main: mfrc522_get_vesion failed.\r\n");
      return;
  }

  main_debug_print("id: %d  version: %d\r\n", id, version);

}

uint8_t readATQA()
{
  uint8_t res;
  uint8_t command = 0x26; // receive
  uint8_t buf[64];
  uint8_t out_len;


  main_debug_print("main: Starting RF...\r\n");
  res = mfrc522_basic_start_rf();
  if (res != 0)
  {
    main_debug_print("main: Starting RF failed.\r\n");
    return 1;
  }
  main_debug_print("main: RF started.\r\n");

  HAL_Delay(500);

  main_debug_print("main: Reading ATQA...\r\n");

  memset(buf, 0, sizeof(buf));
  out_len = sizeof(buf);
  res = mfrc522_basic_transceiver(&command, 1, buf, &out_len); // out_len will be reduced if less data is returned; will not return more than initial value of out_len
  if (res != 0 || out_len == 0)
  {
      main_debug_print("main: mfrc522_basic_transceiver failed.\r\n");
      return 1;
  }

  main_debug_print("main: Received %d bytes of ATQA data: ", out_len);
  main_debug_print_hex(buf, out_len);
  main_debug_print_with_device_id(0, "\r\n");

  return 0;

}

/**
 * ISO 14443-3: WUPA (wake, expect ATQA).
 */
 uint8_t picc_wake(void)
 {
  uint8_t res;
  uint8_t buf[64];
  uint8_t out_len;
  uint8_t wupa = 0x52;

  main_debug_print("main: picc_wake: WUPA...\r\n");
  memset(buf, 0, sizeof(buf));
  out_len = sizeof(buf);
  res = mfrc522_basic_transceiver(&wupa, 1, buf, &out_len);
  if (res != 0 || out_len == 0)
  {
    main_debug_print("main: picc_wake: WUPA failed (expected ATQA bytes).\r\n");
    return 1;
  }

  main_debug_print("main: picc_wake: WUPA response (%u bytes): ", out_len);
  main_debug_print_hex(buf, out_len);
  main_debug_print_with_device_id(0, "\r\n");

  return 0;
}

/**
 * ISO 14443-3: HLTA (halt)
 * HLTA success is silent (no PICC bytes); do not treat out_len==0 as failure for HLTA.
 * Some PICCs only accept HLTA in ACTIVE after select; if HLTA fails, try after full anticollision/select.
 */
uint8_t picc_halt(void)
{
  uint8_t res;
  uint8_t buf[64];
  uint8_t out_len;
  uint8_t hlta[4];

  hlta[0] = 0x50;
  hlta[1] = 0x00;
  calcCRC(hlta, 2);

  main_debug_print("main: picc_halt: HLTA...\r\n");
  memset(buf, 0, sizeof(buf));
  out_len = sizeof(buf);
  res = mfrc522_basic_transceiver(hlta, 4, buf, &out_len);
  if (res != 0)
  {
    main_debug_print("main: picc_halt: HLTA transceive failed.\r\n");
    return 1;
  }
  main_debug_print("main: picc_halt: HLTA done (out_len=%u, expect 0).\r\n", out_len);


  main_debug_print("main: picc_halt: Stopping RF...\r\n");
  res = mfrc522_basic_stop_rf();
  if (res != 0)
  {
    main_debug_print("main: picc_halt: Stopping RF failed.\r\n");
    return 1;
  }
  main_debug_print("main: picc_halt: RF stopped.\r\n");

  return 0;
}


uint8_t readID(void)
{
  uint8_t res;
  uint8_t commandCL1[] = { 0x93, 0x20 };  // mifare anti-coll
  uint8_t commandCL1ACK[] = { 0x93, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00 };  // mifare anti-coll
  uint8_t commandCL2[] = { 0x95, 0x20 };  // mifare anti-col2
  uint8_t commandCL2ACK[] = { 0x95, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00 };  // mifare anti-coll
  uint8_t buf[64];
  uint8_t out_len;

  uint8_t fullId[7];

  if (0 != readATQA()) {
    return 1;
  }

  main_debug_print("main: Reading ID (CL1)...\r\n");

  memset(buf, 0, sizeof(buf));
  out_len = sizeof(buf);
  res = mfrc522_basic_transceiver(commandCL1, 2, buf, &out_len); // out_len will be reduced if less data is returned; will not return more than initial value of out_len
  if (res != 0 || out_len == 0)
  {
      main_debug_print("main: mfrc522_basic_transceiver failed.\r\n");
      return 1;
  }

  main_debug_print("main: Received %d bytes of ID (CL1) data: ", out_len);
  main_debug_print_hex(buf, out_len);
  main_debug_print_with_device_id(0, "\r\n");

  // Copy first 3 bytes of ID
  fullId[0] = buf[1];
  fullId[1] = buf[2];
  fullId[2] = buf[3];

  main_debug_print("main: ACK ID (CL1)...\r\n");

  commandCL1ACK[2] = buf[0];
  commandCL1ACK[3] = buf[1];
  commandCL1ACK[4] = buf[2];
  commandCL1ACK[5] = buf[3];
  commandCL1ACK[6] = buf[4];

  calcCRC(commandCL1ACK, 7);

  memset(buf, 0, sizeof(buf));
  out_len = sizeof(buf);

  res = mfrc522_basic_transceiver(commandCL1ACK, 9, buf, &out_len); // out_len will be reduced if less data is returned; will not return more than initial value of out_len
  if (res != 0 || out_len == 0)
  {
      main_debug_print("main: mfrc522_basic_transceiver failed.\r\n");
      return 1;
  }

  main_debug_print("main: Received %d bytes of ACK ID (CL1) data: ", out_len);
  main_debug_print_hex(buf, out_len);
  main_debug_print_with_device_id(0, "\r\n");

  main_debug_print("main: Reading ID (CL2)...\r\n");

  memset(buf, 0, sizeof(buf));
  out_len = sizeof(buf);
  res = mfrc522_basic_transceiver(commandCL2, 2, buf, &out_len); // out_len will be reduced if less data is returned; will not return more than initial value of out_len
  if (res != 0 || out_len == 0)
  {
      main_debug_print("main: mfrc522_basic_transceiver failed.\r\n");
      return 1;
  }

  main_debug_print("main: Received %d bytes of ID (CL2) data: ", out_len);
  main_debug_print_hex(buf, out_len);
  main_debug_print_with_device_id(0, "\r\n");

  // Copy remaining 5 bytes of ID
  fullId[3] = buf[0];
  fullId[4] = buf[1];
  fullId[5] = buf[2];
  fullId[6] = buf[3];


  main_debug_print("main: ACK ID (CL2)...\r\n");

  commandCL2ACK[2] = buf[0];
  commandCL2ACK[3] = buf[1];
  commandCL2ACK[4] = buf[2];
  commandCL2ACK[5] = buf[3];
  commandCL2ACK[6] = buf[4];

  calcCRC(commandCL2ACK, 7);

  memset(buf, 0, sizeof(buf));
  out_len = sizeof(buf);

  res = mfrc522_basic_transceiver(commandCL2ACK, 9, buf, &out_len); // out_len will be reduced if less data is returned; will not return more than initial value of out_len
  if (res != 0 || out_len == 0)
  {
      main_debug_print("main: mfrc522_basic_transceiver failed.\r\n");
      return 1;
  }

  main_debug_print("main: Received %d bytes of ACK ID (CL1) data: ", out_len);
  main_debug_print_hex(buf, out_len);
  main_debug_print_with_device_id(0, "\r\n");

  main_debug_print("main: Full ID: ");
  main_debug_print_hex(fullId, sizeof(fullId));
  main_debug_print_with_device_id(0, "\r\n\r\n\r\n");

  return 0;
}

/**
 * Ultralight/NTAG PICC read (0x30 + page + CRC_A). Call after anticollision/select (e.g. readID).
 * @param page  NTAG page address
 * @param buf   receive buffer; *out_len is max on entry, actual count on success
 */
uint8_t readNTAGPage(uint8_t page, uint8_t *buf, uint8_t *out_len)
{
  uint8_t res;
  uint8_t cmd[4];

  cmd[0] = 0x30; /* PICC_CMD_MF_READ */
  cmd[1] = page;
  calcCRC(cmd, 2);

  memset(buf, 0, *out_len);
  res = mfrc522_basic_transceiver(cmd, 4, buf, out_len);
  if (res != 0 || *out_len == 0)
  {
    main_debug_print("main: readNTAGPage failed.\r\n");
    return 1;
  }

  return 0;
}

/**
 * Read user TLV area in 16-byte MF READ blocks until Terminator TLV (0xFE 0x00) or buffer full.
 */
static uint8_t ntag_read_user_memory(uint8_t *mem, uint16_t max_len, uint16_t *filled)
{
  uint16_t n = 0;
  uint16_t page;

  for (page = 4; n + 16U <= max_len && page <= NTAG_LAST_READ_START_PAGE; page += 4U)
  {
    uint8_t chunk[16];
    uint8_t clen = sizeof(chunk);

    if (readNTAGPage((uint8_t)page, chunk, &clen) != 0)
    {
      return 1;
    }

    {
      uint8_t to_copy = (clen < 16U) ? clen : 16U;
      memcpy(mem + n, chunk, to_copy);
      n += to_copy;
    }

    if (clen < 16U)
    {
      break;
    }

    for (uint16_t j = 0U; j + 1U < n; j++)
    {
      if (mem[j] == 0xFEU && mem[j + 1U] == 0x00U)
      {
        n = j + 2U;
        *filled = n;
        return 0;
      }
    }
  }

  *filled = n;
  return 0;
}

/**
 * Walk Type 2 TLVs; return first NDEF Message (0x03) value.
 */
static int ntag_find_ndef_message(const uint8_t *mem, uint16_t len,
                                  const uint8_t **ndef_out, uint16_t *ndef_len_out)
{
  uint16_t i = 0;

  while (i + 2U <= len)
  {
    uint8_t t = mem[i];

    if (t == 0xFEU)
    {
      return -1;
    }

    if (t == 0x00U)
    {
      i += 2U;
      continue;
    }

    if (i + 1U >= len)
    {
      return -1;
    }

    {
      uint32_t L;
      uint16_t voff;

      if (mem[i + 1U] == 0xFFU)
      {
        if (i + 5U > len)
        {
          return -1;
        }
        L = ((uint32_t)mem[i + 2U] << 16) | ((uint32_t)mem[i + 3U] << 8) | (uint32_t)mem[i + 4U];
        voff = i + 5U;
      }
      else
      {
        L = mem[i + 1U];
        voff = i + 2U;
      }

      if (t == 0x03U)
      {
        if (voff + L > len)
        {
          return -1;
        }
        *ndef_out = mem + voff;
        *ndef_len_out = (uint16_t)L;
        return 0;
      }

      if (voff + L > len)
      {
        return -1;
      }
      i = voff + (uint16_t)L;
    }
  }

  return -1;
}

/**
 * First NFC Forum well-known Text record ('T') in an NDEF message; UTF-8 payload only.
 */
static int ntag_parse_first_text_record(const uint8_t *ndef, uint16_t ndef_len,
                                        char *text_out, uint16_t text_cap,
                                        char *lang_out, uint16_t lang_cap)
{
  uint16_t i = 0;

  if (lang_out != NULL && lang_cap > 0U)
  {
    lang_out[0] = '\0';
  }

  while (i + 3U <= ndef_len)
  {
    uint8_t flags = ndef[i];
    uint8_t tnf = flags & 0x07U;
    uint8_t sr = (flags >> 4) & 1U;
    uint8_t il = (flags >> 3) & 1U;
    uint8_t type_len = ndef[i + 1U];
    uint32_t plen;
    uint16_t pos = i + 2U;

    if (sr != 0U)
    {
      if (pos >= ndef_len)
      {
        return -1;
      }
      plen = ndef[pos];
      pos++;
    }
    else
    {
      if (pos + 4U > ndef_len)
      {
        return -1;
      }
      plen = ((uint32_t)ndef[pos] << 24) | ((uint32_t)ndef[pos + 1U] << 16)
           | ((uint32_t)ndef[pos + 2U] << 8) | (uint32_t)ndef[pos + 3U];
      pos += 4U;
    }

    {
      uint8_t id_len = 0U;
      if (il != 0U)
      {
        if (pos >= ndef_len)
        {
          return -1;
        }
        id_len = ndef[pos++];
      }

      if (pos + type_len > ndef_len)
      {
        return -1;
      }

      {
        const uint8_t *type_ptr = ndef + pos;
        pos += type_len;
        if (pos + id_len > ndef_len)
        {
          return -1;
        }
        pos += id_len;
        if (pos + plen > ndef_len)
        {
          return -1;
        }

        {
          const uint8_t *payload = ndef + pos;
          pos += (uint16_t)plen;

          if (tnf == 0x01U && type_len == 1U && type_ptr[0] == (uint8_t)'T')
          {
            if (plen == 0U)
            {
              return -1;
            }

            {
              uint8_t status = payload[0];
              uint8_t lang_len = status & 0x3FU;

              if ((status & 0x80U) != 0U)
              {
                /* UTF-16; skip, try next record */
                i = pos;
                continue;
              }

              if (plen < (uint32_t)(1U + lang_len))
              {
                return -1;
              }

              if (lang_out != NULL && lang_cap > 0U)
              {
                uint16_t copy = lang_len;
                if (copy >= lang_cap)
                {
                  copy = (uint16_t)(lang_cap - 1U);
                }
                memcpy(lang_out, payload + 1U, copy);
                lang_out[copy] = '\0';
              }

              {
                uint16_t text_len = (uint16_t)(plen - 1U - (uint32_t)lang_len);
                if (text_len + 1U > text_cap)
                {
                  return -1;
                }
                memcpy(text_out, payload + 1U + lang_len, text_len);
                text_out[text_len] = '\0';
              }

              return 0;
            }
          }
        }

        i = pos;
      }
    }
  }

  return -1;
}

static void trim_ascii(char *s)
{
  size_t len;

  /* trim leading whitespace */
  while ((s[0] == ' ') || (s[0] == '\t') || (s[0] == '\r') || (s[0] == '\n'))
  {
    memmove(s, s + 1, strlen(s));
  }

  len = strlen(s);
  while (len > 0U)
  {
    char c = s[len - 1U];
    if ((c == ' ') || (c == '\t') || (c == '\r') || (c == '\n'))
    {
      s[len - 1U] = '\0';
      len--;
      continue;
    }
    break;
  }
}

static int parse_valhalla_csv_text(const char *csv_text_in, valhallaTag *out_tag)
{
  char csv[NTAG_TEXT_OUT_MAX + 1U];
  char *c1;
  char *c2;
  char *c3;

  if ((csv_text_in == NULL) || (out_tag == NULL))
  {
    return -1;
  }

  memset(out_tag, 0, sizeof(*out_tag));
  strncpy(csv, csv_text_in, sizeof(csv) - 1U);
  csv[sizeof(csv) - 1U] = '\0';

  c1 = strchr(csv, ',');
  if (c1 == NULL)
  {
    return -1;
  }
  c2 = strchr(c1 + 1, ',');
  if (c2 == NULL)
  {
    return -1;
  }
  c3 = strchr(c2 + 1, ',');
  if (c3 == NULL)
  {
    return -1;
  }

  *c1 = '\0';
  *c2 = '\0';
  *c3 = '\0';

  trim_ascii(csv);
  trim_ascii(c1 + 1U);
  trim_ascii(c2 + 1U);
  trim_ascii(c3 + 1U);

  if (strlen(csv) != 1U)
  {
    return -1;
  }
  out_tag->type = csv[0];

  if (strlen(c1 + 1U) == 0U || strlen(c1 + 1U) > 2U)
  {
    return -1;
  }
  if (strlen(c2 + 1U) == 0U || strlen(c2 + 1U) > 2U)
  {
    return -1;
  }
  if (strlen(c3 + 1U) == 0U || strlen(c3 + 1U) > 2U)
  {
    return -1;
  }

  strncpy(out_tag->camp, c1 + 1U, sizeof(out_tag->camp) - 1U);
  out_tag->camp[sizeof(out_tag->camp) - 1U] = '\0';

  strncpy(out_tag->color, c2 + 1U, sizeof(out_tag->color) - 1U);
  out_tag->color[sizeof(out_tag->color) - 1U] = '\0';

  strncpy(out_tag->rune, c3 + 1U, sizeof(out_tag->rune) - 1U);
  out_tag->rune[sizeof(out_tag->rune) - 1U] = '\0';

  return 0;
}

static uint8_t scanValhallaTag(valhallaTag *out_tag)
{
  uint8_t mem[NTAG_USER_READ_MAX_BYTES];
  uint16_t filled = 0;
  const uint8_t *ndef = NULL;
  uint16_t ndef_len = 0;
  char text[NTAG_TEXT_OUT_MAX + 1U];
  char lang[8];

  if (out_tag == NULL)
  {
    return 1;
  }

  memset(mem, 0, sizeof(mem));

  /* NTAG read requires ATQA + anticollision + cascade select first */
  if (readID() != 0)
  {
    return 1;
  }

  main_debug_print("main: Reading NTAG user memory (TLV / NDEF)...\r\n");
  if (ntag_read_user_memory(mem, (uint16_t)sizeof(mem), &filled) != 0)
  {
    return 1;
  }

  main_debug_print("main: Raw user area (%u bytes): ", filled);
  main_debug_print_hex(mem, filled);
  main_debug_print_with_device_id(0, "\r\n");

  if (ntag_find_ndef_message(mem, filled, &ndef, &ndef_len) != 0)
  {
    main_debug_print("main: No NDEF Message TLV (0x03) or incomplete data.\r\n");
    return 1;
  }

  main_debug_print("main: NDEF message length %u bytes.\r\n", ndef_len);

  if (ntag_parse_first_text_record(ndef, ndef_len, text, NTAG_TEXT_OUT_MAX, lang, sizeof(lang)) != 0)
  {
    main_debug_print("main: No NFC Forum Text record ('T') in NDEF message.\r\n");
    return 1;
  }

  main_debug_print("main: NDEF Text language: \"%s\"\r\n", lang);
  main_debug_print("main: NDEF Text payload: \"%s\"\r\n", text);

  if (parse_valhalla_csv_text(text, out_tag) != 0)
  {
    main_debug_print("main: Valhalla text is not compliant (expected: type,camp,color,rune with field limits). Ignoring scan.\r\n");
    return 1;
  }

  return 0;
}

uint8_t readNTAG(void)
{
  rc522_device_t *dev;
  valhallaTag tag;

  if (s_rc522_log_reader == 0xFFU || s_rc522_log_reader >= MFRC522_INTERFACE_MAX_DEVICES)
  {
    main_debug_print("main: readNTAG: no active reader selected.\r\n");
    return 1;
  }
  dev = &s_rc522_devices[s_rc522_log_reader];

  memset(&tag, 0, sizeof(tag));
  if (rc522_scan(dev, &tag) != 0)
  {
    return 1;
  }

  main_debug_print("main: ValhallaTag => type='%c', camp=\"%s\", color=\"%s\", rune=\"%s\"",
                    tag.type, tag.camp, tag.color, tag.rune);
  main_debug_print_with_device_id(0, "\r\n\r\n\r\n");

  return 0;
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */

  snprintf(msg, MSG_MAX, "Startup\r\n");
  HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);

  getInfo();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (0 != initMfrc522()) {
    // Retry init every 5 seconds
    HAL_Delay(5000);
  }

  int i = 0;
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    uint8_t r;

    HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_3);
    HAL_Delay(100);
    HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_3);
    HAL_Delay(1000);

    snprintf(msg, MSG_MAX, "\r\nLoop: %d\r\n", ++i);
    HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);

//    getVersion();

//    getRandom();

//    readATQA();

//    readID();

    /* Round-robin: each configured reader gets a scan on the shared SPI bus. */
    for (r = 0U; r < MFRC522_INTERFACE_MAX_DEVICES; r++)
    {
      rc522_device_t *dev = &s_rc522_devices[r];
      valhallaTag tag;

      if (dev->present == 0U)
      {
        continue;
      }
      memset(&tag, 0, sizeof(tag));
      if (rc522_scan(dev, &tag) != 0)
      {
        main_debug_print("main: loop: rc522_scan(reader=%u) failed.\r\n", dev->index);
        continue;
      }

      main_debug_print("main: ValhallaTag => type='%c', camp=\"%s\", color=\"%s\", rune=\"%s\"",
                      tag.type, tag.camp, tag.color, tag.rune);
      main_debug_print_with_device_id(0, "\r\n");

      (void)picc_halt();
    }
    rc522_log_reader_clear();

  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSE|RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 16;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable MSI Auto calibration
  */
  HAL_RCCEx_EnableMSIPLLMode();
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_8, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : PA3 PA4 PA8 */
  GPIO_InitStruct.Pin = GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : LD3_Pin */
  GPIO_InitStruct.Pin = LD3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD3_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();

  /* basic deinit */
  mfrc522_basic_deinit();

  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
