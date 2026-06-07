ROOT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
PIPELINE := $(ROOT_DIR)/scripts/run_pipeline.sh
APP_CLASSPATH := $(ROOT_DIR)/java_examples
ARTIFACTS_DIR ?= $(ROOT_DIR)/.artifacts
PIPELINE_SOCKET_ARGS ?=

CALC_OUT := $(ARTIFACTS_DIR)/pipeline_calculator
CALC_LLM_OUT := $(ARTIFACTS_DIR)/pipeline_calculator_llm
CAR_OUT := $(ARTIFACTS_DIR)/pipeline_carcontroller
RET_OUT := $(ARTIFACTS_DIR)/pipeline_return_string
INPUT_OUT := $(ARTIFACTS_DIR)/pipeline_input_stream
MAP_OUT := $(ARTIFACTS_DIR)/pipeline_simple_map
SPRING_LOGIN_OUT := $(ARTIFACTS_DIR)/pipeline_spring_login
SPRING_LOGIN_TRUE_OUT := $(ARTIFACTS_DIR)/pipeline_spring_login_true
JOSM_APP_PROGRAM_ARGUMENTS_OUT := $(ARTIFACTS_DIR)/pipeline_josm_app_program_arguments
GUAVA_SPLITTER_OUT := $(ARTIFACTS_DIR)/pipeline_guava_splitter
JSON_PARSER_OUT := $(ARTIFACTS_DIR)/pipeline_json_parser
KLAW_COMPAREUTILS_OUT := $(ARTIFACTS_DIR)/pipeline_klaw_compareutils
KLAW_ADD_NEW_TEAM_OUT := $(ARTIFACTS_DIR)/pipeline_klaw_add_new_team
KOTLIN_SERIALIZER_OUT := $(ARTIFACTS_DIR)/pipeline_kotlin_serializer

.PHONY: all list clean calculator calculator_llm carcontroller return_string input_stream simple_map spring_login spring_login_true josm_app josm_program_arguments guava_splitter json_parser klaw_compareutils klaw_add_new_team kotlin_serializer

all: calculator carcontroller return_string input_stream simple_map spring_login spring_login_true josm_app guava_splitter json_parser klaw_compareutils kotlin_serializer

list:
	@echo "Available targets:"
	@echo "  make calculator"
	@echo "  make calculator_llm"
	@echo "  make carcontroller"
	@echo "  make return_string"
	@echo "  make input_stream"
	@echo "  make simple_map"
	@echo "  make spring_login"
	@echo "  make spring_login_true"
	@echo "  make josm_app"
	@echo "  make josm_program_arguments  # alias for josm_app"
	@echo "  make guava_splitter"
	@echo "  make json_parser"
	@echo "  make klaw_compareutils"
	@echo "  make klaw_add_new_team"
	@echo "  make kotlin_serializer"
	@echo "  make all"
	@echo "  make clean"

calculator:
	$(PIPELINE) \
		--config-file $(ROOT_DIR)/config.json \
		--app-main Calculator \
		--app-classpath $(APP_CLASSPATH) \
		--output-dir $(CALC_OUT) \
		$(PIPELINE_SOCKET_ARGS)

calculator_llm:
	$(PIPELINE) \
		--llm \
		--config-file $(ROOT_DIR)/config.json \
		--app-main Calculator \
		--app-classpath $(APP_CLASSPATH) \
		--output-dir $(CALC_LLM_OUT) \
		$(PIPELINE_SOCKET_ARGS)

carcontroller:
	$(PIPELINE) \
		--config-file $(ROOT_DIR)/config_carcontroller.json \
		--app-main CarController \
		--app-classpath $(APP_CLASSPATH) \
		--output-dir $(CAR_OUT) \
		$(PIPELINE_SOCKET_ARGS)

return_string:
	$(PIPELINE) \
		--config-file $(ROOT_DIR)/config_return_string.json \
		--app-main ReturnStringExample \
		--app-classpath $(APP_CLASSPATH) \
		--output-dir $(RET_OUT) \
		$(PIPELINE_SOCKET_ARGS)

input_stream:
	$(PIPELINE) \
		--config-file $(ROOT_DIR)/config_input_stream_example.json \
		--app-main InputStreamFileExample \
		--app-classpath $(APP_CLASSPATH) \
		--output-dir $(INPUT_OUT) \
		$(PIPELINE_SOCKET_ARGS)

simple_map:
	$(PIPELINE) \
		--config-file $(ROOT_DIR)/config_simple_map.json \
		--app-main SimpleMapExample \
		--app-classpath $(APP_CLASSPATH) \
		--output-dir $(MAP_OUT) \
		$(PIPELINE_SOCKET_ARGS)

spring_login:
	$(ROOT_DIR)/scripts/run_spring_login_pipeline.sh $(PIPELINE_SOCKET_ARGS) $(SPRING_LOGIN_OUT)

spring_login_true:
	LOGIN_VALUE=admin PASSWORD_VALUE=secret EXPECTED_LOGIN_RESULT=true \
	$(ROOT_DIR)/scripts/run_spring_login_pipeline.sh $(PIPELINE_SOCKET_ARGS) $(SPRING_LOGIN_TRUE_OUT)

josm_app:
	$(ROOT_DIR)/scripts/run_josm_app_pipeline.sh $(PIPELINE_SOCKET_ARGS) $(JOSM_APP_PROGRAM_ARGUMENTS_OUT)

josm_program_arguments: josm_app

guava_splitter:
	$(ROOT_DIR)/scripts/run_guava_splitter_pipeline.sh $(PIPELINE_SOCKET_ARGS) $(GUAVA_SPLITTER_OUT)

json_parser:
	$(ROOT_DIR)/scripts/run_json_parser_pipeline.sh $(PIPELINE_SOCKET_ARGS) $(JSON_PARSER_OUT)

klaw_compareutils:
	$(ROOT_DIR)/scripts/run_klaw_compareutils_pipeline.sh $(PIPELINE_SOCKET_ARGS) $(KLAW_COMPAREUTILS_OUT)

klaw_add_new_team:
	$(ROOT_DIR)/scripts/run_klaw_add_new_team_pipeline.sh $(KLAW_ADD_NEW_TEAM_OUT)

kotlin_serializer:
	$(ROOT_DIR)/scripts/run_kotlin_serializer_pipeline.sh $(PIPELINE_SOCKET_ARGS) $(KOTLIN_SERIALIZER_OUT)

clean:
	rm -rf $(CALC_OUT) $(CALC_LLM_OUT) $(CAR_OUT) $(RET_OUT) $(INPUT_OUT) $(MAP_OUT) $(SPRING_LOGIN_OUT) $(SPRING_LOGIN_TRUE_OUT) $(JOSM_APP_PROGRAM_ARGUMENTS_OUT) $(GUAVA_SPLITTER_OUT) $(JSON_PARSER_OUT) $(KLAW_COMPAREUTILS_OUT) $(KLAW_ADD_NEW_TEAM_OUT) $(KOTLIN_SERIALIZER_OUT)
