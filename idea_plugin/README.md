# JVMTI Dumper IDEA Plugin

Prototype plugin for sending method runtime configs to the JVMTI dumper control socket.

Build:

```bash
cd idea_plugin
./gradlew buildPlugin
```

The installable plugin zip is produced under:

```text
idea_plugin/build/distributions/
```

Generated configs are written to:

```text
<opened-project-root>/jvmti-dumper-artifacts/configs/
```

Dump output paths sent to the agent point to:

```text
<opened-project-root>/jvmti-dumper-artifacts/dumps/
```

Generated tests are written to:

```text
<opened-project-root>/jvmti-dumper-artifacts/generated_tests/
```

Recommended JVM options for an IDEA run configuration:

```text
-Xint -agentpath:/absolute/path/to/libdumper.dylib=out:$PROJECT_DIR$/jvmti-dumper-artifacts/output.log,control_port:9009
```

Clicking the gutter icon:

```text
1. Saves runtime config JSON.
2. If no run configuration is currently running, checks the selected toolbar run configuration.
3. If selected VM options contain -agentpath:, starts that run configuration.
4. Sends the JSON to the agent control port.
5. Waits for the dump file and runs bundled test generators.
```
