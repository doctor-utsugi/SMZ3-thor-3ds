#!/usr/bin/env bash
# Builds the SMZ3 app (the vendored Lemuroid fork) with the JDK 21 Gradle already cached (the system JDK 25 is too new for Gradle 8.10).
#   ./build-android.sh            debug APK   -> build/smz3-debug.apk   (com.utsugi.smz3.debug, "SMZ3 debug")
#   ./build-android.sh release    release APK -> build/smz3-release.apk (com.utsugi.smz3, "SMZ3", signed with vendor/Lemuroid/release.jks)
set -euo pipefail
root="$(cd "$(dirname "$0")" && pwd)"
variant="${1:-debug}"
python3 "$root/tools/gen_app_icon.py"
cd "$root/vendor/Lemuroid"
git submodule update --init --depth 1
git -C ../LibretroDroid submodule update --init --depth 1
echo "sdk.dir=$HOME/Android/Sdk" > local.properties
echo "sdk.dir=$HOME/Android/Sdk" > ../LibretroDroid/local.properties
export JAVA_HOME="$HOME/.gradle/jdks/eclipse_adoptium-21-amd64-linux.2"
mkdir -p ../../build
if [ "$variant" = "release" ]; then
    # Local signing key (kept out of git); packaging the 190 MB APK needs more heap than the default 2.5 GB.
    [ -f release.jks ] || "$JAVA_HOME/bin/keytool" -genkeypair -keystore release.jks -alias lemuroid -storepass lemuroid -keypass lemuroid \
        -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=SMZ3 local release, O=utsugi"
    ./gradlew :lemuroid-app:assembleFreeBundleRelease --console=plain -Dorg.gradle.jvmargs="-Xmx8g -Dfile.encoding=UTF-8"
    cp lemuroid-app/build/outputs/apk/freeBundle/release/lemuroid-app-free-bundle-release.apk ../../build/smz3-release.apk
else
    ./gradlew :lemuroid-app:assembleFreeBundleDebug --console=plain
    cp lemuroid-app/build/outputs/apk/freeBundle/debug/lemuroid-app-free-bundle-debug.apk ../../build/smz3-debug.apk
fi
