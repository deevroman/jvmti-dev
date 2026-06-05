#include "dump_serializer.h"

#include <classfile_constants.h>

#include "type_utils.h"

using json = nlohmann::json;

namespace {
struct CustomFieldSerializer {
  const char* kind;
  bool (*matches)(JNIEnv* env, jobject obj);
  void (*serialize)(jvmtiEnv* jvmti, JNIEnv* env, jobject obj, json& out, const DumpSerializerDeps& deps);
};

std::string get_class_signature(jvmtiEnv* jvmti, jclass cls) {
  char* sig = nullptr;
  if (jvmti->GetClassSignature(cls, &sig, nullptr) != JVMTI_ERROR_NONE || !sig) return "";

  std::string out(sig);
  jvmti->Deallocate(reinterpret_cast<unsigned char*>(sig));
  return out;
}

JvmTypeInfo get_object_type_info(jvmtiEnv* jvmti, JNIEnv* env, jobject obj) {
  if (!obj) return ParseJvmTypeDescriptor("");

  jclass cls = env->GetObjectClass(obj);
  return ParseJvmTypeDescriptor(get_class_signature(jvmti, cls));
}

std::string legacy_java_type_name(const JvmTypeInfo& info) {
  if (info.kind == "object") return info.binary_name;
  return info.descriptor;
}

bool read_boxed_primitive(JNIEnv* env, jobject obj, const std::string& descriptor, char& primitive_descriptor,
                          json& primitive_value) {
  jclass cls = env->GetObjectClass(obj);
  if (!cls) return false;

  if (descriptor == "Ljava/lang/Boolean;") {
    jmethodID mid = env->GetMethodID(cls, "booleanValue", "()Z");
    if (!mid) {
      env->DeleteLocalRef(cls);
      return false;
    }
    primitive_descriptor = 'Z';
    primitive_value = static_cast<bool>(env->CallBooleanMethod(obj, mid));
    env->DeleteLocalRef(cls);
    return true;
  }

  if (descriptor == "Ljava/lang/Byte;") {
    jmethodID mid = env->GetMethodID(cls, "byteValue", "()B");
    if (!mid) {
      env->DeleteLocalRef(cls);
      return false;
    }
    primitive_descriptor = 'B';
    primitive_value = static_cast<int>(env->CallByteMethod(obj, mid));
    env->DeleteLocalRef(cls);
    return true;
  }

  if (descriptor == "Ljava/lang/Character;") {
    jmethodID mid = env->GetMethodID(cls, "charValue", "()C");
    if (!mid) {
      env->DeleteLocalRef(cls);
      return false;
    }
    primitive_descriptor = 'C';
    primitive_value = static_cast<int>(env->CallCharMethod(obj, mid));
    env->DeleteLocalRef(cls);
    return true;
  }

  if (descriptor == "Ljava/lang/Short;") {
    jmethodID mid = env->GetMethodID(cls, "shortValue", "()S");
    if (!mid) {
      env->DeleteLocalRef(cls);
      return false;
    }
    primitive_descriptor = 'S';
    primitive_value = static_cast<int>(env->CallShortMethod(obj, mid));
    env->DeleteLocalRef(cls);
    return true;
  }

  if (descriptor == "Ljava/lang/Integer;") {
    jmethodID mid = env->GetMethodID(cls, "intValue", "()I");
    if (!mid) {
      env->DeleteLocalRef(cls);
      return false;
    }
    primitive_descriptor = 'I';
    primitive_value = env->CallIntMethod(obj, mid);
    env->DeleteLocalRef(cls);
    return true;
  }

  if (descriptor == "Ljava/lang/Long;") {
    jmethodID mid = env->GetMethodID(cls, "longValue", "()J");
    if (!mid) {
      env->DeleteLocalRef(cls);
      return false;
    }
    primitive_descriptor = 'J';
    primitive_value = static_cast<long long>(env->CallLongMethod(obj, mid));
    env->DeleteLocalRef(cls);
    return true;
  }

  if (descriptor == "Ljava/lang/Float;") {
    jmethodID mid = env->GetMethodID(cls, "floatValue", "()F");
    if (!mid) {
      env->DeleteLocalRef(cls);
      return false;
    }
    primitive_descriptor = 'F';
    primitive_value = env->CallFloatMethod(obj, mid);
    env->DeleteLocalRef(cls);
    return true;
  }

  if (descriptor == "Ljava/lang/Double;") {
    jmethodID mid = env->GetMethodID(cls, "doubleValue", "()D");
    if (!mid) {
      env->DeleteLocalRef(cls);
      return false;
    }
    primitive_descriptor = 'D';
    primitive_value = env->CallDoubleMethod(obj, mid);
    env->DeleteLocalRef(cls);
    return true;
  }

  env->DeleteLocalRef(cls);
  return false;
}

bool read_enum_value(JNIEnv* env, jobject obj, const JvmTypeInfo& object_type, json& value) {
  jclass enum_cls = env->FindClass("java/lang/Enum");
  if (!enum_cls) return false;

  const bool is_enum = env->IsInstanceOf(obj, enum_cls);
  if (!is_enum) {
    env->DeleteLocalRef(enum_cls);
    return false;
  }

  jmethodID mid_name = env->GetMethodID(enum_cls, "name", "()Ljava/lang/String;");
  jmethodID mid_ordinal = env->GetMethodID(enum_cls, "ordinal", "()I");
  if (!mid_name || !mid_ordinal) {
    env->DeleteLocalRef(enum_cls);
    return false;
  }

  jstring name = reinterpret_cast<jstring>(env->CallObjectMethod(obj, mid_name));
  const jint ordinal = env->CallIntMethod(obj, mid_ordinal);

  value["kind"] = "enum_value";
  value["enum_class"] = object_type.fqcn;
  value["enum_name"] = "";
  value["enum_ordinal"] = ordinal;

  if (name) {
    const char* chars = env->GetStringUTFChars(name, nullptr);
    if (chars) {
      value["enum_name"] = chars;
      env->ReleaseStringUTFChars(name, chars);
    }
    env->DeleteLocalRef(name);
  }

  env->DeleteLocalRef(enum_cls);
  return true;
}

json serialize_object_value(jvmtiEnv* jvmti, JNIEnv* env, jobject obj, const DumpSerializerDeps& deps);

bool is_map_instance(JNIEnv* env, jobject obj) {
  if (!obj) return false;
  jclass map_cls = env->FindClass("java/util/Map");
  if (!map_cls) return false;

  const bool result = env->IsInstanceOf(obj, map_cls);
  env->DeleteLocalRef(map_cls);
  return result;
}

bool is_list_instance(JNIEnv* env, jobject obj) {
  if (!obj) return false;
  jclass list_cls = env->FindClass("java/util/List");
  if (!list_cls) return false;

  const bool result = env->IsInstanceOf(obj, list_cls);
  env->DeleteLocalRef(list_cls);
  return result;
}

bool is_set_instance(JNIEnv* env, jobject obj) {
  if (!obj) return false;
  jclass set_cls = env->FindClass("java/util/Set");
  if (!set_cls) return false;

  const bool result = env->IsInstanceOf(obj, set_cls);
  env->DeleteLocalRef(set_cls);
  return result;
}

bool is_input_stream_instance(JNIEnv* env, jobject obj) {
  if (!obj) return false;
  jclass cls = env->FindClass("java/io/InputStream");
  if (!cls) return false;
  const bool result = env->IsInstanceOf(obj, cls);
  env->DeleteLocalRef(cls);
  return result;
}

void serialize_collection_elements(jvmtiEnv* jvmti, JNIEnv* env, jobject obj, json& out, const DumpSerializerDeps& deps) {
  out["elements"] = json::array();

  jclass collection_cls = env->FindClass("java/util/Collection");
  jclass iterator_cls = env->FindClass("java/util/Iterator");
  if (!collection_cls || !iterator_cls) {
    if (collection_cls) env->DeleteLocalRef(collection_cls);
    if (iterator_cls) env->DeleteLocalRef(iterator_cls);
    return;
  }

  jmethodID mid_size = env->GetMethodID(collection_cls, "size", "()I");
  jmethodID mid_iterator = env->GetMethodID(collection_cls, "iterator", "()Ljava/util/Iterator;");
  jmethodID mid_has_next = env->GetMethodID(iterator_cls, "hasNext", "()Z");
  jmethodID mid_next = env->GetMethodID(iterator_cls, "next", "()Ljava/lang/Object;");
  if (!mid_iterator || !mid_has_next || !mid_next) {
    env->DeleteLocalRef(collection_cls);
    env->DeleteLocalRef(iterator_cls);
    return;
  }

  if (mid_size) out["size"] = env->CallIntMethod(obj, mid_size);

  jobject iterator = env->CallObjectMethod(obj, mid_iterator);
  if (!iterator) {
    env->DeleteLocalRef(collection_cls);
    env->DeleteLocalRef(iterator_cls);
    return;
  }

  while (env->CallBooleanMethod(iterator, mid_has_next) == JNI_TRUE) {
    jobject element = env->CallObjectMethod(iterator, mid_next);
    out["elements"].push_back(serialize_object_value(jvmti, env, element, deps));
    if (element) env->DeleteLocalRef(element);
  }

  env->DeleteLocalRef(iterator);
  env->DeleteLocalRef(collection_cls);
  env->DeleteLocalRef(iterator_cls);
}

void serialize_map_entries(jvmtiEnv* jvmti, JNIEnv* env, jobject obj, json& out, const DumpSerializerDeps& deps) {
  out["entries"] = json::array();

  jclass map_cls = env->FindClass("java/util/Map");
  jclass set_cls = env->FindClass("java/util/Set");
  jclass iterator_cls = env->FindClass("java/util/Iterator");
  jclass entry_cls = env->FindClass("java/util/Map$Entry");
  if (!map_cls || !set_cls || !iterator_cls || !entry_cls) {
    if (map_cls) env->DeleteLocalRef(map_cls);
    if (set_cls) env->DeleteLocalRef(set_cls);
    if (iterator_cls) env->DeleteLocalRef(iterator_cls);
    if (entry_cls) env->DeleteLocalRef(entry_cls);
    return;
  }

  jmethodID mid_entry_set = env->GetMethodID(map_cls, "entrySet", "()Ljava/util/Set;");
  jmethodID mid_iterator = env->GetMethodID(set_cls, "iterator", "()Ljava/util/Iterator;");
  jmethodID mid_has_next = env->GetMethodID(iterator_cls, "hasNext", "()Z");
  jmethodID mid_next = env->GetMethodID(iterator_cls, "next", "()Ljava/lang/Object;");
  jmethodID mid_get_key = env->GetMethodID(entry_cls, "getKey", "()Ljava/lang/Object;");
  jmethodID mid_get_value = env->GetMethodID(entry_cls, "getValue", "()Ljava/lang/Object;");

  if (!mid_entry_set || !mid_iterator || !mid_has_next || !mid_next || !mid_get_key || !mid_get_value) {
    env->DeleteLocalRef(map_cls);
    env->DeleteLocalRef(set_cls);
    env->DeleteLocalRef(iterator_cls);
    env->DeleteLocalRef(entry_cls);
    return;
  }

  jobject entry_set = env->CallObjectMethod(obj, mid_entry_set);
  if (!entry_set) {
    env->DeleteLocalRef(map_cls);
    env->DeleteLocalRef(set_cls);
    env->DeleteLocalRef(iterator_cls);
    env->DeleteLocalRef(entry_cls);
    return;
  }

  jobject iterator = env->CallObjectMethod(entry_set, mid_iterator);
  if (!iterator) {
    env->DeleteLocalRef(entry_set);
    env->DeleteLocalRef(map_cls);
    env->DeleteLocalRef(set_cls);
    env->DeleteLocalRef(iterator_cls);
    env->DeleteLocalRef(entry_cls);
    return;
  }

  while (env->CallBooleanMethod(iterator, mid_has_next) == JNI_TRUE) {
    jobject entry = env->CallObjectMethod(iterator, mid_next);
    if (!entry) break;

    jobject key = env->CallObjectMethod(entry, mid_get_key);
    jobject value = env->CallObjectMethod(entry, mid_get_value);

    json pair;
    pair["key"] = serialize_object_value(jvmti, env, key, deps);
    pair["value"] = serialize_object_value(jvmti, env, value, deps);
    out["entries"].push_back(pair);

    if (key) env->DeleteLocalRef(key);
    if (value) env->DeleteLocalRef(value);
    env->DeleteLocalRef(entry);
  }

  env->DeleteLocalRef(iterator);
  env->DeleteLocalRef(entry_set);
  env->DeleteLocalRef(map_cls);
  env->DeleteLocalRef(set_cls);
  env->DeleteLocalRef(iterator_cls);
  env->DeleteLocalRef(entry_cls);
}

void serialize_list_elements(jvmtiEnv* jvmti, JNIEnv* env, jobject obj, json& out, const DumpSerializerDeps& deps) {
  serialize_collection_elements(jvmti, env, obj, out, deps);
}

void serialize_set_elements(jvmtiEnv* jvmti, JNIEnv* env, jobject obj, json& out, const DumpSerializerDeps& deps) {
  serialize_collection_elements(jvmti, env, obj, out, deps);
}

const CustomFieldSerializer kCustomFieldSerializers[] = {
    {"map", is_map_instance, serialize_map_entries},
    {"list", is_list_instance, serialize_list_elements},
    {"set", is_set_instance, serialize_set_elements},
};

bool try_serialize_custom_field(jvmtiEnv* jvmti, JNIEnv* env, jobject obj, json& out, const DumpSerializerDeps& deps) {
  for (size_t i = 0; i < sizeof(kCustomFieldSerializers) / sizeof(kCustomFieldSerializers[0]); i++) {
    const CustomFieldSerializer& serializer = kCustomFieldSerializers[i];
    if (!serializer.matches(env, obj)) continue;

    out["kind"] = serializer.kind;
    serializer.serialize(jvmti, env, obj, out, deps);
    return true;
  }
  return false;
}

json serialize_object_value(jvmtiEnv* jvmti, JNIEnv* env, jobject obj, const DumpSerializerDeps& deps) {
  if (!obj) return {{"kind", "null"}};

  JvmTypeInfo object_type = get_object_type_info(jvmti, env, obj);

  json value;
  value["java_type_name"] = legacy_java_type_name(object_type);
  value["type"] = TypeInfoToJson(object_type);

  if (object_type.descriptor == "Ljava/lang/String;") {
    const char* chars = env->GetStringUTFChars(reinterpret_cast<jstring>(obj), nullptr);
    value["kind"] = "string";
    value["string_value"] = chars ? chars : "";
    if (chars) env->ReleaseStringUTFChars(reinterpret_cast<jstring>(obj), chars);
    return value;
  }

  if (read_enum_value(env, obj, object_type, value)) {
    return value;
  }

  char primitive_descriptor = '\0';
  json primitive_value;
  if (read_boxed_primitive(env, obj, object_type.descriptor, primitive_descriptor, primitive_value)) {
    value["kind"] = "primitive";
    value["primitive_descriptor"] = std::string(1, primitive_descriptor);
    value["primitive_value"] = primitive_value;
    return value;
  }

  if (!object_type.descriptor.empty() && object_type.descriptor[0] == '[') {
    value["kind"] = "array";
    value["array"] = BuildArrayDump(jvmti, env, obj, object_type.descriptor.c_str(), deps);
    return value;
  }

  json custom;
  if (try_serialize_custom_field(jvmti, env, obj, custom, deps)) {
    value["kind"] = "custom";
    value["object_id"] = deps.resolve_object_id(jvmti, obj);
    value["custom"] = custom;
    return value;
  }

  value["kind"] = "object_ref";
  value["object_id"] = deps.resolve_object_id(jvmti, obj);
  return value;
}

json serialize_primitive_return(char descriptor, jvalue value) {
  json result;
  result["kind"] = "primitive";
  result["primitive_descriptor"] = std::string(1, descriptor);

  switch (descriptor) {
    case 'Z':
      result["primitive_value"] = static_cast<bool>(value.z);
      break;
    case 'B':
      result["primitive_value"] = static_cast<int>(value.b);
      break;
    case 'C':
      result["primitive_value"] = static_cast<int>(value.c);
      break;
    case 'S':
      result["primitive_value"] = static_cast<int>(value.s);
      break;
    case 'I':
      result["primitive_value"] = value.i;
      break;
    case 'J':
      result["primitive_value"] = static_cast<long long>(value.j);
      break;
    case 'F':
      result["primitive_value"] = value.f;
      break;
    case 'D':
      result["primitive_value"] = value.d;
      break;
    default:
      result["kind"] = "unknown";
      result["primitive_value"] = nullptr;
      break;
  }
  return result;
}
}  // namespace

bool ReadPrimitiveField(JNIEnv* env, jobject owner, jfieldID field, char descriptor, json& out) {
  switch (descriptor) {
    case 'Z':
      out = static_cast<bool>(env->GetBooleanField(owner, field));
      return true;
    case 'B':
      out = static_cast<int>(env->GetByteField(owner, field));
      return true;
    case 'C':
      out = static_cast<int>(env->GetCharField(owner, field));
      return true;
    case 'S':
      out = static_cast<int>(env->GetShortField(owner, field));
      return true;
    case 'I':
      out = env->GetIntField(owner, field);
      return true;
    case 'J':
      out = static_cast<long long>(env->GetLongField(owner, field));
      return true;
    case 'F':
      out = env->GetFloatField(owner, field);
      return true;
    case 'D':
      out = env->GetDoubleField(owner, field);
      return true;
    default:
      return false;
  }
}

bool ReadLocalPrimitive(jvmtiEnv* jvmti, jthread thread, jint slot, char descriptor, json& out) {
  switch (descriptor) {
    case 'Z': {
      jint value = 0;
      if (jvmti->GetLocalInt(thread, 0, slot, &value) != JVMTI_ERROR_NONE) return false;
      out = static_cast<bool>(value);
      return true;
    }
    case 'B':
    case 'C':
    case 'S':
    case 'I': {
      jint value = 0;
      if (jvmti->GetLocalInt(thread, 0, slot, &value) != JVMTI_ERROR_NONE) return false;
      out = value;
      return true;
    }
    case 'J': {
      jlong value = 0;
      if (jvmti->GetLocalLong(thread, 0, slot, &value) != JVMTI_ERROR_NONE) return false;
      out = static_cast<long long>(value);
      return true;
    }
    case 'F': {
      jfloat value = 0;
      if (jvmti->GetLocalFloat(thread, 0, slot, &value) != JVMTI_ERROR_NONE) return false;
      out = value;
      return true;
    }
    case 'D': {
      jdouble value = 0;
      if (jvmti->GetLocalDouble(thread, 0, slot, &value) != JVMTI_ERROR_NONE) return false;
      out = value;
      return true;
    }
    default:
      return false;
  }
}

std::string PrettyDescriptorName(const std::string& descriptor) {
  JvmTypeInfo info = ParseJvmTypeDescriptor(descriptor);
  return info.fqcn.empty() ? descriptor : info.fqcn;
}

nlohmann::json SerializeObjectValue(jvmtiEnv* jvmti, JNIEnv* env, jobject obj, const DumpSerializerDeps& deps) {
  return serialize_object_value(jvmti, env, obj, deps);
}

nlohmann::json SerializeReturnValue(jvmtiEnv* jvmti, JNIEnv* env, const std::string& return_descriptor, jvalue return_value,
                                    bool was_popped_by_exception, const DumpSerializerDeps& deps) {
  if (was_popped_by_exception) return {{"kind", "exception"}};
  if (return_descriptor.empty()) return {{"kind", "unknown"}};
  if (return_descriptor == "V") return {{"kind", "void"}};

  if (IsPrimitiveDescriptorChar(return_descriptor[0])) {
    return serialize_primitive_return(return_descriptor[0], return_value);
  }

  if (return_descriptor[0] == 'L' || return_descriptor[0] == '[') {
    if (!return_value.l) return {{"kind", "null"}};
    return serialize_object_value(jvmti, env, return_value.l, deps);
  }

  return {{"kind", "unknown"}};
}

nlohmann::json BuildObjectRef(jvmtiEnv* jvmti, JNIEnv* env, jobject obj, const DumpSerializerDeps& deps) {
  JvmTypeInfo object_type = get_object_type_info(jvmti, env, obj);

  json ref;
  ref["object_id"] = deps.resolve_object_id(jvmti, obj);
  ref["java_type_name"] = legacy_java_type_name(object_type);
  ref["type"] = TypeInfoToJson(object_type);
  ref["fields"] = json::array();
  return ref;
}

nlohmann::json BuildArrayDump(jvmtiEnv* jvmti, JNIEnv* env, jobject array_obj, const char* sig,
                              const DumpSerializerDeps& deps) {
  const std::string descriptor = sig ? sig : "";
  JvmTypeInfo array_type = ParseJvmTypeDescriptor(descriptor);

  json result;
  result["object_id"] = deps.resolve_object_id(jvmti, array_obj);
  result["java_type_name"] = descriptor;
  result["type"] = TypeInfoToJson(array_type);
  result["elements"] = json::array();

  if (!array_obj) return result;

  jsize len = env->GetArrayLength((jarray)array_obj);
  result["length"] = len;
  if (descriptor.size() < 2 || descriptor[0] != '[') return result;

  switch (descriptor[1]) {
    case 'Z': {
      jbooleanArray arr = reinterpret_cast<jbooleanArray>(array_obj);
      jboolean* elems = env->GetBooleanArrayElements(arr, nullptr);
      if (!elems) break;
      for (jsize i = 0; i < len; i++) result["elements"].push_back(static_cast<bool>(elems[i]));
      env->ReleaseBooleanArrayElements(arr, elems, JNI_ABORT);
      break;
    }
    case 'B': {
      jbyteArray arr = reinterpret_cast<jbyteArray>(array_obj);
      jbyte* elems = env->GetByteArrayElements(arr, nullptr);
      if (!elems) break;
      for (jsize i = 0; i < len; i++) result["elements"].push_back(static_cast<int>(elems[i]));
      env->ReleaseByteArrayElements(arr, elems, JNI_ABORT);
      break;
    }
    case 'C': {
      jcharArray arr = reinterpret_cast<jcharArray>(array_obj);
      jchar* elems = env->GetCharArrayElements(arr, nullptr);
      if (!elems) break;
      for (jsize i = 0; i < len; i++) result["elements"].push_back(static_cast<int>(elems[i]));
      env->ReleaseCharArrayElements(arr, elems, JNI_ABORT);
      break;
    }
    case 'S': {
      jshortArray arr = reinterpret_cast<jshortArray>(array_obj);
      jshort* elems = env->GetShortArrayElements(arr, nullptr);
      if (!elems) break;
      for (jsize i = 0; i < len; i++) result["elements"].push_back(static_cast<int>(elems[i]));
      env->ReleaseShortArrayElements(arr, elems, JNI_ABORT);
      break;
    }
    case 'I': {
      jintArray arr = reinterpret_cast<jintArray>(array_obj);
      jint* elems = env->GetIntArrayElements(arr, nullptr);
      if (!elems) break;
      for (jsize i = 0; i < len; i++) result["elements"].push_back(elems[i]);
      env->ReleaseIntArrayElements(arr, elems, JNI_ABORT);
      break;
    }
    case 'J': {
      jlongArray arr = reinterpret_cast<jlongArray>(array_obj);
      jlong* elems = env->GetLongArrayElements(arr, nullptr);
      if (!elems) break;
      for (jsize i = 0; i < len; i++) result["elements"].push_back(static_cast<long long>(elems[i]));
      env->ReleaseLongArrayElements(arr, elems, JNI_ABORT);
      break;
    }
    case 'F': {
      jfloatArray arr = reinterpret_cast<jfloatArray>(array_obj);
      jfloat* elems = env->GetFloatArrayElements(arr, nullptr);
      if (!elems) break;
      for (jsize i = 0; i < len; i++) result["elements"].push_back(elems[i]);
      env->ReleaseFloatArrayElements(arr, elems, JNI_ABORT);
      break;
    }
    case 'D': {
      jdoubleArray arr = reinterpret_cast<jdoubleArray>(array_obj);
      jdouble* elems = env->GetDoubleArrayElements(arr, nullptr);
      if (!elems) break;
      for (jsize i = 0; i < len; i++) result["elements"].push_back(elems[i]);
      env->ReleaseDoubleArrayElements(arr, elems, JNI_ABORT);
      break;
    }
    case 'L':
    case '[': {
      jobjectArray arr = reinterpret_cast<jobjectArray>(array_obj);
      for (jsize i = 0; i < len; i++) {
        jobject elem = env->GetObjectArrayElement(arr, i);
        if (!elem) {
          result["elements"].push_back(nullptr);
          continue;
        }
        result["elements"].push_back(SerializeObjectValue(jvmti, env, elem, deps));
        env->DeleteLocalRef(elem);
      }
      break;
    }
    default:
      break;
  }

  return result;
}

void FillObjectFields(jvmtiEnv* jvmti, JNIEnv* env, jobject obj, json& obj_json, const DumpSerializerDeps& deps) {
  if (!obj) return;

  jclass cls = env->GetObjectClass(obj);

  jint field_count = 0;
  jfieldID* fields = nullptr;

  if (jvmti->GetClassFields(cls, &field_count, &fields) != JVMTI_ERROR_NONE) return;

  for (int i = 0; i < field_count; i++) {
    char* name = nullptr;
    char* sig = nullptr;
    if (jvmti->GetFieldName(cls, fields[i], &name, &sig, nullptr) != JVMTI_ERROR_NONE) continue;
    jint modifiers = 0;
    jvmti->GetFieldModifiers(cls, fields[i], &modifiers);
    if (modifiers & JVM_ACC_STATIC) {
      if (name) jvmti->Deallocate(reinterpret_cast<unsigned char*>(name));
      if (sig) jvmti->Deallocate(reinterpret_cast<unsigned char*>(sig));
      continue;
    }
    if (!name || !sig) {
      if (name) jvmti->Deallocate(reinterpret_cast<unsigned char*>(name));
      if (sig) jvmti->Deallocate(reinterpret_cast<unsigned char*>(sig));
      continue;
    }

    json field_entry;
    field_entry["name"] = name;
    field_entry["java_type_name"] = sig;
    field_entry["type"] = TypeInfoToJson(ParseJvmTypeDescriptor(sig));
    field_entry["modifiers"] = modifiers;
    field_entry["is_public"] = (modifiers & JVM_ACC_PUBLIC) != 0;
    field_entry["is_private"] = (modifiers & JVM_ACC_PRIVATE) != 0;
    field_entry["is_protected"] = (modifiers & JVM_ACC_PROTECTED) != 0;

    if (IsPrimitiveDescriptorChar(sig[0])) {
      json primitive_value;
      if (ReadPrimitiveField(env, obj, fields[i], sig[0], primitive_value)) field_entry["primitive_value"] = primitive_value;
    } else if (sig[0] == 'L') {
      jobject val = env->GetObjectField(obj, fields[i]);
      if (val) {
        JvmTypeInfo runtime_type = get_object_type_info(jvmti, env, val);
        field_entry["object_id"] = deps.resolve_object_id(jvmti, val);
        field_entry["runtime_type"] = TypeInfoToJson(runtime_type);
        if (is_input_stream_instance(env, val)) field_entry["mock_kind"] = "input_stream";
        json custom;
        if (try_serialize_custom_field(jvmti, env, val, custom, deps)) {
          field_entry["custom"] = custom;
        }
        env->DeleteLocalRef(val);
      }
    } else if (sig[0] == '[') {
      jobject arr = env->GetObjectField(obj, fields[i]);
      field_entry["array"] = BuildArrayDump(jvmti, env, arr, sig, deps);
      if (arr) env->DeleteLocalRef(arr);
    }

    obj_json["fields"].push_back(field_entry);

    jvmti->Deallocate(reinterpret_cast<unsigned char*>(name));
    jvmti->Deallocate(reinterpret_cast<unsigned char*>(sig));
  }

  jvmti->Deallocate(reinterpret_cast<unsigned char*>(fields));
}
