#include "llm_dump_formatter.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "type_utils.h"

using json = nlohmann::json;

namespace {
using ObjectId = long long;
using ObjectRefIndex = std::unordered_map<ObjectId, const json*>;

std::string json_string_or_empty(const json& node, const char* key) {
  if (!node.is_object()) return "";
  auto it = node.find(key);
  if (it == node.end() || !it->is_string()) return "";
  return it->get<std::string>();
}

std::string llm_indent(int depth) { return std::string(depth * 2, ' '); }

void append_llm_line(std::ostringstream& out, int depth, const std::string& text) {
  out << llm_indent(depth) << text << "\n";
}

std::string llm_escape_string(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  for (size_t i = 0; i < input.size(); i++) {
    const char ch = input[i];
    switch (ch) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out.push_back(ch);
        break;
    }
  }
  return out;
}

std::string normalize_type_name_for_llm(const std::string& raw_name) {
  if (raw_name.empty()) return "java.lang.Object";
  const char first = raw_name[0];
  if (first == 'L' || first == '[' || IsPrimitiveDescriptorChar(first)) {
    const JvmTypeInfo info = ParseJvmTypeDescriptor(raw_name);
    if (!info.fqcn.empty()) return info.fqcn;
  }
  std::string normalized = raw_name;
  std::replace(normalized.begin(), normalized.end(), '/', '.');
  return normalized;
}

std::string object_type_for_llm(const json& object_ref) {
  const json* type =
      object_ref.find("type") != object_ref.end() && object_ref["type"].is_object() ? &object_ref["type"] : nullptr;
  if (type != nullptr) {
    const std::string fqcn = json_string_or_empty(*type, "fqcn");
    if (!fqcn.empty()) return fqcn;
    const std::string descriptor = json_string_or_empty(*type, "descriptor");
    if (!descriptor.empty()) return normalize_type_name_for_llm(descriptor);
  }
  return normalize_type_name_for_llm(json_string_or_empty(object_ref, "java_type_name"));
}

const char* primitive_kind_for_llm(char descriptor) {
  switch (descriptor) {
    case 'Z':
      return "Boolean";
    case 'B':
      return "Byte";
    case 'C':
      return "Char";
    case 'S':
      return "Short";
    case 'I':
      return "Int";
    case 'J':
      return "Long";
    case 'F':
      return "Float";
    case 'D':
      return "Double";
    default:
      return "Primitive";
  }
}

std::string primitive_value_for_llm(char descriptor, const json& value) {
  std::ostringstream ss;
  ss << primitive_kind_for_llm(descriptor) << "(";
  if (descriptor == 'Z' && value.is_boolean()) {
    ss << (value.get<bool>() ? "true" : "false");
  } else if (value.is_number_integer()) {
    ss << value.get<long long>();
  } else if (value.is_number_unsigned()) {
    ss << value.get<unsigned long long>();
  } else if (value.is_number_float()) {
    ss << value.get<double>();
  } else if (value.is_string()) {
    ss << value.get<std::string>();
  } else {
    ss << value.dump();
  }
  ss << ")";
  return ss.str();
}

ObjectRefIndex index_object_refs(const json& event_entry) {
  ObjectRefIndex refs;
  auto refs_it = event_entry.find("object_refs");
  if (refs_it == event_entry.end() || !refs_it->is_array()) return refs;

  for (const auto& ref : *refs_it) {
    if (!ref.is_object()) continue;
    auto id_it = ref.find("object_id");
    if (id_it == ref.end() || !id_it->is_number_integer()) continue;
    refs[id_it->get<ObjectId>()] = &ref;
  }
  return refs;
}

bool is_string_object_ref(const json& object_ref) {
  const json* type =
      object_ref.find("type") != object_ref.end() && object_ref["type"].is_object() ? &object_ref["type"] : nullptr;
  if (type != nullptr) {
    const std::string descriptor = json_string_or_empty(*type, "descriptor");
    if (descriptor == "Ljava/lang/String;") return true;
    const std::string fqcn = json_string_or_empty(*type, "fqcn");
    if (fqcn == "java.lang.String") return true;
  }
  return normalize_type_name_for_llm(json_string_or_empty(object_ref, "java_type_name")) == "java.lang.String";
}

bool decode_string_from_object_ref(const json& object_ref, std::string& decoded) {
  decoded.clear();
  if (!is_string_object_ref(object_ref)) return false;

  auto fields_it = object_ref.find("fields");
  if (fields_it == object_ref.end() || !fields_it->is_array()) return false;

  std::vector<unsigned char> bytes;
  int coder = 0;
  bool has_bytes = false;

  for (const auto& field : *fields_it) {
    if (!field.is_object()) continue;
    const std::string field_name = json_string_or_empty(field, "name");
    if (field_name == "coder") {
      auto primitive_it = field.find("primitive_value");
      if (primitive_it != field.end() && primitive_it->is_number_integer()) coder = primitive_it->get<int>();
      continue;
    }
    if (field_name != "value") continue;

    auto array_it = field.find("array");
    if (array_it == field.end() || !array_it->is_object()) continue;
    auto elems_it = array_it->find("elements");
    if (elems_it == array_it->end() || !elems_it->is_array()) continue;

    for (const auto& elem : *elems_it) {
      if (!elem.is_number_integer()) return false;
      bytes.push_back(static_cast<unsigned char>(elem.get<int>() & 0xFF));
    }
    has_bytes = true;
  }

  if (!has_bytes) return false;

  if (coder == 1) {
    if ((bytes.size() % 2) != 0) return false;
    for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
      const unsigned int code_unit = (static_cast<unsigned int>(bytes[i]) << 8) | bytes[i + 1];
      if (code_unit >= 32 && code_unit <= 126) {
        decoded.push_back(static_cast<char>(code_unit));
      } else {
        decoded.push_back('?');
      }
    }
  } else {
    for (size_t i = 0; i < bytes.size(); i++) decoded.push_back(static_cast<char>(bytes[i]));
  }

  return true;
}

void render_serialized_value_for_llm(std::ostringstream& out, const json& node, const ObjectRefIndex& refs,
                                     std::unordered_set<ObjectId>& visiting, int depth);
void render_object_by_id_for_llm(std::ostringstream& out, ObjectId object_id, const ObjectRefIndex& refs,
                                 std::unordered_set<ObjectId>& visiting, int depth);

void render_array_for_llm(std::ostringstream& out, const json& array_node, const ObjectRefIndex& refs,
                          std::unordered_set<ObjectId>& visiting, int depth) {
  const std::string type_name =
      array_node.find("type") != array_node.end() && array_node["type"].is_object()
          ? json_string_or_empty(array_node["type"], "fqcn")
          : normalize_type_name_for_llm(json_string_or_empty(array_node, "java_type_name"));
  const ObjectId object_id = array_node.find("object_id") != array_node.end() && array_node["object_id"].is_number_integer()
                                 ? array_node["object_id"].get<ObjectId>()
                                 : 0;
  const long long length = array_node.find("length") != array_node.end() && array_node["length"].is_number_integer()
                               ? array_node["length"].get<long long>()
                               : 0;

  std::ostringstream head;
  head << "Array[" << (type_name.empty() ? "unknown" : type_name) << "]#" << object_id << " (length=" << length
       << ") [";
  append_llm_line(out, depth, head.str());

  auto elements_it = array_node.find("elements");
  if (elements_it != array_node.end() && elements_it->is_array()) {
    std::string element_descriptor;
    if (array_node.find("type") != array_node.end() && array_node["type"].is_object()) {
      element_descriptor = json_string_or_empty(array_node["type"], "element_descriptor");
    }

    for (size_t i = 0; i < elements_it->size(); i++) {
      const json& elem = (*elements_it)[i];
      std::ostringstream label;
      label << "[" << i << "]:";
      append_llm_line(out, depth + 1, label.str());
      if (elem.is_null()) {
        append_llm_line(out, depth + 2, "Null");
      } else if (!element_descriptor.empty() && IsPrimitiveDescriptorChar(element_descriptor[0]) &&
                 (elem.is_boolean() || elem.is_number())) {
        append_llm_line(out, depth + 2, primitive_value_for_llm(element_descriptor[0], elem));
      } else {
        render_serialized_value_for_llm(out, elem, refs, visiting, depth + 2);
      }
    }
  } else {
    append_llm_line(out, depth + 1, "<elements unavailable>");
  }
  append_llm_line(out, depth, "]");
}

void render_custom_value_for_llm(std::ostringstream& out, const json& custom, const ObjectRefIndex& refs,
                                 std::unordered_set<ObjectId>& visiting, int depth) {
  const std::string kind = json_string_or_empty(custom, "kind");
  if (kind == "map") {
    const long long size = custom.find("entries") != custom.end() && custom["entries"].is_array()
                               ? static_cast<long long>(custom["entries"].size())
                               : 0;
    std::ostringstream head;
    head << "Map(size=" << size << ") {";
    append_llm_line(out, depth, head.str());

    if (custom.find("entries") != custom.end() && custom["entries"].is_array()) {
      for (size_t i = 0; i < custom["entries"].size(); i++) {
        const json& entry = custom["entries"][i];
        std::ostringstream lbl;
        lbl << "Entry " << (i + 1) << " key:";
        append_llm_line(out, depth + 1, lbl.str());
        render_serialized_value_for_llm(out, entry.value("key", json()), refs, visiting, depth + 2);
        lbl.str("");
        lbl.clear();
        lbl << "Entry " << (i + 1) << " value:";
        append_llm_line(out, depth + 1, lbl.str());
        render_serialized_value_for_llm(out, entry.value("value", json()), refs, visiting, depth + 2);
      }
    }
    append_llm_line(out, depth, "}");
    return;
  }

  if (kind == "list" || kind == "set") {
    const char* title = kind == "list" ? "List" : "Set";
    const long long size = custom.find("elements") != custom.end() && custom["elements"].is_array()
                               ? static_cast<long long>(custom["elements"].size())
                               : 0;
    std::ostringstream head;
    head << title << "(size=" << size << ") [";
    append_llm_line(out, depth, head.str());
    if (custom.find("elements") != custom.end() && custom["elements"].is_array()) {
      for (size_t i = 0; i < custom["elements"].size(); i++) {
        std::ostringstream lbl;
        lbl << "[" << i << "]:";
        append_llm_line(out, depth + 1, lbl.str());
        render_serialized_value_for_llm(out, custom["elements"][i], refs, visiting, depth + 2);
      }
    }
    append_llm_line(out, depth, "]");
    return;
  }

  append_llm_line(out, depth, custom.dump());
}

void render_field_value_for_llm(std::ostringstream& out, const json& field, const ObjectRefIndex& refs,
                                std::unordered_set<ObjectId>& visiting, int depth) {
  const std::string descriptor = json_string_or_empty(field, "java_type_name");
  if (field.find("primitive_value") != field.end()) {
    const char kind = descriptor.empty() ? '\0' : descriptor[0];
    append_llm_line(out, depth, primitive_value_for_llm(kind, field["primitive_value"]));
    return;
  }

  if (field.find("custom") != field.end() && field["custom"].is_object()) {
    render_custom_value_for_llm(out, field["custom"], refs, visiting, depth);
    return;
  }

  if (field.find("array") != field.end() && field["array"].is_object()) {
    render_array_for_llm(out, field["array"], refs, visiting, depth);
    return;
  }

  if (field.find("object_id") != field.end() && field["object_id"].is_number_integer()) {
    render_object_by_id_for_llm(out, field["object_id"].get<ObjectId>(), refs, visiting, depth);
    return;
  }

  append_llm_line(out, depth, "<value unavailable>");
}

void render_object_by_id_for_llm(std::ostringstream& out, ObjectId object_id, const ObjectRefIndex& refs,
                                 std::unordered_set<ObjectId>& visiting, int depth) {
  if (object_id == 0) {
    append_llm_line(out, depth, "Null");
    return;
  }
  if (visiting.count(object_id)) {
    std::ostringstream cycle;
    cycle << "Object#" << object_id << " <cycle>";
    append_llm_line(out, depth, cycle.str());
    return;
  }

  auto it = refs.find(object_id);
  if (it == refs.end() || it->second == nullptr || !it->second->is_object()) {
    std::ostringstream unknown;
    unknown << "Object#" << object_id << " { <details unavailable> }";
    append_llm_line(out, depth, unknown.str());
    return;
  }

  const json& object_ref = *it->second;
  std::string decoded_string;
  if (decode_string_from_object_ref(object_ref, decoded_string)) {
    append_llm_line(out, depth, "String(\"" + llm_escape_string(decoded_string) + "\")");
    return;
  }

  visiting.insert(object_id);

  std::ostringstream head;
  head << "Object[" << object_type_for_llm(object_ref) << "]#" << object_id << " {";
  append_llm_line(out, depth, head.str());

  auto fields_it = object_ref.find("fields");
  if (fields_it == object_ref.end() || !fields_it->is_array() || fields_it->empty()) {
    append_llm_line(out, depth + 1, "<no fields captured>");
  } else {
    for (const auto& field : *fields_it) {
      const std::string field_name = json_string_or_empty(field, "name");
      append_llm_line(out, depth + 1, (field_name.empty() ? "<unnamed>" : field_name) + ":");
      render_field_value_for_llm(out, field, refs, visiting, depth + 2);
    }
  }

  append_llm_line(out, depth, "}");
  visiting.erase(object_id);
}

void render_serialized_value_for_llm(std::ostringstream& out, const json& node, const ObjectRefIndex& refs,
                                     std::unordered_set<ObjectId>& visiting, int depth) {
  if (node.is_null()) {
    append_llm_line(out, depth, "Null");
    return;
  }
  if (!node.is_object()) {
    append_llm_line(out, depth, node.dump());
    return;
  }

  const std::string kind = json_string_or_empty(node, "kind");
  if (kind == "null") {
    append_llm_line(out, depth, "Null");
    return;
  }
  if (kind == "void") {
    append_llm_line(out, depth, "Void");
    return;
  }
  if (kind == "exception") {
    append_llm_line(out, depth, "ExceptionThrown");
    return;
  }
  if (kind == "string") {
    append_llm_line(out, depth, "String(\"" + llm_escape_string(json_string_or_empty(node, "string_value")) + "\")");
    return;
  }
  if (kind == "primitive") {
    const std::string descriptor = json_string_or_empty(node, "primitive_descriptor");
    const char primitive_kind = descriptor.empty() ? '\0' : descriptor[0];
    append_llm_line(out, depth, primitive_value_for_llm(primitive_kind, node.value("primitive_value", json())));
    return;
  }
  if (kind == "array" && node.find("array") != node.end() && node["array"].is_object()) {
    render_array_for_llm(out, node["array"], refs, visiting, depth);
    return;
  }
  if (kind == "object_ref" && node.find("object_id") != node.end() && node["object_id"].is_number_integer()) {
    render_object_by_id_for_llm(out, node["object_id"].get<ObjectId>(), refs, visiting, depth);
    return;
  }
  // Backward compatibility: older dumps used object_id/type without explicit "kind":"object_ref".
  if (node.find("object_id") != node.end() && node["object_id"].is_number_integer()) {
    render_object_by_id_for_llm(out, node["object_id"].get<ObjectId>(), refs, visiting, depth);
    return;
  }

  if (node.find("primitive_value") != node.end()) {
    const std::string descriptor = json_string_or_empty(node, "primitive_descriptor");
    const char primitive_kind = descriptor.empty() ? '\0' : descriptor[0];
    append_llm_line(out, depth, primitive_value_for_llm(primitive_kind, node["primitive_value"]));
    return;
  }

  append_llm_line(out, depth, node.dump());
}

void render_argument_for_llm(std::ostringstream& out, const json& argument, const ObjectRefIndex& refs, int depth) {
  std::unordered_set<ObjectId> visiting;
  const std::string descriptor = json_string_or_empty(argument, "java_type_name");

  if (argument.find("primitive_value") != argument.end()) {
    const char primitive_kind = descriptor.empty() ? '\0' : descriptor[0];
    append_llm_line(out, depth, primitive_value_for_llm(primitive_kind, argument["primitive_value"]));
    return;
  }
  if (argument.find("array") != argument.end() && argument["array"].is_object()) {
    render_array_for_llm(out, argument["array"], refs, visiting, depth);
    return;
  }
  if (argument.find("object_id") != argument.end() && argument["object_id"].is_number_integer()) {
    render_object_by_id_for_llm(out, argument["object_id"].get<ObjectId>(), refs, visiting, depth);
    return;
  }

  append_llm_line(out, depth, "Null");
}

std::string class_name_for_llm(const json& entry) {
  std::string fqcn = json_string_or_empty(entry, "class_fqcn");
  if (!fqcn.empty()) return fqcn;
  return normalize_type_name_for_llm(json_string_or_empty(entry, "class"));
}
}  // namespace

std::string DefaultLlmDumpPath(const std::string& json_dump_path) {
  if (json_dump_path.size() >= 5 && json_dump_path.substr(json_dump_path.size() - 5) == ".json") {
    return json_dump_path.substr(0, json_dump_path.size() - 5) + ".llm.txt";
  }
  return json_dump_path + ".llm.txt";
}

std::string BuildLlmReadableDump(const nlohmann::json& dump_events) {
  if (!dump_events.is_array() || dump_events.empty()) return "No captured method invocations.\n";

  struct InvocationScenario {
    const json* start = nullptr;
    const json* exit = nullptr;
  };

  std::vector<const json*> start_stack;
  std::vector<InvocationScenario> scenarios;
  for (const auto& event : dump_events) {
    if (!event.is_object()) continue;
    const std::string state_type = json_string_or_empty(event, "state_type");
    if (state_type == "method_start") {
      start_stack.push_back(&event);
      continue;
    }
    if (state_type == "method_exit") {
      InvocationScenario scenario;
      scenario.exit = &event;
      if (!start_stack.empty()) {
        scenario.start = start_stack.back();
        start_stack.pop_back();
      }
      scenarios.push_back(scenario);
    }
  }
  for (size_t i = 0; i < start_stack.size(); i++) {
    InvocationScenario scenario;
    scenario.start = start_stack[i];
    scenario.exit = nullptr;
    scenarios.push_back(scenario);
  }
  if (scenarios.empty()) return "No captured method invocations.\n";

  std::ostringstream out;
  for (size_t i = 0; i < scenarios.size(); i++) {
    const json* start = scenarios[i].start;
    const json* exit = scenarios[i].exit;
    const json* any = start ? start : exit;
    if (!any) continue;

    ObjectRefIndex start_refs = start ? index_object_refs(*start) : ObjectRefIndex{};
    ObjectRefIndex merged_refs = start_refs;
    if (exit) {
      ObjectRefIndex exit_refs = index_object_refs(*exit);
      for (auto it = exit_refs.begin(); it != exit_refs.end(); ++it) merged_refs[it->first] = it->second;
    }

    out << "Invocation " << (i + 1) << ":\n";
    const ObjectId start_this_object_id = start ? start->value("this_object_id", static_cast<ObjectId>(0)) : 0;
    if (start && start_this_object_id != 0) {
      out << "Given an instance:\n";
      std::unordered_set<ObjectId> visiting;
      render_object_by_id_for_llm(out, start_this_object_id, start_refs, visiting, 1);
    } else {
      out << "Given target class: " << class_name_for_llm(*any) << "\n";
    }

    const std::string method_name = json_string_or_empty(*any, "method");
    const std::string method_signature = json_string_or_empty(*any, "method_signature");
    auto args_it = start ? start->find("arguments") : any->end();
    if (start && args_it != start->end() && args_it->is_array() && !args_it->empty()) {
      out << "When calling the method " << method_name;
      if (!method_signature.empty()) out << " " << method_signature;
      out << " with arguments:\n";
      for (size_t arg_index = 0; arg_index < args_it->size(); arg_index++) {
        const json& arg = (*args_it)[arg_index];
        std::string arg_name = json_string_or_empty(arg, "name");
        if (arg_name.empty()) arg_name = "arg" + std::to_string(arg_index);
        std::ostringstream label;
        label << "Arg " << (arg_index + 1) << " (" << arg_name << "):";
        append_llm_line(out, 1, label.str());
        render_argument_for_llm(out, arg, start_refs, 2);
      }
    } else {
      out << "When calling the method " << method_name;
      if (!method_signature.empty()) out << " " << method_signature;
      out << " without arguments.\n";
    }

    if (exit) {
      out << "Then the method should return:\n";
      auto return_it = exit->find("return_value");
      if (return_it != exit->end()) {
        std::unordered_set<ObjectId> visiting;
        render_serialized_value_for_llm(out, *return_it, merged_refs, visiting, 1);
      } else {
        append_llm_line(out, 1, "<return value unavailable>");
      }

      const ObjectId exit_this_object_id = exit->value("this_object_id", start_this_object_id);
      if (exit_this_object_id != 0) {
        out << "And the instance after call is:\n";
        std::unordered_set<ObjectId> visiting;
        render_object_by_id_for_llm(out, exit_this_object_id, merged_refs, visiting, 1);
      }
    } else {
      out << "Then method exit was not captured.\n";
    }

    if (i + 1 < scenarios.size()) out << "\n";
  }

  return out.str();
}
