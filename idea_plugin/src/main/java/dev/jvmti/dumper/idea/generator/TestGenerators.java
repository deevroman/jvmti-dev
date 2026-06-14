package dev.jvmti.dumper.idea.generator;

import org.json.JSONArray;
import org.json.JSONObject;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

public final class TestGenerators {
    private TestGenerators() {
    }

    public static List<Path> generate(Path dumpPath, Path llmDumpPath, Path outputDirectory) throws Exception {
        Files.createDirectories(outputDirectory);

        String jsonDump = Files.readString(dumpPath, StandardCharsets.UTF_8);
        List<Path> generated = new ArrayList<>();

        Path algorithmicDirectory = outputDirectory.resolve("algorithmic");
        Files.createDirectories(algorithmicDirectory);
        String algorithmicSource = JsonToJUnitGenerator.generate(jsonDump);
        Path algorithmicOutputPath = algorithmicDirectory.resolve(extractClassName(jsonDump) + "Test.java");
        Files.writeString(algorithmicOutputPath, algorithmicSource, StandardCharsets.UTF_8);
        generated.add(algorithmicOutputPath);

        String geminiApiKey = System.getenv("GEMINI_API_KEY");
        if (geminiApiKey != null && !geminiApiKey.isBlank()) {
            Path llmDirectory = outputDirectory.resolve("llm");
            Files.createDirectories(llmDirectory);
            String llmDump = Files.isRegularFile(llmDumpPath)
                    ? Files.readString(llmDumpPath, StandardCharsets.UTF_8)
                    : "";
            String model = System.getenv("GEMINI_MODEL");
            String llmSource = LlmJUnitGenerator.generateWithGemini(jsonDump, llmDump, geminiApiKey, model);
            String llmClassName = LlmJUnitGenerator.extractTestClassName(jsonDump, llmSource);
            Path llmOutputPath = llmDirectory.resolve(llmClassName + ".java");
            Files.writeString(llmOutputPath, llmSource, StandardCharsets.UTF_8);
            generated.add(llmOutputPath);
        }

        return generated;
    }

    private static String extractClassName(String json) {
        JSONArray states = new JSONArray(json);
        if (states.length() == 0) {
            return "Generated";
        }

        JSONObject first = states.getJSONObject(0);
        String simpleName = first.optString("class_simple_name", "");
        if (simpleName != null && !simpleName.isBlank()) {
            return simpleName;
        }

        String rawName = first.optString("class", "Generated");
        rawName = rawName.replace('.', '/');
        int slash = rawName.lastIndexOf('/');
        return slash >= 0 ? rawName.substring(slash + 1) : rawName;
    }
}
