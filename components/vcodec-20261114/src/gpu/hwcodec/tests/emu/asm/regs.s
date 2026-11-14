@ SPDX-License-Identifier: MIT
@ Uses the initial registers supplied by --reg.
.syntax unified
.text
	add	r2, r0, r1
	udf	#254
