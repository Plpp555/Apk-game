`trisync-debug.jks` is a **public debug signing key** (PKCS12, store/key password
`trisync-debug`, alias `trisync`). Gradle, CI and `tools/build_apk_offline.sh`
all use it, so APKs from any of them can update each other on a test device.

It is not secret. Sign with your own key before distributing the app publicly.
