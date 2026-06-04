import org.json.JSONArray;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

public class Main {
    public static void main(String[] args) {
        Path inputPath = Paths.get("dump.json");
        Path outputDir = Paths.get(".");
        if (args.length >= 1 && args[0] != null && !args[0].isEmpty()) {
            inputPath = Paths.get(args[0]);
        }
        if (args.length >= 2 && args[1] != null && !args[1].isEmpty()) {
            outputDir = Paths.get(args[1]);
        }

        try {
            String jsonInput = Files.readString(inputPath);
            String generatedTest = JsonToJUnitGenerator.generate(jsonInput);
            System.out.println(generatedTest);

            String className = extractClassName(jsonInput);
            if (className != null) {
                Files.createDirectories(outputDir);
                Path outPath = outputDir.resolve(className + "Test.java");
                Files.writeString(outPath, generatedTest);
                System.out.println("\nТест сохранен в файл: " + outPath);
            }

        } catch (Exception e) {
            System.err.println("Ошибка при чтении или обработке файла: " + e.getMessage());
            e.printStackTrace();
        }
    }

    private static String extractClassName(String json) {
        try {
            JSONArray states = new JSONArray(json);
            if (states.length() > 0) {
                String simpleName = states.getJSONObject(0).optString("class_simple_name", "");
                if (simpleName != null && !simpleName.isEmpty()) return simpleName;
                String rawName = states.getJSONObject(0).getString("class");
                rawName = rawName.replace('.', '/');
                int slash = rawName.lastIndexOf('/');
                return slash >= 0 ? rawName.substring(slash + 1) : rawName;
            }
        } catch (Exception e) {
        }
        return null;
    }
}
