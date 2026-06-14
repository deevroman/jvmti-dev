import io.aiven.klaw.helpers.CompareUtils;

public class KlawCompareUtilsRunner {
    public boolean evaluateFalse(String value) {
        return CompareUtils.isFalse(value);
    }

    public boolean evaluateTrue(String value) {
        return CompareUtils.isTrue(value);
    }

    public boolean compareEqual(int expected, String actual) {
        return CompareUtils.isEqual(expected, actual);
    }

    public boolean evaluateTrimmedFalse(String value) {
        return CompareUtils.isFalse(value.trim());
    }

    public boolean compareFalseAndTrue(String falseValue, String trueValue) {
        return CompareUtils.isFalse(falseValue) && CompareUtils.isTrue(trueValue);
    }

    public static void main(String[] args) {
        KlawCompareUtilsRunner runner = new KlawCompareUtilsRunner();
        long start = System.nanoTime();
        System.out.println("Klaw false result: " + runner.evaluateFalse("false"));
        long end = System.nanoTime();
        System.out.println("Time: " + (end - start) / 1_000_000.0 + " ms");

        start = System.nanoTime();
        System.out.println("Klaw true result: " + runner.evaluateTrue("true"));
        end = System.nanoTime();
        System.out.println("Time: " + (end - start) / 1_000_000.0 + " ms");

        start = System.nanoTime();
        System.out.println("Klaw equal result: " + runner.compareEqual(42, "42"));
        end = System.nanoTime();
        System.out.println("Time: " + (end - start) / 1_000_000.0 + " ms");

        start = System.nanoTime();
        System.out.println("Klaw trimmed false result: " + runner.evaluateTrimmedFalse(" false "));
        end = System.nanoTime();
        System.out.println("Time: " + (end - start) / 1_000_000.0 + " ms");

        start = System.nanoTime();
        System.out.println("Klaw false-and-true result: " + runner.compareFalseAndTrue("false", "true"));
        end = System.nanoTime();
        System.out.println("Time: " + (end - start) / 1_000_000.0 + " ms");
    }
}
