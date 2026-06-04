public class EngineState {

    public int rpm;
    public boolean running;
    public int[] recentReadings;

    public EngineState() {
        this(800, true, new int[]{790, 795, 800});
    }

    public EngineState(int rpm, boolean running, int[] recentReadings) {
        this.rpm = rpm;
        this.running = running;
        this.recentReadings = recentReadings;
    }
}
