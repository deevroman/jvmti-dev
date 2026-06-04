import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

public class SimpleClassTest {

    @Test
    public void generatedTest() throws Exception {

        SimpleClass obj = new SimpleClass();
        obj.sorokDva = 42;
        obj.False = false;
        obj.True = true;
        obj.Primes = new int[]{2, 3, 5, 7, 11};
        // setting object reference for ref
        java.lang.reflect.Field f_ref = SimpleClass.class.getDeclaredField("ref");
        f_ref.setAccessible(true);
        f_ref.set(obj, obj);

        int number = 4;
        boolean flag = true;

        int result = obj.increment(number, flag);

        assertEquals(37, result);

        assertEquals(84, obj.sorokDva);
        assertEquals(false, obj.False);
        assertEquals(true, obj.True);

    }
}
