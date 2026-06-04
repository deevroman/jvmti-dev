import java.util.HashMap;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

public class CarController {

    public int trips = 0;
    public EngineState engine = new EngineState();
    public Map<String, Integer> limits = new HashMap<>();
    public List<String> modes = new ArrayList<>();
    public Set<Integer> thresholds = new LinkedHashSet<>();

    public CarController() {
        limits.put("idle", 800);
        limits.put("sport", 1200);
        modes.add("ECO");
        modes.add("NORMAL");
        thresholds.add(700);
        thresholds.add(900);
    }

    public int accelerate(int deltaRpm, int extraLoad) {
        trips += 1;
        engine.rpm += deltaRpm;
        limits.put("lastDelta", deltaRpm);
        limits.put("lastExtraLoad", extraLoad);
        if (extraLoad > 0) {
            engine.rpm += extraLoad;
        }
        engine.recentReadings[0] = engine.rpm;
        limits.put("currentRpm", engine.rpm);
        modes.add(extraLoad > 0 ? "BOOST" : "CRUISE");
        thresholds.add(engine.rpm);
        return engine.rpm;
    }

    public static void main(String[] args) {
        CarController controller = new CarController();
        int result = controller.accelerate(200, 50);
        System.out.println("RPM: " + result);
        System.out.println("Trips: " + controller.trips);
        System.out.println("Running: " + controller.engine.running);
    }
}
