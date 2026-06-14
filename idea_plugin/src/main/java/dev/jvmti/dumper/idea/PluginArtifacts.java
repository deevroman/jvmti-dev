package dev.jvmti.dumper.idea;

import com.intellij.openapi.project.Project;
import com.intellij.psi.PsiClass;
import com.intellij.psi.PsiMethod;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;

final class PluginArtifacts {
    private static final DateTimeFormatter TIMESTAMP_FORMAT =
            DateTimeFormatter.ofPattern("yyyyMMdd_HHmmss_SSS");

    private PluginArtifacts() {
    }

    static PreparedConfig prepare(Project project, PsiMethod method) throws IOException {
        PsiClass containingClass = method.getContainingClass();
        if (containingClass == null || containingClass.getQualifiedName() == null) {
            throw new IOException("Cannot resolve containing class for method " + method.getName());
        }

        Path artifactsDirectory = artifactsDirectory(project);
        Path configsDirectory = artifactsDirectory.resolve("configs");
        Path dumpsDirectory = artifactsDirectory.resolve("dumps");
        Path generatedTestsDirectory = artifactsDirectory.resolve("generated_tests");
        Files.createDirectories(configsDirectory);
        Files.createDirectories(dumpsDirectory);
        Files.createDirectories(generatedTestsDirectory);

        String timestamp = TIMESTAMP_FORMAT.format(LocalDateTime.now());
        String classPart = simpleClassName(containingClass);
        String methodPart = sanitize(method.getName());
        String baseName = classPart + "_" + methodPart + "_" + timestamp;

        RuntimeConfigPayload payload = new RuntimeConfigPayload(
                containingClass.getQualifiedName(),
                method.getName(),
                JvmMethodDescriptor.of(method),
                dumpsDirectory.resolve(baseName + ".json"),
                dumpsDirectory.resolve(baseName + ".llm.txt")
        );

        Path configPath = configsDirectory.resolve(baseName + ".json");
        String json = payload.toJson();
        Files.writeString(configPath, json + System.lineSeparator(), StandardCharsets.UTF_8);
        return new PreparedConfig(
                configPath,
                dumpsDirectory.resolve(baseName + ".json"),
                dumpsDirectory.resolve(baseName + ".llm.txt"),
                generatedTestsDirectory.resolve(baseName),
                payload.toSocketJson()
        );
    }

    private static Path artifactsDirectory(Project project) throws IOException {
        String basePath = project.getBasePath();
        if (basePath == null || basePath.isBlank()) {
            throw new IOException("Project base path is unavailable");
        }
        return Path.of(basePath).toAbsolutePath().resolve("jvmti-dumper-artifacts");
    }

    private static String simpleClassName(PsiClass psiClass) {
        String name = psiClass.getName();
        if (name == null || name.isBlank()) {
            name = psiClass.getQualifiedName();
        }
        return sanitize(name);
    }

    private static String sanitize(String value) {
        return value.replaceAll("[^A-Za-z0-9._-]", "_");
    }

    record PreparedConfig(
            Path configPath,
            Path dumpPath,
            Path llmDumpPath,
            Path generatedTestsDirectory,
            String socketJson
    ) {
    }
}
