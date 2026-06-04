import java.util.LinkedHashMap;
import java.util.Map;

public class SimpleMapExample {

    public Map<Integer, Integer> values = new LinkedHashMap<>();
    public int calls = 0;

    public SimpleMapExample() {
        values.put(1, 2);
        values.put(2, 3);
    }

    public int update(int delta) {
        calls += 1;
        values.put(1, values.get(1) + delta);
        values.put(3, values.get(1) + values.get(2));
        return values.get(3);
    }

    public static void main(String[] args) {
        SimpleMapExample example = new SimpleMapExample();
        int result = example.update(4);
        System.out.println("Result: " + result);
        System.out.println("Calls: " + example.calls);
    }
}
