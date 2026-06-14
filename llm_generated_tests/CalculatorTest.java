import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class GeneratedCalculatorTest {

    @Test
    void testAdd_1() throws Exception {
        // Initial state of the Calculator instance
        Calculator calculator = new Calculator();
        calculator.cnt = 0;

        // Method arguments
        int a = 2;
        int b = 10000;

        // Call the method
        int result = calculator.add(a, b);

        // Assert the return value
        assertEquals(10002, result);

        // Assert the final state of the Calculator instance
        assertEquals(1, calculator.cnt);
    }
}