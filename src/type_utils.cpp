#include "type_utils.h"

#include <algorithm>

bool IsPrimitiveDescriptorChar(char descriptor) {
  switch (descriptor) {
    case 'Z':
    case 'B':
    case 'C':
    case 'S':
    case 'I':
    case 'J':
    case 'F':
    case 'D':
      return true;
    default:
      return false;
  }
}

std::string PrimitiveNameFromDescriptorChar(char descriptor) {
  switch (descriptor) {
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
    default:
      return "unknown";
  }
}

std::string BinaryNameToFqcn(std::string binary_name) {
  std::replace(binary_name.begin(), binary_name.end(), '/', '.');
  return binary_name;
}

std::string BinaryNameToSimpleName(const std::string& binary_name) {
  const auto separator = binary_name.find_last_of('/');
  if (separator == std::string::npos) return binary_name;
  return binary_name.substr(separator + 1);
}

JvmTypeInfo ParseJvmTypeDescriptor(const std::string& descriptor) {
  JvmTypeInfo info;
  info.descriptor = descriptor;

  if (descriptor.empty()) {
    info.kind = "unknown";
    return info;
  }

  if (descriptor[0] == '[') {
    info.kind = "array";
    info.is_array = true;

    int dimensions = 0;
    while (dimensions < static_cast<int>(descriptor.size()) && descriptor[dimensions] == '[') dimensions++;
    info.array_dimensions = dimensions;

    info.element_descriptor = descriptor.substr(dimensions);
    JvmTypeInfo element = ParseJvmTypeDescriptor(info.element_descriptor);
    info.element_fqcn = element.fqcn;

    std::string base = element.fqcn.empty() ? element.descriptor : element.fqcn;
    for (int i = 0; i < dimensions; i++) base += "[]";
    info.fqcn = base;
    info.simple_name = info.fqcn;
    return info;
  }

  if (descriptor[0] == 'L' && descriptor.size() >= 2 && descriptor.back() == ';') {
    info.kind = "object";
    info.binary_name = descriptor.substr(1, descriptor.size() - 2);
    info.fqcn = BinaryNameToFqcn(info.binary_name);
    info.simple_name = BinaryNameToSimpleName(info.binary_name);
    return info;
  }

  if (descriptor.size() == 1 && IsPrimitiveDescriptorChar(descriptor[0])) {
    info.kind = "primitive";
    info.is_primitive = true;
    info.fqcn = PrimitiveNameFromDescriptorChar(descriptor[0]);
    info.simple_name = info.fqcn;
    return info;
  }

  if (descriptor == "V") {
    info.kind = "void";
    info.fqcn = "void";
    info.simple_name = "void";
    return info;
  }

  info.kind = "unknown";
  info.fqcn = descriptor;
  info.simple_name = descriptor;
  return info;
}

std::string MethodReturnDescriptor(const std::string& method_descriptor) {
  const auto split = method_descriptor.find(')');
  if (split == std::string::npos || split + 1 >= method_descriptor.size()) return "";
  return method_descriptor.substr(split + 1);
}

nlohmann::json TypeInfoToJson(const JvmTypeInfo& type_info) {
  nlohmann::json j;
  j["descriptor"] = type_info.descriptor;
  j["kind"] = type_info.kind;
  j["fqcn"] = type_info.fqcn;
  j["simple_name"] = type_info.simple_name;
  j["is_primitive"] = type_info.is_primitive;
  j["is_array"] = type_info.is_array;

  if (!type_info.binary_name.empty()) j["binary_name"] = type_info.binary_name;
  if (type_info.is_array) {
    j["array_dimensions"] = type_info.array_dimensions;
    j["element_descriptor"] = type_info.element_descriptor;
    j["element_fqcn"] = type_info.element_fqcn;
  }

  return j;
}
