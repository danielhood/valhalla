/**
 * Valhalla tag layout shared with shrine-rfid-reader (Core/Src/main.c).
 */

#ifndef VALHALLA_TAG_H
#define VALHALLA_TAG_H

#include <stdint.h>

#ifndef SHRINE_LED_VALHALLA_TAG_MAX
#define SHRINE_LED_VALHALLA_TAG_MAX 4U
#endif

typedef struct
{
  char type;
  char camp[3];
  char color[3];
  char rune[3];
} valhallaTag;

#if defined(__GNUC__)
_Static_assert(sizeof(valhallaTag) == 10U, "valhallaTag layout must stay in sync with RFID reader");
#endif

#define VALHALLA_TAGS_I2C_PAYLOAD_BYTES ((uint16_t)(sizeof(valhallaTag) * (SHRINE_LED_VALHALLA_TAG_MAX)))

#endif /* VALHALLA_TAG_H */
