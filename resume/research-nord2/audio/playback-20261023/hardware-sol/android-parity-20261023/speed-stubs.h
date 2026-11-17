#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#define ENOTSUPP 524
#define EINVAL 22
#define dev_dbg(...) ((void)0)
#define I2C_HS_HOLD_TIME (1u<<2)
#define I2C_TIME_DEFAULT_VALUE 1
#define I2C_HS_SPEED 0x80
#define I2C_TIMING_SAMPLE_COUNT_MASK 7
#define I2C_TIMING_STEP_DIV_MASK 63
#define I2C_HS_HOLD_SEL (1u<<15)
#define I2C_TIME_CLR_VALUE 0
struct comp {unsigned check_max_freq,cnt_constraint,set_ltiming;};
struct mt_i2c {struct comp *dev_comp;unsigned speed_hz,hs_only,high_speed_reg,timing_reg,ltiming_reg; struct {unsigned isEnable,timing;}ext_data;};
#define I2C_DEFAUT_SPEED		100000/* hz */
#define MAX_FS_MODE_SPEED		400000/* hz */
#define MAX_FS_PLUS_MODE_SPEED          1000000/* hz */
#define MAX_HS_MODE_SPEED		3400000/* hz */
#define MAX_SAMPLE_CNT_DIV	8
#define MAX_STEP_CNT_DIV		64
#define MAX_HS_STEP_CNT_DIV	8
#define HALF_DUTY_CYCLE		50
#define DUTY_CYCLE				45
