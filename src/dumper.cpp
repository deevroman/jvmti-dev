#include <classfile_constants.h>
#include <jvmti.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "control_socket.h"
#include "dump_serializer.h"
#include "llm_dump_formatter.h"
#include "type_utils.h"

using json = nlohmann::json;

static json g_dump = json::array();
static std::mutex g_dump_mutex;
static std::mutex g_tag_mutex;
static std::mutex g_method_exit_mutex;

static FILE* out;
static std::string target_class, target_method, target_method_signature;
static std::string dump_file_path = "dump.json";
static std::string llm_dump_file_path;
static std::string config_file_path;
static int control_port = 9009;
static jrawMonitorID vmtrace_lock;
static jlong start_time;
static std::atomic<jlong> g_next_object_tag{1};
static std::map<jlong, int> g_method_exit_depth_by_thread;

struct MockObjectTrace {
  jlong object_id = 0;
  std::string mock_kind;
  std::string field_name;
  std::string declared_descriptor;
  std::string declared_fqcn;
  std::string runtime_descriptor;
  std::string runtime_fqcn;
  json calls = json::array();
};

struct InvocationTraceContext {
  jlong thread_id = 0;
  std::map<jlong, MockObjectTrace> mock_objects;
};

static std::mutex g_invocation_mutex;
static std::map<jlong, std::vector<InvocationTraceContext>> g_invocation_stacks;

static void trace(jvmtiEnv* jvmti, const char* fmt, ...) {
  jlong current_time;
  jvmti->GetTime(&current_time);

  char buf[1024];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  jvmti->RawMonitorEnter(vmtrace_lock);

  fprintf(out, "[%.5f] %s\n", (current_time - start_time) / 1000000000.0, buf);

  jvmti->RawMonitorExit(vmtrace_lock);
  fflush(out);
}

static char* fix_class_name(char* class_name) {
  // Strip 'L' and ';' from class signature
  class_name[strlen(class_name) - 1] = 0;
  return class_name + 1;
}

static std::string normalize_class_name(std::string class_name) {
  std::replace(class_name.begin(), class_name.end(), '.', '/');
  return class_name;
}

static bool has_target_config() {
  return !target_class.empty() && !target_method.empty() && !target_method_signature.empty();
}

static jlong get_or_assign_object_tag(jvmtiEnv* jvmti, jobject obj) {
  if (obj == nullptr) return 0;

  std::lock_guard<std::mutex> lock(g_tag_mutex);

  jlong tag = 0;
  if (jvmti->GetTag(obj, &tag) != JVMTI_ERROR_NONE) return 0;
  if (tag != 0) return tag;

  jlong new_tag = g_next_object_tag.fetch_add(1);
  if (new_tag == 0) new_tag = g_next_object_tag.fetch_add(1);

  if (jvmti->SetTag(obj, new_tag) != JVMTI_ERROR_NONE) return 0;
  return new_tag;
}

static void apply_runtime_config(const RuntimeConfig& runtime_config) {
  target_class = normalize_class_name(runtime_config.target_class);
  target_method = runtime_config.target_method;
  target_method_signature = runtime_config.target_method_signature;
  if (!runtime_config.dump_path.empty()) dump_file_path = runtime_config.dump_path;
  if (!runtime_config.llm_dump_path.empty()) llm_dump_file_path = runtime_config.llm_dump_path;
}

static std::string json_string_or_empty(const json& node, const char* key) {
  if (!node.is_object()) return "";
  auto it = node.find(key);
  if (it == node.end() || !it->is_string()) return "";
  return it->get<std::string>();
}

static void register_mock_object_from_field(const json& field, InvocationTraceContext& ctx) {
  if (!field.is_object()) return;
  auto mock_kind_it = field.find("mock_kind");
  auto object_id_it = field.find("object_id");
  if (mock_kind_it == field.end() || !mock_kind_it->is_string()) return;
  if (object_id_it == field.end() || !object_id_it->is_number_integer()) return;

  const jlong object_id = object_id_it->get<jlong>();
  if (object_id == 0) return;

  MockObjectTrace& trace = ctx.mock_objects[object_id];
  trace.object_id = object_id;
  if (trace.mock_kind.empty()) trace.mock_kind = mock_kind_it->get<std::string>();
  if (trace.field_name.empty()) trace.field_name = json_string_or_empty(field, "name");
  if (trace.declared_descriptor.empty()) trace.declared_descriptor = json_string_or_empty(field, "java_type_name");

  if (trace.declared_fqcn.empty()) {
    auto type_it = field.find("type");
    if (type_it != field.end() && type_it->is_object()) trace.declared_fqcn = json_string_or_empty(*type_it, "fqcn");
  }

  auto runtime_type_it = field.find("runtime_type");
  if (runtime_type_it != field.end() && runtime_type_it->is_object()) {
    if (trace.runtime_descriptor.empty()) trace.runtime_descriptor = json_string_or_empty(*runtime_type_it, "descriptor");
    if (trace.runtime_fqcn.empty()) trace.runtime_fqcn = json_string_or_empty(*runtime_type_it, "fqcn");
  }
}

static void collect_mock_objects_from_object_ref(const json& object_ref, InvocationTraceContext& ctx) {
  if (!object_ref.is_object()) return;
  auto fields_it = object_ref.find("fields");
  if (fields_it == object_ref.end() || !fields_it->is_array()) return;
  for (const auto& field : *fields_it) register_mock_object_from_field(field, ctx);
}

static InvocationTraceContext build_invocation_context(jvmtiEnv* jvmti, jthread thread, const json& object_refs) {
  InvocationTraceContext ctx;
  ctx.thread_id = get_or_assign_object_tag(jvmti, thread);
  if (!object_refs.is_array()) return ctx;

  for (const auto& object_ref : object_refs) collect_mock_objects_from_object_ref(object_ref, ctx);
  return ctx;
}

static void push_invocation_context(const InvocationTraceContext& ctx) {
  if (ctx.thread_id == 0) return;
  std::lock_guard<std::mutex> lock(g_invocation_mutex);
  g_invocation_stacks[ctx.thread_id].push_back(ctx);
}

static bool is_mock_object_tracked(jlong thread_id, jlong object_id) {
  if (thread_id == 0 || object_id == 0) return false;
  std::lock_guard<std::mutex> lock(g_invocation_mutex);

  auto stack_it = g_invocation_stacks.find(thread_id);
  if (stack_it == g_invocation_stacks.end() || stack_it->second.empty()) return false;
  const InvocationTraceContext& ctx = stack_it->second.back();
  return ctx.mock_objects.find(object_id) != ctx.mock_objects.end();
}

static void append_mock_call(jlong thread_id, jlong object_id, const json& call) {
  if (thread_id == 0 || object_id == 0) return;
  std::lock_guard<std::mutex> lock(g_invocation_mutex);

  auto stack_it = g_invocation_stacks.find(thread_id);
  if (stack_it == g_invocation_stacks.end() || stack_it->second.empty()) return;
  InvocationTraceContext& ctx = stack_it->second.back();
  auto mock_it = ctx.mock_objects.find(object_id);
  if (mock_it == ctx.mock_objects.end()) return;

  mock_it->second.calls.push_back(call);
}

static bool pop_invocation_context(jlong thread_id, InvocationTraceContext& out) {
  if (thread_id == 0) return false;
  std::lock_guard<std::mutex> lock(g_invocation_mutex);

  auto stack_it = g_invocation_stacks.find(thread_id);
  if (stack_it == g_invocation_stacks.end() || stack_it->second.empty()) return false;

  out = stack_it->second.back();
  stack_it->second.pop_back();
  if (stack_it->second.empty()) g_invocation_stacks.erase(stack_it);
  return true;
}

static void begin_thread_method_exit_tracing(jvmtiEnv* jvmti, jthread thread) {
  const jlong thread_id = get_or_assign_object_tag(jvmti, thread);
  if (thread_id == 0) return;

  bool should_enable = false;
  {
    std::lock_guard<std::mutex> lock(g_method_exit_mutex);
    int& depth = g_method_exit_depth_by_thread[thread_id];
    if (depth == 0) should_enable = true;
    depth += 1;
  }

  if (!should_enable) return;

  const jvmtiError err = jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_METHOD_EXIT, thread);
  if (err != JVMTI_ERROR_NONE) {
    trace(jvmti, "Failed to enable METHOD_EXIT for thread %lld: %d", static_cast<long long>(thread_id), err);
  }
}

static void end_thread_method_exit_tracing(jvmtiEnv* jvmti, jthread thread) {
  const jlong thread_id = get_or_assign_object_tag(jvmti, thread);
  if (thread_id == 0) return;

  bool should_disable = false;
  {
    std::lock_guard<std::mutex> lock(g_method_exit_mutex);
    auto it = g_method_exit_depth_by_thread.find(thread_id);
    if (it == g_method_exit_depth_by_thread.end()) return;

    if (it->second > 0) it->second -= 1;
    if (it->second <= 0) {
      g_method_exit_depth_by_thread.erase(it);
      should_disable = true;
    }
  }

  if (!should_disable) return;

  const jvmtiError err = jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_METHOD_EXIT, thread);
  if (err != JVMTI_ERROR_NONE) {
    trace(jvmti, "Failed to disable METHOD_EXIT for thread %lld: %d", static_cast<long long>(thread_id), err);
  }
}

static json invocation_mock_objects_to_json(const InvocationTraceContext& ctx) {
  json out = json::array();
  for (const auto& entry : ctx.mock_objects) {
    const MockObjectTrace& trace = entry.second;
    json item;
    item["object_id"] = trace.object_id;
    item["mock_kind"] = trace.mock_kind;
    if (!trace.field_name.empty()) item["field_name"] = trace.field_name;
    if (!trace.declared_descriptor.empty()) item["declared_descriptor"] = trace.declared_descriptor;
    if (!trace.declared_fqcn.empty()) item["declared_fqcn"] = trace.declared_fqcn;

    json runtime_type = json::object();
    if (!trace.runtime_descriptor.empty()) runtime_type["descriptor"] = trace.runtime_descriptor;
    if (!trace.runtime_fqcn.empty()) runtime_type["fqcn"] = trace.runtime_fqcn;
    if (!runtime_type.empty()) item["runtime_type"] = runtime_type;

    item["calls"] = trace.calls;
    out.push_back(item);
  }
  return out;
}

static int descriptor_slot_width(const std::string& descriptor) {
  if (descriptor.empty()) return 1;
  const char type = descriptor[0];
  return (type == 'J' || type == 'D') ? 2 : 1;
}

static size_t consume_type_descriptor(const std::string& descriptor, size_t start) {
  if (start >= descriptor.size()) return start;
  const char ch = descriptor[start];

  if (IsPrimitiveDescriptorChar(ch)) return start + 1;
  if (ch == 'L') {
    const size_t end = descriptor.find(';', start);
    return end == std::string::npos ? start : end + 1;
  }
  if (ch == '[') {
    size_t i = start;
    while (i < descriptor.size() && descriptor[i] == '[') i++;
    if (i >= descriptor.size()) return start;
    if (descriptor[i] == 'L') {
      const size_t end = descriptor.find(';', i);
      return end == std::string::npos ? start : end + 1;
    }
    if (IsPrimitiveDescriptorChar(descriptor[i])) return i + 1;
    return start;
  }
  return start;
}

static std::vector<std::string> parse_method_argument_descriptors(const std::string& method_descriptor) {
  std::vector<std::string> descriptors;
  if (method_descriptor.empty() || method_descriptor[0] != '(') return descriptors;

  size_t i = 1;
  while (i < method_descriptor.size() && method_descriptor[i] != ')') {
    const size_t next = consume_type_descriptor(method_descriptor, i);
    if (next <= i) break;
    descriptors.push_back(method_descriptor.substr(i, next - i));
    i = next;
  }

  return descriptors;
}

static bool method_signature_matches_target(jvmtiEnv* jvmti, jmethodID method) {
  char* method_signature = nullptr;
  const jvmtiError err = jvmti->GetMethodName(method, nullptr, &method_signature, nullptr);
  if (err != JVMTI_ERROR_NONE || !method_signature) return false;

  const bool matches = target_method_signature == method_signature;
  jvmti->Deallocate(reinterpret_cast<unsigned char*>(method_signature));
  return matches;
}

static jlong resolve_object_id(jvmtiEnv* jvmti, jobject obj) { return get_or_assign_object_tag(jvmti, obj); }
static const DumpSerializerDeps kDumpSerializerDeps{resolve_object_id};

std::string field_modifiers_to_string(jint mod) {
  std::string s;

  if (mod & JVM_ACC_PUBLIC) s += "public ";
  if (mod & JVM_ACC_PRIVATE) s += "private ";
  if (mod & JVM_ACC_PROTECTED) s += "protected ";

  if (mod & JVM_ACC_STATIC) s += "static ";
  if (mod & JVM_ACC_FINAL) s += "final ";
  if (mod & JVM_ACC_VOLATILE) s += "volatile ";
  if (mod & JVM_ACC_TRANSIENT) s += "transient ";

  if (mod & JVM_ACC_SYNTHETIC) s += "synthetic ";

  if (!s.empty()) s.pop_back();

  return s;
}

static void add_method_event(const std::string& class_name, const std::string& method_name,
                             const std::string& state_type, jlong this_object_id, const json& arguments,
                             const json& object_refs, const json& return_value = {}) {
  std::lock_guard<std::mutex> lock(g_dump_mutex);

  json entry;
  entry["class"] = class_name;
  entry["method"] = method_name;
  entry["state_type"] = state_type;
  entry["this_object_id"] = this_object_id;
  entry["arguments"] = arguments;
  entry["object_refs"] = object_refs;
  entry["return_value"] = return_value;

  g_dump.push_back(entry);
}

struct PrintCtx {
  std::unordered_set<jobject> visited;
  int max_depth = 5;
};

std::string indent(int depth) { return std::string(depth * 2, ' '); }

void print_object(jvmtiEnv* jvmti, JNIEnv* env, jobject obj, PrintCtx& ctx, int depth);

void print_array(jvmtiEnv* jvmti, JNIEnv* env, jobject arr, PrintCtx& ctx, int depth) {
  jsize len = env->GetArrayLength((jarray)arr);
  jclass cls = env->GetObjectClass(arr);

  char* sig = nullptr;
  jvmti->GetClassSignature(cls, &sig, nullptr);

  trace(jvmti, "%sarray %s len=%d @%p", indent(depth).c_str(), sig, len, arr);

  if (depth >= ctx.max_depth) {
    trace(jvmti, "%s<max depth>", indent(depth + 1).c_str());
    goto cleanup;
  }

  // Object[]
  if (sig[1] == 'L' || sig[1] == '[') {
    jobjectArray oa = (jobjectArray)arr;
    for (jsize i = 0; i < len; i++) {
      jobject elem = env->GetObjectArrayElement(oa, i);
      trace(jvmti, "%s[%d]:", indent(depth + 1).c_str(), i);
      print_object(jvmti, env, elem, ctx, depth + 2);
    }
  }

cleanup:
  if (sig) jvmti->Deallocate((unsigned char*)sig);
}

void print_object(jvmtiEnv* jvmti, JNIEnv* env, jobject obj, PrintCtx& ctx, int depth) {
  if (obj == nullptr) {
    trace(jvmti, "%snull", indent(depth).c_str());
    return;
  }

  if (ctx.visited.count(obj)) {
    trace(jvmti, "%s<cycle @%p>", indent(depth).c_str(), obj);
    return;
  }

  ctx.visited.insert(obj);

  jclass cls = env->GetObjectClass(obj);

  char* class_sig = nullptr;
  jvmti->GetClassSignature(cls, &class_sig, nullptr);

  trace(jvmti, "%sobject %s @%p", indent(depth).c_str(), fix_class_name(class_sig), obj);

  // массив
  if (class_sig[0] == '[') {
    print_array(jvmti, env, obj, ctx, depth + 1);
    jvmti->Deallocate((unsigned char*)class_sig);
    return;
  }

  if (depth >= ctx.max_depth) {
    trace(jvmti, "%s<max depth>", indent(depth + 1).c_str());
    jvmti->Deallocate((unsigned char*)class_sig);
    return;
  }

  jint field_count = 0;
  jfieldID* fields = nullptr;
  if (jvmti->GetClassFields(cls, &field_count, &fields) != JVMTI_ERROR_NONE) {
    jvmti->Deallocate((unsigned char*)class_sig);
    return;
  }

  for (int i = 0; i < field_count; i++) {
    char* name = nullptr;
    char* sig = nullptr;
    jvmti->GetFieldName(cls, fields[i], &name, &sig, nullptr);
    jint modifiers = 0;
    jvmti->GetFieldModifiers(cls, fields[i], &modifiers);
    std::string mods = field_modifiers_to_string(modifiers);

    trace(jvmti, "%s%s%s%s:", indent(depth + 1).c_str(), mods.empty() ? "" : mods.c_str(), mods.empty() ? "" : " ",
          name);

    if (modifiers & JVM_ACC_STATIC) {
      trace(jvmti, "%s<static field skipped>", indent(depth + 2).c_str());
      if (name) jvmti->Deallocate((unsigned char*)name);
      if (sig) jvmti->Deallocate((unsigned char*)sig);
      continue;
    }

    if (sig[0] == 'L' || sig[0] == '[') {
      jobject val = env->GetObjectField(obj, fields[i]);
      print_object(jvmti, env, val, ctx, depth + 2);
    } else if (IsPrimitiveDescriptorChar(sig[0])) {
      json primitive_value;
      if (ReadPrimitiveField(env, obj, fields[i], sig[0], primitive_value))
        trace(jvmti, "%s%s", indent(depth + 2).c_str(), primitive_value.dump().c_str());
      else
        trace(jvmti, "%s<primitive unavailable>", indent(depth + 2).c_str());
    } else {
      trace(jvmti, "%s<primitive %s>", indent(depth + 2).c_str(), sig);
    }

    if (name) jvmti->Deallocate((unsigned char*)name);
    if (sig) jvmti->Deallocate((unsigned char*)sig);
  }

  if (fields) jvmti->Deallocate((unsigned char*)fields);
  if (class_sig) jvmti->Deallocate((unsigned char*)class_sig);
}

class ClassName {
 private:
  jvmtiEnv* _jvmti;
  char* _name;

 public:
  ClassName(jvmtiEnv* jvmti, jclass klass) : _jvmti(jvmti), _name(NULL) {
    _jvmti->GetClassSignature(klass, &_name, NULL);
  }

  ~ClassName() { _jvmti->Deallocate((unsigned char*)_name); }

  char* name() { return _name == NULL ? NULL : fix_class_name(_name); }
};

class MethodName {
 private:
  jvmtiEnv* _jvmti;
  char* _holder_name;
  char* _method_name;

 public:
  MethodName(jvmtiEnv* jvmti, jmethodID method) : _jvmti(jvmti), _holder_name(NULL), _method_name(NULL) {
    jclass holder;
    if (_jvmti->GetMethodDeclaringClass(method, &holder) == 0) {
      _jvmti->GetClassSignature(holder, &_holder_name, NULL);
      _jvmti->GetMethodName(method, &_method_name, NULL, NULL);
    }
  }

  ~MethodName() {
    _jvmti->Deallocate((unsigned char*)_method_name);
    _jvmti->Deallocate((unsigned char*)_holder_name);
  }

  char* holder() { return _holder_name == NULL ? NULL : fix_class_name(_holder_name); }

  char* name() { return _method_name; }
};

class ThreadName {
 private:
  jvmtiEnv* _jvmti;
  char* _name;

 public:
  ThreadName(jvmtiEnv* jvmti, jthread thread) : _jvmti(jvmti), _name(NULL) {
    jvmtiThreadInfo info;
    _name = _jvmti->GetThreadInfo(thread, &info) == 0 ? info.name : NULL;
  }

  ~ThreadName() { _jvmti->Deallocate((unsigned char*)_name); }

  char* name() { return _name; }
};

void JNICALL VMStart(jvmtiEnv* jvmti, JNIEnv* env) { trace(jvmti, "VM started"); }

void JNICALL VMInit(jvmtiEnv* jvmti, JNIEnv* env, jthread thread) {
  trace(jvmti, "VM initialized");
  if (has_target_config()) {
    trace(jvmti, "Using static target config: class=%s method=%s signature=%s", target_class.c_str(),
          target_method.c_str(), target_method_signature.c_str());
    return;
  }

  RuntimeConfig runtime_config;
  std::string error_message;

  if (!config_file_path.empty()) {
    trace(jvmti, "Loading runtime config from file: %s", config_file_path.c_str());
    if (!load_runtime_config_from_file(config_file_path, runtime_config, error_message)) {
      trace(jvmti, "Runtime config file error: %s", error_message.c_str());
      return;
    }

    apply_runtime_config(runtime_config);
    trace(jvmti,
          "Runtime config loaded from file: class=%s method=%s signature=%s dump=%s llm_dump=%s",
          target_class.c_str(), target_method.c_str(), target_method_signature.c_str(), dump_file_path.c_str(),
          (llm_dump_file_path.empty() ? "<auto>" : llm_dump_file_path.c_str()));
    return;
  }

  trace(jvmti, "Waiting for runtime config on 127.0.0.1:%d", control_port);

  if (!wait_for_runtime_config(static_cast<uint16_t>(control_port), runtime_config, error_message)) {
    trace(jvmti, "Runtime config error: %s", error_message.c_str());
    return;
  }

  apply_runtime_config(runtime_config);

  trace(jvmti, "Runtime config received: class=%s method=%s signature=%s dump=%s llm_dump=%s", target_class.c_str(),
        target_method.c_str(), target_method_signature.c_str(), dump_file_path.c_str(),
        (llm_dump_file_path.empty() ? "<auto>" : llm_dump_file_path.c_str()));
}

void JNICALL VMDeath(jvmtiEnv* jvmti, JNIEnv* env) {
  trace(jvmti, "VM destroyed");
  std::lock_guard<std::mutex> lock(g_dump_mutex);

  std::ofstream out(dump_file_path);
  out << g_dump.dump(4);
  out.close();

  const std::string resolved_llm_dump_path = llm_dump_file_path.empty() ? DefaultLlmDumpPath(dump_file_path) : llm_dump_file_path;
  std::ofstream llm_out(resolved_llm_dump_path);
  if (llm_out) {
    llm_out << BuildLlmReadableDump(g_dump);
    llm_out.close();
    trace(jvmti, "LLM-readable dump written to: %s", resolved_llm_dump_path.c_str());
  } else {
    trace(jvmti, "Failed to write LLM-readable dump to: %s", resolved_llm_dump_path.c_str());
  }
}

void JNICALL ClassFileLoadHook(jvmtiEnv* jvmti, JNIEnv* env, jclass class_being_redefined, jobject loader,
                               const char* name, jobject protection_domain, jint data_len, const unsigned char* data,
                               jint* new_data_len, unsigned char** new_data) {
  // trace(jvmti, "Loading class: %s (%d bytes)", name, data_len);
}

void JNICALL ClassPrepare(jvmtiEnv* jvmti, JNIEnv* env, jthread thread, jclass klass) {
  ClassName cn(jvmti, klass);
  const char* class_name = cn.name();
  auto name = std::string(class_name ? class_name : "");
  // trace(jvmti, "Class prepared: %s", name.c_str());

  if (!has_target_config() || name != target_class) return;

  jint methods_count = 0;
  jmethodID* methods = nullptr;
  jvmtiError methods_err = jvmti->GetClassMethods(klass, &methods_count, &methods);
  if (methods_err != JVMTI_ERROR_NONE || !methods) {
    trace(jvmti, "GetClassMethods failed for %s: %d", name.c_str(), methods_err);
    return;
  }

  bool breakpoint_set = false;
  for (int i = 0; i < methods_count; i++) {
    jmethodID method = methods[i];
    char* method_name = nullptr;
    char* method_signature = nullptr;
    if (jvmti->GetMethodName(method, &method_name, &method_signature, nullptr) != JVMTI_ERROR_NONE) continue;

    const bool method_match =
        method_name && method_signature && target_method == method_name && target_method_signature == method_signature;

    if (method_name) jvmti->Deallocate(reinterpret_cast<unsigned char*>(method_name));
    if (method_signature) jvmti->Deallocate(reinterpret_cast<unsigned char*>(method_signature));

    if (!method_match) continue;

    jint entry_count = 0;
    jvmtiLineNumberEntry* table = nullptr;
    if (jvmti->GetLineNumberTable(method, &entry_count, &table) == JVMTI_ERROR_NONE && entry_count > 0) {
      jlocation loc = table[0].start_location;
      jvmti->SetBreakpoint(method, loc);
      jvmti->Deallocate(reinterpret_cast<unsigned char*>(table));
      breakpoint_set = true;
      break;
    }

    jlocation start = 0;
    jlocation end = 0;
    if (jvmti->GetMethodLocation(method, &start, &end) == JVMTI_ERROR_NONE) {
      jvmti->SetBreakpoint(method, start);
      breakpoint_set = true;
      break;
    }
  }

  if (methods) jvmti->Deallocate(reinterpret_cast<unsigned char*>(methods));

  if (breakpoint_set)
    trace(jvmti, "Breakpoint set at %s.%s%s", target_class.c_str(), target_method.c_str(),
          target_method_signature.c_str());
  else
    trace(jvmti, "Target method not found in class %s", target_class.c_str());
}

void JNICALL DynamicCodeGenerated(jvmtiEnv* jvmti, const char* name, const void* address, jint length) {
  // trace(jvmti, "Dynamic code generated: %s (%d bytes)", name, length);
}

void JNICALL CompiledMethodLoad(jvmtiEnv* jvmti, jmethodID method, jint code_size, const void* code_addr,
                                jint map_length, const jvmtiAddrLocationMap* map, const void* compile_info) {
  MethodName mn(jvmti, method);
  // trace(jvmti, "Method compiled: %s.%s (%d bytes)", mn.holder(), mn.name(),
  // code_size);
}

void JNICALL CompiledMethodUnload(jvmtiEnv* jvmti, jmethodID method, const void* code_addr) {
  MethodName mn(jvmti, method);
  trace(jvmti, "Method flushed: %s.%s", mn.holder(), mn.name());
}

void JNICALL ThreadStart(jvmtiEnv* jvmti, JNIEnv* env, jthread thread) {
  ThreadName tn(jvmti, thread);
  // trace(jvmti, "Thread started: %s", tn.name());
}

void JNICALL ThreadEnd(jvmtiEnv* jvmti, JNIEnv* env, jthread thread) {
  ThreadName tn(jvmti, thread);
  // trace(jvmti, "Thread finished: %s", tn.name());
}

void JNICALL GarbageCollectionStart(jvmtiEnv* jvmti) { trace(jvmti, "GC started"); }

void JNICALL GarbageCollectionFinish(jvmtiEnv* jvmti) { trace(jvmti, "GC finished"); }

void print_object_fields(jvmtiEnv* jvmti, JNIEnv* env, jthread thread, jmethodID method) {
  jvmtiError err;

  jobject this_obj = nullptr;
  err = jvmti->GetLocalObject(thread, 0, 0, &this_obj);
  if (err != JVMTI_ERROR_NONE || this_obj == nullptr) {
    trace(jvmti, "Cannot get 'this' (err=%d)", err);
    return;
  }

  jclass cls = env->GetObjectClass(this_obj);
  char* class_sig = nullptr;
  jvmti->GetClassSignature(cls, &class_sig, nullptr);
  trace(jvmti, "Fields of %s:", fix_class_name(class_sig));

  jint field_count = 0;
  jfieldID* fields = nullptr;
  err = jvmti->GetClassFields(cls, &field_count, &fields);
  if (err != JVMTI_ERROR_NONE) {
    trace(jvmti, "GetClassFields failed: %d", err);
    return;
  }

  for (int i = 0; i < field_count; i++) {
    char* name = nullptr;
    char* sig = nullptr;
    jvmti->GetFieldName(cls, fields[i], &name, &sig, nullptr);
    jint modifiers = 0;
    jvmti->GetFieldModifiers(cls, fields[i], &modifiers);

    if (modifiers & JVM_ACC_STATIC) {
      if (name) jvmti->Deallocate((unsigned char*)name);
      if (sig) jvmti->Deallocate((unsigned char*)sig);
      continue;
    }

    if (sig && IsPrimitiveDescriptorChar(sig[0])) {
      json value;
      if (ReadPrimitiveField(env, this_obj, fields[i], sig[0], value))
        trace(jvmti, "  %s = %s (%s)", name, value.dump().c_str(), PrimitiveNameFromDescriptorChar(sig[0]).c_str());
      else
        trace(jvmti, "  %s = <primitive unavailable>", name);
    } else if (sig && sig[0] == 'L') {
      jobject val = env->GetObjectField(this_obj, fields[i]);
      trace(jvmti, "  %s = %p (object)", name, val);

      PrintCtx ctx;
      ctx.max_depth = 4;

      print_object(jvmti, env, val, ctx, 0);
    } else if (sig && sig[0] == '[') {
      jobject arr = env->GetObjectField(this_obj, fields[i]);
      std::string type = PrettyDescriptorName(sig);

      if (arr == nullptr) {
        trace(jvmti, "  %s = null (%s)", name, type.c_str());
      } else {
        jsize len = env->GetArrayLength((jarray)arr);
        trace(jvmti, "  %s = array len=%d (%s)", name, len, type.c_str());
      }
    } else {
      trace(jvmti, "  %s = <unsupported type %s>", name, sig ? sig : "?");
    }

    if (name) jvmti->Deallocate((unsigned char*)name);
    if (sig) jvmti->Deallocate((unsigned char*)sig);
  }

  if (class_sig) jvmti->Deallocate((unsigned char*)class_sig);
  if (fields) jvmti->Deallocate((unsigned char*)fields);
}

void JNICALL BreakpointHandler(jvmtiEnv* jvmti, JNIEnv* env, jthread thread, jmethodID method, jlocation location) {
  if (!has_target_config()) return;

  MethodName mn(jvmti, method);
  const char* holder = mn.holder();
  const char* method_name = mn.name();
  if (!holder || !method_name) return;
  std::string class_name = holder;
  std::string class_fqcn = BinaryNameToFqcn(class_name);

  char* method_sig_raw = nullptr;
  std::string method_signature;
  if (jvmti->GetMethodName(method, nullptr, &method_sig_raw, nullptr) == JVMTI_ERROR_NONE && method_sig_raw) {
    method_signature = method_sig_raw;
    jvmti->Deallocate(reinterpret_cast<unsigned char*>(method_sig_raw));
  }
  const std::string method_return_descriptor = MethodReturnDescriptor(method_signature);

  if (class_name != target_class || strcmp(method_name, target_method.c_str()) != 0 ||
      !method_signature_matches_target(jvmti, method))
    return;

  begin_thread_method_exit_tracing(jvmti, thread);

  json arguments = json::array();
  json object_refs = json::array();
  jlong this_object_id = 0;
  jobject this_obj = nullptr;
  if (jvmti->GetLocalObject(thread, 0, 0, &this_obj) == JVMTI_ERROR_NONE && this_obj != nullptr) {
    this_object_id = get_or_assign_object_tag(jvmti, this_obj);
    json this_ref = BuildObjectRef(jvmti, env, this_obj, kDumpSerializerDeps);
    FillObjectFields(jvmti, env, this_obj, this_ref, kDumpSerializerDeps);
    object_refs.push_back(this_ref);
  }

  jint frame_count = 0;
  jvmtiFrameInfo frames[1];
  jvmtiError err = jvmti->GetStackTrace(thread, 0, 1, frames, &frame_count);
  if (err == JVMTI_ERROR_NONE && frame_count > 0) {
    trace(jvmti, "Breakpoint hit: %s.%s%s", class_name.c_str(), method_name, method_signature.c_str());

    jint count = 0;
    jvmtiLocalVariableEntry* table = nullptr;
    jvmtiError err = jvmti->GetLocalVariableTable(method, &count, &table);
    if (err != JVMTI_ERROR_NONE) {
      trace(jvmti, "GetLocalVariableTable failed: %d", err);
    }

    jint modifiers = 0;
    jvmti->GetMethodModifiers(method, &modifiers);
    const bool is_static_method = (modifiers & JVM_ACC_STATIC) != 0;
    jint slot = is_static_method ? 0 : 1;
    const std::vector<std::string> argument_descriptors = parse_method_argument_descriptors(method_signature);

    for (size_t arg_index = 0; arg_index < argument_descriptors.size(); arg_index++) {
      const std::string& descriptor = argument_descriptors[arg_index];
      std::string arg_name = "arg" + std::to_string(arg_index);

      if (table != nullptr) {
        for (int i = 0; i < count; i++) {
          const auto& var = table[i];
          if (var.slot != slot || var.start_location != 0) continue;
          if (var.name != nullptr && strcmp(var.name, "this") != 0) arg_name = var.name;
          break;
        }
      }

      json arg_entry;
      arg_entry["name"] = arg_name;
      arg_entry["java_type_name"] = descriptor;
      arg_entry["type"] = TypeInfoToJson(ParseJvmTypeDescriptor(descriptor));

      if (!descriptor.empty() && IsPrimitiveDescriptorChar(descriptor[0])) {
        json primitive_value;
        if (ReadLocalPrimitive(jvmti, thread, slot, descriptor[0], primitive_value)) {
          trace(jvmti, "  arg %s = %s (%s)", arg_name.c_str(), primitive_value.dump().c_str(),
                PrimitiveNameFromDescriptorChar(descriptor[0]).c_str());
          arg_entry["primitive_value"] = primitive_value;
        } else {
          trace(jvmti, "  arg %s = <unavailable>", arg_name.c_str());
        }
      } else if (!descriptor.empty() && descriptor[0] == 'L') {
        jobject obj;
        if (jvmti->GetLocalObject(thread, 0, slot, &obj) == JVMTI_ERROR_NONE && obj != nullptr) {
          arg_entry["object_id"] = get_or_assign_object_tag(jvmti, obj);

          json ref = BuildObjectRef(jvmti, env, obj, kDumpSerializerDeps);
          FillObjectFields(jvmti, env, obj, ref, kDumpSerializerDeps);
          object_refs.push_back(ref);
        }
      } else if (!descriptor.empty() && descriptor[0] == '[') {
        jobject arr;
        if (jvmti->GetLocalObject(thread, 0, slot, &arr) == JVMTI_ERROR_NONE && arr != nullptr) {
          arg_entry["array"] = BuildArrayDump(jvmti, env, arr, descriptor.c_str(), kDumpSerializerDeps);
        }
      }
      arguments.push_back(arg_entry);
      slot += descriptor_slot_width(descriptor);
    }

    if (table != nullptr) jvmti->Deallocate((unsigned char*)table);
  }

  InvocationTraceContext invocation_context = build_invocation_context(jvmti, thread, object_refs);
  json mock_objects = invocation_mock_objects_to_json(invocation_context);
  push_invocation_context(invocation_context);

  {
    std::lock_guard<std::mutex> lock(g_dump_mutex);

    json entry;
    entry["class"] = class_name;
    entry["class_fqcn"] = class_fqcn;
    entry["class_simple_name"] = BinaryNameToSimpleName(class_name);
    entry["method"] = method_name;
    entry["method_signature"] = method_signature;
    entry["method_return_type"] = TypeInfoToJson(ParseJvmTypeDescriptor(method_return_descriptor));
    entry["state_type"] = "method_start";
    entry["this_object_id"] = this_object_id;
    entry["arguments"] = arguments;
    entry["object_refs"] = object_refs;
    entry["mock_objects"] = mock_objects;
    entry["return_value"] = nullptr;

    g_dump.push_back(entry);
  }
  print_object_fields(jvmti, env, thread, method);
}

std::map<std::string, std::string> parse_agent_options(const char* options) {
  std::map<std::string, std::string> result;
  if (!options || !*options) return result;

  std::istringstream ss(options);
  std::string token;
  while (std::getline(ss, token, ',')) {
    const auto eq = token.find(':');
    if (eq != std::string::npos) {
      result[token.substr(0, eq)] = token.substr(eq + 1);
    } else {
      result[token] = "true";
    }
  }
  return result;
}

JNIEXPORT bool InitArgs(char* options, jint& value1) {
  auto args = parse_agent_options(options);

  std::string out_file = "stderr";

  if (args.count("out")) {
    out_file = args["out"];
  }

  out = (out_file == "stderr") ? stderr : fopen(out_file.c_str(), "w");
  if (!out) {
    fprintf(stderr, "Cannot open log file: %s\n", out_file.c_str());
    value1 = JVMTI_ERROR_INTERNAL;
    return true;
  }

  if (args.count("dump")) dump_file_path = args["dump"];
  if (args.count("llm_dump")) llm_dump_file_path = args["llm_dump"];
  if (args.count("config_file")) config_file_path = args["config_file"];

  if (args.count("control_port")) {
    char* parse_end = nullptr;
    const long parsed_port = strtol(args["control_port"].c_str(), &parse_end, 10);
    if (parse_end == args["control_port"].c_str() || *parse_end != '\0' || parsed_port <= 0 || parsed_port > 65535) {
      fprintf(stderr, "Invalid control_port: %s\n", args["control_port"].c_str());
      value1 = JVMTI_ERROR_ILLEGAL_ARGUMENT;
      return true;
    }
    control_port = static_cast<int>(parsed_port);
  }

  if (args.count("target_class")) target_class = normalize_class_name(args["target_class"]);
  if (args.count("target_method")) target_method = args["target_method"];
  if (args.count("target_method_signature")) target_method_signature = args["target_method_signature"];

  return false;
}

void JNICALL MethodExitHandler(jvmtiEnv* jvmti, JNIEnv* env, jthread thread, jmethodID method,
                               jboolean was_popped_by_exception, jvalue return_value) {
  if (!has_target_config()) return;

  MethodName mn(jvmti, method);
  const char* holder = mn.holder();
  const char* method_name = mn.name();
  if (!holder || !method_name) return;
  std::string class_name = holder;
  std::string class_fqcn = BinaryNameToFqcn(class_name);
  std::string method_name_string = method_name;

  char* method_sig_raw = nullptr;
  std::string method_signature;
  if (jvmti->GetMethodName(method, nullptr, &method_sig_raw, nullptr) == JVMTI_ERROR_NONE && method_sig_raw) {
    method_signature = method_sig_raw;
    jvmti->Deallocate(reinterpret_cast<unsigned char*>(method_sig_raw));
  }
  const std::string return_descriptor = MethodReturnDescriptor(method_signature);
  const json return_type_json = TypeInfoToJson(ParseJvmTypeDescriptor(return_descriptor));

  const jlong thread_id = get_or_assign_object_tag(jvmti, thread);
  jint method_modifiers = 0;
  jvmti->GetMethodModifiers(method, &method_modifiers);
  const bool is_static_method = (method_modifiers & JVM_ACC_STATIC) != 0;

  if (!is_static_method) {
    jobject callee_this = nullptr;
    if (jvmti->GetLocalObject(thread, 0, 0, &callee_this) == JVMTI_ERROR_NONE && callee_this != nullptr) {
      const jlong callee_object_id = get_or_assign_object_tag(jvmti, callee_this);
      if (is_mock_object_tracked(thread_id, callee_object_id)) {
        json call;
        call["class"] = class_name;
        call["class_fqcn"] = class_fqcn;
        call["class_simple_name"] = BinaryNameToSimpleName(class_name);
        call["method"] = method_name_string;
        call["method_signature"] = method_signature;
        call["return_type"] = return_type_json;
        call["return_value"] =
            SerializeReturnValue(jvmti, env, return_descriptor, return_value, was_popped_by_exception, kDumpSerializerDeps);
        call["was_popped_by_exception"] = was_popped_by_exception == JNI_TRUE;
        append_mock_call(thread_id, callee_object_id, call);
      }
      env->DeleteLocalRef(callee_this);
    }
  }

  const bool is_target_method = class_name == target_class && method_name_string == target_method &&
                                method_signature == target_method_signature;
  if (!is_target_method) return;

  json arguments = json::array();
  json object_refs = json::array();
  jlong this_object_id = 0;

  jobject this_obj = nullptr;
  if (jvmti->GetLocalObject(thread, 0, 0, &this_obj) == JVMTI_ERROR_NONE && this_obj != nullptr) {
    this_object_id = get_or_assign_object_tag(jvmti, this_obj);

    json this_ref = BuildObjectRef(jvmti, env, this_obj, kDumpSerializerDeps);
    FillObjectFields(jvmti, env, this_obj, this_ref, kDumpSerializerDeps);
    object_refs.push_back(this_ref);
  }

  json ret_json =
      SerializeReturnValue(jvmti, env, return_descriptor, return_value, was_popped_by_exception, kDumpSerializerDeps);

  if (!was_popped_by_exception && !return_descriptor.empty() && (return_descriptor[0] == 'L' || return_descriptor[0] == '[') &&
      return_value.l != nullptr) {
    jobject obj = return_value.l;
    json ref = BuildObjectRef(jvmti, env, obj, kDumpSerializerDeps);
    FillObjectFields(jvmti, env, obj, ref, kDumpSerializerDeps);
    object_refs.push_back(ref);
  }

  InvocationTraceContext invocation_context;
  const bool has_context = pop_invocation_context(thread_id, invocation_context);
  const json mock_objects = has_context ? invocation_mock_objects_to_json(invocation_context) : json::array();

  {
    std::lock_guard<std::mutex> lock(g_dump_mutex);

    json entry;
    entry["class"] = class_name;
    entry["class_fqcn"] = class_fqcn;
    entry["class_simple_name"] = BinaryNameToSimpleName(class_name);
    entry["method"] = method_name;
    entry["method_signature"] = method_signature;
    entry["method_return_type"] = return_type_json;
    entry["state_type"] = "method_exit";
    entry["this_object_id"] = this_object_id;
    entry["arguments"] = arguments;
    entry["object_refs"] = object_refs;
    entry["mock_objects"] = mock_objects;
    entry["return_type"] = return_type_json;
    entry["return_value"] = ret_json;

    g_dump.push_back(entry);
  }

  end_thread_method_exit_tracing(jvmti, thread);
}

jint JNICALL Agent_OnLoad(JavaVM* vm, char* options, void* reserved) {
  jint value1;
  if (InitArgs(options, value1)) {
    return value1;
  }

  jvmtiEnv* jvmti;
  vm->GetEnv((void**)&jvmti, JVMTI_VERSION_1_0);

  jvmti->CreateRawMonitor("vmtrace_lock", &vmtrace_lock);
  jvmti->GetTime(&start_time);

  trace(jvmti, "Dumper started");

  jvmtiCapabilities capabilities = {0};
  capabilities.can_generate_all_class_hook_events = 1;
  capabilities.can_generate_compiled_method_load_events = 1;
  capabilities.can_generate_garbage_collection_events = 1;

  capabilities.can_generate_breakpoint_events = 1;
  capabilities.can_access_local_variables = 1;
  capabilities.can_generate_method_entry_events = 1;
  capabilities.can_generate_method_exit_events = 1;
  capabilities.can_generate_frame_pop_events = 1;
  capabilities.can_get_line_numbers = 1;
  capabilities.can_get_bytecodes = 1;
  capabilities.can_get_source_file_name = 1;
  capabilities.can_get_synthetic_attribute = 1;
  capabilities.can_generate_single_step_events = 1;

  capabilities.can_generate_exception_events = 1;
  capabilities.can_generate_field_access_events = 1;
  capabilities.can_generate_field_modification_events = 1;
  capabilities.can_generate_vm_object_alloc_events = 1;
  capabilities.can_tag_objects = 1;

  jvmtiError err = jvmti->AddCapabilities(&capabilities);
  if (err != JVMTI_ERROR_NONE) {
    trace(jvmti, "AddCapabilities failed: %d", err);
  }

  jvmtiEventCallbacks callbacks = {0};
  callbacks.Breakpoint = BreakpointHandler;
  callbacks.VMStart = VMStart;
  callbacks.VMInit = VMInit;
  callbacks.VMDeath = VMDeath;
  callbacks.ClassFileLoadHook = ClassFileLoadHook;
  callbacks.ClassPrepare = ClassPrepare;
  callbacks.DynamicCodeGenerated = DynamicCodeGenerated;
  callbacks.CompiledMethodLoad = CompiledMethodLoad;
  callbacks.CompiledMethodUnload = CompiledMethodUnload;
  callbacks.ThreadStart = ThreadStart;
  callbacks.ThreadEnd = ThreadEnd;
  callbacks.GarbageCollectionStart = GarbageCollectionStart;
  callbacks.GarbageCollectionFinish = GarbageCollectionFinish;
  jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks));
  callbacks.MethodExit = &MethodExitHandler;
  jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks));

  jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_METHOD_EXIT, nullptr);

  jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_VM_START, NULL);
  jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_VM_INIT, NULL);
  jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_VM_DEATH, NULL);
  jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, NULL);
  jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_CLASS_PREPARE, NULL);
  jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_DYNAMIC_CODE_GENERATED, NULL);
  jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_COMPILED_METHOD_LOAD, NULL);
  jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_COMPILED_METHOD_UNLOAD, NULL);
  jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_THREAD_START, NULL);
  jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_THREAD_END, NULL);
  jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_GARBAGE_COLLECTION_START, NULL);
  jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_GARBAGE_COLLECTION_FINISH, NULL);

  jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_BREAKPOINT, NULL);

  return 0;
}

JNIEXPORT void JNICALL Agent_OnUnload(JavaVM* vm) {
  if (out != NULL && out != stderr) {
    fclose(out);
  }
}
