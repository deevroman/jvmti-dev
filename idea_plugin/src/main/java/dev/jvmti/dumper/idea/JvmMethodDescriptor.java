package dev.jvmti.dumper.idea;

import com.intellij.psi.PsiArrayType;
import com.intellij.psi.PsiClass;
import com.intellij.psi.PsiClassType;
import com.intellij.psi.PsiMethod;
import com.intellij.psi.PsiParameter;
import com.intellij.psi.PsiPrimitiveType;
import com.intellij.psi.PsiType;

import java.util.HashMap;
import java.util.Map;

final class JvmMethodDescriptor {
    private static final Map<String, String> PRIMITIVE_DESCRIPTORS = new HashMap<>();

    static {
        PRIMITIVE_DESCRIPTORS.put("boolean", "Z");
        PRIMITIVE_DESCRIPTORS.put("byte", "B");
        PRIMITIVE_DESCRIPTORS.put("char", "C");
        PRIMITIVE_DESCRIPTORS.put("short", "S");
        PRIMITIVE_DESCRIPTORS.put("int", "I");
        PRIMITIVE_DESCRIPTORS.put("long", "J");
        PRIMITIVE_DESCRIPTORS.put("float", "F");
        PRIMITIVE_DESCRIPTORS.put("double", "D");
        PRIMITIVE_DESCRIPTORS.put("void", "V");
    }

    private JvmMethodDescriptor() {
    }

    static String of(PsiMethod method) {
        StringBuilder descriptor = new StringBuilder("(");
        for (PsiParameter parameter : method.getParameterList().getParameters()) {
            descriptor.append(ofType(parameter.getType()));
        }
        descriptor.append(')');

        PsiType returnType = method.getReturnType();
        descriptor.append(returnType == null ? "V" : ofType(returnType));
        return descriptor.toString();
    }

    private static String ofType(PsiType type) {
        if (type instanceof PsiPrimitiveType) {
            String descriptor = PRIMITIVE_DESCRIPTORS.get(type.getCanonicalText());
            return descriptor == null ? "Ljava/lang/Object;" : descriptor;
        }

        if (type instanceof PsiArrayType arrayType) {
            return "[" + ofType(arrayType.getComponentType());
        }

        if (type instanceof PsiClassType classType) {
            PsiClass resolved = classType.resolve();
            if (resolved != null && resolved.getQualifiedName() != null) {
                return "L" + resolved.getQualifiedName().replace('.', '/') + ";";
            }

            String canonicalText = stripGenerics(classType.getCanonicalText());
            if (canonicalText.length() > 0) {
                return "L" + canonicalText.replace('.', '/') + ";";
            }
        }

        return "Ljava/lang/Object;";
    }

    private static String stripGenerics(String canonicalText) {
        int genericsStart = canonicalText.indexOf('<');
        if (genericsStart >= 0) {
            return canonicalText.substring(0, genericsStart);
        }
        return canonicalText;
    }
}
