import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

public class InputStreamFileExample {

    public InputStream fileInput;
    public int lastReadByte = -1;

    public int readFirstByteAndClose() throws Exception {
        int value = fileInput.read();
        lastReadByte = value;
        fileInput.close();
        return value;
    }

    public static void main(String[] args) throws Exception {
        File temp = File.createTempFile("jvmti-input", ".txt");
        temp.deleteOnExit();
        Files.write(temp.toPath(), "Q".getBytes(StandardCharsets.UTF_8));

        InputStreamFileExample example = new InputStreamFileExample();
        example.fileInput = new FileInputStream(temp);
        int value = example.readFirstByteAndClose();

        System.out.println("Read byte: " + value);
        System.out.println("Stored byte: " + example.lastReadByte);
    }
}
