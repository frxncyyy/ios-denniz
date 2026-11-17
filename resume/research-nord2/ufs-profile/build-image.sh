#!/bin/sh
set -eu
sh tools/nord2-image-from-kernel.sh research/ci-39488497958/xnu-kernel/kernel.development.sweet \
 shim/out/nord2-xnu/nord2-ios-ufs-profile-fastlog-stop23.img \
 --base-boot research/nord2/rescue/nord2-adb-rescue.img \
 --zopfli ../toolchains/zopfli/zopfli --product-adt research/nord2/ipsw/d22ap.adt \
 debug=0xc cpus=8 keepsyms=1 -noprogress nord2stop=0 nord2vm=1 nord2fb=1 \
 nord2stopat=23 rd=ufsroot0 ufsroot=userdata ufsrw=1 rootrw=1 \
 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin \
 launchd_unsecure_cache=1 wdt=-1 nord2display=1 nord2input=1 nord2wifi=2 \
 nord2fastlog=1 nord2ufsprofile=1
