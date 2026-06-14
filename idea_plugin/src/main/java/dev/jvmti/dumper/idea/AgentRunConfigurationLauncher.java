package dev.jvmti.dumper.idea;

import com.intellij.execution.ExecutionManager;
import com.intellij.execution.ProgramRunnerUtil;
import com.intellij.execution.RunManager;
import com.intellij.execution.RunnerAndConfigurationSettings;
import com.intellij.execution.configurations.RunConfiguration;
import com.intellij.execution.executors.DefaultRunExecutor;
import com.intellij.openapi.application.ApplicationManager;
import com.intellij.openapi.application.ModalityState;
import com.intellij.openapi.progress.ProgressIndicator;
import com.intellij.openapi.project.Project;
import org.jdom.Element;

import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

final class AgentRunConfigurationLauncher {
    private static final int DEFAULT_CONTROL_PORT = 9009;
    private static final Pattern CONTROL_PORT_PATTERN =
            Pattern.compile("(?:^|[\\s,])control_port:(\\d{1,5})(?:$|[\\s,])");

    private AgentRunConfigurationLauncher() {
    }

    static AgentEndpoint ensureAgentEndpoint(Project project, ProgressIndicator indicator) throws Exception {
        RunnerAndConfigurationSettings selectedSettings = RunManager.getInstance(project).getSelectedConfiguration();
        AgentOptions selectedOptions = selectedSettings == null
                ? AgentOptions.defaultOptions()
                : inspect(selectedSettings.getConfiguration());

        boolean hasRunningConfiguration = !ExecutionManager.getInstance(project)
                .getRunningDescriptors(settings -> true)
                .isEmpty();

        if (hasRunningConfiguration) {
            return new AgentEndpoint(selectedOptions.controlPort(), false);
        }

        if (selectedSettings == null) {
            throw new IllegalStateException("No selected run configuration in the toolbar");
        }
        if (!selectedOptions.hasAgentPath()) {
            throw new IllegalStateException(
                    "Selected run configuration '" + selectedSettings.getName() +
                            "' has no -agentpath option. Add JVMTI dumper VM options first."
            );
        }

        if (indicator != null) {
            indicator.setText("Starting run configuration");
            indicator.setText2(selectedSettings.getName());
        }

        DefaultRunExecutor executor = (DefaultRunExecutor) DefaultRunExecutor.getRunExecutorInstance();
        selectedSettings.checkSettings(executor);

        Throwable[] error = new Throwable[1];
        ApplicationManager.getApplication().invokeAndWait(() -> {
            try {
                ProgramRunnerUtil.executeConfiguration(project, selectedSettings, executor);
            } catch (Throwable throwable) {
                error[0] = throwable;
            }
        }, ModalityState.defaultModalityState());

        if (error[0] != null) {
            if (error[0] instanceof Exception exception) {
                throw exception;
            }
            throw new RuntimeException(error[0]);
        }

        return new AgentEndpoint(selectedOptions.controlPort(), true);
    }

    private static AgentOptions inspect(RunConfiguration configuration) {
        String text = collectConfigurationText(configuration).toLowerCase(Locale.ROOT);
        boolean hasAgentPath = text.contains("-agentpath:");

        int controlPort = DEFAULT_CONTROL_PORT;
        Matcher matcher = CONTROL_PORT_PATTERN.matcher(text);
        if (matcher.find()) {
            try {
                int parsed = Integer.parseInt(matcher.group(1));
                if (parsed > 0 && parsed <= 65535) {
                    controlPort = parsed;
                }
            } catch (NumberFormatException ignored) {
            }
        }

        return new AgentOptions(hasAgentPath, controlPort);
    }

    private static String collectConfigurationText(RunConfiguration configuration) {
        List<String> chunks = new ArrayList<>();
        chunks.add(configuration.getName());
        chunks.add(configuration.getClass().getName());
        collectGetterText(configuration, chunks);
        collectXmlText(configuration, chunks);
        return String.join("\n", chunks);
    }

    private static void collectGetterText(RunConfiguration configuration, List<String> chunks) {
        String[] methodNames = {
                "getVMParameters",
                "getVMParametersString",
                "getVmParameters",
                "getVmParametersString",
                "getVMOptions",
                "getVmOptions"
        };

        for (String methodName : methodNames) {
            try {
                Method method = configuration.getClass().getMethod(methodName);
                if (method.getParameterCount() == 0 && method.getReturnType() == String.class) {
                    Object value = method.invoke(configuration);
                    if (value instanceof String string && !string.isBlank()) {
                        chunks.add(string);
                    }
                }
            } catch (ReflectiveOperationException ignored) {
            }
        }
    }

    private static void collectXmlText(RunConfiguration configuration, List<String> chunks) {
        try {
            Element root = new Element("configuration");
            configuration.writeExternal(root);
            appendElementText(root, chunks);
        } catch (Exception ignored) {
        }
    }

    private static void appendElementText(Element element, List<String> chunks) {
        chunks.add(element.getName());
        if (element.getText() != null && !element.getText().isBlank()) {
            chunks.add(element.getText());
        }
        for (org.jdom.Attribute attribute : element.getAttributes()) {
            chunks.add(attribute.getName());
            chunks.add(attribute.getValue());
        }
        for (Element child : element.getChildren()) {
            appendElementText(child, chunks);
        }
    }

    record AgentEndpoint(int controlPort, boolean startedConfiguration) {
    }

    private record AgentOptions(boolean hasAgentPath, int controlPort) {
        private static AgentOptions defaultOptions() {
            return new AgentOptions(false, DEFAULT_CONTROL_PORT);
        }
    }
}
