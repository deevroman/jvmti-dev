package dev.jvmti.dumper.idea;

import com.intellij.codeInsight.daemon.LineMarkerInfo;
import com.intellij.codeInsight.daemon.LineMarkerProvider;
import com.intellij.icons.AllIcons;
import com.intellij.notification.NotificationGroupManager;
import com.intellij.notification.NotificationType;
import com.intellij.openapi.editor.markup.GutterIconRenderer;
import com.intellij.openapi.progress.ProgressManager;
import com.intellij.openapi.project.Project;
import com.intellij.psi.PsiElement;
import com.intellij.psi.PsiIdentifier;
import com.intellij.psi.PsiMethod;
import org.jetbrains.annotations.NotNull;
import org.jetbrains.annotations.Nullable;

final class JvmtiDumperLineMarkerProvider implements LineMarkerProvider {
    private static final String NOTIFICATION_GROUP = "JVMTI Dumper";

    @Override
    public @Nullable LineMarkerInfo<?> getLineMarkerInfo(@NotNull PsiElement element) {
        if (!(element instanceof PsiIdentifier) || !(element.getParent() instanceof PsiMethod method)) {
            return null;
        }
        if (method.isConstructor() || method.getContainingClass() == null) {
            return null;
        }

        return new LineMarkerInfo<PsiElement>(
                element,
                element.getTextRange(),
                AllIcons.Actions.Dump,
                ignored -> "Send JVMTI dumper config for " + method.getName(),
                (event, clickedElement) -> sendConfig(clickedElement.getProject(), method),
                GutterIconRenderer.Alignment.LEFT,
                () -> "Send to JVMTI Dumper"
        );
    }

    private static void sendConfig(Project project, PsiMethod method) {
        try {
            PluginArtifacts.PreparedConfig preparedConfig = PluginArtifacts.prepare(project, method);
            ProgressManager.getInstance().run(new JvmtiDumperCaptureTask(project, preparedConfig));
        } catch (Exception exception) {
            notify(
                    project,
                    NotificationType.ERROR,
                    "JVMTI Dumper config failed",
                    exception.getMessage() == null ? exception.toString() : exception.getMessage()
            );
        }
    }

    private static void notify(Project project, NotificationType type, String title, String content) {
        NotificationGroupManager.getInstance()
                .getNotificationGroup(NOTIFICATION_GROUP)
                .createNotification(title, content, type)
                .notify(project);
    }
}
