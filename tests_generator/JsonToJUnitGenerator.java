import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.nio.charset.StandardCharsets;
import java.util.Set;

public class JsonToJUnitGenerator {

    public static String generate(String json) {
        JSONArray states = new JSONArray(json);

        JSONObject start = states.getJSONObject(0);
        JSONObject exit = states.getJSONObject(1);

        String className = start.getString("class");
        String classSimpleName = start.optString("class_simple_name", "");
        if (classSimpleName == null || classSimpleName.isEmpty()) {
            String normalized = className.replace('.', '/');
            int slash = normalized.lastIndexOf('/');
            classSimpleName = slash >= 0 ? normalized.substring(slash + 1) : normalized;
        }
        String classFqcn = start.optString("class_fqcn", "");
        if (classFqcn == null || classFqcn.isEmpty()) {
            classFqcn = className.replace('/', '.');
        }
        String methodName = start.getString("method");
        long thisObjectId = start.optLong("this_object_id", -1L);
        String returnDescriptor = "I";
        if (start.has("method_return_type")) {
            returnDescriptor = start.getJSONObject("method_return_type").optString("descriptor", "I");
        }
        Map<Long, JSONObject> objectRefsById = indexObjectRefsById(start.optJSONArray("object_refs"));
        Map<Long, JSONObject> mockObjectsById = indexMockObjectsById(exit.optJSONArray("mock_objects"));
        List<String> mockVerificationLines = new ArrayList<>();

        StringBuilder sb = new StringBuilder();

        sb.append("import org.junit.jupiter.api.Test;\n");
        sb.append("import static org.junit.jupiter.api.Assertions.*;\n\n");
        sb.append("public class ").append(classSimpleName).append("Test {\n\n");

        sb.append("    @Test\n");
        sb.append("    public void generatedTest() throws Exception {\n\n");

        // создание объекта
        sb.append("        ").append(classFqcn)
                .append(" obj = new ").append(classFqcn).append("();\n");

        // сеттим поля из method_start
        JSONObject objectStart = start.getJSONArray("object_refs").getJSONObject(0);
        JSONArray fieldsStart = objectStart.getJSONArray("fields");

        for (int i = 0; i < fieldsStart.length(); i++) {
            JSONObject field = fieldsStart.getJSONObject(i);
            String name = field.getString("name");
            String type = field.getString("java_type_name");
            boolean publicField = isPublicField(field);
            JSONObject mockObject = resolveMockObjectForField(field, mockObjectsById);

            if (mockObject != null) {
                if (appendMockFieldInitialization(sb, field, mockObject, thisObjectId, publicField, mockVerificationLines)) continue;
            }

            if (field.has("primitive_value") && isPrimitiveDescriptor(type)) {
                if (publicField) {
                    sb.append("        obj.").append(name).append(" = ")
                            .append(primitiveLiteral(type, field.get("primitive_value"))).append(";\n");
                } else {
                    sb.append("        setField(obj, ").append(javaStringLiteral(name)).append(", ")
                            .append(primitiveLiteral(type, field.get("primitive_value"))).append(");\n");
                }
            } else if (field.has("array")) {
                String arrayLiteral = buildArrayLiteral(field.getJSONObject("array"));
                if (arrayLiteral != null) {
                    if (publicField) {
                        sb.append("        obj.").append(name).append(" = ").append(arrayLiteral).append(";\n");
                    } else {
                        sb.append("        setField(obj, ").append(javaStringLiteral(name)).append(", ").append(arrayLiteral).append(");\n");
                    }
                }
            } else if (field.has("custom")) {
                appendCustomFieldInitialization(sb, field, thisObjectId, publicField);
            } else if (field.has("object_id")) {
                long objectId = field.optLong("object_id", -1L);
                if (objectId == thisObjectId) {
                    if (publicField) {
                        sb.append("        obj.").append(name).append(" = obj;\n");
                    } else {
                        sb.append("        setField(obj, ").append(javaStringLiteral(name)).append(", obj);\n");
                    }
                } else {
                    String fqcn = extractFieldFqcn(field);
                    if (!fqcn.isEmpty()) {
                        String refVar = "ref_" + sanitizeIdentifier(name);
                        String refType = publicField ? fqcn : "Object";
                        sb.append("        ").append(refType).append(" ").append(refVar).append(" = (").append(refType)
                                .append(") Class.forName(\"")
                                .append(fqcn).append("\").getDeclaredConstructor().newInstance();\n");
                        if (publicField) {
                            sb.append("        obj.").append(name).append(" = ").append(refVar).append(";\n");
                        } else {
                            sb.append("        setField(obj, ").append(javaStringLiteral(name)).append(", ").append(refVar).append(");\n");
                        }
                    }
                }
            }
        }

        sb.append("\n");

        // аргументы
        JSONArray args = start.getJSONArray("arguments");
        String[] argNames = new String[args.length()];
        for (int i = 0; i < args.length(); i++) {
            JSONObject arg = args.getJSONObject(i);
            String name = arg.getString("name");
            String type = arg.getString("java_type_name");
            argNames[i] = name;

            if (arg.has("primitive_value") && isPrimitiveDescriptor(type)) {
                sb.append("        ").append(descriptorToJavaType(type)).append(" ").append(name)
                        .append(" = ").append(primitiveLiteral(type, arg.get("primitive_value"))).append(";\n");
            } else if (arg.has("array")) {
                String arrayLiteral = buildArrayLiteral(arg.getJSONObject("array"));
                String javaType = descriptorToJavaType(type);
                if (arrayLiteral != null && javaType != null) {
                    sb.append("        ").append(javaType).append(" ").append(name)
                            .append(" = ").append(arrayLiteral).append(";\n");
                } else if (javaType != null) {
                    sb.append("        ").append(javaType).append(" ").append(name).append(" = null;\n");
                }
            } else if (arg.has("object_id")) {
                String javaType = descriptorToJavaType(type);
                long objectId = arg.optLong("object_id", -1L);
                JSONObject objectRef = objectRefsById.get(objectId);
                String literal = objectArgumentLiteral(type, objectRef);
                if (javaType != null && literal != null) {
                    sb.append("        ").append(javaType).append(" ").append(name)
                            .append(" = ").append(literal).append(";\n");
                } else if (javaType != null) {
                    sb.append("        ").append(javaType).append(" ").append(name).append(" = null;\n");
                }
            } else {
                String javaType = descriptorToJavaType(type);
                if (javaType != null) {
                    sb.append("        ").append(javaType).append(" ").append(name).append(" = null;\n");
                }
            }
        }

        sb.append("\n");

        // вызов метода
        boolean hasReturn = !"V".equals(returnDescriptor);
        if (hasReturn) {
            String returnType = descriptorToJavaType(returnDescriptor);
            if (returnType == null) returnType = "Object";
            sb.append("        ").append(returnType).append(" result = obj.");
        } else {
            sb.append("        obj.");
        }
        sb.append(methodName).append("(");

        for (int i = 0; i < argNames.length; i++) {
            if (argNames[i] != null) sb.append(argNames[i]);
            if (i < argNames.length - 1) sb.append(", ");
        }

        sb.append(");\n\n");

        // assert return
        if (hasReturn && exit.has("return_value")) {
            Object returnNode = exit.get("return_value");

            if (isReturnException(returnNode)) {
                // no return assertions for exception path
            } else if (isReturnNull(returnNode)) {
                sb.append("        assertNull(result);\n\n");
            } else if (isPrimitiveDescriptor(returnDescriptor)) {
                String expectedLiteral = extractPrimitiveReturnLiteral(returnNode, returnDescriptor);
                if (expectedLiteral != null) {
                    sb.append("        assertEquals(").append(expectedLiteral).append(", result);\n\n");
                }
            } else if ("Ljava/lang/String;".equals(returnDescriptor)) {
                String expectedLiteral = extractStringReturnLiteral(returnNode);
                if (expectedLiteral != null) {
                    sb.append("        assertEquals(").append(expectedLiteral).append(", result);\n\n");
                }
            } else if (isPrimitiveArrayDescriptor(returnDescriptor)) {
                String arrayLiteral = extractPrimitiveArrayReturnLiteral(returnNode);
                if (arrayLiteral != null) {
                    sb.append("        assertArrayEquals(").append(arrayLiteral).append(", result);\n\n");
                }
            }
        }

        if (!mockVerificationLines.isEmpty()) {
            for (String verificationLine : mockVerificationLines) {
                sb.append("        ").append(verificationLine).append("\n");
            }
            sb.append("\n");
        }

        // assert полей после выхода
        JSONObject objectExit = exit.getJSONArray("object_refs").getJSONObject(0);
        JSONArray fieldsExit = objectExit.getJSONArray("fields");

        for (int i = 0; i < fieldsExit.length(); i++) {
            JSONObject field = fieldsExit.getJSONObject(i);
            String name = field.getString("name");
            String type = field.getString("java_type_name");
            boolean publicField = isPublicField(field);

            if (field.has("primitive_value") && isPrimitiveDescriptor(type)) {
                if (publicField) {
                    sb.append("        assertEquals(")
                            .append(primitiveLiteral(type, field.get("primitive_value")))
                            .append(", obj.").append(name).append(");\n");
                } else {
                    sb.append("        assertEquals(")
                            .append(primitiveLiteral(type, field.get("primitive_value")))
                            .append(", ").append(readPrimitiveFieldExpr(name, type)).append(");\n");
                }
            } else if (field.has("custom")) {
                appendCustomFieldAssertion(sb, field, thisObjectId, publicField);
            } else if (field.has("array")) {
                appendArrayFieldAssertion(sb, field, publicField);
            } else if (field.has("mock_kind")) {
                sb.append("        assertNotNull(").append(readObjectFieldExpr(name, publicField)).append(");\n");
            } else if (field.has("object_id")) {
                appendObjectFieldAssertion(sb, field, thisObjectId, publicField);
            }
        }

        sb.append("\n    }\n");
        appendGeneratedTestHelpers(sb);
        sb.append("}\n");

        return sb.toString();
    }

    private static void appendGeneratedTestHelpers(StringBuilder sb) {
        sb.append("\n");
        sb.append("    private static java.lang.reflect.Field fieldOf(Object target, String fieldName) throws Exception {\n");
        sb.append("        java.lang.reflect.Field f = target.getClass().getDeclaredField(fieldName);\n");
        sb.append("        f.setAccessible(true);\n");
        sb.append("        return f;\n");
        sb.append("    }\n\n");

        sb.append("    private static void setField(Object target, String fieldName, Object value) throws Exception {\n");
        sb.append("        fieldOf(target, fieldName).set(target, value);\n");
        sb.append("    }\n\n");

        sb.append("    private static Object getField(Object target, String fieldName) throws Exception {\n");
        sb.append("        return fieldOf(target, fieldName).get(target);\n");
        sb.append("    }\n\n");

        sb.append("    private static boolean getBooleanField(Object target, String fieldName) throws Exception {\n");
        sb.append("        return fieldOf(target, fieldName).getBoolean(target);\n");
        sb.append("    }\n\n");
        sb.append("    private static byte getByteField(Object target, String fieldName) throws Exception {\n");
        sb.append("        return fieldOf(target, fieldName).getByte(target);\n");
        sb.append("    }\n\n");
        sb.append("    private static char getCharField(Object target, String fieldName) throws Exception {\n");
        sb.append("        return fieldOf(target, fieldName).getChar(target);\n");
        sb.append("    }\n\n");
        sb.append("    private static short getShortField(Object target, String fieldName) throws Exception {\n");
        sb.append("        return fieldOf(target, fieldName).getShort(target);\n");
        sb.append("    }\n\n");
        sb.append("    private static int getIntField(Object target, String fieldName) throws Exception {\n");
        sb.append("        return fieldOf(target, fieldName).getInt(target);\n");
        sb.append("    }\n\n");
        sb.append("    private static long getLongField(Object target, String fieldName) throws Exception {\n");
        sb.append("        return fieldOf(target, fieldName).getLong(target);\n");
        sb.append("    }\n\n");
        sb.append("    private static float getFloatField(Object target, String fieldName) throws Exception {\n");
        sb.append("        return fieldOf(target, fieldName).getFloat(target);\n");
        sb.append("    }\n\n");
        sb.append("    private static double getDoubleField(Object target, String fieldName) throws Exception {\n");
        sb.append("        return fieldOf(target, fieldName).getDouble(target);\n");
        sb.append("    }\n\n");

        sb.append("    @SafeVarargs\n");
        sb.append("    private static <K, V> java.util.Map<K, V> mapOfEntries(java.util.Map.Entry<K, V>... entries) {\n");
        sb.append("        java.util.Map<K, V> map = new java.util.LinkedHashMap<>();\n");
        sb.append("        for (java.util.Map.Entry<K, V> e : entries) map.put(e.getKey(), e.getValue());\n");
        sb.append("        return map;\n");
        sb.append("    }\n\n");

        sb.append("    private static <K, V> java.util.Map.Entry<K, V> entry(K key, V value) {\n");
        sb.append("        return new java.util.AbstractMap.SimpleEntry<>(key, value);\n");
        sb.append("    }\n\n");

        sb.append("    @SafeVarargs\n");
        sb.append("    private static <T> java.util.List<T> listOf(T... values) {\n");
        sb.append("        java.util.List<T> list = new java.util.ArrayList<>();\n");
        sb.append("        for (T value : values) list.add(value);\n");
        sb.append("        return list;\n");
        sb.append("    }\n\n");

        sb.append("    @SafeVarargs\n");
        sb.append("    private static <T> java.util.Set<T> setOf(T... values) {\n");
        sb.append("        java.util.Set<T> set = new java.util.LinkedHashSet<>();\n");
        sb.append("        for (T value : values) set.add(value);\n");
        sb.append("        return set;\n");
        sb.append("    }\n\n");
    }

    private static boolean isPrimitiveDescriptor(String descriptor) {
        return "Z".equals(descriptor) || "B".equals(descriptor) || "C".equals(descriptor)
                || "S".equals(descriptor) || "I".equals(descriptor) || "J".equals(descriptor)
                || "F".equals(descriptor) || "D".equals(descriptor);
    }

    private static String descriptorToJavaType(String descriptor) {
        if (descriptor == null || descriptor.isEmpty()) return null;
        switch (descriptor.charAt(0)) {
            case 'Z':
                return "boolean";
            case 'B':
                return "byte";
            case 'C':
                return "char";
            case 'S':
                return "short";
            case 'I':
                return "int";
            case 'J':
                return "long";
            case 'F':
                return "float";
            case 'D':
                return "double";
            case 'V':
                return "void";
            case '[': {
                String elementType = descriptorToJavaType(descriptor.substring(1));
                return elementType == null ? null : elementType + "[]";
            }
            case 'L': {
                if (!descriptor.endsWith(";")) return null;
                return descriptor.substring(1, descriptor.length() - 1).replace('/', '.');
            }
            default:
                return null;
        }
    }

    private static String primitiveLiteral(String descriptor, Object value) {
        switch (descriptor) {
            case "Z":
                return Boolean.toString(((Boolean) value));
            case "B":
                return "(byte)" + ((Number) value).intValue();
            case "C":
                return "(char)" + ((Number) value).intValue();
            case "S":
                return "(short)" + ((Number) value).intValue();
            case "I":
                return Integer.toString(((Number) value).intValue());
            case "J":
                return ((Number) value).longValue() + "L";
            case "F":
                return ((Number) value).floatValue() + "f";
            case "D":
                return ((Number) value).doubleValue() + "d";
            default:
                return "null";
        }
    }

    private static boolean isPrimitiveArrayDescriptor(String descriptor) {
        return descriptor != null && descriptor.length() == 2
                && descriptor.charAt(0) == '['
                && isPrimitiveDescriptor(Character.toString(descriptor.charAt(1)));
    }

    private static boolean isReturnException(Object returnNode) {
        if (!(returnNode instanceof JSONObject)) return false;
        JSONObject node = (JSONObject) returnNode;
        return "exception".equals(node.optString("kind", ""));
    }

    private static boolean isReturnNull(Object returnNode) {
        if (returnNode == null || returnNode == JSONObject.NULL) return true;
        if (returnNode instanceof JSONObject) {
            JSONObject node = (JSONObject) returnNode;
            return "null".equals(node.optString("kind", ""));
        }
        return false;
    }

    private static String extractPrimitiveReturnLiteral(Object returnNode, String returnDescriptor) {
        if (returnNode == null || returnNode == JSONObject.NULL) return null;

        String descriptor = returnDescriptor;
        Object rawValue = null;

        if (returnNode instanceof JSONObject) {
            JSONObject node = (JSONObject) returnNode;
            if (!"primitive".equals(node.optString("kind", ""))) return null;
            if (!node.has("primitive_value")) return null;
            String nodeDescriptor = node.optString("primitive_descriptor", "");
            if (!nodeDescriptor.isEmpty()) descriptor = nodeDescriptor;
            rawValue = node.get("primitive_value");
        } else {
            rawValue = returnNode;
        }

        if (!isPrimitiveDescriptor(descriptor) || rawValue == null || rawValue == JSONObject.NULL) return null;
        return primitiveLiteral(descriptor, rawValue);
    }

    private static String extractStringReturnLiteral(Object returnNode) {
        if (returnNode == null || returnNode == JSONObject.NULL) return "null";
        if (returnNode instanceof JSONObject) {
            JSONObject node = (JSONObject) returnNode;
            String kind = node.optString("kind", "");
            if ("null".equals(kind)) return "null";
            if (!"string".equals(kind)) return null;
            return javaStringLiteral(node.optString("string_value", ""));
        }
        if (returnNode instanceof String) return javaStringLiteral((String) returnNode);
        return null;
    }

    private static String extractPrimitiveArrayReturnLiteral(Object returnNode) {
        if (returnNode == null || returnNode == JSONObject.NULL) return "null";
        if (!(returnNode instanceof JSONObject)) return null;
        JSONObject node = (JSONObject) returnNode;
        String kind = node.optString("kind", "");
        if ("null".equals(kind)) return "null";
        if (!"array".equals(kind)) return null;
        JSONObject array = node.optJSONObject("array");
        if (array == null) return null;
        return buildArrayLiteral(array);
    }

    private static String readPrimitiveFieldExpr(String fieldName, String descriptor) {
        String arg = "obj, " + javaStringLiteral(fieldName);
        switch (descriptor) {
            case "Z":
                return "getBooleanField(" + arg + ")";
            case "B":
                return "getByteField(" + arg + ")";
            case "C":
                return "getCharField(" + arg + ")";
            case "S":
                return "getShortField(" + arg + ")";
            case "I":
                return "getIntField(" + arg + ")";
            case "J":
                return "getLongField(" + arg + ")";
            case "F":
                return "getFloatField(" + arg + ")";
            case "D":
                return "getDoubleField(" + arg + ")";
            default:
                return "getField(" + arg + ")";
        }
    }

    private static String buildArrayLiteral(JSONObject arrayObj) {
        JSONObject typeObj = arrayObj.optJSONObject("type");
        if (typeObj == null) return null;
        String descriptor = typeObj.optString("descriptor", "");
        if (descriptor.length() != 2 || descriptor.charAt(0) != '[') return null;

        String javaType = descriptorToJavaType(descriptor);
        if (javaType == null) return null;

        JSONArray elements = arrayObj.optJSONArray("elements");
        if (elements == null) return null;

        String elementDescriptor = Character.toString(descriptor.charAt(1));
        StringBuilder sb = new StringBuilder();
        sb.append("new ").append(javaType).append("{");
        for (int i = 0; i < elements.length(); i++) {
            if (i > 0) sb.append(", ");
            Object value = elements.get(i);
            if (isPrimitiveDescriptor(elementDescriptor)) {
                sb.append(primitiveLiteral(elementDescriptor, value));
            } else {
                sb.append("null");
            }
        }
        sb.append("}");
        return sb.toString();
    }

    private static Map<Long, JSONObject> indexMockObjectsById(JSONArray mockObjects) {
        Map<Long, JSONObject> byId = new LinkedHashMap<>();
        if (mockObjects == null) return byId;
        for (int i = 0; i < mockObjects.length(); i++) {
            JSONObject mock = mockObjects.optJSONObject(i);
            if (mock == null) continue;
            long objectId = mock.optLong("object_id", -1L);
            if (objectId < 0L) continue;
            byId.put(objectId, mock);
        }
        return byId;
    }

    private static Map<Long, JSONObject> indexObjectRefsById(JSONArray objectRefs) {
        Map<Long, JSONObject> byId = new LinkedHashMap<>();
        if (objectRefs == null) return byId;
        for (int i = 0; i < objectRefs.length(); i++) {
            JSONObject ref = objectRefs.optJSONObject(i);
            if (ref == null) continue;
            long objectId = ref.optLong("object_id", -1L);
            if (objectId < 0L) continue;
            byId.put(objectId, ref);
        }
        return byId;
    }

    private static String objectArgumentLiteral(String descriptor, JSONObject objectRef) {
        if (descriptor == null || objectRef == null) return null;
        if ("Ljava/lang/String;".equals(descriptor)) {
            return stringLiteralFromObjectRef(objectRef);
        }
        return null;
    }

    private static String stringLiteralFromObjectRef(JSONObject objectRef) {
        if (objectRef == null) return null;
        JSONObject typeObj = objectRef.optJSONObject("type");
        if (typeObj == null) return null;
        String typeDescriptor = typeObj.optString("descriptor", "");
        if (!"Ljava/lang/String;".equals(typeDescriptor)) return null;

        JSONArray fields = objectRef.optJSONArray("fields");
        if (fields == null) return null;

        JSONArray byteElements = null;
        int coder = 0;

        for (int i = 0; i < fields.length(); i++) {
            JSONObject field = fields.optJSONObject(i);
            if (field == null) continue;
            String fieldName = field.optString("name", "");
            if ("value".equals(fieldName)) {
                JSONObject array = field.optJSONObject("array");
                if (array != null) byteElements = array.optJSONArray("elements");
            } else if ("coder".equals(fieldName)) {
                Object raw = field.opt("primitive_value");
                if (raw instanceof Number) coder = ((Number) raw).intValue();
            }
        }

        if (byteElements == null) return null;
        byte[] rawBytes = new byte[byteElements.length()];
        for (int i = 0; i < byteElements.length(); i++) {
            Object value = byteElements.opt(i);
            if (!(value instanceof Number)) return null;
            rawBytes[i] = (byte) (((Number) value).intValue() & 0xFF);
        }

        String decoded;
        if (coder == 0) {
            decoded = new String(rawBytes, StandardCharsets.ISO_8859_1);
        } else {
            decoded = new String(rawBytes, StandardCharsets.UTF_16BE);
        }
        return javaStringLiteral(decoded);
    }

    private static JSONObject resolveMockObjectForField(JSONObject field, Map<Long, JSONObject> mockObjectsById) {
        if (field == null) return null;
        String mockKind = field.optString("mock_kind", "");
        if (mockKind.isEmpty()) return null;
        long objectId = field.optLong("object_id", -1L);
        if (objectId < 0L) return null;
        return mockObjectsById.get(objectId);
    }

    private static boolean appendMockFieldInitialization(
            StringBuilder sb,
            JSONObject field,
            JSONObject mockObject,
            long thisObjectId,
            boolean publicField,
            List<String> verificationLines
    ) {
        String mockKind = field.optString("mock_kind", "");
        if (!"input_stream".equals(mockKind)) return false;

        String fieldName = field.optString("name", "mockField");
        String fieldNameLiteral = javaStringLiteral(fieldName);
        String declaredType = descriptorToJavaType(field.optString("java_type_name", ""));
        if (declaredType == null || declaredType.isEmpty()) {
            declaredType = "java.io.InputStream";
        }

        String mockVar = "mock_" + sanitizeIdentifier(fieldName);
        sb.append("        ").append(declaredType).append(" ").append(mockVar)
                .append(" = org.mockito.Mockito.mock(").append(declaredType).append(".class);\n");
        appendInputStreamMockStubbings(sb, mockVar, mockObject, thisObjectId);
        appendInputStreamMockVerifications(verificationLines, mockVar, mockObject);
        appendFieldAssignment(sb, fieldName, mockVar, fieldNameLiteral, publicField);
        return true;
    }

    private static void appendInputStreamMockStubbings(StringBuilder sb, String mockVar, JSONObject mockObject, long thisObjectId) {
        if (mockObject == null) return;
        JSONArray calls = mockObject.optJSONArray("calls");
        if (calls == null || calls.length() == 0) return;

        Map<String, List<String>> returnValuesByInvocation = new LinkedHashMap<>();
        Set<String> voidInvocations = new LinkedHashSet<>();

        for (int i = 0; i < calls.length(); i++) {
            JSONObject call = calls.optJSONObject(i);
            if (call == null) continue;

            String methodName = call.optString("method", "");
            String methodSignature = call.optString("method_signature", "");
            if (methodName.isEmpty() || methodSignature.isEmpty()) continue;

            String returnDescriptor = extractMethodReturnDescriptor(methodSignature);
            String argsExpr = buildMethodArgumentMatchers(methodSignature);
            String invocation = mockVar + "." + methodName + "(" + argsExpr + ")";
            if ("V".equals(returnDescriptor)) {
                voidInvocations.add(invocation);
                continue;
            }

            Object returnNode = call.opt("return_value");
            if (isReturnException(returnNode)) continue;
            String returnExpr = serializedReturnValueToJavaExpr(returnNode, returnDescriptor, thisObjectId);
            if (returnExpr == null) continue;

            List<String> returnValues = returnValuesByInvocation.computeIfAbsent(invocation, key -> new ArrayList<>());
            returnValues.add(returnExpr);
        }

        for (String invocation : voidInvocations) {
            sb.append("        org.mockito.Mockito.doNothing().when(").append(mockVar).append(").")
                    .append(invocation.substring(mockVar.length() + 1)).append(";\n");
        }

        for (Map.Entry<String, List<String>> entry : returnValuesByInvocation.entrySet()) {
            List<String> values = entry.getValue();
            if (values.isEmpty()) continue;
            sb.append("        org.mockito.Mockito.when(").append(entry.getKey()).append(").thenReturn(");
            for (int i = 0; i < values.size(); i++) {
                if (i > 0) sb.append(", ");
                sb.append(values.get(i));
            }
            sb.append(");\n");
        }
    }

    private static void appendInputStreamMockVerifications(List<String> out, String mockVar, JSONObject mockObject) {
        if (out == null || mockObject == null) return;
        JSONArray calls = mockObject.optJSONArray("calls");
        if (calls == null || calls.length() == 0) return;

        Map<String, Integer> counts = new LinkedHashMap<>();
        for (int i = 0; i < calls.length(); i++) {
            JSONObject call = calls.optJSONObject(i);
            if (call == null) continue;

            String methodName = call.optString("method", "");
            String methodSignature = call.optString("method_signature", "");
            if (methodName.isEmpty() || methodSignature.isEmpty()) continue;

            String argsExpr = buildMethodArgumentMatchers(methodSignature);
            String invocation = methodName + "(" + argsExpr + ")";
            counts.put(invocation, counts.getOrDefault(invocation, 0) + 1);
        }

        for (Map.Entry<String, Integer> entry : counts.entrySet()) {
            out.add("org.mockito.Mockito.verify(" + mockVar + ", org.mockito.Mockito.times(" + entry.getValue() + "))." + entry.getKey() + ";");
        }
    }

    private static String serializedReturnValueToJavaExpr(Object returnNode, String returnDescriptor, long thisObjectId) {
        if (returnNode == null || returnNode == JSONObject.NULL) return "null";

        if (isPrimitiveDescriptor(returnDescriptor)) {
            return extractPrimitiveReturnLiteral(returnNode, returnDescriptor);
        }
        if ("Ljava/lang/String;".equals(returnDescriptor)) {
            return extractStringReturnLiteral(returnNode);
        }
        if (isPrimitiveArrayDescriptor(returnDescriptor)) {
            return extractPrimitiveArrayReturnLiteral(returnNode);
        }
        if (returnNode instanceof JSONObject) {
            return serializedValueToJavaExpr((JSONObject) returnNode, thisObjectId);
        }
        return null;
    }

    private static String extractMethodReturnDescriptor(String methodDescriptor) {
        if (methodDescriptor == null || methodDescriptor.isEmpty()) return "";
        int split = methodDescriptor.lastIndexOf(')');
        if (split < 0 || split + 1 >= methodDescriptor.length()) return "";
        return methodDescriptor.substring(split + 1);
    }

    private static String buildMethodArgumentMatchers(String methodDescriptor) {
        List<String> descriptors = parseMethodParameterDescriptors(methodDescriptor);
        if (descriptors.isEmpty()) return "";

        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < descriptors.size(); i++) {
            if (i > 0) sb.append(", ");
            sb.append(argumentMatcherForDescriptor(descriptors.get(i)));
        }
        return sb.toString();
    }

    private static List<String> parseMethodParameterDescriptors(String methodDescriptor) {
        List<String> descriptors = new ArrayList<>();
        if (methodDescriptor == null || methodDescriptor.length() < 2 || methodDescriptor.charAt(0) != '(') return descriptors;

        int i = 1;
        while (i < methodDescriptor.length() && methodDescriptor.charAt(i) != ')') {
            int next = consumeTypeDescriptor(methodDescriptor, i);
            if (next <= i) break;
            descriptors.add(methodDescriptor.substring(i, next));
            i = next;
        }
        return descriptors;
    }

    private static int consumeTypeDescriptor(String descriptor, int startIndex) {
        if (startIndex < 0 || startIndex >= descriptor.length()) return startIndex;
        char ch = descriptor.charAt(startIndex);

        if (isPrimitiveDescriptor(Character.toString(ch))) return startIndex + 1;
        if (ch == 'L') {
            int end = descriptor.indexOf(';', startIndex);
            return end < 0 ? startIndex : end + 1;
        }
        if (ch == '[') {
            int i = startIndex;
            while (i < descriptor.length() && descriptor.charAt(i) == '[') i++;
            if (i >= descriptor.length()) return startIndex;
            if (descriptor.charAt(i) == 'L') {
                int end = descriptor.indexOf(';', i);
                return end < 0 ? startIndex : end + 1;
            }
            if (isPrimitiveDescriptor(Character.toString(descriptor.charAt(i)))) return i + 1;
            return startIndex;
        }
        return startIndex;
    }

    private static String argumentMatcherForDescriptor(String descriptor) {
        if (descriptor == null || descriptor.isEmpty()) return "org.mockito.ArgumentMatchers.any()";
        if (descriptor.length() == 1) {
            switch (descriptor.charAt(0)) {
                case 'Z':
                    return "org.mockito.ArgumentMatchers.anyBoolean()";
                case 'B':
                    return "org.mockito.ArgumentMatchers.anyByte()";
                case 'C':
                    return "org.mockito.ArgumentMatchers.anyChar()";
                case 'S':
                    return "org.mockito.ArgumentMatchers.anyShort()";
                case 'I':
                    return "org.mockito.ArgumentMatchers.anyInt()";
                case 'J':
                    return "org.mockito.ArgumentMatchers.anyLong()";
                case 'F':
                    return "org.mockito.ArgumentMatchers.anyFloat()";
                case 'D':
                    return "org.mockito.ArgumentMatchers.anyDouble()";
                default:
                    return "org.mockito.ArgumentMatchers.any()";
            }
        }

        String javaType = descriptorToJavaType(descriptor);
        if (javaType == null || javaType.isEmpty()) return "org.mockito.ArgumentMatchers.any()";
        return "org.mockito.ArgumentMatchers.any(" + javaType + ".class)";
    }

    private static void appendCustomFieldInitialization(StringBuilder sb, JSONObject field, long thisObjectId, boolean publicField) {
        JSONObject custom = field.optJSONObject("custom");
        if (custom == null) return;

        String customKind = custom.optString("kind", "");
        String name = field.optString("name", "field");
        String suffix = sanitizeIdentifier(name);
        String fieldNameLiteral = javaStringLiteral(name);

        if ("map".equals(customKind)) {
            String mapVar = "map_" + suffix;
            JSONArray entries = custom.optJSONArray("entries");
            String keyType = inferMapEntryType(entries, true);
            String valueType = inferMapEntryType(entries, false);
            String mapExpr = buildMapInitializerExpression(entries, thisObjectId);
            sb.append("        java.util.Map<").append(keyType).append(", ").append(valueType).append("> ").append(mapVar)
                    .append(" = ").append(mapExpr).append(";\n");
            appendFieldAssignment(sb, name, mapVar, fieldNameLiteral, publicField);
            return;
        }

        if ("list".equals(customKind)) {
            String listVar = "list_" + suffix;
            JSONArray elements = custom.optJSONArray("elements");
            String elementType = inferElementsType(elements);
            String listExpr = buildSequenceInitializerExpression("list", elements, thisObjectId);
            sb.append("        java.util.List<").append(elementType).append("> ").append(listVar)
                    .append(" = ").append(listExpr).append(";\n");
            appendFieldAssignment(sb, name, listVar, fieldNameLiteral, publicField);
            return;
        }

        if ("set".equals(customKind)) {
            String setVar = "set_" + suffix;
            JSONArray elements = custom.optJSONArray("elements");
            String elementType = inferElementsType(elements);
            String setExpr = buildSequenceInitializerExpression("set", elements, thisObjectId);
            sb.append("        java.util.Set<").append(elementType).append("> ").append(setVar)
                    .append(" = ").append(setExpr).append(";\n");
            appendFieldAssignment(sb, name, setVar, fieldNameLiteral, publicField);
        }
    }

    private static void appendCustomFieldAssertion(StringBuilder sb, JSONObject field, long thisObjectId, boolean publicField) {
        JSONObject custom = field.optJSONObject("custom");
        if (custom == null) return;

        String customKind = custom.optString("kind", "");
        String name = field.optString("name", "field");
        String suffix = sanitizeIdentifier(name);
        String fieldExpr = readObjectFieldExpr(name, publicField);

        if ("map".equals(customKind)) {
            JSONArray entries = custom.optJSONArray("entries");
            String keyType = inferMapEntryType(entries, true);
            String valueType = inferMapEntryType(entries, false);
            String expectedExpr = buildMapInitializerExpression(entries, thisObjectId);
            String expectedVar = "expected_map_" + suffix;
            sb.append("        java.util.Map<").append(keyType).append(", ").append(valueType).append("> ")
                    .append(expectedVar).append(" = ").append(expectedExpr).append(";\n");
            sb.append("        assertEquals(").append(expectedVar).append(", ").append(fieldExpr).append(");\n");
            return;
        }

        if ("list".equals(customKind)) {
            JSONArray elements = custom.optJSONArray("elements");
            String elementType = inferElementsType(elements);
            String expectedExpr = buildSequenceInitializerExpression("list", elements, thisObjectId);
            String expectedVar = "expected_list_" + suffix;
            sb.append("        java.util.List<").append(elementType).append("> ")
                    .append(expectedVar).append(" = ").append(expectedExpr).append(";\n");
            sb.append("        assertEquals(").append(expectedVar).append(", ").append(fieldExpr).append(");\n");
            return;
        }

        if ("set".equals(customKind)) {
            JSONArray elements = custom.optJSONArray("elements");
            String elementType = inferElementsType(elements);
            String expectedExpr = buildSequenceInitializerExpression("set", elements, thisObjectId);
            String expectedVar = "expected_set_" + suffix;
            sb.append("        java.util.Set<").append(elementType).append("> ")
                    .append(expectedVar).append(" = ").append(expectedExpr).append(";\n");
            sb.append("        assertEquals(").append(expectedVar).append(", ").append(fieldExpr).append(");\n");
        }
    }

    private static void appendArrayFieldAssertion(StringBuilder sb, JSONObject field, boolean publicField) {
        JSONObject array = field.optJSONObject("array");
        if (array == null) return;

        String expectedLiteral = buildArrayLiteral(array);
        if (expectedLiteral == null) return;

        JSONObject typeObj = array.optJSONObject("type");
        if (typeObj == null) return;
        String descriptor = typeObj.optString("descriptor", "");
        String javaType = descriptorToJavaType(descriptor);
        if (javaType == null || javaType.isEmpty()) return;

        String name = field.optString("name", "field");
        String actualExpr = publicField
                ? "obj." + name
                : "(" + javaType + ") getField(obj, " + javaStringLiteral(name) + ")";
        sb.append("        assertArrayEquals(").append(expectedLiteral).append(", ").append(actualExpr).append(");\n");
    }

    private static void appendObjectFieldAssertion(StringBuilder sb, JSONObject field, long thisObjectId, boolean publicField) {
        String name = field.optString("name", "field");
        String fieldExpr = readObjectFieldExpr(name, publicField);
        long objectId = field.optLong("object_id", -1L);

        if (objectId == thisObjectId) {
            sb.append("        assertSame(obj, ").append(fieldExpr).append(");\n");
            return;
        }

        String fqcn = extractFieldFqcn(field);
        if (publicField) {
            sb.append("        assertNotNull(").append(fieldExpr).append(");\n");
            if (!fqcn.isEmpty()) {
                sb.append("        assertEquals(").append(javaStringLiteral(fqcn)).append(", ")
                        .append(fieldExpr).append(".getClass().getName());\n");
            }
        } else {
            String actualVar = "actual_" + sanitizeIdentifier(name);
            sb.append("        Object ").append(actualVar).append(" = ").append(fieldExpr).append(";\n");
            sb.append("        assertNotNull(").append(actualVar).append(");\n");
            if (!fqcn.isEmpty()) {
                sb.append("        assertEquals(").append(javaStringLiteral(fqcn)).append(", ")
                        .append(actualVar).append(".getClass().getName());\n");
            }
        }
    }

    private static String readObjectFieldExpr(String fieldName, boolean publicField) {
        if (publicField) return "obj." + fieldName;
        return "getField(obj, " + javaStringLiteral(fieldName) + ")";
    }

    private static void appendFieldAssignment(StringBuilder sb, String fieldName, String valueExpr, String fieldNameLiteral, boolean publicField) {
        if (publicField) {
            sb.append("        obj.").append(fieldName).append(" = ").append(valueExpr).append(";\n");
        } else {
            sb.append("        setField(obj, ").append(fieldNameLiteral).append(", ").append(valueExpr).append(");\n");
        }
    }

    private static String buildMapInitializerExpression(JSONArray entries, long thisObjectId) {
        if (entries == null || entries.length() == 0) return "mapOfEntries()";
        StringBuilder sb = new StringBuilder();
        sb.append("mapOfEntries(");
        for (int i = 0; i < entries.length(); i++) {
            JSONObject entry = entries.optJSONObject(i);
            if (entry == null) continue;
            String keyExpr = serializedValueToJavaExpr(entry.optJSONObject("key"), thisObjectId);
            String valueExpr = serializedValueToJavaExpr(entry.optJSONObject("value"), thisObjectId);
            if (sb.charAt(sb.length() - 1) != '(') sb.append(", ");
            sb.append("entry(").append(keyExpr).append(", ").append(valueExpr).append(")");
        }
        sb.append(")");
        return sb.toString();
    }

    private static boolean isPublicField(JSONObject field) {
        if (field == null) return false;
        if (field.has("is_public")) return field.optBoolean("is_public", false);
        if (field.has("modifiers")) {
            int modifiers = field.optInt("modifiers", 0);
            return (modifiers & 0x0001) != 0;
        }
        return false;
    }

    private static String buildSequenceInitializerExpression(String kind, JSONArray elements, long thisObjectId) {
        String fn = "list".equals(kind) ? "listOf" : "setOf";
        if (elements == null || elements.length() == 0) return fn + "()";
        StringBuilder sb = new StringBuilder();
        sb.append(fn).append("(");
        for (int i = 0; i < elements.length(); i++) {
            String valueExpr = serializedValueToJavaExpr(elements.optJSONObject(i), thisObjectId);
            if (i > 0) sb.append(", ");
            sb.append(valueExpr);
        }
        sb.append(")");
        return sb.toString();
    }

    private static String inferMapEntryType(JSONArray entries, boolean key) {
        if (entries == null || entries.length() == 0) return "Object";
        String inferred = null;
        String nodeName = key ? "key" : "value";
        for (int i = 0; i < entries.length(); i++) {
            JSONObject entry = entries.optJSONObject(i);
            if (entry == null) continue;
            String current = inferSerializedNodeType(entry.optJSONObject(nodeName));
            if (current == null || current.isEmpty()) continue;
            if (inferred == null) {
                inferred = current;
            } else if (!inferred.equals(current)) {
                return "Object";
            }
        }
        return inferred == null ? "Object" : inferred;
    }

    private static String inferElementsType(JSONArray elements) {
        if (elements == null || elements.length() == 0) return "Object";
        String inferred = null;
        for (int i = 0; i < elements.length(); i++) {
            String current = inferSerializedNodeType(elements.optJSONObject(i));
            if (current == null || current.isEmpty()) continue;
            if (inferred == null) {
                inferred = current;
            } else if (!inferred.equals(current)) {
                return "Object";
            }
        }
        return inferred == null ? "Object" : inferred;
    }

    private static String inferSerializedNodeType(JSONObject node) {
        if (node == null) return null;
        String kind = node.optString("kind", "null");
        switch (kind) {
            case "null":
                return null;
            case "string":
                return "String";
            case "primitive": {
                String primitiveDescriptor = node.optString("primitive_descriptor", "");
                return boxedTypeForPrimitiveDescriptor(primitiveDescriptor);
            }
            case "array": {
                JSONObject array = node.optJSONObject("array");
                if (array == null) return "Object";
                JSONObject type = array.optJSONObject("type");
                if (type == null) return "Object";
                String descriptor = type.optString("descriptor", "");
                String javaType = descriptorToJavaType(descriptor);
                return javaType == null ? "Object" : javaType;
            }
            case "object_ref": {
                JSONObject typeObj = node.optJSONObject("type");
                if (typeObj == null) return "Object";
                String fqcn = typeObj.optString("fqcn", "");
                return fqcn.isEmpty() ? "Object" : fqcn;
            }
            default:
                return "Object";
        }
    }

    private static String boxedTypeForPrimitiveDescriptor(String descriptor) {
        if (descriptor == null || descriptor.isEmpty()) return "Object";
        switch (descriptor.charAt(0)) {
            case 'Z':
                return "Boolean";
            case 'B':
                return "Byte";
            case 'C':
                return "Character";
            case 'S':
                return "Short";
            case 'I':
                return "Integer";
            case 'J':
                return "Long";
            case 'F':
                return "Float";
            case 'D':
                return "Double";
            default:
                return "Object";
        }
    }

    private static String serializedValueToJavaExpr(JSONObject node, long thisObjectId) {
        if (node == null) return "null";

        String kind = node.optString("kind", "null");
        switch (kind) {
            case "null":
                return "null";
            case "string":
                return javaStringLiteral(node.optString("string_value", ""));
            case "primitive": {
                String primitiveDescriptor = node.optString("primitive_descriptor", "");
                if (primitiveDescriptor.isEmpty() || !node.has("primitive_value")) return "null";
                return primitiveLiteral(primitiveDescriptor, node.get("primitive_value"));
            }
            case "array": {
                JSONObject array = node.optJSONObject("array");
                if (array == null) return "null";
                String literal = buildArrayLiteral(array);
                return literal == null ? "null" : literal;
            }
            case "object_ref": {
                long objectId = node.optLong("object_id", -1L);
                if (objectId == thisObjectId) return "obj";

                JSONObject typeObj = node.optJSONObject("type");
                if (typeObj == null) return "null";
                String fqcn = typeObj.optString("fqcn", "");
                if (fqcn.isEmpty()) return "null";
                return "Class.forName(" + javaStringLiteral(fqcn) + ").getDeclaredConstructor().newInstance()";
            }
            default:
                return "null";
        }
    }

    private static String javaStringLiteral(String value) {
        String escaped = value
                .replace("\\", "\\\\")
                .replace("\"", "\\\"")
                .replace("\n", "\\n")
                .replace("\r", "\\r")
                .replace("\t", "\\t");
        return "\"" + escaped + "\"";
    }

    private static String extractFieldFqcn(JSONObject field) {
        JSONObject runtimeType = field.optJSONObject("runtime_type");
        if (runtimeType != null) {
            String fqcn = runtimeType.optString("fqcn", "");
            if (!fqcn.isEmpty()) return fqcn;
        }
        JSONObject declaredType = field.optJSONObject("type");
        if (declaredType != null) {
            String fqcn = declaredType.optString("fqcn", "");
            if (!fqcn.isEmpty()) return fqcn;
        }
        String descriptor = field.optString("java_type_name", "");
        String javaType = descriptorToJavaType(descriptor);
        return javaType == null ? "" : javaType;
    }

    private static String sanitizeIdentifier(String value) {
        if (value == null || value.isEmpty()) return "field";
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < value.length(); i++) {
            char ch = value.charAt(i);
            if (Character.isLetterOrDigit(ch) || ch == '_') {
                sb.append(ch);
            } else {
                sb.append('_');
            }
        }
        if (!Character.isJavaIdentifierStart(sb.charAt(0))) {
            sb.insert(0, '_');
        }
        return sb.toString();
    }
}
