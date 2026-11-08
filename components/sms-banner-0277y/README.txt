0277y SMS banner: IMDNotificationsController (IMDPersistence) posts nothing while _haveMigrated == NO
("We don't post notifications during system migration", info level, not in the persisted log).
_haveMigrated = !DMIsMigrationNeeded() && CFPreferencesCopyValue("kIMDPersistenceLastBuild", "com.apple.imdsmsrecordstore",
"mobile", CurrentHost) case-insensitively equal to SystemVersion.plist ProductBuildVersion ("20H392").
The key is normally written by the Messages DataMigrator plugin after an install/upgrade migration; on this rootfs the file is absent.

Install: /private/var/mobile/Library/Preferences/com.apple.imdsmsrecordstore.plist  owner 501:501 (mobile), mode 0600
Content: binary plist { kIMDPersistenceLastBuild = "20H392" }  (this directory's file)
