# JVMTI Tests generator

## Быстрое демо

Плагин предсобран для JDK 21/25 для arm64/x86 Linux и arm64 macOS.

<details>
<summary>Скачать одной командой:</summary>

```bash
mkdir -p .jvmti-dumper && \
JAVA_MAJOR="$(java -version 2>&1 | awk -F '[\".]' '/version/ {print $2}')" && \
case "$(uname -s)" in Linux) OS=linux ;; Darwin) OS=macos ;; *) echo "Unsupported OS: $(uname -s)" >&2; exit 1 ;; esac && \
case "$(uname -m)" in x86_64|amd64) ARCH=x64 ;; arm64|aarch64) ARCH=arm64 ;; *) echo "Unsupported arch: $(uname -m)" >&2; exit 1 ;; esac && \
case "$JAVA_MAJOR" in 21|25) ;; *) echo "Unsupported Java major version: $JAVA_MAJOR. Use Java 21 or 25." >&2; exit 1 ;; esac && \
if [ "$OS" = macos ] && [ "$ARCH" != arm64 ]; then echo "macOS prebuilt agent is available only for arm64." >&2; exit 1; fi && \
case "$OS" in linux) EXT=so ;; macos) EXT=dylib ;; esac && \
ASSET="libdumper-${OS}-${ARCH}-java${JAVA_MAJOR}.${EXT}" && \
curl -fL -o ".jvmti-dumper/$ASSET" "https://github.com/deevroman/jvmti-dev/releases/latest/download/$ASSET" && \
echo ".jvmti-dumper/$ASSET"
```

</details>

## Пример генерации

Создайте и создадим простой пример `Calculator.java`:

```bash
cat > Calculator.java <<'EOF'
public class Calculator {
    public int cnt = 0;

    public int add(int a, int b) {
        this.cnt += 1;
        return a + b;
    }

    public static void main(String[] args) {
        Calculator calc = new Calculator();
        int result = calc.add(2, 10000);
        System.out.println("Result: " + result);
        System.out.println("Count: " + calc.cnt);
    }
}
EOF
javac -g Calculator.java
```

## Конфиг метода

Создайте `runtime_config.json` одной командой:

```bash
cat > runtime_config.json <<'EOF'
{
  "target_class": "Calculator",
  "target_method": "add",
  "target_method_signature": "(II)I",
  "dump": "out/dump_calculator.json",
  "llm_dump": "out/dump_calculator.llm.txt"
}
EOF
```

## Запустим JVM

```bash
mkdir -p out && \
AGENT_LIB="$(find .jvmti-dumper -type f \( -name '*.so' -o -name '*.dylib' \) | head -n 1)" && \
java -Xint \
  "-agentpath:${AGENT_LIB}=out:out/agent.log,config_file:runtime_config.json" \
  -cp . Calculator
```

Ожидаемый вывод приложения:

```text
Result: 10002
Count: 1
```

Дампы будут в папке `out`

```text
out/dump_calculator.json
out/dump_calculator.llm.txt
out/agent.log
```

---

## Алгоритмическая генерация теста

Сейчас генератор тестов лежит в исходниках репозитория, поэтому для этого шага нужно склонировать проект:

```bash
git clone https://github.com/deevroman/jvmti-dev.git .jvmti-dumper-repo
```

Запустите генератор для полученного JSON dump:

```bash
mkdir -p generated_tests .jvmti-dumper-repo/.artifacts/generator_classes && \
javac \
  -cp .jvmti-dumper-repo/tests_generator/json-20230227.jar \
  -d .jvmti-dumper-repo/.artifacts/generator_classes \
  .jvmti-dumper-repo/tests_generator/JsonToJUnitGenerator.java \
  .jvmti-dumper-repo/tests_generator/Main.java && \
java \
  -cp .jvmti-dumper-repo/.artifacts/generator_classes:.jvmti-dumper-repo/tests_generator/json-20230227.jar \
  Main \
  out/dump_calculator.json \
  generated_tests
```

Должен появиться файл:

```text
generated_tests/CalculatorTest.java
```

## Запуск сгенерированного теста

```bash
mkdir -p test_classes && \
javac \
  -cp .jvmti-dumper-repo/tests_generator/lib/junit-platform-console-standalone-1.9.3.jar:.:generated_tests \
  -d test_classes \
  generated_tests/CalculatorTest.java
```

Запустите его через JUnit:

```bash
java -jar .jvmti-dumper-repo/tests_generator/lib/junit-platform-console-standalone-1.9.3.jar \
  --class-path test_classes:. \
  --select-class CalculatorTest
```

Успешный запуск должен завершиться с `1 tests successful` и `0 tests failed`.
