| stage | function | records | m2v instrs | AIR instrs | m2v cycles (bound) | AIR cycles (bound) | cycles ratio | m2v fma/cvt/sfu | AIR fma/cvt/sfu |
|---|---|---|---|---|---|---|---|---|---|
| FS | fixed_frag_lph_cph | 200 | 112 | 48 | 3.86 (ls) | 2.80 (ls) | 1.38x | 1.11/1.79/1.45 | 0.40/0.81/0.31 |
| FS | downsample_4_frag_lph | 4 | 149 | 61 | 11.00 (ls) | 6.00 (ls) | 1.83x | 1.78/2.02/1.16 | 0.56/0.94/0.00 |
| FS | downsample_8_frag_lph | 3 | 249 | 152 | 9.00 (ls) | 5.00 (ls) | 1.80x | 3.43/3.21/1.08 | 1.56/2.44/0.00 |
| FS | narrow_blur_11_frag_lph | 3 | 208 | 80 | 10.00 (ls) | 6.00 (ls) | 1.67x | 2.75/2.78/1.50 | 0.88/1.06/0.38 |
| FS | narrow_blur_15_frag_lph | 3 | 235 | 97 | 12.00 (ls) | 7.00 (ls) | 1.71x | 3.12/3.12/1.50 | 1.12/1.25/0.38 |
| FS | narrow_blur_23_frag_lph | 3 | 290 | 134 | 17.00 (ls) | 10.00 (ls) | 1.70x | 3.88/3.81/1.50 | 1.62/1.69/0.38 |
| FS | narrow_blur_19_frag_lph | 2 | 263 | 117 | 15.00 (ls) | 9.00 (ls) | 1.67x | 3.50/3.47/1.50 | 1.38/1.50/0.38 |
| FS | narrow_blur_27_frag_lph | 2 | 319 | 153 | 20.00 (ls) | 11.00 (ls) | 1.82x | 4.25/4.19/1.50 | 1.88/1.94/0.38 |
| FS | narrow_blur_7_frag_lph | 2 | 181 | 63 | 7.00 (ls) | 5.00 (ls) | 1.40x | 2.38/2.47/1.50 | 0.62/0.88/0.38 |
| FS | siriChipFrag | 2 | 138 | 51 | 3.00 (ls) | 2.00 (ls) | 1.50x | 1.72/1.84/1.62 | 0.62/0.53/0.50 |
| FS | attachment_clear_frag_lph | 1 | 9 | 9 | 0.22 (cvt) | 0.22 (cvt) | 1.00x | 0.00/0.22/0.00 | 0.00/0.22/0.00 |
| FS | blitNearest | 1 | 79 | 19 | 1.09 (cvt) | 1.00 (ls) | 1.09x | 1.03/1.09/0.75 | 0.09/0.34/0.00 |
| FS | compositeSphere | 1 | 247 | 96 | 4.62 (sfu) | 4.00 (ls) | 1.16x | 2.62/3.59/4.62 | 1.22/1.19/1.00 |
| FS | home_affordance_base_lph | 1 | 178 | 64 | 4.00 (ls) | 2.00 (ls) | 2.00x | 2.84/2.09/1.38 | 1.09/0.69/0.00 |
| FS | luma_boost_lph | 1 | 137 | 46 | 3.00 (ls) | 3.00 (ls) | 1.00x | 1.56/2.09/1.62 | 0.28/0.94/0.00 |
| FS | shadowFrag | 1 | 186 | 74 | 2.88 (cvt) | 1.12 (cvt) | 2.56x | 2.41/2.88/1.25 | 0.81/1.12/0.62 |
| FS | single_pass_blur_3_lph | 1 | 189 | 66 | 11.00 (ls) | 7.00 (ls) | 1.57x | 2.38/2.59/1.50 | 0.62/0.91/0.38 |
| FS | sphereFrag | 1 | 9 | 9 | 0.22 (cvt) | 0.22 (cvt) | 1.00x | 0.00/0.22/0.00 | 0.00/0.22/0.00 |
| FS | variable_blur_downsample_frag_lph | 1 | 153 | 59 | 2.31 (cvt) | 2.00 (ls) | 1.16x | 1.84/2.31/1.38 | 0.38/1.19/0.00 |
| FS | variable_blur_frag_lph | 1 | 552 | 376 | 15.00 (ls) | 11.00 (ls) | 1.36x | 6.75/8.22/5.25 | 5.09/5.12/2.75 |
| VS | fixed_vert_lph_spc | 200 | 59 | 57 | 12.00 (ls) | 12.00 (ls) | 1.00x | 0.69/0.81/0.25 | 0.69/0.78/0.12 |
| VS | downsample_blur_vert_lph | 24 | 59 | 53 | 12.00 (ls) | 10.00 (ls) | 1.20x | 0.69/0.81/0.25 | 0.69/0.72/0.12 |
| VS | siriChipVert | 2 | 142 | 124 | 25.00 (ls) | 18.00 (ls) | 1.39x | 1.47/2.06/0.75 | 1.50/1.78/0.38 |
| VS | std_vert1_lph | 2 | 59 | 53 | 12.00 (ls) | 10.00 (ls) | 1.20x | 0.69/0.81/0.25 | 0.69/0.72/0.12 |
| VS | compSphereVert | 1 | 97 | 78 | 18.00 (ls) | 12.00 (ls) | 1.50x | 0.88/1.50/0.62 | 0.88/1.19/0.25 |
| VS | path_blit_vert_lph | 1 | 59 | 48 | 14.00 (ls) | 10.00 (ls) | 1.40x | 0.19/1.16/0.62 | 0.19/1.00/0.38 |
| VS | quadGen | 1 | 34 | 30 | 7.00 (ls) | 5.00 (ls) | 1.40x | 0.12/0.72/0.38 | 0.12/0.66/0.38 |
| VS | shadowGenVert | 1 | 66 | 57 | 8.00 (ls) | 6.00 (ls) | 1.33x | 0.56/1.19/0.62 | 0.56/1.00/0.50 |
| VS | sphereVert | 1 | 97 | 78 | 18.00 (ls) | 12.00 (ls) | 1.50x | 0.88/1.50/0.62 | 0.88/1.19/0.25 |
| VS | variable_blur_vert_lph | 1 | 59 | 53 | 12.00 (ls) | 10.00 (ls) | 1.20x | 0.69/0.81/0.25 | 0.69/0.72/0.12 |
total FRAGMENT: instrs m2v 29580 AIR 12912 (2.29x); cycles m2v 1094.0 AIR 754.4 (1.45x)
total POSITION: instrs m2v 14030 AIR 13370 (1.05x); cycles m2v 2839.0 AIR 2751.0 (1.03x)
fixed_frag cycles ratio min/median/max 0.55/1.08/4.60; instr ratio 0.53/1.85/18.80; AIR worse cycles in 9
compile CPU (Mali entry only, translation excluded): m2v 1.3s AIR 6.4s over 234 records
