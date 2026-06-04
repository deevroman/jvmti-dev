#ifndef DUMPER_TYPE_UTILS_H
#define DUMPER_TYPE_UTILS_H

#include <string>

#include <nlohmann/json.hpp>

struct JvmTypeInfo {
  std::string descriptor;
  std::string kind;
  std::string binary_name;
  std::string fqcn;
  std::string simple_name;
  bool is_primitive = false;
  bool is_array = false;
  int array_dimensions = 0;
  std::string element_descriptor;
  std::string element_fqcn;
};

bool IsPrimitiveDescriptorChar(char descriptor);
std::string PrimitiveNameFromDescriptorChar(char descriptor);

std::string BinaryNameToFqcn(std::string binary_name);
std::string BinaryNameToSimpleName(const std::string& binary_name);

JvmTypeInfo ParseJvmTypeDescriptor(const std::string& descriptor);
std::string MethodReturnDescriptor(const std::string& method_descriptor);

nlohmann::json TypeInfoToJson(const JvmTypeInfo& type_info);

#endif  // DUMPER_TYPE_UTILS_H
