plugins {
    id("com.android.application")
}

android {
    namespace = "com.luaudio.androidtests"
    compileSdk = 36

    sourceSets["main"].java.srcDirs("src/main/java")

    defaultConfig {
        applicationId = "com.luaudio.androidtests"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"

        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }

        externalNativeBuild {
            cmake {
                arguments += "-DANDROID_STL=c++_shared"
                arguments += "-DLUAUDIO_ANDROID_TESTS=ON"
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }

    externalNativeBuild {
        cmake {
            path = file("../../../CMakeLists.txt")
        }
    }
}
