#ifndef DUMPER_DUMP_SERIALIZER_H
#define DUMPER_DUMP_SERIALIZER_H

#include <functional>
#include <string>

#include <jvmti.h>
#include <nlohmann/json.hpp>

struct DumpSerializerDeps {
  std::function<jlong(jvmtiEnv*, jobject)> resolve_object_id;
};

nlohmann::json BuildObjectRef(jvmtiEnv* jvmti, JNIEnv* env, jobject obj, const DumpSerializerDeps& deps);
nlohmann::json BuildArrayDump(jvmtiEnv* jvmti, JNIEnv* env, jobject array_obj, const char* sig,
                              const DumpSerializerDeps& deps);
void FillObjectFields(jvmtiEnv* jvmti, JNIEnv* env, jobject obj, nlohmann::json& obj_json, const DumpSerializerDeps& deps);

bool ReadPrimitiveField(JNIEnv* env, jobject owner, jfieldID field, char descriptor, nlohmann::json& out);
bool ReadLocalPrimitive(jvmtiEnv* jvmti, jthread thread, jint slot, char descriptor, nlohmann::json& out);
std::string PrettyDescriptorName(const std::string& descriptor);
nlohmann::json SerializeObjectValue(jvmtiEnv* jvmti, JNIEnv* env, jobject obj, const DumpSerializerDeps& deps);
nlohmann::json SerializeReturnValue(jvmtiEnv* jvmti, JNIEnv* env, const std::string& return_descriptor, jvalue return_value,
                                    bool was_popped_by_exception, const DumpSerializerDeps& deps);

#endif  // DUMPER_DUMP_SERIALIZER_H
