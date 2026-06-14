import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.google.gson.stream.JsonReader;
import java.io.BufferedWriter;
import java.io.IOException;
import java.io.StringReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.util.Queue;
import java.util.concurrent.ConcurrentLinkedQueue;

public class JsonParserRunner {
    public int extractValue(String json) {
        JsonObject root =
                CallSiteTiming.timed(
                                "JsonParserRunner.extractValue#JsonParser.parseString(Ljava/lang/String;)Lcom/google/gson/JsonElement;",
                                () -> JsonParser.parseString(json))
                        .getAsJsonObject();
        return root.get("value").getAsInt();
    }

    public int extractValueWithReader(String json) {
        try (StringReader reader = new StringReader(json)) {
            JsonObject root =
                    CallSiteTiming.timed(
                                    "JsonParserRunner.extractValueWithReader#JsonParser.parseReader(Ljava/io/Reader;)Lcom/google/gson/JsonElement;",
                                    () -> JsonParser.parseReader(reader))
                            .getAsJsonObject();
            return root.get("value").getAsInt();
        }
    }

    public int extractValueWithJsonReader(String json) {
        try (JsonReader reader = new JsonReader(new StringReader(json))) {
            JsonObject root =
                    CallSiteTiming.timed(
                                    "JsonParserRunner.extractValueWithJsonReader#JsonParser.parseReader(Lcom/google/gson/stream/JsonReader;)Lcom/google/gson/JsonElement;",
                                    () -> JsonParser.parseReader(reader))
                            .getAsJsonObject();
            return root.get("value").getAsInt();
        } catch (IOException exception) {
            throw new RuntimeException(exception);
        }
    }

    public String firstPropertyName(String json) {
        try (JsonReader reader = new JsonReader(new StringReader(json))) {
            reader.beginObject();
            return reader.nextName();
        } catch (IOException exception) {
            throw new RuntimeException(exception);
        }
    }

    public String extractNameWithReader(String json) {
        try (JsonReader reader = new JsonReader(new StringReader(json))) {
            reader.beginObject();
            reader.nextName();
            reader.nextInt();
            return reader.nextName() + "=" + reader.nextString();
        } catch (IOException exception) {
            throw new RuntimeException(exception);
        }
    }

    public static void main(String[] args) {
        String json = "{\"value\":42,\"name\":\"demo\"}";
        JsonParserRunner runner = new JsonParserRunner();
        long start = System.nanoTime();
        System.out.println("Parsed value: " + runner.extractValue(json));
        long end = System.nanoTime();
        System.out.println("Time: " + (end - start) / 1_000_000.0 + " ms");

        start = System.nanoTime();
        System.out.println("Parsed value via reader: " + runner.extractValueWithReader(json));
        end = System.nanoTime();
        System.out.println("Time: " + (end - start) / 1_000_000.0 + " ms");

        start = System.nanoTime();
        System.out.println("Parsed value via JsonReader: " + runner.extractValueWithJsonReader(json));
        end = System.nanoTime();
        System.out.println("Time: " + (end - start) / 1_000_000.0 + " ms");

        start = System.nanoTime();
        System.out.println("First property: " + runner.firstPropertyName(json));
        end = System.nanoTime();
        System.out.println("Time: " + (end - start) / 1_000_000.0 + " ms");

        start = System.nanoTime();
        System.out.println("Name field: " + runner.extractNameWithReader(json));
        end = System.nanoTime();
        System.out.println("Time: " + (end - start) / 1_000_000.0 + " ms");
    }

    @FunctionalInterface
    private interface ThrowingSupplier<T> {
        T get() throws Exception;
    }

    private static final class CallSiteTiming {
        private static final Queue<Record> RECORDS = new ConcurrentLinkedQueue<>();
        private static volatile boolean headerWritten = false;

        static {
            Runtime.getRuntime().addShutdownHook(new Thread(CallSiteTiming::flushToFile));
        }

        private static <T> T timed(String callSiteId, ThrowingSupplier<T> supplier) {
            long startNs = System.nanoTime();
            boolean threw = true;
            try {
                T result = supplier.get();
                threw = false;
                return result;
            } catch (RuntimeException | Error runtimeException) {
                throw runtimeException;
            } catch (Exception checkedException) {
                throw new RuntimeException(checkedException);
            } finally {
                long endNs = System.nanoTime();
                RECORDS.add(new Record(callSiteId, startNs, endNs, threw, Thread.currentThread().getName()));
            }
        }

        private static void flushToFile() {
            if (RECORDS.isEmpty()) {
                return;
            }

            String filePathRaw = System.getenv("CALLSITE_METRICS_FILE");
            if (filePathRaw == null || filePathRaw.isBlank()) {
                filePathRaw = "callsite_metrics.csv";
            }
            Path filePath = Paths.get(filePathRaw);

            try {
                Path parent = filePath.getParent();
                if (parent != null) {
                    Files.createDirectories(parent);
                }

                try (BufferedWriter writer =
                        Files.newBufferedWriter(
                                filePath,
                                StandardCharsets.UTF_8,
                                StandardOpenOption.CREATE,
                                StandardOpenOption.APPEND)) {
                    if (!headerWritten && Files.size(filePath) == 0) {
                        writer.write("call_site_id,thread,start_ns,end_ns,duration_ns,threw");
                        writer.newLine();
                        headerWritten = true;
                    }

                    Record record;
                    while ((record = RECORDS.poll()) != null) {
                        writer.write(record.toCsvLine());
                        writer.newLine();
                    }
                }
            } catch (IOException ioException) {
                System.err.println("Failed to write callsite metrics: " + ioException.getMessage());
            }
        }
    }

    private static final class Record {
        private final String callSiteId;
        private final long startNs;
        private final long endNs;
        private final boolean threw;
        private final String threadName;

        private Record(String callSiteId, long startNs, long endNs, boolean threw, String threadName) {
            this.callSiteId = callSiteId;
            this.startNs = startNs;
            this.endNs = endNs;
            this.threw = threw;
            this.threadName = threadName;
        }

        private String toCsvLine() {
            long durationNs = endNs - startNs;
            return csvEscape(callSiteId)
                    + ","
                    + csvEscape(threadName)
                    + ","
                    + startNs
                    + ","
                    + endNs
                    + ","
                    + durationNs
                    + ","
                    + threw;
        }

        private static String csvEscape(String value) {
            String escaped = value.replace("\"", "\"\"");
            return "\"" + escaped + "\"";
        }
    }
}
