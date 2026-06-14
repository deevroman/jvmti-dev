package dev.jvmti.dumper.idea;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.ConnectException;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.nio.charset.StandardCharsets;
import java.time.Duration;

import com.intellij.openapi.progress.ProgressIndicator;

final class RuntimeConfigSender {
    private static final String HOST = "127.0.0.1";
    private static final int PORT = 9009;
    private static final int TIMEOUT_MS = 5_000;

    private RuntimeConfigSender() {
    }

    static String send(String json) throws IOException {
        return sendOnce(json, PORT);
    }

    static String sendWithRetry(
            String json,
            int port,
            Duration timeout,
            ProgressIndicator indicator
    ) throws IOException, InterruptedException {
        long deadline = System.nanoTime() + timeout.toNanos();
        IOException lastException = null;

        while (System.nanoTime() < deadline) {
            if (indicator != null) {
                indicator.checkCanceled();
                indicator.setText2("127.0.0.1:" + port);
            }

            try {
                return sendOnce(json, port);
            } catch (ConnectException | SocketTimeoutException exception) {
                lastException = exception;
                Thread.sleep(500);
            }
        }

        if (lastException != null) {
            throw new IOException("Timed out waiting for JVMTI agent control socket on 127.0.0.1:" + port, lastException);
        }
        throw new IOException("Timed out waiting for JVMTI agent control socket on 127.0.0.1:" + port);
    }

    private static String sendOnce(String json, int port) throws IOException {
        try (Socket socket = new Socket()) {
            socket.connect(new InetSocketAddress(HOST, port), TIMEOUT_MS);
            socket.setSoTimeout(TIMEOUT_MS);

            OutputStream output = socket.getOutputStream();
            output.write(json.getBytes(StandardCharsets.UTF_8));
            output.write('\n');
            output.flush();
            socket.shutdownOutput();

            InputStream input = socket.getInputStream();
            byte[] buffer = input.readNBytes(4096);
            if (buffer.length == 0) {
                return "Config sent to " + HOST + ":" + port;
            }
            return new String(buffer, StandardCharsets.UTF_8).trim();
        }
    }
}
