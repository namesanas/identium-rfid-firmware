#!/usr/bin/env sh
set -eu
if ! command -v flutter >/dev/null 2>&1; then
  echo "Flutter SDK is required. Install Flutter and Android SDK, then rerun this script." >&2
  exit 127
fi
flutter pub get
flutter test
flutter build apk --debug
APK="build/app/outputs/flutter-apk/app-debug.apk"
if [ ! -f "$APK" ]; then
  echo "Flutter reported success but APK was not found at $APK" >&2
  exit 1
fi
mkdir -p dist
cp "$APK" dist/TAGlink-Mobile-debug.apk
printf 'Built: %s\n' "dist/TAGlink-Mobile-debug.apk"
