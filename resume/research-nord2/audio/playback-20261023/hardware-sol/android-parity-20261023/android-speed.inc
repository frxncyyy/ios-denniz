static int mtk_i2c_calculate_speed(struct mt_i2c *i2c,
	unsigned int clk_src_in_hz,
	unsigned int speed_hz,
	unsigned int *timing_step_cnt,
	unsigned int *timing_sample_cnt)
{
	unsigned int khz;
	unsigned int step_cnt;
	unsigned int sample_cnt;
	unsigned int sclk;
	unsigned int hclk;
	unsigned int max_step_cnt;
	unsigned int sample_div = MAX_SAMPLE_CNT_DIV;
	unsigned int step_div;
	unsigned int min_div;
	unsigned int best_mul;
	unsigned int cnt_mul;

	if (speed_hz > MAX_HS_MODE_SPEED) {
		if (i2c->dev_comp->check_max_freq)
			return -EINVAL;
		max_step_cnt = MAX_HS_STEP_CNT_DIV;
	} else if (speed_hz > MAX_FS_MODE_SPEED) {
		max_step_cnt = MAX_HS_STEP_CNT_DIV;
	} else {
		max_step_cnt = MAX_STEP_CNT_DIV;
	}
	step_div = max_step_cnt;

	/* Find the best combination */
	khz = speed_hz / 1000;
	hclk = clk_src_in_hz / 1000;
	min_div = ((hclk >> 1) + khz - 1) / khz;
	best_mul = MAX_SAMPLE_CNT_DIV * max_step_cnt;
	for (sample_cnt = 1; sample_cnt <= MAX_SAMPLE_CNT_DIV; sample_cnt++) {
		step_cnt = (min_div + sample_cnt - 1) / sample_cnt;
		cnt_mul = step_cnt * sample_cnt;
		if (step_cnt > max_step_cnt)
			continue;
		if (cnt_mul < best_mul) {
			best_mul = cnt_mul;
			sample_div = sample_cnt;
			step_div = step_cnt;
			if (best_mul == min_div)
				break;
		}
	}
	sample_cnt = sample_div;
	step_cnt = step_div;
	sclk = hclk / (2 * sample_cnt * step_cnt);
	if (sclk > khz) {
		dev_dbg(i2c->dev, "%s mode: unsupported speed (%ldkhz)\n",
			(speed_hz > MAX_FS_MODE_SPEED) ? "HS" : "ST/FT",
			(long int)khz);
		return -ENOTSUPP;
	}

	if (i2c->dev_comp->cnt_constraint) {
		if (--sample_cnt) {/* --sample_cnt = 0, setp_cnt needn't -1  */
			step_cnt--;
		}
	} else {
		sample_cnt--;
		step_cnt--;
	}

	*timing_step_cnt = step_cnt;
	*timing_sample_cnt = sample_cnt;

	return 0;
}

static int i2c_set_speed(struct mt_i2c *i2c, unsigned int clk_src_in_hz)
{
	int ret;
	unsigned int step_cnt = 0;
	unsigned int sample_cnt = 0;
	unsigned int l_step_cnt = 0;
	unsigned int l_sample_cnt = 0;
	unsigned int speed_hz;
	unsigned int duty = HALF_DUTY_CYCLE;

	if (i2c->ext_data.isEnable && i2c->ext_data.timing)
		speed_hz = i2c->ext_data.timing;
	else
		speed_hz = i2c->speed_hz;

	if (speed_hz > MAX_FS_PLUS_MODE_SPEED && !i2c->hs_only) {
		/* Set the hign speed mode register */
		ret = mtk_i2c_calculate_speed(i2c, clk_src_in_hz,
			MAX_FS_MODE_SPEED, &l_step_cnt, &l_sample_cnt);
		if (ret < 0)
			return ret;

		ret = mtk_i2c_calculate_speed(i2c, clk_src_in_hz,
			speed_hz, &step_cnt, &sample_cnt);
		if (ret < 0)
			return ret;

		i2c->high_speed_reg = I2C_HS_HOLD_TIME |
			I2C_TIME_DEFAULT_VALUE | I2C_HS_SPEED |
			(sample_cnt & I2C_TIMING_SAMPLE_COUNT_MASK) << 12 |
			((step_cnt - 1) & I2C_TIMING_SAMPLE_COUNT_MASK) << 8;

		i2c->timing_reg =
			(l_sample_cnt & I2C_TIMING_SAMPLE_COUNT_MASK) << 8 |
			(l_step_cnt & I2C_TIMING_STEP_DIV_MASK) << 0;

		if (i2c->dev_comp->set_ltiming) {
			i2c->ltiming_reg = I2C_HS_HOLD_SEL | (l_sample_cnt << 6)
				| (l_step_cnt << 0) |
				(sample_cnt &
					I2C_TIMING_SAMPLE_COUNT_MASK) << 12 |
				((step_cnt + 1) &
					I2C_TIMING_SAMPLE_COUNT_MASK) << 9;
		}
	} else {
		if (speed_hz > I2C_DEFAUT_SPEED
			&& speed_hz <= MAX_FS_MODE_SPEED
			&& i2c->dev_comp->set_ltiming)
			duty = DUTY_CYCLE;

		ret = mtk_i2c_calculate_speed(i2c, clk_src_in_hz,
			(speed_hz * 50 / duty), &step_cnt, &sample_cnt);
		if (ret < 0)
			return ret;

		i2c->timing_reg =
			(sample_cnt & I2C_TIMING_SAMPLE_COUNT_MASK) << 8 |
			(step_cnt & I2C_TIMING_STEP_DIV_MASK) << 0;

		if (i2c->dev_comp->set_ltiming) {
			ret = mtk_i2c_calculate_speed(i2c, clk_src_in_hz,
				(speed_hz * 50 / (100 - duty)),
				&l_step_cnt, &l_sample_cnt);
			if (ret < 0)
				return ret;

			i2c->ltiming_reg =
				(l_sample_cnt &
					I2C_TIMING_SAMPLE_COUNT_MASK) << 6 |
				(l_step_cnt & I2C_TIMING_STEP_DIV_MASK) << 0;
		}
		/* Disable the high speed transaction */
		i2c->high_speed_reg = I2C_TIME_CLR_VALUE;
	}

	return 0;
}
