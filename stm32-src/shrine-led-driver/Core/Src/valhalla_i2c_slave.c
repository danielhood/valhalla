#include <string.h>
#include "main.h"
#include "valhalla_i2c_slave.h"

#ifndef NO_DEBUG_MAIN
static uint32_t s_uart_log_last_rx_count;
#endif

static I2C_HandleTypeDef *s_hi2c;

/** Mirrors shrine-rfid-reader `static valhallaTag s_last_valhalla_tag_by_board[MFRC522_INTERFACE_MAX_DEVICES]`. */
static valhallaTag s_last_valhalla_tag_by_board[SHRINE_LED_VALHALLA_TAG_MAX];

static uint8_t s_rx_staging[VALHALLA_TAGS_I2C_PAYLOAD_BYTES];
static volatile uint32_t s_rx_cmpl_count;

void valhalla_i2c_slave_init(I2C_HandleTypeDef *hi2c)
{
  s_hi2c = hi2c;
  memset((void *)s_last_valhalla_tag_by_board, 0, sizeof(s_last_valhalla_tag_by_board));
  s_rx_cmpl_count = 0U;
  if (HAL_I2C_EnableListen_IT(hi2c) != HAL_OK)
  {
    Error_Handler();
  }
}

const valhallaTag *valhalla_i2c_get_last_tags(void)
{
  return s_last_valhalla_tag_by_board;
}

uint32_t valhalla_i2c_rx_complete_count(void)
{
  return s_rx_cmpl_count;
}

void valhalla_i2c_slave_poll_uart_log(void)
{
#ifndef NO_DEBUG_MAIN
  uint32_t n;
  uint32_t i;

  n = s_rx_cmpl_count;
  if (n == s_uart_log_last_rx_count)
  {
    return;
  }
  s_uart_log_last_rx_count = n;

  main_debug_print("USART2: I2C ValhallaTag snapshot #%lu (%u bytes)\r\n",
                   (unsigned long)n, (unsigned)VALHALLA_TAGS_I2C_PAYLOAD_BYTES);

  for (i = 0U; i < (uint32_t)SHRINE_LED_VALHALLA_TAG_MAX; i++)
  {
    const valhallaTag *t = &s_last_valhalla_tag_by_board[i];
    char type_c = (t->type != '\0') ? t->type : '?';

    main_debug_print("  [%lu] type=%c camp=\"%s\" color=\"%s\" rune=\"%s\"\r\n",
                     (unsigned long)i, type_c, t->camp, t->color, t->rune);
  }
#endif
}

void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c, uint8_t TransferDirection,
                          uint16_t AddrMatchCode)
{
  (void)AddrMatchCode;

  if (hi2c != s_hi2c || s_hi2c == NULL)
  {
    return;
  }

  if (TransferDirection == I2C_DIRECTION_TRANSMIT)
  {
    if (HAL_I2C_Slave_Seq_Receive_IT(hi2c, s_rx_staging, VALHALLA_TAGS_I2C_PAYLOAD_BYTES, I2C_LAST_FRAME)
        != HAL_OK)
    {
      (void)HAL_I2C_EnableListen_IT(hi2c);
    }
  }
}

void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
  if (hi2c != s_hi2c)
  {
    return;
  }

  memcpy((void *)s_last_valhalla_tag_by_board, s_rx_staging, sizeof(s_last_valhalla_tag_by_board));
  s_rx_cmpl_count++;
}

void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *hi2c)
{
  if (hi2c != s_hi2c)
  {
    return;
  }
  (void)HAL_I2C_EnableListen_IT(hi2c);
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
  if (hi2c != s_hi2c)
  {
    return;
  }
  (void)HAL_I2C_EnableListen_IT(hi2c);
}
