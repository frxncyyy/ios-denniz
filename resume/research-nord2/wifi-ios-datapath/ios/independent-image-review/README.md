Prepared independent existing-image checker. No image verdict exists yet.

Wait for root IMAGE_READY with exact source/CI attempt/kernel/image/payload SHA256 and byte lengths, then invoke verify-existing.py --execute with those explicit arguments and --scan-resume-requested true. Never invoke stage builder, approved runner or device tools. The checker preserves stage/builder receipts and gates, uses a1GiB address-space limit, and only invokes the stage --check-existing verifier (including its temporary loader-source byte comparison).

The expected new path is shim/out/nord2-xnu/nord2-ios-wifi-datapath-stop23.img. Old source/kernel/image approval identities are not inherited. Recovery image hashes are deliberate immutable d9a/6e4/rescue references. Normal HW0 DDONE+token storage retirement, group-key renewal intent and DHCP observational limits are explicit metadata requirements; none is hardware proof.
