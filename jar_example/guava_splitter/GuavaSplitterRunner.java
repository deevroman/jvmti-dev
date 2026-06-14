import com.google.common.base.Splitter;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Map;
import java.util.List;
import java.util.function.Supplier;

public class GuavaSplitterRunner {
    private static final Path FIXTURE_DIR =
            Paths.get(System.getProperty("java.io.tmpdir"), "guava_splitter_pipeline");
    private static final int LARGE_TOKEN_COUNT = 4096;

    public List<String> splitCsv() {
        return Splitter.on(',').trimResults().omitEmptyStrings().splitToList(loadLargeCsvInput());
    }

    public int countCsvTokens() {
        return Splitter.on(',').trimResults().omitEmptyStrings().splitToList(loadLargeCsvInput()).size();
    }

    public String firstCsvToken() {
        return Splitter.on(',').trimResults().omitEmptyStrings().splitToList(loadLargeCsvInput()).get(0);
    }

    public String splitKeyValuePairs() {
        Map<String, String> values =
                Splitter.on(',')
                        .trimResults()
                        .withKeyValueSeparator('=')
                        .split(loadLargeKeyValueInput());
        return values.get("key2048");
    }

    public String splitFixedLength() {
        return String.join("|", Splitter.fixedLength(2).splitToList(loadLargeFixedLengthInput()));
    }

    public static void main(String[] args) {
        GuavaSplitterRunner runner = new GuavaSplitterRunner();
        long start = System.nanoTime();
        List<String> tokens = runner.splitCsv();
        long end = System.nanoTime();
        System.out.println("Guava tokens size: " + tokens.size());
        System.out.println("Time: " + (end - start) / 1_000_000.0 + " ms");

        start = System.nanoTime();
        System.out.println("Guava token count: " + runner.countCsvTokens());
        end = System.nanoTime();
        System.out.println("Time: " + (end - start) / 1_000_000.0 + " ms");

        start = System.nanoTime();
        System.out.println("Guava first token: " + runner.firstCsvToken());
        end = System.nanoTime();
        System.out.println("Time: " + (end - start) / 1_000_000.0 + " ms");

        start = System.nanoTime();
        System.out.println("Guava selected value: " + runner.splitKeyValuePairs());
        end = System.nanoTime();
        System.out.println("Time: " + (end - start) / 1_000_000.0 + " ms");

        start = System.nanoTime();
        System.out.println("Guava fixed chunks prefix: " + runner.splitFixedLength().substring(0, 14));
        end = System.nanoTime();
        System.out.println("Time: " + (end - start) / 1_000_000.0 + " ms");
    }

    private static String loadLargeCsvInput() {
        return loadLargeLine(
                "large.csv.txt",
                () -> {
                    StringBuilder builder = new StringBuilder();
                    for (int i = 0; i < LARGE_TOKEN_COUNT; i++) {
                        if (i > 0) {
                            builder.append(", ");
                        }
                        builder.append("token").append(i);
                    }
                    return builder.toString();
                });
    }

    private static String loadLargeKeyValueInput() {
        return loadLargeLine(
                "large.map.txt",
                () -> {
                    StringBuilder builder = new StringBuilder();
                    for (int i = 0; i < LARGE_TOKEN_COUNT; i++) {
                        if (i > 0) {
                            builder.append(", ");
                        }
                        builder.append("key").append(i).append("=").append("value").append(i);
                    }
                    return builder.toString();
                });
    }

    private static String loadLargeFixedLengthInput() {
        return loadLargeLine(
                "large.fixed.txt",
                () -> {
                    StringBuilder builder = new StringBuilder();
                    for (int i = 0; i < LARGE_TOKEN_COUNT; i++) {
                        builder.append("AB");
                    }
                    return builder.toString();
                });
    }

    private static String loadLargeLine(String fileName, Supplier<String> supplier) {
        Path path = FIXTURE_DIR.resolve(fileName);
        try {
            Files.createDirectories(FIXTURE_DIR);
            if (!Files.exists(path)) {
                Files.writeString(path, supplier.get() + System.lineSeparator(), StandardCharsets.UTF_8);
            }
            return Files.readAllLines(path, StandardCharsets.UTF_8).get(0);
        } catch (IOException exception) {
            throw new RuntimeException("Failed to prepare Guava fixture: " + path, exception);
        }
    }
}
