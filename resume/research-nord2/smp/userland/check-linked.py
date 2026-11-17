from pathlib import Path
import json
s=Path('research/nord2/smp/userland/critical-functions-disassembly.txt').read_text()
def body(n):return s.split('<'+n+'>:',1)[1].split('\n\n',1)[0]
r=body('_nord2_log_read'); w=body('_nord2_putc'); t=body('_nusb_background_thread'); c=body('_nord2_usb_prepare_return'); leave=body('_nord2_leave_to')
assert r.index('ldr\tw10, [x9, #8]')<r.index('dmb\tsy')<r.index('ldrb\tw11')
assert w.index('strb\tw19, [x8, #12]')<w.index('dmb\tsy')<w.index('str\tw10, [x8, #8]')
steps=['<_cpu_to_processor>','<_thread_bind>','<_thread_block>','<_cpu_number>','cbnz\tw0','<_nord2_usb_enumerate>','stlr\tw8','<_nord2_stop_at>']
positions=[t.index(x) for x in steps]; assert positions==sorted(positions)
assert c.index('MPIDR_EL1')<c.index('mov\tw0, #-1')<c.index('\tret')<c.index('str\tw10, [x11, #9220]')
assert leave.index('ICC_SGI1R_EL1')<leave.index('smc\t#0')<leave.index('<_nord2_usb_prepare_return>')
assert 'ldar' in body('_sweet_cpu_signal_is_enabled') and body('_nord2_smp_userland_ready').count('ldar')==2
r=dict(usb_thread_binds_and_checks_CPU0_before_MMIO=True,usb_completion_release=True,usb_cleanup_owner_check_precedes_MMIO=True,
 pmsg_read_and_write_publication_DMB_SY=True,coordinated_SGI_affinity_checks_precede_USB_cleanup=True,ready_and_stop_acquire=True,signal_observer_LDAR=True)
Path('research/nord2/smp/userland/critical-functions-check.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
