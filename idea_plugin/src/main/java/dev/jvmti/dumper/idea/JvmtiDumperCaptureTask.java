package dev.jvmti.dumper.idea;

import com.intellij.notification.NotificationGroupManager;
import com.intellij.notification.NotificationType;
import com.intellij.openapi.application.ApplicationManager;
import com.intellij.openapi.fileEditor.FileEditorManager;
import com.intellij.openapi.progress.ProcessCanceledException;
import com.intellij.openapi.progress.ProgressIndicator;
import com.intellij.openapi.progress.Task;
import com.intellij.openapi.project.Project;
import com.intellij.openapi.vfs.LocalFileSystem;
import com.intellij.openapi.vfs.VirtualFile;
import dev.jvmti.dumper.idea.generator.TestGenerators;
import org.jetbrains.annotations.NotNull;

import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.List;

final class JvmtiDumperCaptureTask extends Task.Backgroundable {
    private static final String NOTIFICATION_GROUP = "JVMTI Dumper";
    private static final Duration POLL_INTERVAL = Duration.ofMillis(500);
    private static final Duration CONTROL_SOCKET_TIMEOUT = Duration.ofSeconds(60);

    private final PluginArtifacts.PreparedConfig config;

    JvmtiDumperCaptureTask(Project project, PluginArtifacts.PreparedConfig config) {
        super(project, "Waiting for JVMTI dump", true);
        this.config = config;
    }

    @Override
    public void run(@NotNull ProgressIndicator indicator) {
        try {
            indicator.setIndeterminate(true);
            AgentRunConfigurationLauncher.AgentEndpoint endpoint =
                    AgentRunConfigurationLauncher.ensureAgentEndpoint(getProject(), indicator);

            indicator.checkCanceled();
            indicator.setText(endpoint.startedConfiguration()
                    ? "Waiting for JVMTI agent control socket"
                    : "Sending JVMTI dumper config");
            String response = RuntimeConfigSender.sendWithRetry(
                    config.socketJson(),
                    endpoint.controlPort(),
                    CONTROL_SOCKET_TIMEOUT,
                    indicator
            );

            indicator.checkCanceled();
            indicator.setText("Waiting for dump file");
            indicator.setText2(config.dumpPath().toString());
            waitForStableFile(config.dumpPath(), indicator);

            indicator.checkCanceled();
            indicator.setText("Generating tests");
            List<Path> generated = TestGenerators.generate(
                    config.dumpPath(),
                    config.llmDumpPath(),
                    config.generatedTestsDirectory()
            );
            openFirstGeneratedTest(generated);

            notify(
                    NotificationType.INFORMATION,
                    "JVMTI dump captured",
                    "Config: " + config.configPath() +
                            "\nDump: " + config.dumpPath() +
                            "\nTests: " + config.generatedTestsDirectory() +
                            "\nGenerated files: " + generated.size() +
                            "\nSocket response: " + response
            );
        } catch (ProcessCanceledException canceled) {
            throw canceled;
        } catch (Exception exception) {
            notify(
                    NotificationType.ERROR,
                    "JVMTI Dumper workflow failed",
                    exception.getMessage() == null ? exception.toString() : exception.getMessage()
            );
        }
    }

    @Override
    public void onCancel() {
        notify(
                NotificationType.WARNING,
                "JVMTI dump wait cancelled",
                "Config was saved: " + config.configPath()
        );
    }

    private static void waitForStableFile(Path path, ProgressIndicator indicator) throws Exception {
        long previousSize = -1L;
        int stablePolls = 0;

        while (true) {
            indicator.checkCanceled();
            if (Files.isRegularFile(path)) {
                long size = Files.size(path);
                if (size > 0 && size == previousSize) {
                    stablePolls++;
                    if (stablePolls >= 2) {
                        return;
                    }
                } else {
                    stablePolls = 0;
                }
                previousSize = size;
                indicator.setText2(path + " (" + size + " bytes)");
            }
            Thread.sleep(POLL_INTERVAL.toMillis());
        }
    }

    private void openFirstGeneratedTest(List<Path> generated) {
        if (generated.isEmpty()) {
            return;
        }

        Path path = generated.get(0);
        ApplicationManager.getApplication().invokeLater(() -> {
            VirtualFile virtualFile = LocalFileSystem.getInstance().refreshAndFindFileByNioFile(path);
            if (virtualFile != null) {
                FileEditorManager.getInstance(getProject()).openFile(virtualFile, true);
            }
        });
    }

    private void notify(NotificationType type, String title, String content) {
        NotificationGroupManager.getInstance()
                .getNotificationGroup(NOTIFICATION_GROUP)
                .createNotification(title, content, type)
                .notify(getProject());
    }
}
