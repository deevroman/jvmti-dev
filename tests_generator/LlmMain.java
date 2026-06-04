import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

public class LlmMain {
    public static void main(String[] args) {
        CliArgs cliArgs;
        try {
            cliArgs = CliArgs.parse(args);
        } catch (IllegalArgumentException e) {
            System.err.println(e.getMessage());
            printUsage();
            System.exit(2);
            return;
        }

        try {
            String jsonDump = Files.readString(cliArgs.inputPath);
            String llmDump = cliArgs.llmDumpPath == null ? "" : Files.readString(cliArgs.llmDumpPath);
            String source = LlmJUnitGenerator.generateWithGemini(
                    jsonDump,
                    llmDump,
                    cliArgs.geminiApiKey,
                    cliArgs.model
            );
            String className = LlmJUnitGenerator.extractTestClassName(jsonDump, source);

            Files.createDirectories(cliArgs.outputDir);
            Path outputPath = cliArgs.outputDir.resolve(className + ".java");
            Files.writeString(outputPath, source);
            System.out.println(source);
            System.out.println("\nLLM test saved to: " + outputPath);
        } catch (Exception e) {
            System.err.println("Failed to generate LLM test: " + e.getMessage());
            e.printStackTrace();
            System.exit(1);
        }
    }

    private static void printUsage() {
        System.err.println("Usage:");
        System.err.println("  java ... LlmMain --input <dump.json> --output-dir <dir> [--llm-dump <dump.llm.txt>] [--gemini-api-key <key>] [--model <model>]");
        System.err.println();
        System.err.println("Environment:");
        System.err.println("  GEMINI_API_KEY can be used instead of --gemini-api-key.");
        System.err.println("  Default model: " + LlmJUnitGenerator.defaultModel());
    }

    private static final class CliArgs {
        private Path inputPath;
        private Path outputDir = Paths.get(".");
        private Path llmDumpPath;
        private String geminiApiKey = System.getenv("GEMINI_API_KEY");
        private String model = LlmJUnitGenerator.defaultModel();

        private static CliArgs parse(String[] args) {
            CliArgs result = new CliArgs();
            for (int i = 0; i < args.length; i++) {
                String arg = args[i];
                switch (arg) {
                    case "--input":
                        result.inputPath = Paths.get(requiredValue(args, ++i, arg));
                        break;
                    case "--output-dir":
                        result.outputDir = Paths.get(requiredValue(args, ++i, arg));
                        break;
                    case "--llm-dump":
                        result.llmDumpPath = Paths.get(requiredValue(args, ++i, arg));
                        break;
                    case "--gemini-api-key":
                        result.geminiApiKey = requiredValue(args, ++i, arg);
                        break;
                    case "--model":
                        result.model = requiredValue(args, ++i, arg);
                        break;
                    case "-h":
                    case "--help":
                        printUsage();
                        System.exit(0);
                        break;
                    default:
                        throw new IllegalArgumentException("Unknown argument: " + arg);
                }
            }
            if (result.inputPath == null) {
                throw new IllegalArgumentException("Missing required argument: --input");
            }
            return result;
        }

        private static String requiredValue(String[] args, int index, String option) {
            if (index >= args.length || args[index].isBlank()) {
                throw new IllegalArgumentException("Missing value for " + option);
            }
            return args[index];
        }
    }
}
