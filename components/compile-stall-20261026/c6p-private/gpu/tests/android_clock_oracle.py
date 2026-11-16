#!/usr/bin/env python3
"""Execute unchanged Android clock arithmetic/CG code against the native port.

Only host register models, never hardware. Extracted vendor source stays in
ignored build output; record its hashes rather than copy it into this tree.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('android', type=Path)
args = p.parse_args()
android = args.android.resolve()
root = Path(__file__).resolve().parents[2]
out = root / 'gpu/build/android-clock-oracle'
out.mkdir(exist_ok=True)
freq_path = android / 'drivers/gpu/mediatek/gpufreq/v1/mt6893/mtk_gpufreq_plat.c'
header_path = android / 'drivers/gpu/mediatek/gpufreq/v1/include/mt6893/mtk_gpufreq_internal_plat.h'
freq, header = freq_path.read_text(), header_path.read_text()


def function(name):
    m = re.search(r'^static [^;{}]*\b' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{', freq, re.M)
    assert m, name
    end, depth = m.end(), 1
    while depth:
        depth += (freq[end] == '{') - (freq[end] == '}')
        end += 1
    return freq[m.start():end]


functions = [function(n) for n in ['__mt_gpufreq_calculate_dds',
             '__mt_gpufreq_get_cur_freq', 'mt_gpufreq_external_cg_control']]
defines = '\n'.join(re.search(r'^#define\s+' + n + r'\s+.*', header, re.M).group()
                    for n in ['POSDIV_8_MIN_FREQ', 'POSDIV_4_MAX_FREQ', 'POSDIV_SHIFT',
                              'DDS_SHIFT', 'TO_MHZ_HEAD', 'TO_MHZ_TAIL',
                              'ROUNDING_VALUE', 'MFGPLL_FIN'])
enum = re.search(r'enum g_posdiv_power_enum\s*\{[^}]*\};', header).group()
tables = []
for segment in [1, 2]:
    body = re.search(r'g_opp_table_segment_' + str(segment) + r'\[\]\s*=\s*\{(.*?)\};', header, re.S).group(1)
    table = re.findall(r'GPUOP\(\s*(\d+),\s*(\d+),\s*(\d+),\s*(POSDIV_POWER_\d+),\s*\d+\)', body)
    assert len(table) == 41
    tables.append(table)
rows = []
for a, b in zip(*tables):
    assert a[0] == b[0] and a[3] == b[3]
    rows.append('{' + ','.join([a[0], str(max(int(a[1]), int(b[1])) * 10),
                               str(max(int(a[2]), int(b[2])) * 10), a[3]]) + '}')
source = f'''
#define main board_model_main
#include "{root / 'gpu/tests/nord2_board_test.c'}"
#undef main
#include "{android / 'include/linux/mfd/mt6315/registers.h'}"
#include "{android / 'include/linux/mfd/mt6359p/registers.h'}"
{defines}
{enum}
#define gpufreq_pr_info(...) ((void)0)
#define gpufreq_pr_debug(...) ((void)0)
typedef uint32_t u32;
static uintptr_t g_mfg_base;
static uint32_t oracle_con1, oracle_mfg[0x200/4];
#define MFGPLL_CON1 0x26cu
#define DRV_Reg32(x) (oracle_con1)
#define readl(x) (oracle_mfg[(x)/4])
#define writel(v,x) (oracle_mfg[(x)/4]=(v))
{chr(10).join(functions)}
static const struct n2rail_opp android_opps[] = {{ {','.join(rows)} }};
int main(void) {{
    CHECK(board_model_main()==0);
    CHECK(N2RAIL_VGPU_EN == MT6315_PMIC_RG_BUCK_VBUCK1_EN_ADDR);
    CHECK(N2RAIL_VGPU_VOSEL == MT6315_PMIC_RG_BUCK_VBUCK1_VOSEL_ADDR);
    CHECK(N2RAIL_VGPU_DA_EN == MT6315_PMIC_DA_VBUCK1_EN_ADDR);
    CHECK(N2RAIL_VGPU_DA_VOSEL == MT6315_PMIC_DA_VBUCK1_VOSEL_ADDR);
    CHECK(N2RAIL_VSRAM_CON0 == PMIC_RG_LDO_VSRAM_OTHERS_EN_ADDR);
    CHECK(N2RAIL_VSRAM_MON == PMIC_DA_VSRAM_OTHERS_B_EN_ADDR);
    CHECK(N2RAIL_VSRAM_ELR == PMIC_RG_LDO_VSRAM_OTHERS_VOSEL_ADDR);
    CHECK(N2RAIL_VSRAM_VOSEL1 == PMIC_DA_VSRAM_OTHERS_VOSEL_ADDR);
    CHECK(PMIC_DA_VSRAM_OTHERS_VOSEL_SHIFT == 8);
    for (unsigned i=0; i<41; ++i) {{
        CHECK(!memcmp(&android_opps[i], &n2rail_opp_table[i], sizeof(struct n2rail_opp)));
        oracle_con1 = (android_opps[i].posdiv_power << 24) |
            __mt_gpufreq_calculate_dds(android_opps[i].khz, android_opps[i].posdiv_power);
        CHECK(__mt_gpufreq_get_cur_freq() == android_opps[i].khz);
        CHECK(n2rail_mfgpll_khz(oracle_con1) == __mt_gpufreq_get_cur_freq());
    }}
    for (unsigned i=0; i<10000; ++i) {{
        oracle_con1 = ((i%8)<<24) | ((i*16057u)&0x3fffff);
        CHECK(n2rail_mfgpll_khz(oracle_con1) == __mt_gpufreq_get_cur_freq());
    }}
    static struct rig g;
    world_cold(&g.w); g.w.apmixed[N2CLK_MFGPLL_CON1/4]=0;
    rig_init(&g); CHECK(nord2_gpu_board_preflight(&g.b));
    CHECK(nord2_gpu_board_power_on(&g.b));
    CHECK(g.w.apmixed[N2CLK_MFGPLL_CON1/4] == ((POSDIV_POWER_8<<24) |
          __mt_gpufreq_calculate_dds(350000,POSDIV_POWER_8)));
    CHECK(nord2_gpu_board_power_off(&g.b));
    /* Compare unchanged Android CG RMW code, with varying unrelated bits. */
    for (unsigned n=0;n<128;n++) {{
        world_cold(&g.w);rig_init(&g);g.w.spm[0x16c/4]=0x1fc;
        for(unsigned i=0;i<0x200/4;i++)
            oracle_mfg[i]=g.w.mfgcfg[i]=(n*16057u+i*7919u)^0xa5a5a5a5u;
        g.clk.mux_on=true;
        CHECK(n2clk_mfg_cg_on(&g.clk)==N2CLK_OK);
        mt_gpufreq_external_cg_control();
        const unsigned offs[]={{0x20,0x24,0x2c,0xb0,0xb4}};
        for(unsigned i=0;i<5;i++)CHECK(g.w.mfgcfg[offs[i]/4]==oracle_mfg[offs[i]/4]);
    }}
    if(failures) return 1;
    puts("Android oracle PASS: 41 OPPs, 10000 PLL decodes, exact 350MHz DDS, PMIC registers, 128 external-CG states");
    return 0;
}}
'''
(out / 'oracle.c').write_text(source)
command = ['cc', '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
           '-std=c11', '-I' + str(root / 'gpu/include'), str(out / 'oracle.c')]
command += [str(root / ('gpu/src/' + n + '.c')) for n in
            ['nord2_gpu_board', 'nord2_gpu_rail', 'nord2_gpu_clk', 'nord2_gpu_scpsys', 'nord2_gpu_hw']]
command += ['-o', str(out / 'oracle')]
subprocess.run(command, check=True)
subprocess.run([str(out / 'oracle')], check=True)
record = {'result': 'PASS', 'hardware_test': False,
          'android_commit': subprocess.check_output(['git', '-C', str(android), 'rev-parse', 'HEAD'], text=True).strip(),
          'functions': [hashlib.sha256(s.encode()).hexdigest() for s in functions],
          'sources': {str(x.relative_to(android)): hashlib.sha256(x.read_bytes()).hexdigest()
                      for x in [freq_path, header_path,
                                android / 'include/linux/mfd/mt6315/registers.h',
                                android / 'include/linux/mfd/mt6359p/registers.h']}}
(out / 'receipt.json').write_text(json.dumps(record, indent=2) + '\n')
