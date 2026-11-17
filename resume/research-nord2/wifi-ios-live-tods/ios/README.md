# Live ToDS comparison image (not yet built or authorized)

This fresh stage keeps the executed ee8b6da0 image and its evidence intact.
The next image will preserve that exact ADT, boot arguments and tested RAM ADB
contents, correct the STA wire states to0/2, add the bounded peer-metadata query and
alternate ordinary-data ToDS profile. Ethernet remains the default. No hardware commands are in this stage.

Required before a flash request: complete source review and local checks,
committed source, successful full GitHub build on that commit, artifact/source
identity and ISA checks, independently verified packed kernel/loader/ADT/rescue
and image SHA-256. The image itself has not been tested and DHCP is unproven.
Recovery remains the already tested d9a44316 iOS image or d78bf9fe RAM ADB rescue.
