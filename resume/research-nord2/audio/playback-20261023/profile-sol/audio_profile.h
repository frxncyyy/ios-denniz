#ifndef NORD2_AUDIO_PROFILE_H
#define NORD2_AUDIO_PROFILE_H
#include <stdint.h>
struct nah_board;
const struct nah_board *nah_board_profile(void);
const struct nah_board *nsp_select(uint16_t first, uint16_t second);
#endif
