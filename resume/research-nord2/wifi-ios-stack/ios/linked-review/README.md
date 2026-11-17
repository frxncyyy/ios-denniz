# Pending stack-only linked review

Source integrated by root from frozen registry50e69c6a / candidate ec4043df.
Wait for root's verified completed CI artifact path, bytes, SHA, source and run.
Do not poll/download artifacts. extract.py is prepared but has not been run.

The script captures only controller start/stop, new IONetworkStack and
IONetworkStackUserClient methods, and the existing registry TU constructor.
Actual new class ancestry, vtable targets, original property/log constants and
real mod_init_func registration are extracted from the same Mach-O. Evidence is
not an automatic PASS from marker strings.

Manual review will verify:

- Current-task/admin/type0 admission, real UserClient construction and provider
  open/task references; service and client property paths retain auth checks.
- Dictionary command/ID8/unit bounds, command0/1 owned-repeat NoSpace, foreign
  NoDevice and command2 live-empty-pending behavior; no ifnet create/rename.
- Existing attached ifnet identity lookup/reference and entry ID, state1 after
  real attach, property/init/attach/start cleanup before service publication.
- Controller deactivation before terminate/detach; client close/stop/free
  retires task/provider references once and owned net remains valid until free.
- New class names and correct IOService/IOUserClient metaclass parents, true
  actual initializer entry and callable methods; no proprietary-vtable guess.

Root owns normal flags/full source equality/ISA audits. No unchanged scans,
quiesce, RF or whole-kernel review is planned. Static linked PASS will not prove
live sandbox acceptance, registry state, faster initialization or Settings UI.
