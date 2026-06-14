package dev.jvmti.dumper.idea;

import java.nio.file.Path;

final class RuntimeConfigPayload {
    private final String targetClass;
    private final String targetMethod;
    private final String targetMethodSignature;
    private final Path dumpPath;
    private final Path llmDumpPath;

    RuntimeConfigPayload(
            String targetClass,
            String targetMethod,
            String targetMethodSignature,
            Path dumpPath,
            Path llmDumpPath
    ) {
        this.targetClass = targetClass;
        this.targetMethod = targetMethod;
        this.targetMethodSignature = targetMethodSignature;
        this.dumpPath = dumpPath;
        this.llmDumpPath = llmDumpPath;
    }

    String toJson() {
        return "{\n" +
                "  \"target_class\": " + quote(targetClass) + ",\n" +
                "  \"target_method\": " + quote(targetMethod) + ",\n" +
                "  \"target_method_signature\": " + quote(targetMethodSignature) + ",\n" +
                "  \"dump\": " + quote(dumpPath.toString()) + ",\n" +
                "  \"llm_dump\": " + quote(llmDumpPath.toString()) + "\n" +
                "}";
    }

    String toSocketJson() {
        return "{" +
                "\"target_class\":" + quote(targetClass) + "," +
                "\"target_method\":" + quote(targetMethod) + "," +
                "\"target_method_signature\":" + quote(targetMethodSignature) + "," +
                "\"dump\":" + quote(dumpPath.toString()) + "," +
                "\"llm_dump\":" + quote(llmDumpPath.toString()) +
                "}";
    }

    private static String quote(String value) {
        StringBuilder result = new StringBuilder(value.length() + 2);
        result.append('"');
        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);
            switch (c) {
                case '"' -> result.append("\\\"");
                case '\\' -> result.append("\\\\");
                case '\b' -> result.append("\\b");
                case '\f' -> result.append("\\f");
                case '\n' -> result.append("\\n");
                case '\r' -> result.append("\\r");
                case '\t' -> result.append("\\t");
                default -> {
                    if (c < 0x20) {
                        result.append(String.format("\\u%04x", (int) c));
                    } else {
                        result.append(c);
                    }
                }
            }
        }
        result.append('"');
        return result.toString();
    }
}
