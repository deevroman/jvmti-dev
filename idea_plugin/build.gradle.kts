plugins {
    id("java")
    id("org.jetbrains.intellij.platform")
}

group = "dev.jvmti.dumper"
version = "0.1.0"

dependencies {
    implementation("org.json:json:20240303")

    intellijPlatform {
        intellijIdeaCommunity("2024.3.6")
        bundledPlugin("com.intellij.java")
    }
}

java {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
}

intellijPlatform {
    pluginConfiguration {
        ideaVersion {
            sinceBuild = "243"
            untilBuild = provider { null }
        }
    }
}

tasks {
    withType<JavaCompile> {
        options.encoding = "UTF-8"
    }
}
