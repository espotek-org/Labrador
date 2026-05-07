# Labrador Android Release Runbook

This is the repeatable release process for the Labrador Android app, starting from a fresh Mac that may not have Android build tools installed.

## Project-specific constants

Use these unless the Play Console listing changes:

```text
Repo/project folder: /Users/chrises/git/labrador/Android_App
Play package name:  org.qtproject.example.Labrador
Gradle app module:  app
Release bundle:     app/build/outputs/bundle/release/app-release.aab
```

Important distinction:

```text
namespace     = Java/Kotlin/R namespace used by the build
applicationId = Android package name checked by Google Play
```

For this existing Play Store listing, `applicationId` must be:

```kotlin
applicationId = "org.qtproject.example.Labrador"
```

Do not change it to `com.EspoTek.Labrador` unless publishing as a different app.

---

## 1. Fresh-machine setup

### 1.1 Install Java without Oracle Java

Use Eclipse Temurin OpenJDK 17:

```bash
brew install --cask temurin@17
```

Set Java for the current terminal:

```bash
export JAVA_HOME=$(/usr/libexec/java_home -v 17)
export PATH="$JAVA_HOME/bin:$PATH"
java --version
```

Optional permanent zsh setup:

```bash
cat >> ~/.zshrc <<'ZSHRC'
export JAVA_HOME=$(/usr/libexec/java_home -v 17)
export PATH="$JAVA_HOME/bin:$PATH"
ZSHRC
source ~/.zshrc
```

Android Gradle Plugin requires JDK 17 for modern builds.

### 1.2 Install Android Studio or command-line SDK tools

Simplest path:

```bash
brew install --cask android-studio
open -a "Android Studio"
```

Complete the setup wizard and install the Android SDK.

Then create `local.properties` from the Android project folder:

```bash
cd /Users/chrises/git/labrador/Android_App
echo "sdk.dir=$HOME/Library/Android/sdk" > local.properties
```

### 1.3 Install required SDK packages from CLI

Make sure SDK tools are on `PATH`:

```bash
export ANDROID_HOME="$HOME/Library/Android/sdk"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
export PATH="$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$PATH"
```

Accept licenses:

```bash
sdkmanager --licenses
```

Install the SDK, build tools, CMake, platform tools, and NDK needed by the project:

```bash
sdkmanager \
  "platform-tools" \
  "platforms;android-36" \
  "build-tools;36.0.0" \
  "cmake;3.22.1" \
  "ndk;27.0.12077973"
```

If Gradle asks for a different NDK version, install the one it asks for. Native/C++ builds often require exact NDK/CMake versions.

---

## 2. Prepare the source tree

From the repo root:

```bash
cd /Users/chrises/git/labrador
git status
git pull
```

Initialize any submodules, especially SDL:

```bash
git submodule update --init --recursive
```

---

## 3. Update Gradle release settings

Edit:

```text
Android_App/app/build.gradle.kts
```

Check these values:

```kotlin
android {
    namespace = "com.EspoTek.Labrador"

    compileSdk {
        version = release(36)
    }

    defaultConfig {
        applicationId = "org.qtproject.example.Labrador"
        minSdk = 25
        targetSdk = 36
        versionCode = 11
        versionName = "2.0.1"
    }
}
```

### Version rules

`versionCode` must be higher than every version previously uploaded to this Play Console listing.

`versionName` is user-visible only. Examples:

```text
versionCode = 10, versionName = "2.0"
versionCode = 11, versionName = "2.0.1"
versionCode = 12, versionName = "2.1"
```

### Package-name rule

For the existing Labrador Play listing, keep:

```kotlin
applicationId = "org.qtproject.example.Labrador"
```

If this is wrong, Play Console rejects the upload with:

```text
Your APK or Android App Bundle needs to have the package name org.qtproject.example.Labrador
```

---

## 4. Check whether SDK versions need bumping

Google Play currently requires new apps and updates to target Android 15 / API level 35 or higher, except some platform-specific categories such as Wear OS, Android Automotive OS, and Android TV, which have different requirements.

This project currently uses:

```kotlin
compileSdk {
    version = release(36)
}

targetSdk = 36
```

That is above the current Play minimum. If Play Console later complains that the target SDK is too low:

1. Check the current Play target API requirement.
2. Install the required SDK platform using `sdkmanager`.
3. Update both `compileSdk` and `targetSdk` if appropriate.
4. Rebuild and test.

Example SDK bump to API 37, if required in future:

```bash
sdkmanager "platforms;android-37" "build-tools;37.0.0"
```

Then in `app/build.gradle.kts`:

```kotlin
compileSdk {
    version = release(37)
}

targetSdk = 37
```

If upgrading Android Gradle Plugin is required, use Android Studio's AGP Upgrade Assistant or update the plugin/Gradle wrapper carefully. Newer AGP versions may also change the default NDK.

---

## 5. Signing setup

### 5.1 Credentials involved

There are two unrelated credentials:

```text
Upload keystore / .jks / .keystore  signs the .aab before upload
Google Play Console key JSON        lets CI/API upload to Play Console
```

The `google_play_console_key` cannot sign the app.

### 5.2 Play App Signing

Play App Signing is enabled for Labrador. That means Google holds the final app signing key. Locally, sign releases with the upload key.

If the upload key password is lost, reset the upload key in:

```text
Play Console → Release → App integrity
```

After a reset, Google may enforce a waiting period before the new upload key is valid. Do not expect uploads signed with the new key to work until the time shown in Play Console.

### 5.3 Generate a new upload keystore, if needed

Store this outside the repo:

```bash
mkdir -p ~/android-keys/labrador
cd ~/android-keys/labrador

keytool -genkeypair \
  -v \
  -keystore labrador-upload-keystore.jks \
  -keyalg RSA \
  -keysize 2048 \
  -validity 10000 \
  -alias upload
```

Export certificate for Play Console upload-key reset:

```bash
keytool -export \
  -rfc \
  -keystore labrador-upload-keystore.jks \
  -alias upload \
  -file labrador-upload-certificate.pem
```

Upload `labrador-upload-certificate.pem` to the Play Console upload-key reset flow.

### 5.4 Local signing properties

Create this file:

```text
Android_App/keystore.properties
```

Example:

```properties
storeFile=/Users/chrises/android-keys/labrador/labrador-upload-keystore.jks
storePassword=YOUR_STORE_PASSWORD
keyAlias=upload
keyPassword=YOUR_KEY_PASSWORD
```

The property names must be exact:

```text
storeFile
storePassword
keyAlias
keyPassword
```

If `storeFile` is missing or misspelled, Gradle fails with:

```text
Missing storeFile in keystore.properties
```

Never commit signing credentials:

```bash
cd /Users/chrises/git/labrador/Android_App
cat >> .gitignore <<'GITIGNORE'
keystore.properties
*.jks
*.keystore
*.pem
google_play_console_key*.json
GITIGNORE
```

---

## 6. Expected `app/build.gradle.kts` shape

This is the relevant structure. Keep project-specific dependencies/native config as needed.

```kotlin
import java.io.FileInputStream
import java.util.Properties

plugins {
    alias(libs.plugins.android.application)
}

val keystorePropertiesFile = rootProject.file("keystore.properties")
val keystoreProperties = Properties()

if (keystorePropertiesFile.exists()) {
    keystoreProperties.load(FileInputStream(keystorePropertiesFile))
}

android {
    namespace = "com.EspoTek.Labrador"

    compileSdk {
        version = release(36)
    }

    defaultConfig {
        applicationId = "org.qtproject.example.Labrador"
        minSdk = 25
        targetSdk = 36
        versionCode = 11
        versionName = "2.0.1"

        ndk {
            abiFilters.add("arm64-v8a")
            abiFilters.add("armeabi-v7a")
        }
    }

    signingConfigs {
        create("release") {
            val storeFilePath = keystoreProperties.getProperty("storeFile")
                ?: error("Missing storeFile in keystore.properties")
            val storePasswordValue = keystoreProperties.getProperty("storePassword")
                ?: error("Missing storePassword in keystore.properties")
            val keyAliasValue = keystoreProperties.getProperty("keyAlias")
                ?: error("Missing keyAlias in keystore.properties")
            val keyPasswordValue = keystoreProperties.getProperty("keyPassword")
                ?: error("Missing keyPassword in keystore.properties")

            storeFile = file(storeFilePath)
            storePassword = storePasswordValue
            keyAlias = keyAliasValue
            keyPassword = keyPasswordValue
        }
    }

    buildTypes {
        release {
            signingConfig = signingConfigs.getByName("release")
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    sourceSets {
        getByName("main") {
            java {
                directories.add("src/main/cpp/deps/SDL/android-project/app/src/main/java")
            }
        }
    }

    aaptOptions {
        ignoreAssetsPattern = "!pulse.svg:!svg(2)-converted.svg:!settings-gear-svgrepo-com.svg:!mm.svg:!Greek_uc_delta.svg:!Readme.txt"
    }
}

dependencies {
    implementation("com.google.android.material:material:1.2.1")
    implementation("com.android.support.constraint:constraint-layout:1.0.2")
    implementation("androidx.appcompat:appcompat:1.7.1")
}
```

Notes:

- The `aaptOptions` warning is deprecation noise unless it becomes an error.
- The `srcDirs`/`directories` warning is also deprecation noise unless it becomes an error.
- The old `com.android.support.constraint` dependency is pre-AndroidX. Leave it alone if the app builds. If dependency conflicts appear later, consider replacing it with `androidx.constraintlayout:constraintlayout` after testing.

---

## 7. Build the release bundle

From the Android project folder:

```bash
cd /Users/chrises/git/labrador/Android_App
./gradlew clean bundleRelease
```

Find the output:

```bash
find app/build/outputs/bundle/release -name "*.aab" -ls
```

Expected:

```text
app/build/outputs/bundle/release/app-release.aab
```

---

## 8. Upload to Play Console

Use Internal testing first.

```text
Google Play Console
→ Select Labrador
→ Test and release
→ Testing
→ Internal testing
→ Create new release
→ Upload app-release.aab
```

Upload:

```text
Android_App/app/build/outputs/bundle/release/app-release.aab
```

Add release notes, for example:

```text
Updated Labrador for compatibility with current Android versions.
```

Click **Next** and review all warnings/errors.

Do not start rollout or send for review until the warnings are understood.

---

## 9. Common Play Console errors

### Wrong package name

Error:

```text
Your APK or Android App Bundle needs to have the package name org.qtproject.example.Labrador
```

Fix:

```kotlin
applicationId = "org.qtproject.example.Labrador"
```

Rebuild and upload again.

### Upload certificate not valid yet

Error:

```text
You uploaded an app bundle that is signed with an upload certificate that is not yet valid because it has been recently reset.
```

Fix: wait until the exact UTC time shown by Play Console. Then upload the same rebuilt `.aab` again, assuming no source/version changes were made.

### Version code too low

Fix:

```kotlin
versionCode = <higher number than latest uploaded version>
```

Rebuild and upload again.

### Target SDK too low

Fix:

1. Check current Play target API requirement.
2. Install required SDK platform.
3. Update `compileSdk` and `targetSdk`.
4. Rebuild and test.

### Missing SDLActivity

Error:

```text
package org.libsdl.app does not exist
```

Fix:

```bash
git submodule update --init --recursive
find app/src/main/cpp/deps/SDL -name "SDLActivity.java"
./gradlew clean bundleRelease
```

### SDK location not found

Error:

```text
SDK location not found
```

Fix:

```bash
cd /Users/chrises/git/labrador/Android_App
echo "sdk.dir=$HOME/Library/Android/sdk" > local.properties
```

---

## 10. Production rollout

After Internal testing succeeds:

```text
Play Console
→ Test and release
→ Production
→ Create new release
→ Upload app-release.aab or promote from internal testing
```

Prefer staged rollout:

```text
5% → 20% → 50% → 100%
```

Monitor:

```text
Android vitals
Crashes
ANRs
User reviews
USB/device-specific issues
```

---

## 11. What to keep safe

Store these somewhere durable, not just on one laptop:

```text
labrador-upload-keystore.jks
store password
key alias
key password
labrador-upload-certificate.pem
google_play_console_key JSON, if using CI upload
```

Do not commit any of them.

---

## 12. Optional GitHub Actions update

If GitHub Actions currently builds an APK with:

```bash
./gradlew assembleRelease
```

Change or add:

```bash
./gradlew bundleRelease
```

Artifact path should be:

```text
Android_App/app/build/outputs/bundle/release/*.aab
```

For CI signing, store keystore/passwords as GitHub Actions secrets. Do not store them in the repo.

The Google Play Console key JSON is only for automated Play uploads. It is not needed for manual Play Console uploads.
