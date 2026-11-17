# Parent review notes (candidate still being written)

The external Opus task is authorized for private analysis/candidate code only.
Its files have NOT been integrated, built, tested or approved for a phone test.

Early inspection of audio_id.h/c identifies GPIO I2C8 takeover and recovery
pulses, despite the gauge/amplifier register operations being read-only.
The same bus carries DDR and UFS supply regulators. Before promotion, review
controller-idle/ownership proof, initial line state, every uncertain commit,
restoration semantics and whether recovery clock pulses could finish an
unknown transaction to another slave. Prefer refusing a busy/stuck bus over
blind recovery. Check reset/power prerequisites and actual selected amplifier
variant; a source config option alone is not live identification.

Do not merge this candidate into the current ROM/battery image. Review the
completed AUDIO-REPORT/INTEGRATION and source before running its host tests;
then request a narrow revision or implement one. No audio hardware success
or iOS audio service exists yet. Current core task remains Wi-Fi.
