import java.io.FileInputStream
import java.util.Properties

plugins {
    alias(libs.plugins.android.application)
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
        versionCode = 10
        versionName = "2.0"

        ndk {
            abiFilters.add("arm64-v8a")
            abiFilters.add("armeabi-v7a")
        }
    }

    buildTypes {
        release {
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
            java.srcDirs(
                "src/main/java",
                "src/main/cpp/deps/SDL/android-project/app/src/main/java"
            )
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
    //implementation("androidx.documentfile:documentfile:1.1.0")

    // implementation(fileTree(mapOf("dir" to "libs", "include" to listOf("*.jar"))))
    // implementation("androidx.appcompat:appcompat:1.0.2")
}
