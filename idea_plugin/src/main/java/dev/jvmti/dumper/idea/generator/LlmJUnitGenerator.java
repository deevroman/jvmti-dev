package dev.jvmti.dumper.idea.generator;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.IOException;
import java.net.URI;
import java.net.URLEncoder;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.time.Duration;

public class LlmJUnitGenerator {
    private static final String DEFAULT_MODEL = "gemini-2.5-flash";
    private static final String GEMINI_API_BASE = "https://generativelanguage.googleapis.com/v1beta/models/";

    public static String generateWithGemini(
            String jsonDump,
            String llmDump,
            String apiKey,
            String model
    ) throws IOException, InterruptedException {
        if (apiKey == null || apiKey.isBlank()) {
            throw new IllegalArgumentException("Gemini API key is required. Pass --gemini-api-key or set GEMINI_API_KEY.");
        }
        String resolvedModel = model == null || model.isBlank() ? DEFAULT_MODEL : model;
        String prompt = buildPrompt(jsonDump, llmDump);
        JSONObject requestBody = new JSONObject()
                .put("systemInstruction", new JSONObject()
                        .put("parts", new JSONArray()
                                .put(new JSONObject().put("text", systemPrompt()))))
                .put("contents", new JSONArray()
                        .put(new JSONObject()
                                .put("role", "user")
                                .put("parts", new JSONArray()
                                        .put(new JSONObject().put("text", prompt)))))
                .put("generationConfig", new JSONObject()
                        .put("temperature", 0.1)
                        .put("topP", 0.8));

        String encodedModel = URLEncoder.encode(resolvedModel, StandardCharsets.UTF_8).replace("+", "%20");
        URI uri = URI.create(GEMINI_API_BASE + encodedModel + ":generateContent");
        HttpRequest request = HttpRequest.newBuilder(uri)
                .timeout(Duration.ofSeconds(120))
                .header("Content-Type", "application/json")
                .header("x-goog-api-key", apiKey)
                .POST(HttpRequest.BodyPublishers.ofString(requestBody.toString(), StandardCharsets.UTF_8))
                .build();

        HttpResponse<String> response = HttpClient.newHttpClient()
                .send(request, HttpResponse.BodyHandlers.ofString(StandardCharsets.UTF_8));
        if (response.statusCode() < 200 || response.statusCode() >= 300) {
            throw new IOException("Gemini API returned HTTP " + response.statusCode() + ": " + response.body());
        }

        return extractJavaSource(response.body());
    }

    static String defaultModel() {
        return DEFAULT_MODEL;
    }

    private static String buildPrompt(String jsonDump, String llmDump) {
        StringBuilder prompt = new StringBuilder();
        prompt.append("Generate a JUnit 5 test from the following JVM method dump.\n\n");
        prompt.append("Requirements:\n");
        prompt.append("- Return exactly one Java source file.\n");
        prompt.append("- Do not wrap the answer in Markdown fences.\n");
        prompt.append("- The test must compile with JUnit Jupiter.\n");
        prompt.append("- Use the observed method arguments, return value, and object state transitions.\n");
        prompt.append("- Prefer direct field access for public fields and reflection helpers for private fields.\n");
        prompt.append("- Keep helper methods inside the generated test class.\n");
        prompt.append("- Do not define, stub, mock, or shadow the target class or its domain classes.\n");
        prompt.append("- Assume production classes are already available on the test classpath.\n");
        prompt.append("- Do not invent unavailable external services or dependencies.\n\n");
        if (llmDump != null && !llmDump.isBlank()) {
            prompt.append("LLM-readable dump:\n");
            prompt.append("```\n").append(llmDump).append("\n```\n\n");
        }
        prompt.append("Machine-readable JSON dump:\n");
        prompt.append("```json\n").append(jsonDump).append("\n```\n");
        return prompt.toString();
    }

    private static String systemPrompt() {
        return "You generate concise, deterministic Java JUnit 5 tests from captured JVM state. "
                + "Return only compilable Java code, no explanations.";
    }

    private static String extractJavaSource(String responseBody) throws IOException {
        JSONObject response = new JSONObject(responseBody);
        JSONArray candidates = response.optJSONArray("candidates");
        if (candidates == null || candidates.length() == 0) {
            throw new IOException("Gemini response has no candidates: " + responseBody);
        }

        JSONObject content = candidates.getJSONObject(0).optJSONObject("content");
        if (content == null) {
            throw new IOException("Gemini response candidate has no content: " + responseBody);
        }
        JSONArray parts = content.optJSONArray("parts");
        if (parts == null || parts.length() == 0) {
            throw new IOException("Gemini response content has no parts: " + responseBody);
        }

        StringBuilder text = new StringBuilder();
        for (int i = 0; i < parts.length(); i++) {
            String partText = parts.getJSONObject(i).optString("text", "");
            if (!partText.isEmpty()) {
                if (text.length() > 0) text.append('\n');
                text.append(partText);
            }
        }
        String source = stripMarkdownFence(text.toString().trim());
        if (source.isBlank()) {
            throw new IOException("Gemini response text is empty: " + responseBody);
        }
        return source;
    }

    private static String stripMarkdownFence(String text) {
        String trimmed = text.trim();
        if (!trimmed.startsWith("```")) {
            return trimmed;
        }

        int firstLineEnd = trimmed.indexOf('\n');
        if (firstLineEnd < 0) {
            return trimmed;
        }
        int lastFence = trimmed.lastIndexOf("```");
        if (lastFence <= firstLineEnd) {
            return trimmed;
        }
        return trimmed.substring(firstLineEnd + 1, lastFence).trim();
    }

    static String extractTestClassName(String jsonDump, String generatedSource) {
        String fromSource = extractPublicClassName(generatedSource);
        if (fromSource != null) return fromSource;

        try {
            JSONArray states = new JSONArray(jsonDump);
            if (states.length() > 0) {
                JSONObject first = states.getJSONObject(0);
                String simpleName = first.optString("class_simple_name", "");
                if (!simpleName.isBlank()) return simpleName + "Test";
                String rawName = first.optString("class", "");
                if (!rawName.isBlank()) {
                    rawName = rawName.replace('.', '/');
                    int slash = rawName.lastIndexOf('/');
                    String base = slash >= 0 ? rawName.substring(slash + 1) : rawName;
                    return base + "Test";
                }
            }
        } catch (Exception ignored) {
        }
        return "GeneratedTest";
    }

    private static String extractPublicClassName(String source) {
        String marker = "public class ";
        int index = source.indexOf(marker);
        if (index < 0) return null;
        int start = index + marker.length();
        int end = start;
        while (end < source.length() && Character.isJavaIdentifierPart(source.charAt(end))) {
            end++;
        }
        if (end <= start) return null;
        return source.substring(start, end);
    }
}
