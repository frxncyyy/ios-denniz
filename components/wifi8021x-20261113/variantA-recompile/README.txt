Variant A of the 802.1X work (2026-11-13, NOT the delivered one): the same logic as hooks inside the native station binder
(wifi_station_service_bind.inc -> sweet_touch.o) and with these prebuilt CI members recompiled: wifi_wpa2, wifi_station_transaction,
wifi_station_wire, wifi_ios_station_view, wifi_ios_client.cpo, wifi_ios_registry.cpo. Base = the withdrawn q10jgw stage (0430 v1 + 0431).
Host-tested (crypto, decoder, engine, owner fixture AKM 1/5 + timeout, previous suites) but abandoned after the q10jgw regression:
the delivered patches (../0450, ../0451) recompile no prebuilt member except wifi_ios_join.o and leave sweet_touch.o untouched.
Files are renamed (*.patch.txt, A-sources-*) so that no build script picks them up.
