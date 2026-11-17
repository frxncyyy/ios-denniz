I'll start by surveying the existing research, predecessors, and the upstream sources.

Still reading the predecessor code (AFE plan, I2C8 transport, native adapter) before writing anything; next I'll check the Android sources for the PLL, I2S and TFA sequences.

No audio code is integrated in canonical yet, so the research candidates are the baseline. Next I'm checking the existing live-control and I2C patterns, then the Android sources.

Now the Android kernel sources. Starting with the TFA98xx driver, since the amplifier sequence is the biggest unknown.

The generic TFA flow depends on the board container for I2S/profile registers. Checking whether a `tfa98xx.cnt` exists anywhere under `research/nord2`.

Looking for where vendor firmware was previously extracted (to locate any `tfa98xx.cnt`), then back to the Android AFE/clock sources.

You've hit your session limit · resets 6:50pm (Europe/Rome)
