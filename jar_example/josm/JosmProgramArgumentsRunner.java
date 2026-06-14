public class JosmProgramArgumentsRunner {
    public static void main(String[] args) {
        new org.openstreetmap.josm.gui.ProgramArguments(
                "--help",
                "--version",
                "--set=language=en"
        );
    }
}
