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
        // Legacy Qt-era id — must never change, it is the Play Store listing.
        applicationId = "org.qtproject.example.Labrador"
        minSdk = 25
        targetSdk = 36
        versionCode = 12
        versionName = "2.0"

        ndk {
            abiFilters.add("arm64-v8a")
            abiFilters.add("armeabi-v7a")
        }
    }

    // Release signing only configured when keystore.properties exists, so
    // debug builds work on a fresh checkout.
    if (keystorePropertiesFile.exists()) {
        signingConfigs {
            create("release") {
                storeFile = file(keystoreProperties.getProperty("storeFile"))
                storePassword = keystoreProperties.getProperty("storePassword")
                keyAlias = keystoreProperties.getProperty("keyAlias")
                keyPassword = keystoreProperties.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        release {
            if (keystorePropertiesFile.exists()) {
                signingConfig = signingConfigs.getByName("release")
            }
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
        debug {
            // Debug builds install side-by-side with any Play/release build by
            // using a distinct package id. The RELEASE build above keeps the
            // exact Play listing id (org.qtproject.example.Labrador) untouched.
            applicationIdSuffix = ".unified"
            versionNameSuffix = "-unified"
        }
    }

    externalNativeBuild {
        cmake {
            // The unified cross-platform build — same tree the desktop uses
            path = file("../../CMakeLists.txt")
            version = "3.22.1"
        }
    }

    sourceSets {
        getByName("main") {
            java.srcDirs(
                "src/main/java",
                // SDL's Java glue (SDLActivity) from the vendored SDL checkout
                "../../deps/SDL/android-project/app/src/main/java"
            )
            // Shared cross-platform assets (fonts, pinouts, help) + the
            // Android-specific ones (firmware hex)
            assets.srcDirs("src/main/assets", "../../assets")
        }
    }
}

dependencies {
    implementation("com.google.android.material:material:1.2.1")
    implementation("com.android.support.constraint:constraint-layout:1.0.2")
    implementation("androidx.appcompat:appcompat:1.7.1")
}
