#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
p=Path(__file__).resolve().parent
src=p.parents[4].parent/'nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893'
s=(src/'mt6893_pos.c').read_text()
assert 'CONN_RF_SPI_MST_REG_SPI_STA, 5,' in s
assert 'CONN_RF_SPI_MST_REG_SPI_TOP_ADDR, 0x0000b000, 0x0000a000,' in s
h=(src/'include/mt6893_consys_reg_offset.h').read_text()
for symbol,value in [('SPI_STA','0x0000'),('SPI_TOP_ADDR','0x0050'),('SPI_TOP_WDAT','0x0054'),('SPI_TOP_RDAT','0x0058')]:
 assert any(('CONN_RF_SPI_MST_REG_'+symbol) in line and value in line for line in h.splitlines())
assert any('ATOP_CHIP_ID' in line and '0x02c' in line for line in h.splitlines())
assert 'CONN_SEMA_RFSPI_INDEX = 11' in (src/'include/mt6893.h').read_text()
with tempfile.TemporaryDirectory() as d:
 out=Path(d)/'test'
 subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'id.c'),str(p/'test.c'),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
print('PASS selectedMT6893 source-address/semaphore/readformat audit; no native adapter/hardware')
