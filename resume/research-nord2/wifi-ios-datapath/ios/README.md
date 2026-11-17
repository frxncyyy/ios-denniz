# Combined normal TX / group-key renewal test

Source: `b441429316787f81d62106e7a9892f282b28e174`.
Full CI: `39859151068`, attempt 1, SUCCESS. Local composed suite and fifteen
actual ARM64 translation-unit compiles passed. Artifact source/ISA/marker audits
passed; final linked review70351db8/manifest570f3a63 passed and all62 members
were verified. Packaging session41708 completed successfully under1GiB. Image33478656 bytes,
SHA25608e6be56c42f62c0b0e4771132c9865f84a49f4f634c9a235503e595d6403a42;
payload14993648 bytes, SHA2567af08ad87ab0ad6bdc45520d6964691c8c115b025f59616fe0ff79b7ca5ee70f.
Independent actual-image review3e4b5b3b/manifest5e158b8d PASS. Parent verified
all7 review members and protected pins. Prepared-image gate TRUE; human/RF/
conditional-recovery authorization gates remain FALSE. No flash from this stage.

The phone remains on `b8851761`; its one Join was captured and consumed.
The open network is available but unsupported by the current WPA2-only profile.

Changes prepared together:
- Native normal HW0 TXD/TXP and separate payload-token lifetime, with checked
  descriptor completion and firmware token return before reuse.
- Authenticated GTK renewal, per-key replay/WTBL state and protected Group2.
- Infrastructure/FAST scan requests and the observed 110ms dwell profile.
- Bounded numeric RX rejection, advanced TX status and own-client DHCP matching.

The new path matches the selected Android source; this is not yet proof it
fixes the observed DHCP failure. A locally completed frame does not prove an
air acknowledgment. Even a matched DHCP response does not establish a lease.

After exact image verification and human boot-only authorization, collect one
WPA2-Personal Join with private address disabled and credentials entered only
on the phone. Record checkbox/IP/Safari separately from native progress. Leave
the connection untouched through the late key-renewal window. No open-network
comparison, repeated Join, new partition write, or stock Android boot.
